#include "backend.h"

#include "codegen.h"
#include "installation.h"

#include <llvm/Analysis/TargetLibraryInfo.h>
#include <llvm/DebugInfo/DIContext.h>
#include <llvm/DebugInfo/DWARF/DWARFContext.h>
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
#include <llvm/Transforms/IPO/GlobalDCE.h>
#include <llvm/TargetParser/Triple.h>

#include <lld/Common/Driver.h>

#include <algorithm>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <tuple>
#include <vector>

LLD_HAS_DRIVER(coff)
LLD_HAS_DRIVER(elf)

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

    // D19: there is no C library. LLVM may call only the functions of mem_x64.S,
    // and must not turn a loop into strlen, as it would in fs_linux.cro.
    llvm::TargetLibraryInfoImpl library(machine.getTargetTriple());
    library.disableAllFunctions();
    for (llvm::LibFunc kept : {llvm::LibFunc_memcpy, llvm::LibFunc_memmove, llvm::LibFunc_memset, llvm::LibFunc_memcmp}) {
        library.setAvailable(kept);
    }
    if (machine.getTargetTriple().isOSLinux()) library.setAvailable(llvm::LibFunc_bcmp);
    functions.registerPass([&] { return llvm::TargetLibraryAnalysis(library); });

    llvm::PassBuilder builder(&machine);
    builder.registerModuleAnalyses(modules);
    builder.registerCGSCCAnalyses(cgscc);
    builder.registerFunctionAnalyses(functions);
    builder.registerLoopAnalyses(loops);
    builder.crossRegisterProxies(loops, functions, cgscc, modules);

    llvm::ModulePassManager passes;
    switch (level) {
    case 0:  // without optimizations too, the functions that nothing calls go away (D241)
        passes = builder.buildO0DefaultPipeline(llvm::OptimizationLevel::O0);
        passes.addPass(llvm::GlobalDCEPass());
        break;
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
        error = "LLVM cannot generate object files for " + machine.getTargetTriple().str();
        return false;
    }
    passes.run(module);
    out.flush();
    return true;
}

// ---- The table of the stack traces (D339) ----
//
// The runtime turns a return address into functions, files and lines without the
// debug information: for the code of the program, from the label cromo_code at the
// start of its section (codegen.cpp), the points where the line or the inlined
// calls change, each with its frames from the innermost. It is made from the DWARF
// of the object, which has the lines on both systems, and linked as an object of
// its own:
//   cromo_trace_table = { code, size, count, offsets[count], chains[count],
//                         frames[3 each: name, file, line | 1 << 31 if a caller follows],
//                         strings }

struct TraceFrame {
    std::string function;
    std::string file;
    uint32_t line = 0;
    bool operator<(const TraceFrame& other) const {
        return std::tie(function, file, line) < std::tie(other.function, other.file, other.line);
    }
    bool operator==(const TraceFrame& other) const {
        return function == other.function && file == other.file && line == other.line;
    }
};

std::string irBytes(const std::string& bytes) {
    std::string out = "c\"";
    for (unsigned char c : bytes) {
        if (c == '"' || c == '\\' || c < 0x20 || c >= 0x7F) {
            const char* hex = "0123456789ABCDEF";
            out += '\\';
            out += hex[c >> 4];
            out += hex[c & 0xF];
        } else {
            out += static_cast<char>(c);
        }
    }
    return out + "\"";
}

// A path compared whatever its separators: DWARF joins the folder and the name.
std::string samePath(std::string path) {
    std::replace(path.begin(), path.end(), '\\', '/');
    return path;
}

std::string irNumbers(const std::vector<uint32_t>& numbers) {
    std::string out = "[" + std::to_string(numbers.size()) + " x i32] [";
    for (size_t i = 0; i < numbers.size(); ++i) out += (i ? ", i32 " : "i32 ") + std::to_string(numbers[i]);
    return out + "]";
}

// The IR of the table, from the object of the program.
bool traceTableIr(const std::string& objectPath, const BackendOptions& options, std::string& ir, std::string& error) {
    llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>> data = llvm::MemoryBuffer::getFile(objectPath);
    if (!data) {
        error = "cannot read " + objectPath + ": " + data.getError().message();
        return false;
    }
    llvm::Expected<std::unique_ptr<llvm::object::ObjectFile>> object =
        llvm::object::ObjectFile::createObjectFile((*data)->getMemBufferRef());
    if (!object) {
        error = "internal error: cannot read the object: " + llvm::toString(object.takeError());
        return false;
    }
    uint64_t section = UINT64_MAX;
    uint64_t anchor = 0;
    uint64_t size = 0;
    for (const llvm::object::SymbolRef& symbol : (*object)->symbols()) {
        llvm::Expected<llvm::StringRef> name = symbol.getName();
        if (!name) {
            llvm::consumeError(name.takeError());
            continue;
        }
        if (*name != "cromo_code") continue;
        llvm::Expected<llvm::object::section_iterator> place = symbol.getSection();
        llvm::Expected<uint64_t> address = symbol.getAddress();
        if (!place || !address || *place == (*object)->section_end()) {
            if (!place) llvm::consumeError(place.takeError());
            if (!address) llvm::consumeError(address.takeError());
            break;
        }
        section = (*place)->getIndex();
        anchor = *address;
        size = (*place)->getAddress() + (*place)->getSize() - anchor;
    }
    if (section == UINT64_MAX) {
        error = "internal error: the object has no label cromo_code";
        return false;
    }

    std::map<std::string, std::string> names;
    for (const auto& [path, name] : options.fileNames) names[samePath(path)] = name;
    std::unique_ptr<llvm::DWARFContext> dwarf = llvm::DWARFContext::create(**object);
    std::set<uint64_t> starts;
    for (const std::unique_ptr<llvm::DWARFUnit>& unit : dwarf->compile_units()) {
        const llvm::DWARFDebugLine::LineTable* lines = dwarf->getLineTableForUnit(unit.get());
        if (!lines) continue;
        for (const llvm::DWARFDebugLine::Row& row : lines->Rows) {
            if (!row.EndSequence && row.Address.SectionIndex == section && row.Address.Address >= anchor) {
                starts.insert(row.Address.Address);
            }
        }
    }

    llvm::DILineInfoSpecifier spec(llvm::DILineInfoSpecifier::FileLineInfoKind::AbsoluteFilePath,
                                   llvm::DILineInfoSpecifier::FunctionNameKind::ShortName);
    std::vector<uint32_t> offsets, chains, frames;
    std::map<std::vector<TraceFrame>, uint32_t> chainIndex;
    std::map<std::string, uint32_t> stringIndex;
    std::string strings;
    auto text = [&](const std::string& value) {
        auto found = stringIndex.find(value);
        if (found != stringIndex.end()) return found->second;
        uint32_t offset = static_cast<uint32_t>(strings.size());
        strings += value;
        strings += '\0';
        stringIndex[value] = offset;
        return offset;
    };
    std::vector<TraceFrame> previous;
    bool first = true;
    for (uint64_t address : starts) {
        llvm::DIInliningInfo info = dwarf->getInliningInfoForAddress({address, section}, spec);
        std::vector<TraceFrame> chain;
        for (uint32_t i = 0; i < info.getNumberOfFrames(); ++i) {
            const llvm::DILineInfo& line = info.getFrame(i);
            if (line.FunctionName == "cromo_start") continue;  // the entry point (codegen.cpp)
            auto shown = names.find(samePath(line.FileName));
            std::string function = line.FunctionName == llvm::DILineInfo::BadString ? "?" : line.FunctionName;
            chain.push_back({function, shown == names.end() ? line.FileName : shown->second, line.Line});
        }
        if (!chain.empty() && chain.front().line == 0) continue;  // code of no line: the point before holds
        if (!first && chain == previous) continue;
        first = false;
        previous = chain;
        offsets.push_back(static_cast<uint32_t>(address - anchor));
        if (chain.empty()) {
            chains.push_back(UINT32_MAX);
            continue;
        }
        auto found = chainIndex.find(chain);
        if (found != chainIndex.end()) {
            chains.push_back(found->second);
            continue;
        }
        uint32_t index = static_cast<uint32_t>(frames.size() / 3);
        chainIndex[chain] = index;
        chains.push_back(index);
        for (size_t i = 0; i < chain.size(); ++i) {
            frames.push_back(text(chain[i].function));
            frames.push_back(text(chain[i].file));
            frames.push_back(chain[i].line | (i + 1 < chain.size() ? 0x80000000u : 0));
        }
    }
    if (strings.empty()) strings += '\0';
    ir = "@cromo_code = external global i8\n";
    ir += "@cromo.trace.offsets = private unnamed_addr constant " + irNumbers(offsets) + "\n";
    ir += "@cromo.trace.chains = private unnamed_addr constant " + irNumbers(chains) + "\n";
    ir += "@cromo.trace.frames = private unnamed_addr constant " + irNumbers(frames) + "\n";
    ir += "@cromo.trace.strings = private unnamed_addr constant [" + std::to_string(strings.size()) + " x i8] " +
          irBytes(strings) + "\n";
    ir += "@cromo_trace_table = constant { ptr, i64, i64, ptr, ptr, ptr, ptr } { ptr @cromo_code, i64 " +
          std::to_string(size) + ", i64 " + std::to_string(offsets.size()) +
          ", ptr @cromo.trace.offsets, ptr @cromo.trace.chains, ptr @cromo.trace.frames, ptr @cromo.trace.strings }\n";
    return true;
}

bool emitTraceTable(const std::string& objectPath, const BackendOptions& options, llvm::TargetMachine& machine,
                    const std::string& tablePath, std::string& error) {
    std::string ir;
    if (!traceTableIr(objectPath, options, ir, error)) return false;
    llvm::LLVMContext context;
    llvm::SMDiagnostic diagnostic;
    auto buffer = llvm::MemoryBuffer::getMemBuffer(ir, "cromo.trace", false);
    std::unique_ptr<llvm::Module> module = llvm::parseIR(buffer->getMemBufferRef(), diagnostic, context);
    if (!module) {
        std::string text;
        llvm::raw_string_ostream stream(text);
        diagnostic.print("cromo", stream);
        error = "internal error: the table of the stack traces is not valid:\n" + text;
        return false;
    }
    module->setTargetTriple(machine.getTargetTriple());
    module->setDataLayout(machine.createDataLayout());
    return emitObject(*module, machine, tablePath, error);
}

// The functions of kernel32 that the runtime imports: the `__imp_` symbols that
// it leaves undefined, because it declares them with `dllimport`.
bool runtimeImports(std::vector<std::string>& names, std::string& error) {
    std::string path = runtimeLibraryPath(Target::Windows).string();
    llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>> data = llvm::MemoryBuffer::getFile(path);
    if (!data) {
        error = "cannot read the runtime " + path + ": " + data.getError().message() + " (D237)";
        return false;
    }
    llvm::Expected<std::unique_ptr<llvm::object::Archive>> archive = llvm::object::Archive::create((*data)->getMemBufferRef());
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

bool runLld(const std::vector<std::string>& args, std::string& error) {
    std::vector<const char*> argv;
    for (const std::string& arg : args) argv.push_back(arg.c_str());

    std::string messages;
    llvm::raw_string_ostream stream(messages);
    lld::Result result =
        lld::lldMain(argv, stream, stream, {{lld::WinLink, &lld::coff::link}, {lld::Gnu, &lld::elf::link}});
    stream.flush();
    if (result.retCode != 0) {
        error = "linking failed:\n" + messages;
        return false;
    }
    return true;
}

bool linkWindows(const std::string& objectPath, const std::string& tablePath, const std::string& runtimePath,
                 const BackendOptions& options, std::vector<std::string>& temporaries, std::string& error) {
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
        tablePath,
        runtimePath,
    };
    // D338: the PDB next to the executable. /debug would keep the code that nothing
    // calls. The DWARF, made for the table of the stack traces, stays in the
    // executable too: without it Bitdefender takes the small ones for malware.
    if (options.debugInfo) {
        args.push_back("/debug");
        args.push_back("/opt:ref");
    }
    for (const std::string& archive : options.archives) args.push_back(archive);
    for (const ImportLibrary& import : imports) {
        std::string path = options.outputPath + "." + import.dll.substr(0, import.dll.size() - 4) + ".lib";
        temporaries.push_back(path);
        if (!writeImportLibrary(import, path, error)) return false;
        args.push_back(path);
    }
    return runLld(args, error);
}

// A static executable at a fixed address, without the dynamic loader (D142, D145).
// The static libraries of the extern blocks come before the runtime, because
// their C code can call memcpy and the other functions of mem_x64.S.
bool linkLinux(const std::string& objectPath, const std::string& tablePath, const std::string& runtimePath,
               const BackendOptions& options, std::string& error) {
    // --eh-frame-hdr: the index of the unwind tables, for the stack traces (D375).
    std::vector<std::string> args = {"ld.lld", "-o", options.outputPath, "-e", "_start", "-static", "--eh-frame-hdr",
                                     objectPath, tablePath};
    if (!options.debugInfo) args.push_back("--strip-debug");  // D338: only the table of the stack traces
    for (const std::string& archive : options.archives) args.push_back(archive);
    args.push_back(runtimePath);
    return runLld(args, error);
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

    llvm::Triple triple(targetTriple(options.target));
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
        error = "cannot create the target machine for " + triple.str();
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
    // D339: the table of the stack traces counts from cromo_code, at the start of
    // .text: every function stays there, also those that LLVM finds cold and would
    // move to .text.unlikely.
    for (llvm::Function& function : *module) {
        if (!function.isDeclaration()) function.setSection(".text");
    }

    bool linux = options.target == Target::Linux;
    std::string objectPath = options.outputPath + (linux ? ".o" : ".obj");
    std::string runtimePath = runtimeLibraryPath(options.target).string();  // in lib/ (D237)
    if (!emitObject(*module, *machine, objectPath, error)) return false;
    std::string tablePath = options.outputPath + (linux ? ".trace.o" : ".trace.obj");
    if (!emitTraceTable(objectPath, options, *machine, tablePath, error)) {
        llvm::sys::fs::remove(objectPath);
        llvm::sys::fs::remove(tablePath);
        return false;
    }

    std::vector<std::string> temporaries;
    bool linked = linux ? linkLinux(objectPath, tablePath, runtimePath, options, error)
                        : linkWindows(objectPath, tablePath, runtimePath, options, temporaries, error);
    for (const std::string& path : temporaries) llvm::sys::fs::remove(path);
    llvm::sys::fs::remove(tablePath);
    if (!options.keepObject) llvm::sys::fs::remove(objectPath);
    return linked;
}

}  // namespace cromo
