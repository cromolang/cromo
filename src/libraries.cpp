#include "libraries.h"

#include <llvm/Object/COFF.h>
#include <llvm/Object/ObjectFile.h>
#include <llvm/Support/Error.h>

#include <algorithm>
#include <filesystem>
#include <set>
#include <system_error>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace cromo {

namespace fs = std::filesystem;

namespace {

// The system folder of Windows, where kernel32.dll is (D137).
fs::path systemFolder() {
    wchar_t buffer[MAX_PATH];
    UINT size = GetSystemDirectoryW(buffer, MAX_PATH);
    if (size == 0 || size >= MAX_PATH) return {};
    return fs::path(buffer);
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
    if (!coff || coff->getMachine() != IMAGE_FILE_MACHINE_AMD64) {
        error = dll.filename().string() + " is not a DLL for x64 (D139)";
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
        if (!function.isExtern) continue;
        auto use = std::find_if(uses.begin(), uses.end(), [&](const Use& u) { return u.name == function.library; });
        if (use == uses.end()) use = uses.insert(uses.end(), Use{function.library, {}, {}});
        use->functions.push_back(&function);
        if (function.loc.file == kLibraryFile) continue;  // embedded in cromo: it has no folder
        fs::path folder = fs::absolute(sources[function.loc.file].path).parent_path();
        if (std::find(use->folders.begin(), use->folders.end(), folder) == use->folders.end()) {
            use->folders.push_back(folder);
        }
    }

    Libraries libraries;
    if (target == Target::Linux) {  // the linker checks the names (D146)
        for (const Use& use : uses) {
            std::string file = "lib" + use.name + ".a";
            auto folder = std::find_if(use.folders.begin(), use.folders.end(),
                                       [&](const fs::path& f) { return isFile(f / file); });
            if (folder != use.folders.end()) {
                libraries.archives.push_back((*folder / file).string());
            } else {
                diag.error(use.functions[0]->loc, "cannot find the library '" + use.name + "': there is no " + file +
                                                      " next to the modules that use it (D143)");
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
        if (dll.empty()) {
            diag.error(use.functions[0]->loc, "cannot find the library '" + use.name + "': there is no " + use.name +
                                                  ".lib or " + use.name + ".dll next to the modules that use it, nor " +
                                                  use.name + ".dll in " + system.string() + " (D137)");
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
                diag.error(function->loc, use.name + ".dll has no function '" + function->name + "' (D137)");
            } else if (std::find(import.functions.begin(), import.functions.end(), function->name) ==
                       import.functions.end()) {
                import.functions.push_back(function->name);  // several modules can declare it (D133)
            }
        }
        libraries.imports.push_back(std::move(import));
    }
    return libraries;
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
            error = "cannot copy " + from.filename().string() + " next to the executable: " + ec.message() + " (D138)";
            return false;
        }
    }
    return true;
}

}  // namespace cromo
