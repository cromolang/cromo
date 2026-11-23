// What the compiler written in Cromo asks of LLVM and of LLD and their C API does
// not give (D291): the linker, the passes with the functions of C that they may
// call, the lines of an object, the names of an archive, of a DLL and of a shared
// library, the import library of a DLL. The compiler calls these functions and
// those of LLVM-C through `extern` blocks (compiler/llvmc.cro); everything else is in
// Cromo. A text that a function gives back is allocated with malloc, for shim_free.
#include <llvm-c/Core.h>
#include <llvm-c/TargetMachine.h>

#include <llvm/Analysis/TargetLibraryInfo.h>
#include <llvm/DebugInfo/DIContext.h>
#include <llvm/DebugInfo/DWARF/DWARFContext.h>
#include <llvm/IR/Module.h>
#include <llvm/MC/MCSubtargetInfo.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Object/Archive.h>
#include <llvm/Object/COFF.h>
#include <llvm/Object/COFFImportFile.h>
#include <llvm/Object/ELFObjectFile.h>
#include <llvm/Object/ObjectFile.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/TargetParser/Triple.h>
#include <llvm/Transforms/IPO/GlobalDCE.h>

#include <lld/Common/Driver.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include <vector>

LLD_HAS_DRIVER(coff)
LLD_HAS_DRIVER(elf)

namespace {

// A text for Cromo: its bytes, and their number in `length`.
char* given(const std::string& text, int64_t* length) {
    char* copy = static_cast<char*>(std::malloc(text.size() + 1));
    std::memcpy(copy, text.data(), text.size());
    copy[text.size()] = 0;
    *length = static_cast<int64_t>(text.size());
    return copy;
}

std::vector<std::string> lines(const char* text, int64_t length) {
    std::vector<std::string> result;
    std::string line;
    for (int64_t i = 0; i < length; ++i) {
        if (text[i] == '\n') {
            result.push_back(line);
            line.clear();
        } else {
            line += text[i];
        }
    }
    if (!line.empty()) result.push_back(line);
    return result;
}

}  // namespace

extern "C" {

#ifdef _WIN32
typedef void(__cdecl* Constructor)(void);
typedef int(__cdecl* Initializer)(void);
extern Initializer __xi_a[];
extern Initializer __xi_z[];
extern Constructor __xc_a[];
extern Constructor __xc_z[];
int __cdecl _initterm_e(Initializer*, Initializer*);
void __cdecl _initterm(Constructor*, Constructor*);
bool __cdecl __scrt_initialize_crt(int);
bool __cdecl __scrt_initialize_onexit_tables(int);
#endif

// A program of Cromo starts at cromo_start, not at the start of the C runtime: on
// Windows this does what mainCRTStartup does before main, the C runtime and the
// constructors of C++ of LLVM. With glibc the start of the runtime does it (D428).
int32_t shim_start() {
#ifdef _WIN32
    if (!__scrt_initialize_crt(1) || !__scrt_initialize_onexit_tables(1)) return 1;
    if (_initterm_e(__xi_a, __xi_z) != 0) return 2;
    _initterm(__xc_a, __xc_z);
#endif
    return 0;
}

void shim_free(void* text) {
    std::free(text);
}

// The passes of a level, from 0 to 3. D19: there is no C library. LLVM may call
// only the functions of mem_x64.S, and must not turn a loop into strlen.
void shim_optimize(LLVMModuleRef moduleRef, LLVMTargetMachineRef machineRef, int32_t level) {
    llvm::Module& module = *llvm::unwrap(moduleRef);
    llvm::TargetMachine& machine = *reinterpret_cast<llvm::TargetMachine*>(machineRef);
    llvm::LoopAnalysisManager loops;
    llvm::FunctionAnalysisManager functions;
    llvm::CGSCCAnalysisManager cgscc;
    llvm::ModuleAnalysisManager modules;

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

// Position independent code, as Android wants it (P43), and an executable too with `pie`.
void shim_set_pic(LLVMModuleRef moduleRef, int32_t pie) {
    llvm::Module& module = *llvm::unwrap(moduleRef);
    module.setPICLevel(llvm::PICLevel::BigPIC);
    if (pie) module.setPIELevel(llvm::PIELevel::Large);
}

// Whether the processor of a machine has a feature, as `avx2` (D513).
int32_t shim_has_feature(LLVMTargetMachineRef machineRef, const char* feature) {
    const llvm::TargetMachine& machine = *reinterpret_cast<llvm::TargetMachine*>(machineRef);
    return machine.getMCSubtargetInfo().checkFeatures(std::string("+") + feature) ? 1 : 0;
}

// Whether LLVM knows a processor of the architecture of a triple (D513).
int32_t shim_valid_cpu(const char* tripleText, const char* cpu) {
    llvm::Triple triple(tripleText);
    std::string lookupError;
    const llvm::Target* target = llvm::TargetRegistry::lookupTarget(triple, lookupError);
    std::unique_ptr<llvm::MCSubtargetInfo> info(target ? target->createMCSubtargetInfo(triple, "", "") : nullptr);
    return info && info->isCPUStringValid(cpu) ? 1 : 0;
}

// LLD with the arguments of `arguments`, one for each line: lld-link or ld.lld by
// the first. It gives what the linker wrote, and its exit code in `code`.
char* shim_link(const char* arguments, int64_t size, int32_t* code, int64_t* length) {
    std::vector<std::string> args = lines(arguments, size);
    std::vector<const char*> argv;
    for (const std::string& arg : args) argv.push_back(arg.c_str());
    std::string messages;
    llvm::raw_string_ostream stream(messages);
    lld::Result result = lld::lldMain(argv, stream, stream, {{lld::WinLink, &lld::coff::link}, {lld::Gnu, &lld::elf::link}});
    stream.flush();
    *code = result.retCode;
    return given(messages, length);
}

// The lines of the code of an object after its label cromo_code, for the table of
// the stack traces (D339), from its DWARF. The first line of the text is the size
// of the code from the label; then, for each address where a line starts, in
// their order, `@offset` and a line for each of its frames from the innermost:
// function, file and line, with tabs between them. With `status` not 0, the text is
// the error.
char* shim_trace(const char* objectPath, int32_t* status, int64_t* length) {
    *status = 1;
    llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>> data = llvm::MemoryBuffer::getFile(objectPath);
    if (!data) return given(std::string("cannot read ") + objectPath + ": " + data.getError().message(), length);
    llvm::Expected<std::unique_ptr<llvm::object::ObjectFile>> object =
        llvm::object::ObjectFile::createObjectFile((*data)->getMemBufferRef());
    if (!object) return given("internal error: cannot read the object: " + llvm::toString(object.takeError()), length);
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
    if (section == UINT64_MAX) return given("internal error: the object has no label cromo_code", length);

    std::unique_ptr<llvm::DWARFContext> dwarf = llvm::DWARFContext::create(**object);
    std::set<uint64_t> starts;
    for (const std::unique_ptr<llvm::DWARFUnit>& unit : dwarf->compile_units()) {
        const llvm::DWARFDebugLine::LineTable* table = dwarf->getLineTableForUnit(unit.get());
        if (!table) continue;
        for (const llvm::DWARFDebugLine::Row& row : table->Rows) {
            if (!row.EndSequence && row.Address.SectionIndex == section && row.Address.Address >= anchor) {
                starts.insert(row.Address.Address);
            }
        }
    }
    llvm::DILineInfoSpecifier spec(llvm::DILineInfoSpecifier::FileLineInfoKind::AbsoluteFilePath,
                                   llvm::DILineInfoSpecifier::FunctionNameKind::ShortName);
    std::string text = std::to_string(size) + "\n";
    for (uint64_t address : starts) {
        llvm::DIInliningInfo info = dwarf->getInliningInfoForAddress({address, section}, spec);
        text += "@" + std::to_string(address - anchor) + "\n";
        for (uint32_t i = 0; i < info.getNumberOfFrames(); ++i) {
            const llvm::DILineInfo& line = info.getFrame(i);
            std::string function = line.FunctionName == llvm::DILineInfo::BadString ? "?" : line.FunctionName;
            text += function + "\t" + line.FileName + "\t" + std::to_string(line.Line) + "\n";
        }
    }
    *status = 0;
    return given(text, length);
}

// The names of the objects of an archive, one for each line after a letter: `U`
// for one left undefined and `D` for one defined, in capitals when it is global or
// weak. With `status` not 0, the text is the error.
char* shim_archive_names(const char* path, int32_t* status, int64_t* length) {
    *status = 1;
    llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>> data = llvm::MemoryBuffer::getFile(path);
    if (!data) return given(data.getError().message(), length);
    llvm::Expected<std::unique_ptr<llvm::object::Archive>> archive = llvm::object::Archive::create((*data)->getMemBufferRef());
    if (!archive) return given(llvm::toString(archive.takeError()), length);
    std::string text;
    llvm::Error failure = llvm::Error::success();
    for (const llvm::object::Archive::Child& child : (*archive)->children(failure)) {
        llvm::Expected<llvm::MemoryBufferRef> member = child.getMemoryBufferRef();
        if (!member) {
            llvm::consumeError(member.takeError());
            continue;
        }
        llvm::Expected<std::unique_ptr<llvm::object::ObjectFile>> object = llvm::object::ObjectFile::createObjectFile(*member);
        if (!object) {  // not an object, as the list of the symbols
            llvm::consumeError(object.takeError());
            continue;
        }
        for (const llvm::object::SymbolRef& symbol : (*object)->symbols()) {
            llvm::Expected<uint32_t> flags = symbol.getFlags();
            llvm::Expected<llvm::StringRef> name = symbol.getName();
            if (!flags || !name) {
                if (!flags) llvm::consumeError(flags.takeError());
                if (!name) llvm::consumeError(name.takeError());
                continue;
            }
            if (name->empty()) continue;
            bool global = (*flags & (llvm::object::SymbolRef::SF_Global | llvm::object::SymbolRef::SF_Weak)) != 0;
            bool undefined = (*flags & llvm::object::SymbolRef::SF_Undefined) != 0;
            text += undefined ? (global ? 'U' : 'u') : (global ? 'D' : 'd');
            text += name->str() + "\n";
        }
    }
    if (failure) return given(llvm::toString(std::move(failure)), length);
    *status = 0;
    return given(text, length);
}

// The names exported by a DLL, one for each line, also those it forwards to another
// DLL (D140). With `status` 1 the text is why it cannot be read, and with 2 it is
// not a DLL for x64 (D139).
char* shim_dll_exports(const char* path, int32_t* status, int64_t* length) {
    llvm::Expected<llvm::object::OwningBinary<llvm::object::ObjectFile>> file = llvm::object::ObjectFile::createObjectFile(path);
    if (!file) {
        *status = 1;
        return given(llvm::toString(file.takeError()), length);
    }
    const auto* coff = llvm::dyn_cast<llvm::object::COFFObjectFile>(file->getBinary());
    if (!coff || coff->getMachine() != 0x8664) {  // IMAGE_FILE_MACHINE_AMD64
        *status = 2;
        return given("", length);
    }
    std::string text;
    for (const llvm::object::ExportDirectoryEntryRef& entry : coff->export_directories()) {
        llvm::StringRef name;
        if (llvm::Error failure = entry.getSymbolName(name)) {
            llvm::consumeError(std::move(failure));
            continue;
        }
        if (!name.empty()) text += name.str() + "\n";  // the exports by ordinal only have no name
    }
    *status = 0;
    return given(text, length);
}

// The names of a shared library of Linux for the programs linked with it, one for
// each line. With `status` not 0 it cannot be read.
char* shim_shared_names(const char* path, int32_t* status, int64_t* length) {
    llvm::Expected<llvm::object::OwningBinary<llvm::object::ObjectFile>> file = llvm::object::ObjectFile::createObjectFile(path);
    const auto* elf = file ? llvm::dyn_cast<llvm::object::ELFObjectFileBase>(file->getBinary()) : nullptr;
    if (!elf) {
        if (!file) llvm::consumeError(file.takeError());
        *status = 1;
        return given("", length);
    }
    std::string text;
    for (const llvm::object::ELFSymbolRef& symbol : elf->getDynamicSymbolIterators()) {
        llvm::Expected<llvm::StringRef> name = symbol.getName();
        if (!name) {
            llvm::consumeError(name.takeError());
            continue;
        }
        text += name->str() + "\n";
    }
    *status = 0;
    return given(text, length);
}

// The import library of a DLL for x64, with the functions of `names`, one for each
// line (D136), as `lld-link /def` would write it. With `status` not 0, the text is
// the error.
char* shim_import_library(const char* dll, const char* names, int64_t size, const char* path, int32_t* status, int64_t* length) {
    std::vector<llvm::object::COFFShortExport> exports;
    for (const std::string& function : lines(names, size)) {
        llvm::object::COFFShortExport entry;
        entry.Name = function;
        exports.push_back(std::move(entry));
    }
    if (llvm::Error failure = llvm::object::writeImportLibrary(dll, path, exports, llvm::COFF::IMAGE_FILE_MACHINE_AMD64, false)) {
        *status = 1;
        return given(llvm::toString(std::move(failure)), length);
    }
    *status = 0;
    return given("", length);
}

}  // extern "C"
