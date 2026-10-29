#include "backend.h"

#include "codegen.h"
#include "runtime_blob.h"

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Object/Archive.h>
#include <llvm/Object/COFFImportFile.h>
#include <llvm/Object/ObjectFile.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/TargetParser/Triple.h>

#include <lld/Common/Driver.h>

#include <algorithm>
#include <memory>
#include <optional>
#include <vector>

LLD_HAS_DRIVER(coff)

namespace cromo {

namespace {

void initializeLlvm() {
    static bool initialized = false;
    if (initialized) return;
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();
    llvm::InitializeNativeTargetAsmParser();
    initialized = true;
}

void optimize(llvm::Module& module, llvm::TargetMachine& machine, int level) {
    llvm::LoopAnalysisManager loops;
    llvm::FunctionAnalysisManager functions;
    llvm::CGSCCAnalysisManager cgscc;
    llvm::ModuleAnalysisManager modules;

    llvm::PassBuilder builder(&machine);
    builder.registerModuleAnalyses(modules);
    builder.registerCGSCCAnalyses(cgscc);
    builder.registerFunctionAnalyses(functions);
    builder.registerLoopAnalyses(loops);
    builder.crossRegisterProxies(loops, functions, cgscc, modules);

    llvm::ModulePassManager passes;
    switch (level) {
    case 0: passes = builder.buildO0DefaultPipeline(llvm::OptimizationLevel::O0); break;
    case 1: passes = builder.buildPerModuleDefaultPipeline(llvm::OptimizationLevel::O1); break;
    case 3: passes = builder.buildPerModuleDefaultPipeline(llvm::OptimizationLevel::O3); break;
    default: passes = builder.buildPerModuleDefaultPipeline(llvm::OptimizationLevel::O2); break;
    }
    passes.run(module, modules);
}

bool emitObject(llvm::Module& module, llvm::TargetMachine& machine, const std::string& path, std::string& error) {
    std::error_code ec;
    llvm::raw_fd_ostream out(path, ec, llvm::sys::fs::OF_None);
    if (ec) {
        error = "cannot write " + path + ": " + ec.message();
        return false;
    }
    llvm::legacy::PassManager passes;
    if (machine.addPassesToEmitFile(passes, out, nullptr, llvm::CodeGenFileType::ObjectFile)) {
        error = "LLVM cannot generate object files for " + std::string(kTargetTriple);
        return false;
    }
    passes.run(module);
    out.flush();
    return true;
}

bool writeFile(const std::string& path, std::string_view data, std::string& error) {
    std::error_code ec;
    llvm::raw_fd_ostream out(path, ec, llvm::sys::fs::OF_None);
    if (ec) {
        error = "cannot write " + path + ": " + ec.message();
        return false;
    }
    out.write(data.data(), data.size());
    return true;
}

// The functions of kernel32 that the runtime imports: the `__imp_` symbols that
// it leaves undefined, because it declares them with `dllimport`.
bool runtimeImports(std::vector<std::string>& names, std::string& error) {
    std::string_view data = runtimeLibrary();
    llvm::MemoryBufferRef buffer(llvm::StringRef(data.data(), data.size()), "cromo_rt.lib");
    llvm::Expected<std::unique_ptr<llvm::object::Archive>> archive = llvm::object::Archive::create(buffer);
    if (!archive) {
        error = "internal error: cannot read the runtime: " + llvm::toString(archive.takeError());
        return false;
    }
    std::string problem;
    llvm::Error failure = llvm::Error::success();
    for (const llvm::object::Archive::Child& child : (*archive)->children(failure)) {
        llvm::Expected<llvm::MemoryBufferRef> member = child.getMemoryBufferRef();
        if (!member) {
            problem = llvm::toString(member.takeError());
            break;
        }
        llvm::Expected<std::unique_ptr<llvm::object::ObjectFile>> object = llvm::object::ObjectFile::createObjectFile(*member);
        if (!object) {
            problem = llvm::toString(object.takeError());
            break;
        }
        for (const llvm::object::SymbolRef& symbol : (*object)->symbols()) {
            llvm::Expected<uint32_t> flags = symbol.getFlags();
            if (!flags) {
                problem = llvm::toString(flags.takeError());
                break;
            }
            llvm::Expected<llvm::StringRef> name = symbol.getName();
            if (!name) {
                problem = llvm::toString(name.takeError());
                break;
            }
            if ((*flags & llvm::object::SymbolRef::SF_Undefined) && name->starts_with("__imp_")) {
                names.push_back(name->drop_front(6).str());
            }
        }
        if (!problem.empty()) break;
    }
    if (failure) problem = llvm::toString(std::move(failure));
    if (!problem.empty()) {
        error = "internal error: cannot read the runtime: " + problem;
        return false;
    }
    return true;
}

// The import library of a DLL, written by cromo instead of taken from the
// Windows SDK (D136), as `lld-link /def` would do.
bool writeImportLibrary(const ImportLibrary& import, const std::string& path, std::string& error) {
    std::vector<llvm::object::COFFShortExport> exports;
    for (const std::string& function : import.functions) {
        llvm::object::COFFShortExport entry;
        entry.Name = function;
        exports.push_back(std::move(entry));
    }
    if (llvm::Error failure = llvm::object::writeImportLibrary(import.dll, path, exports,
                                                               llvm::COFF::IMAGE_FILE_MACHINE_AMD64, false)) {
        error = "cannot write the import library of " + import.dll + ": " + llvm::toString(std::move(failure));
        return false;
    }
    return true;
}

bool link(const std::string& objectPath, const std::string& runtimePath, const BackendOptions& options,
          std::vector<std::string>& temporaries, std::string& error) {
    // The DLLs of the extern blocks, and kernel32 for the runtime.
    std::vector<ImportLibrary> imports = options.imports;
    auto kernel32 = std::find_if(imports.begin(), imports.end(), [](const ImportLibrary& i) { return i.dll == "kernel32.dll"; });
    if (kernel32 == imports.end()) kernel32 = imports.insert(imports.end(), ImportLibrary{"kernel32.dll", {}});
    std::vector<std::string> needed;
    if (!runtimeImports(needed, error)) return false;
    for (const std::string& name : needed) {
        if (std::find(kernel32->functions.begin(), kernel32->functions.end(), name) == kernel32->functions.end()) {
            kernel32->functions.push_back(name);
        }
    }

    // No C runtime (D19): our own entry point and no default libraries.
    std::vector<std::string> args = {
        "lld-link",
        "/nologo",
        "/out:" + options.outputPath,
        "/entry:cromo_start",
        "/subsystem:console",
        "/machine:x64",
        "/nodefaultlib",
        objectPath,
        runtimePath,
    };
    for (const std::string& archive : options.archives) args.push_back(archive);
    for (const ImportLibrary& import : imports) {
        std::string path = options.outputPath + "." + import.dll.substr(0, import.dll.size() - 4) + ".lib";
        temporaries.push_back(path);
        if (!writeImportLibrary(import, path, error)) return false;
        args.push_back(path);
    }
    std::vector<const char*> argv;
    for (const std::string& arg : args) argv.push_back(arg.c_str());

    std::string messages;
    llvm::raw_string_ostream stream(messages);
    lld::Result result = lld::lldMain(argv, stream, stream, {{lld::WinLink, &lld::coff::link}});
    stream.flush();
    if (result.retCode != 0) {
        error = "linking failed:\n" + messages;
        return false;
    }
    return true;
}

}  // namespace

bool buildExecutable(const std::string& irText, const std::string& moduleName, const BackendOptions& options,
                     std::string& error) {
    initializeLlvm();

    llvm::LLVMContext context;
    llvm::SMDiagnostic diagnostic;
    auto buffer = llvm::MemoryBuffer::getMemBuffer(irText, moduleName, false);
    std::unique_ptr<llvm::Module> module = llvm::parseIR(buffer->getMemBufferRef(), diagnostic, context);
    if (!module) {
        std::string text;
        llvm::raw_string_ostream stream(text);
        diagnostic.print("cromo", stream);
        error = "internal error: the generated IR is not valid:\n" + text;
        return false;
    }

    llvm::Triple triple(kTargetTriple);
    std::string lookupError;
    const llvm::Target* target = llvm::TargetRegistry::lookupTarget(triple, lookupError);
    if (!target) {
        error = "target not available: " + lookupError;
        return false;
    }

    llvm::TargetOptions targetOptions;
    llvm::CodeGenOptLevel codegenLevel = options.optLevel == 0 ? llvm::CodeGenOptLevel::None
                                         : options.optLevel == 1 ? llvm::CodeGenOptLevel::Less
                                         : options.optLevel == 3 ? llvm::CodeGenOptLevel::Aggressive
                                                                 : llvm::CodeGenOptLevel::Default;
    std::unique_ptr<llvm::TargetMachine> machine(target->createTargetMachine(
        triple, "x86-64", "", targetOptions, std::nullopt, std::nullopt, codegenLevel));
    if (!machine) {
        error = "cannot create the target machine for " + std::string(kTargetTriple);
        return false;
    }
    module->setTargetTriple(triple);
    module->setDataLayout(machine->createDataLayout());

    std::string verifyMessages;
    llvm::raw_string_ostream verifyStream(verifyMessages);
    if (llvm::verifyModule(*module, &verifyStream)) {
        verifyStream.flush();
        error = "internal error: the generated IR fails LLVM verification:\n" + verifyMessages;
        return false;
    }

    optimize(*module, *machine, options.optLevel);

    std::string objectPath = options.outputPath + ".obj";
    std::string runtimePath = options.outputPath + ".rt.lib";
    if (!emitObject(*module, *machine, objectPath, error)) return false;
    if (!writeFile(runtimePath, runtimeLibrary(), error)) return false;

    std::vector<std::string> temporaries = {runtimePath};
    bool linked = link(objectPath, runtimePath, options, temporaries, error);
    for (const std::string& path : temporaries) llvm::sys::fs::remove(path);
    if (!options.keepObject) llvm::sys::fs::remove(objectPath);
    return linked;
}

}  // namespace cromo
