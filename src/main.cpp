#include "backend.h"
#include "codegen.h"
#include "diagnostics.h"
#include "lexer.h"
#include "libraries.h"
#include "ownership.h"
#include "parser.h"
#include "runtime_blob.h"
#include "sema.h"

#include <llvm/Config/llvm-config.h>

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

void printUsage(FILE* out) {
    std::fputs(
        "usage: cromo <command> [options] [file.cro] [-- arguments]\n"
        "\n"
        "commands (D151):\n"
        "  build      compiles file.cro, or main.cro of this folder, into an executable\n"
        "  run        compiles and runs the program; its arguments come after --\n"
        "  version    prints the version of cromo and of LLVM\n"
        "  help       prints this text\n"
        "\n"
        "options of build and run:\n"
        "  -o <file>    name of the executable (build; default: the source name, or the folder\n"
        "               without a file, with .exe for Windows)\n"
        "  --target     the system of the executable: windows (default) or linux (build)\n"
        "  --release    no overflow checks: integers wrap around (default: debug, with checks)\n"
        "  -O<n>        optimization level (default: -O2)\n"
        "  --emit-ir    print the generated LLVM IR instead of producing the executable (build)\n",
        out);
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

// The options of build and run, and the program to compile.
struct Compilation {
    std::string inputPath;
    cromo::BackendOptions options;
    cromo::CodegenOptions codegen;
    bool emitIr = false;
};

// Compiles the program and writes the executable. Returns the exit code of cromo.
int build(Compilation& compilation) {
    cromo::BackendOptions& options = compilation.options;
    cromo::CodegenOptions& codegen = compilation.codegen;
    const std::string& inputPath = compilation.inputPath;
    codegen.target = options.target;

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

    // The libraries of the extern blocks, with their C functions checked in the DLLs (D136, D137, D143).
    cromo::Libraries libraries = cromo::findLibraries(module, sources, options.target, diag);
    if (diag.hasErrors()) return 1;
    options.archives = libraries.archives;
    options.imports = libraries.imports;

    std::string sourceName = fs::path(inputPath).filename().string();
    for (const cromo::SourceFile& source : sources) codegen.sourcePaths.push_back(source.path);
    std::string ir = cromo::generateIR(module, codegen);
    if (compilation.emitIr) {
        std::fwrite(ir.data(), 1, ir.size(), stdout);
        return 0;
    }

    std::string error;
    if (!cromo::buildExecutable(ir, sourceName, options, error) ||
        !cromo::copyLibraries(libraries.copies, options.outputPath, error)) {
        std::fprintf(stderr, "cromo: %s\n", error.c_str());
        return 1;
    }
    return 0;
}

// Reads the options of build and run, from `args[first]` to `args[last]`
// (excluded). Returns false, after printing why, if they are wrong.
bool readOptions(const std::vector<std::string>& args, size_t first, size_t last, bool run, Compilation& compilation) {
    cromo::BackendOptions& options = compilation.options;
    const char* command = run ? "run" : "build";
    for (size_t i = first; i < last; ++i) {
        const std::string& arg = args[i];
        if ((arg == "-o" || arg == "--emit-ir" || arg == "--target") && run) {
            std::fprintf(stderr, "cromo: %s is an option of cromo build (D159)\n", arg.c_str());
            return false;
        }
        if (arg == "-o" && i + 1 < last) {
            options.outputPath = args[++i];
        } else if (arg == "--target" && i + 1 < last) {
            const std::string& target = args[++i];  // D142, D144
            if (target == "windows") {
                options.target = cromo::Target::Windows;
            } else if (target == "linux") {
                options.target = cromo::Target::Linux;
            } else {
                std::fprintf(stderr, "cromo: unknown target: %s (windows or linux)\n", target.c_str());
                return false;
            }
        } else if (arg == "--release") {
            compilation.codegen.release = true;  // D57
        } else if (arg == "--emit-ir") {
            compilation.emitIr = true;
        } else if (arg.size() == 3 && arg[0] == '-' && arg[1] == 'O' && arg[2] >= '0' && arg[2] <= '3') {
            options.optLevel = arg[2] - '0';
        } else if (arg[0] != '-' && compilation.inputPath.empty()) {
            compilation.inputPath = arg;
        } else {
            std::fprintf(stderr, "cromo: unknown option of cromo %s: %s\n\n", command, arg.c_str());
            printUsage(stderr);
            return false;
        }
    }
    bool windows = options.target == cromo::Target::Windows;
    if (compilation.inputPath.empty()) {
        // D155: main.cro of this folder, and the executable named after the folder.
        std::error_code ec;
        if (!fs::is_regular_file("main.cro", ec)) {
            std::fprintf(stderr,
                         "cromo: there is no main.cro in this folder: write the file to compile, as cromo %s hello.cro "
                         "(D155)\n",
                         command);
            return false;
        }
        compilation.inputPath = "main.cro";
        if (options.outputPath.empty()) {
            std::string folder = fs::current_path(ec).filename().string();
            options.outputPath = (folder.empty() ? std::string("main") : folder) + (windows ? ".exe" : "");
        }
    }
    if (options.outputPath.empty()) {
        options.outputPath = fs::path(compilation.inputPath).replace_extension(windows ? ".exe" : "").string();
    }
    return true;
}

#ifdef _WIN32

std::string narrow(const std::wstring& text) {
    if (text.empty()) return {};
    int size = WideCharToMultiByte(CP_ACP, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_ACP, 0, text.data(), static_cast<int>(text.size()), out.data(), size, nullptr, nullptr);
    return out;
}

// Writes an argument in a command line so that the program reads it back as it
// is, with the rules of the C runtime of Microsoft.
std::wstring quoteArgument(const std::wstring& arg) {
    if (!arg.empty() && arg.find_first_of(L" \t\"") == std::wstring::npos) return arg;
    std::wstring out = L"\"";
    size_t backslashes = 0;
    for (wchar_t c : arg) {
        if (c == L'\\') {
            ++backslashes;
            continue;
        }
        out.append(c == L'"' ? backslashes * 2 + 1 : backslashes, L'\\');
        backslashes = 0;
        out += c;
    }
    out.append(backslashes * 2, L'\\');
    return out + L"\"";
}

// D156: compiles the program in a temporary folder, runs it with its arguments
// and deletes the folder. Returns the exit code of the program.
int run(Compilation& compilation, const std::vector<std::wstring>& programArgs) {
    std::error_code ec;
    fs::path folder = fs::temp_directory_path(ec) / ("cromo-run-" + std::to_string(GetCurrentProcessId()));
    fs::create_directories(folder, ec);
    if (ec) {
        std::fprintf(stderr, "cromo: cannot create the folder %s: %s\n", folder.string().c_str(), ec.message().c_str());
        return 1;
    }
    std::string stem = compilation.options.outputPath.empty() ? std::string("main")
                                                               : fs::path(compilation.options.outputPath).stem().string();
    compilation.options.outputPath = (folder / (stem + ".exe")).string();
    int code = build(compilation);
    if (code == 0) {
        std::wstring line = quoteArgument(fs::path(compilation.options.outputPath).wstring());
        for (const std::wstring& arg : programArgs) line += L" " + quoteArgument(arg);
        STARTUPINFOW startup{};
        startup.cb = sizeof startup;
        PROCESS_INFORMATION process{};
        if (!CreateProcessW(nullptr, line.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &startup, &process)) {
            std::fprintf(stderr, "cromo: cannot run %s (error %lu)\n", compilation.options.outputPath.c_str(),
                         GetLastError());
            code = 1;
        } else {
            WaitForSingleObject(process.hProcess, INFINITE);
            DWORD exitCode = 1;
            GetExitCodeProcess(process.hProcess, &exitCode);
            CloseHandle(process.hThread);
            CloseHandle(process.hProcess);
            code = static_cast<int>(exitCode);
        }
    }
    fs::remove_all(folder, ec);
    return code;
}

#endif

}  // namespace

#ifdef _WIN32
int wmain(int argc, wchar_t** wargv) {
    SetConsoleOutputCP(CP_UTF8);
    std::vector<std::wstring> wide(wargv, wargv + argc);
    std::vector<std::string> args;
    for (const std::wstring& arg : wide) args.push_back(narrow(arg));
#else
int main(int argc, char** argv) {
    std::vector<std::string> args(argv, argv + argc);
#endif

    if (args.size() < 2) {
        printUsage(stderr);
        return 2;
    }
    const std::string& command = args[1];
    if (command == "help" || command == "-h" || command == "--help") {
        printUsage(stdout);
        return 0;
    }
    if (command == "version") {
        std::printf("cromo %s (LLVM %s)\n", CROMO_VERSION, LLVM_VERSION_STRING);  // D157
        return 0;
    }
    if (command == "update") {
        std::fputs("cromo: update comes when the resource with the new versions exists (D153)\n", stderr);
        return 2;
    }
    if (command == "fmt") {
        std::fputs("cromo: fmt comes with milestone M11 (D152)\n", stderr);
        return 2;
    }
    if (command == "lsp" || command == "rls") {
        std::fputs("cromo: the language server comes with milestone M12 (D152)\n", stderr);
        return 2;
    }
    if (command != "build" && command != "run") {
        if (fs::path(command).extension() == ".cro") {
            std::fprintf(stderr, "cromo: the command comes first: cromo build %s (D158)\n", command.c_str());
        } else {
            std::fprintf(stderr, "cromo: unknown command: %s\n\n", command.c_str());
            printUsage(stderr);
        }
        return 2;
    }

    bool isRun = command == "run";
    size_t separator = std::find(args.begin() + 2, args.end(), "--") - args.begin();
    if (!isRun && separator != args.size()) {
        std::fputs("cromo: the arguments after -- are for cromo run (D156)\n", stderr);
        return 2;
    }
    Compilation compilation;
    if (!readOptions(args, 2, separator, isRun, compilation)) return 2;
    if (!isRun) return build(compilation);

#ifdef _WIN32
    std::vector<std::wstring> programArgs;
    for (size_t i = separator + 1; i < wide.size(); ++i) programArgs.push_back(wide[i]);
    return run(compilation, programArgs);
#else
    std::fputs("cromo: cromo run works only on Windows for now\n", stderr);
    return 2;
#endif
}
