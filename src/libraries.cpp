#include "libraries.h"

#include "installation.h"
#include "loader.h"

#include <llvm/Object/Archive.h>
#include <llvm/Object/COFF.h>
#include <llvm/Object/ELFObjectFile.h>
#include <llvm/Object/ObjectFile.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/Error.h>

#include <algorithm>
#include <filesystem>
#include <set>
#include <system_error>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace cromo {

namespace fs = std::filesystem;

namespace {

// The system folder of Windows, where kernel32.dll is (D137); none on Linux.
fs::path systemFolder() {
#ifdef _WIN32
    wchar_t buffer[MAX_PATH];
    UINT size = GetSystemDirectoryW(buffer, MAX_PATH);
    if (size == 0 || size >= MAX_PATH) return {};
    return fs::path(buffer);
#else
    return {};
#endif
}

bool isFile(const fs::path& path) {
    std::error_code ec;
    return fs::is_regular_file(path, ec);
}

// The names exported by a DLL, also those it forwards to another DLL (D140).
// Returns false, with the reason in `error`, if it is not a DLL for x64 (D139).
bool readExports(const fs::path& dll, std::set<std::string>& names, std::string& error) {
    llvm::Expected<llvm::object::OwningBinary<llvm::object::ObjectFile>> file =
        llvm::object::ObjectFile::createObjectFile(dll.string());
    if (!file) {
        error = "cannot read " + dll.string() + ": " + llvm::toString(file.takeError());
        return false;
    }
    const auto* coff = llvm::dyn_cast<llvm::object::COFFObjectFile>(file->getBinary());
    if (!coff || coff->getMachine() != 0x8664) {  // IMAGE_FILE_MACHINE_AMD64
        error = dll.filename().string() + " is not a DLL for x64";
        return false;
    }
    for (const llvm::object::ExportDirectoryEntryRef& entry : coff->export_directories()) {
        llvm::StringRef name;
        if (llvm::Error failure = entry.getSymbolName(name)) {
            llvm::consumeError(std::move(failure));
            continue;
        }
        if (!name.empty()) names.insert(name.str());  // the exports by ordinal only have no name
    }
    return true;
}

// The global names that the objects of an archive define, and those that they
// leave undefined.
bool readArchive(const std::string& path, std::set<std::string>& defined, std::set<std::string>& undefined,
                 std::string& error) {
    llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>> data = llvm::MemoryBuffer::getFile(path);
    if (!data) {
        error = "cannot read " + path + ": " + data.getError().message();
        return false;
    }
    llvm::Expected<std::unique_ptr<llvm::object::Archive>> archive = llvm::object::Archive::create((*data)->getMemBufferRef());
    if (!archive) {
        error = "cannot read " + path + ": " + llvm::toString(archive.takeError());
        return false;
    }
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
            if (!(*flags & (llvm::object::SymbolRef::SF_Global | llvm::object::SymbolRef::SF_Weak)) || name->empty()) continue;
            if (*flags & llvm::object::SymbolRef::SF_Undefined) {
                undefined.insert(name->str());
            } else {
                defined.insert(name->str());
            }
        }
    }
    if (failure) {
        error = "cannot read " + path + ": " + llvm::toString(std::move(failure));
        return false;
    }
    return true;
}

// The C functions of one library, and the folders of the modules that declare
// them, in the order in which the modules are loaded (D139).
struct Use {
    std::string name;
    std::vector<const FunctionDecl*> functions;
    std::vector<fs::path> folders;
};

}  // namespace

Libraries findLibraries(const Module& module, const std::deque<SourceFile>& sources, Target target, Diagnostics& diag) {
    std::vector<Use> uses;
    for (const FunctionDecl& function : module.functions) {
        if (!function.isExtern || function.library == "cromo") continue;  // the runtime, always linked (D240)
        auto use = std::find_if(uses.begin(), uses.end(), [&](const Use& u) { return u.name == function.library; });
        if (use == uses.end()) use = uses.insert(uses.end(), Use{function.library, {}, {}});
        use->functions.push_back(&function);
        // The C libraries of the modules of the library are in its folder for the
        // system, as lib/windows/SDL3.lib and lib/linux/libSDL3.a (P31).
        bool library = function.loc.file == kLibraryFile || isLibraryFile(sources[function.loc.file].path);
        fs::path folder = library ? runtimeLibraryPath(target).parent_path()
                                  : fs::absolute(sources[function.loc.file].path).parent_path();
        if (std::find(use->folders.begin(), use->folders.end(), folder) == use->folders.end()) {
            use->folders.push_back(folder);
        }
    }

    Libraries libraries;
    if (isLinux(target)) {  // the linker checks the names (D146)
        for (const Use& use : uses) {
            std::string file = "lib" + use.name + ".a";
            auto folder = std::find_if(use.folders.begin(), use.folders.end(),
                                       [&](const fs::path& f) { return isFile(f / file); });
            // On Android a library can be shared, as libSDL3.so, which goes in the APK (P44).
            std::string shared = "lib" + use.name + ".so";
            auto sharedFolder = std::find_if(use.folders.begin(), use.folders.end(),
                                             [&](const fs::path& f) { return isFile(f / shared); });
            if (folder == use.folders.end() && isAndroid(target) && sharedFolder != use.folders.end()) {
                libraries.archives.push_back((*sharedFolder / shared).string());
            } else if (folder != use.folders.end()) {
                libraries.archives.push_back((*folder / file).string());
            } else if (use.folders.size() == 1 && use.folders[0] == runtimeLibraryPath(target).parent_path()) {
                // A module of the library, as std.gui on ARM64, which has no SDL yet (P31).
                diag.error(use.functions[0]->loc, "the library has no " + file + " for this system, in " +
                                                      use.folders[0].string() + "");
            } else {
                diag.error(use.functions[0]->loc, "cannot find the library '" + use.name + "': there is no " + file +
                                                      " next to the modules that use it");
            }
        }
        return libraries;
    }

    fs::path system = systemFolder();
    for (const Use& use : uses) {
        fs::path archive;
        fs::path dll;
        for (const fs::path& folder : use.folders) {
            if (archive.empty() && isFile(folder / (use.name + ".lib"))) archive = folder / (use.name + ".lib");
            if (dll.empty() && isFile(folder / (use.name + ".dll"))) dll = folder / (use.name + ".dll");
        }
        if (!dll.empty()) libraries.copies.push_back(dll.string());  // D138, D141
        if (!archive.empty()) {
            libraries.archives.push_back(archive.string());  // the linker checks the names (D140)
            continue;
        }
        if (dll.empty() && !system.empty() && isFile(system / (use.name + ".dll"))) dll = system / (use.name + ".dll");
        if (dll.empty() && system.empty()) {
            // From Linux there is no folder of the system to check the DLLs of Windows:
            // the functions are taken as declared, and the program finds out when it starts.
            ImportLibrary import{use.name + ".dll", {}};
            for (const FunctionDecl* function : use.functions) {
                if (std::find(import.functions.begin(), import.functions.end(), function->name) == import.functions.end()) {
                    import.functions.push_back(function->name);
                }
            }
            libraries.imports.push_back(std::move(import));
            continue;
        }
        if (dll.empty()) {
            diag.error(use.functions[0]->loc, "cannot find the library '" + use.name + "': there is no " + use.name +
                                                  ".lib or " + use.name + ".dll next to the modules that use it, nor " +
                                                  use.name + ".dll in " + system.string() + "");
            continue;
        }
        std::set<std::string> exported;
        std::string error;
        if (!readExports(dll, exported, error)) {
            diag.error(use.functions[0]->loc, error);
            continue;
        }
        ImportLibrary import{use.name + ".dll", {}};
        for (const FunctionDecl* function : use.functions) {
            if (!exported.count(function->name)) {
                diag.error(function->loc, use.name + ".dll has no function '" + function->name + "'");
            } else if (std::find(import.functions.begin(), import.functions.end(), function->name) ==
                       import.functions.end()) {
                import.functions.push_back(function->name);  // several modules can declare it (D133)
            }
        }
        libraries.imports.push_back(std::move(import));
    }
    return libraries;
}

bool callsTheCLibrary(const std::vector<std::string>& archives, bool& calls, std::string& error) {
    calls = false;
    if (archives.empty()) return true;
    std::set<std::string> defined;
    std::set<std::string> undefined;
    for (const std::string& archive : archives) {
        if (!readArchive(archive, defined, undefined, error)) return false;
    }
    std::set<std::string> runtime;  // memcpy and the others, which a C library can call without glibc
    std::set<std::string> unused;
    if (!readArchive(runtimeLibraryPath(Target::Linux).string(), runtime, unused, error)) return false;
    for (const char* stub : kGlibcLibraries) {
        fs::path path = glibcFolder() / stub;
        llvm::Expected<llvm::object::OwningBinary<llvm::object::ObjectFile>> file =
            llvm::object::ObjectFile::createObjectFile(path.string());
        const auto* elf = file ? llvm::dyn_cast<llvm::object::ELFObjectFileBase>(file->getBinary()) : nullptr;
        if (!elf) {
            if (!file) llvm::consumeError(file.takeError());
            error = "cannot read the stub of glibc " + path.string() + "";
            return false;
        }
        for (const llvm::object::ELFSymbolRef& symbol : elf->getDynamicSymbolIterators()) {
            llvm::Expected<llvm::StringRef> name = symbol.getName();
            if (!name) {
                llvm::consumeError(name.takeError());
                continue;
            }
            if (undefined.count(name->str()) && !defined.count(name->str()) && !runtime.count(name->str())) {
                calls = true;
                return true;
            }
        }
    }
    return true;
}

bool copyLibraries(const std::vector<std::string>& dlls, const std::string& executable, std::string& error) {
    fs::path folder = fs::absolute(executable).parent_path();
    for (const std::string& dll : dlls) {
        fs::path from(dll);
        fs::path to = folder / from.filename();
        std::error_code ec;
        if (fs::equivalent(from.parent_path(), folder, ec)) continue;
        // Only a copy that is missing or different (D141): the program may be running.
        bool same = isFile(to) && fs::file_size(to, ec) == fs::file_size(from, ec) &&
                    fs::last_write_time(to, ec) == fs::last_write_time(from, ec);
        if (same && !ec) continue;
        ec.clear();
        fs::file_time_type time = fs::last_write_time(from, ec);
        if (!ec) fs::copy_file(from, to, fs::copy_options::overwrite_existing, ec);
        if (!ec) fs::last_write_time(to, time, ec);
        if (ec) {
            error = "cannot copy " + from.filename().string() + " next to the executable: " + ec.message() + "";
            return false;
        }
    }
    return true;
}

}  // namespace cromo
