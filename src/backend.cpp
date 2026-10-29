#include "backend.h"

#include "codegen.h"
#include "runtime_blob.h"
#include "windows_sdk.h"

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/TargetParser/Triple.h>

#include <lld/Common/Driver.h>

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

bool link(const std::string& objectPath, const std::string& runtimePath, const BackendOptions& options,
          std::string& error) {
    const std::string& outputPath = options.outputPath;
    std::string sdkLibDir = findWindowsSdkLibDir();
    if (sdkLibDir.empty()) {
        error = "Windows SDK not found: kernel32.lib is needed to link the executable";
        return false;
    }

    // No C runtime (D19): our own entry point and no default libraries.
    std::vector<std::string> args = {
        "lld-link",
        "/nologo",
        "/out:" + outputPath,
        "/entry:cromo_start",
        "/subsystem:console",
        "/machine:x64",
        "/nodefaultlib",
        "/libpath:" + sdkLibDir,
        objectPath,
        runtimePath,
        "kernel32.lib",
    };
    // The libraries of the extern blocks (D128): next to the modules, then in the SDK.
    std::vector<std::string> folders = options.libraryPaths;
    folders.push_back(sdkLibDir);
    for (const std::string& folder : options.libraryPaths) args.push_back("/libpath:" + folder);
    for (const std::string& library : options.libraries) {
        if (library == "kernel32") continue;
        bool found = false;
        for (const std::string& folder : folders) found = found || llvm::sys::fs::exists(folder + "/" + library + ".lib");
        if (!found) {
            error = "cannot find the library '" + library + "': there is no " + library + ".lib next to the modules "
                    "that use it, nor in the Windows SDK";
            return false;
        }
        args.push_back(library + ".lib");
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

    bool linked = link(objectPath, runtimePath, options, error);
    llvm::sys::fs::remove(runtimePath);
    if (!options.keepObject) llvm::sys::fs::remove(objectPath);
    return linked;
}

}  // namespace cromo
