#include "backend.h"
#include "codegen.h"
#include "diagnostics.h"
#include "format.h"
#include "lexer.h"
#include "libraries.h"
#include "loader.h"
#include "lsp.h"
#include "ownership.h"
#include "installation.h"
#include "parser.h"
#include "sema.h"

#include <llvm/Config/llvm-config.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace {

void printUsage(FILE* out) {
    std::fputs(
        "usage: cromo <command> [options] [file.cro] [-- arguments]\n"
        "       cromo fmt [--check] [files and folders | -]\n"
        "\n"
        "commands (D151):\n"
        "  build      compiles file.cro, or main.cro of this folder, into an executable\n"
        "  run        compiles and runs the program; its arguments come after --\n"
        "  fmt        formats the .cro files, by default of this folder and its subfolders;\n"
        "             --check only lists the ones to format, - formats the standard input\n"
        "  lsp        the language server for editors, on the standard input and output\n"
        "  version    prints the version of cromo and of LLVM\n"
        "  help       prints this text\n"
        "\n"
        "options of build and run:\n"
        "  -o <file>    name of the executable (build; default: the source name, or the folder\n"
        "               without a file, with .exe for Windows)\n"
        "  --target     the system of the executable: windows or linux (build; default: the one\n"
        "               cromo runs on)\n"
        "  --release    no overflow checks: integers wrap around (default: debug, with checks)\n"
        "  -O<n>        optimization level (default: -O2)\n"
        "  --emit-ir    print the generated LLVM IR instead of producing the executable (build)\n",
        out);
}

namespace fs = std::filesystem;

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
    if (!cromo::readSourceFile(inputPath, sources[0].text)) {
        std::fprintf(stderr, "cromo: cannot read %s\n", inputPath.c_str());
        return 2;
    }

    // The standard library is compiled together with the program (D97), from lib/ (D237).
    if (cromo::libraryFolder().empty()) {
        std::fputs("cromo: cannot find the library: lib/ is missing next to the folder of cromo (D237)\n", stderr);
        return 2;
    }
    sources.push_back({cromo::libraryFile("core.cro").string(), {}});
    if (!cromo::readSourceFile(sources[1].path, sources[1].text)) {
        std::fprintf(stderr, "cromo: cannot read %s\n", sources[1].path.c_str());
        return 2;
    }
    cromo::Diagnostics diag(sources[0]);
    diag.addFile(sources[1]);
    cromo::Module module = cromo::loadProgram(sources, diag, options.target);
    if (diag.hasErrors() || !cromo::check(module, diag) || !cromo::checkMoves(module, diag)) return 1;

    // The libraries of the extern blocks, with their C functions checked in the DLLs (D136, D137, D143).
    cromo::Libraries libraries = cromo::findLibraries(module, sources, options.target, diag);
    if (diag.hasErrors()) return 1;
    options.archives = libraries.archives;
    options.imports = libraries.imports;

    std::string sourceName = fs::path(inputPath).filename().string();
    // In the messages of the program, a file of the library is named without the
    // folder where cromo is installed: "cromo:std/fs.cro".
    for (const cromo::SourceFile& source : sources) {
        codegen.sourcePaths.push_back(cromo::isLibraryFile(source.path) ? cromo::libraryName(source.path) : source.path);
        std::error_code ec;
        fs::path full = fs::absolute(source.path, ec);  // a debugger finds the file from anywhere (D338)
        codegen.debugPaths.push_back(ec ? source.path : full.lexically_normal().string());
        options.fileNames.emplace_back(codegen.debugPaths.back(), codegen.sourcePaths.back());
    }
    codegen.optimized = options.optLevel > 0;
    options.debugInfo = !codegen.release;
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

// Every .cro file of a folder and of its subfolders, except the folders whose
// name starts with a dot (D165).
void collectSources(const fs::path& folder, std::vector<fs::path>& files) {
    std::error_code ec;
    std::vector<fs::path> entries;
    for (const fs::directory_entry& entry : fs::directory_iterator(folder, ec)) entries.push_back(entry.path());
    std::sort(entries.begin(), entries.end());
    for (const fs::path& path : entries) {
        if (fs::is_directory(path, ec)) {
            if (path.filename().string().rfind('.', 0) != 0) collectSources(path, files);
        } else if (path.extension() == ".cro") {
            files.push_back(path.lexically_normal());
        }
    }
}

// The text of a file as it is, and whether it starts with the UTF-8 BOM, which is
// written back.
bool readRaw(const fs::path& path, std::string& text, bool& bom) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    bom = text.compare(0, 3, "\xEF\xBB\xBF") == 0;
    if (bom) text.erase(0, 3);
    return true;
}

// Formats one text; on failure the problem is already printed.
bool formatText(const std::string& path, const std::string& text, std::string& out) {
    cromo::SourceFile file{path, text};
    cromo::Diagnostics diag(file);
    std::string error;
    if (cromo::formatSource(file, diag, out, error)) return true;
    if (!error.empty()) std::fprintf(stderr, "cromo: %s: internal error of cromo fmt: %s (D169)\n", path.c_str(), error.c_str());
    return false;
}

// D161-D163: cromo fmt [--check] [files and folders | -]. Returns the exit code.
int formatCommand(const std::vector<std::string>& args) {
    bool check = false;
    bool standardInput = false;
    std::vector<std::string> paths;
    for (size_t i = 2; i < args.size(); ++i) {
        if (args[i] == "--check") {
            check = true;
        } else if (args[i] == "-") {
            standardInput = true;
        } else if (!args[i].empty() && args[i][0] == '-') {
            std::fprintf(stderr, "cromo: unknown option of cromo fmt: %s\n", args[i].c_str());
            return 2;
        } else {
            paths.push_back(args[i]);
        }
    }

    if (standardInput) {  // for editors (D164)
        if (check || !paths.empty()) {
            std::fputs("cromo: cromo fmt - only reads the standard input\n", stderr);
            return 2;
        }
#ifdef _WIN32
        _setmode(_fileno(stdin), _O_BINARY);  // the line endings stay as they are
        _setmode(_fileno(stdout), _O_BINARY);
#endif
        std::string text((std::istreambuf_iterator<char>(std::cin)), std::istreambuf_iterator<char>());
        bool bom = text.compare(0, 3, "\xEF\xBB\xBF") == 0;
        if (bom) text.erase(0, 3);
        std::string out;
        if (!formatText("<stdin>", text, out)) return 1;
        if (bom) out.insert(0, "\xEF\xBB\xBF");
        std::fwrite(out.data(), 1, out.size(), stdout);
        return 0;
    }

    std::vector<fs::path> files;
    if (paths.empty()) collectSources(".", files);
    for (const std::string& path : paths) {
        std::error_code ec;
        if (fs::is_directory(path, ec)) {
            collectSources(path, files);
        } else if (fs::is_regular_file(path, ec)) {
            files.push_back(fs::path(path).lexically_normal());
        } else {
            std::fprintf(stderr, "cromo: cannot find %s\n", path.c_str());
            return 2;
        }
    }
    int result = 0;
    for (const fs::path& file : files) {
        std::string name = file.string();
        std::string text;
        bool bom = false;
        if (!readRaw(file, text, bom)) {
            std::fprintf(stderr, "cromo: cannot read %s\n", name.c_str());
            result = 1;
            continue;
        }
        std::string out;
        if (!formatText(name, text, out)) {
            result = 1;
            continue;
        }
        if (out == text) continue;
        std::printf("%s\n", name.c_str());
        if (check) {  // D165: the files still to format
            result = 1;
            continue;
        }
        std::ofstream write(file, std::ios::binary);
        write << (bom ? "\xEF\xBB\xBF" : "") << out;
        if (!write) {
            std::fprintf(stderr, "cromo: cannot write %s\n", name.c_str());
            result = 1;
        }
    }
    return result;
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

#else

// D156 on Linux: the same, with fork and exec.
int run(Compilation& compilation, const std::vector<std::string>& programArgs) {
    std::error_code ec;
    fs::path folder = fs::temp_directory_path(ec) / ("cromo-run-" + std::to_string(getpid()));
    fs::create_directories(folder, ec);
    if (ec) {
        std::fprintf(stderr, "cromo: cannot create the folder %s: %s\n", folder.string().c_str(), ec.message().c_str());
        return 1;
    }
    std::string stem = compilation.options.outputPath.empty() ? std::string("main")
                                                               : fs::path(compilation.options.outputPath).stem().string();
    compilation.options.outputPath = (folder / stem).string();
    int code = build(compilation);
    if (code == 0) {
        std::vector<char*> argv;
        argv.push_back(compilation.options.outputPath.data());
        std::vector<std::string> copies = programArgs;
        for (std::string& arg : copies) argv.push_back(arg.data());
        argv.push_back(nullptr);
        std::fflush(nullptr);
        pid_t child = fork();
        if (child == 0) {
            execv(argv[0], argv.data());
            _exit(127);
        }
        int status = 0;
        if (child < 0 || waitpid(child, &status, 0) < 0) {
            std::fprintf(stderr, "cromo: cannot run %s\n", compilation.options.outputPath.c_str());
            code = 1;
        } else if (WIFEXITED(status)) {
            code = WEXITSTATUS(status);
        } else {
            code = 128 + WTERMSIG(status);  // as the shells say a signal
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
    if (command == "fmt") return formatCommand(args);
    if (command == "lsp") return cromo::runLanguageServer();  // D170
    if (command == "rls") {
        std::fputs("cromo: the language server is cromo lsp (D170)\n", stderr);
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
#else
    std::vector<std::string> programArgs(args.begin() + std::min(separator + 1, args.size()), args.end());
#endif
    return run(compilation, programArgs);
}
