#include "backend.h"
#include "codegen.h"
#include "diagnostics.h"
#include "lexer.h"
#include "ownership.h"
#include "parser.h"
#include "runtime_blob.h"
#include "sema.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace {

void printUsage() {
    std::fputs(
        "usage: cromo <source.cro> [-o <executable>] [--release] [-O0|-O1|-O2|-O3] [--emit-ir]\n"
        "\n"
        "  -o <file>    name of the executable (default: the source name with .exe)\n"
        "  --release    no overflow checks: integers wrap around (default: debug, with checks)\n"
        "  -O<n>        optimization level (default: -O2)\n"
        "  --emit-ir    print the generated LLVM IR instead of producing the executable\n",
        stderr);
}

bool readFile(const std::string& path, std::string& text) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    if (text.size() >= 3 && text.compare(0, 3, "\xEF\xBB\xBF") == 0) text.erase(0, 3);  // UTF-8 BOM
    return true;
}

namespace fs = std::filesystem;

// Reads the main file, the standard library and every module they import (D50).
// `import a.b` is the file a/b.cro, or every .cro file of the folder a/b, in the
// folder of the main file (D130). `sources` is a deque: Diagnostics keeps their
// addresses.
class Loader {
public:
    Loader(std::deque<cromo::SourceFile>& sources, cromo::Diagnostics& diag) : sources_(sources), diag_(diag) {
        fs::path main = fs::absolute(sources[0].path);
        root_ = main.parent_path();
        loaded_[fs::weakly_canonical(main)] = 0;
        names_ = {main.stem().string(), "std"};
    }

    cromo::Module load() {
        parseFile(cromo::kLibraryFile);
        parseFile(0);
        // The list of files grows while their imports are loaded.
        for (size_t i = 0; i < sources_.size(); ++i) {
            for (size_t k = 0; k < program_.files[i].imports.size(); ++k) {
                std::vector<int> files = resolve(program_.files[i].imports[k]);
                program_.files[i].imports[k].files = std::move(files);
            }
            checkQualifiers(program_.files[i]);
        }
        return std::move(program_);
    }

private:
    void parseFile(int index) {
        cromo::Module module = cromo::parse(cromo::tokenize(sources_[index], index, diag_), diag_);
        module.files.resize(index + 1);
        module.files[index].name = names_[index];
        program_.append(std::move(module));
    }

    std::vector<int> resolve(const cromo::ImportDecl& import) {
        std::string dotted;
        fs::path relative;
        for (const std::string& part : import.path) {
            dotted += (dotted.empty() ? "" : ".") + part;
            relative /= part;
        }
        if (import.path[0] == "std") {
            diag_.error(import.loc, "the standard library has no module " + dotted +
                                        " yet: its core is visible without import (D130)");
            return {};
        }
        fs::path file = root_ / relative;
        file += ".cro";
        fs::path folder = root_ / relative;
        std::error_code ec;
        bool isFile = fs::is_regular_file(file, ec);
        bool isFolder = fs::is_directory(folder, ec);
        std::string shown = relative.generic_string();
        if (isFile && isFolder) {
            diag_.error(import.loc, "both " + shown + ".cro and the folder " + shown + " exist: rename one of them (D130)");
            return {};
        }
        if (isFile) return {loadFile(file, dotted, import.loc)};
        if (isFolder) {
            std::vector<fs::path> paths;
            for (const fs::directory_entry& entry : fs::directory_iterator(folder, ec)) {
                if (entry.is_regular_file() && entry.path().extension() == ".cro") paths.push_back(entry.path());
            }
            std::sort(paths.begin(), paths.end());
            if (paths.empty()) diag_.error(import.loc, "the folder " + shown + " has no .cro files (D130)");
            std::vector<int> files;
            for (const fs::path& path : paths) files.push_back(loadFile(path, dotted + "." + path.stem().string(), import.loc));
            return files;
        }
        diag_.error(import.loc, "cannot find the module " + dotted + ": there is no " + shown + ".cro or folder " + shown +
                                    " next to " + fs::path(sources_[0].path).filename().string() + " (D130)");
        return {};
    }

    int loadFile(const fs::path& path, const std::string& name, cromo::SourceLocation at) {
        fs::path key = fs::weakly_canonical(path);
        auto found = loaded_.find(key);
        if (found != loaded_.end()) return found->second;
        sources_.push_back({path.string(), {}});
        if (!readFile(path.string(), sources_.back().text)) diag_.error(at, "cannot read " + path.string());
        int index = diag_.addFile(sources_.back());
        loaded_[key] = index;
        names_.push_back(name);
        parseFile(index);
        return index;
    }

    // Two imports whose names would be written the same way in front (D126).
    void checkQualifiers(const cromo::ModuleFile& file) {
        std::set<std::string> seen;
        for (const cromo::ImportDecl& import : file.imports) {
            if (!seen.insert(import.qualifier()).second) {
                diag_.error(import.loc, "another import is also called " + import.qualifier() + " (D131)");
            }
        }
    }

    std::deque<cromo::SourceFile>& sources_;
    cromo::Diagnostics& diag_;
    fs::path root_;
    std::map<fs::path, int> loaded_;
    std::vector<std::string> names_;  // of the modules, by file
    cromo::Module program_;
};

}  // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif

    std::string inputPath;
    cromo::BackendOptions options;
    cromo::CodegenOptions codegen;
    bool emitIr = false;

    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if (std::strcmp(arg, "-o") == 0 && i + 1 < argc) {
            options.outputPath = argv[++i];
        } else if (std::strcmp(arg, "--release") == 0) {
            codegen.release = true;  // D57
        } else if (std::strcmp(arg, "--emit-ir") == 0) {
            emitIr = true;
        } else if (std::strlen(arg) == 3 && arg[0] == '-' && arg[1] == 'O' && arg[2] >= '0' && arg[2] <= '3') {
            options.optLevel = arg[2] - '0';
        } else if (std::strcmp(arg, "-h") == 0 || std::strcmp(arg, "--help") == 0) {
            printUsage();
            return 0;
        } else if (arg[0] != '-' && inputPath.empty()) {
            inputPath = arg;
        } else {
            std::fprintf(stderr, "cromo: unknown option: %s\n\n", arg);
            printUsage();
            return 2;
        }
    }
    if (inputPath.empty()) {
        printUsage();
        return 2;
    }
    if (options.outputPath.empty()) {
        options.outputPath = std::filesystem::path(inputPath).replace_extension(".exe").string();
    }

    std::deque<cromo::SourceFile> sources;
    sources.push_back({inputPath, {}});
    if (!readFile(inputPath, sources[0].text)) {
        std::fprintf(stderr, "cromo: cannot read %s\n", inputPath.c_str());
        return 2;
    }

    // The standard library is compiled together with the program (D97).
    sources.push_back({"cromo:core.cro", std::string(cromo::coreLibrary())});
    cromo::Diagnostics diag(sources[0]);
    diag.addFile(sources[1]);
    cromo::Module module = Loader(sources, diag).load();
    if (diag.hasErrors() || !cromo::check(module, diag) || !cromo::checkMoves(module, diag)) return 1;

    // The libraries of the extern blocks, looked for next to their modules (D128, D133).
    std::set<std::string> libraries;
    for (const cromo::FunctionDecl& function : module.functions) {
        if (!function.isExtern || !libraries.insert(function.library).second) continue;
        options.libraries.push_back(function.library);
    }
    for (const cromo::FunctionDecl& function : module.functions) {
        if (!function.isExtern || function.loc.file == cromo::kLibraryFile) continue;
        std::string folder = fs::absolute(sources[function.loc.file].path).parent_path().string();
        if (std::find(options.libraryPaths.begin(), options.libraryPaths.end(), folder) == options.libraryPaths.end()) {
            options.libraryPaths.push_back(folder);
        }
    }

    std::string sourceName = fs::path(inputPath).filename().string();
    for (const cromo::SourceFile& source : sources) codegen.sourcePaths.push_back(source.path);
    std::string ir = cromo::generateIR(module, codegen);
    if (emitIr) {
        std::fwrite(ir.data(), 1, ir.size(), stdout);
        return 0;
    }

    std::string error;
    if (!cromo::buildExecutable(ir, sourceName, options, error)) {
        std::fprintf(stderr, "cromo: %s\n", error.c_str());
        return 1;
    }
    return 0;
}
