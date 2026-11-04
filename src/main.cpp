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
#include <thread>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#else
#include <fcntl.h>
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
        "  test       runs the [[test]] functions of file.cro, or of main.cro of this folder, and\n"
        "             of the files name_test.cro of its modules; the names after the file choose\n"
        "             the tests that contain them\n"
        "  run        compiles and runs the program; its arguments come after --\n"
        "  fmt        formats the .cro files, by default of this folder and its subfolders;\n"
        "             --check only lists the ones to format, - formats the standard input\n"
        "  lsp        the language server for editors, on the standard input and output\n"
        "  update     installs the latest version of cromo and makes it the active one; with a\n"
        "             version, as cromo update 0.28.0, that one, also an older one\n"
        "  version    prints the version of cromo and of LLVM\n"
        "  help       prints this text\n"
        "\n"
        "options of build and run:\n"
        "  -o <file>    name of the executable (build; default: the source name, or the folder\n"
        "               without a file, with .exe for Windows)\n"
        "  --target     the system of the executable: windows, linux, linux-arm64, android-arm64\n"
        "               or android-x86_64 (build; default: the one cromo runs on)\n"
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
    bool runtime = false;  // --runtime: the object of std.runtime, for the archive of the runtime (D393)
    bool checkNames = false;  // --check-names: only the names of the modules, for the pilot (D423)
    std::vector<std::string> filters;  // cromo test: the names of the tests to run (D352)
    std::vector<std::string> tests;    // cromo test: the names of the tests, by their number
};

// Compiles the program and writes the executable. Returns the exit code of cromo.
int build(Compilation& compilation) {
    cromo::BackendOptions& options = compilation.options;
    cromo::CodegenOptions& codegen = compilation.codegen;
    const std::string& inputPath = compilation.inputPath;
    codegen.target = options.target;

    std::deque<cromo::SourceFile> sources;
    // The standard library is compiled together with the program (D97), from lib/ (D237).
    if (cromo::libraryFolder().empty()) {
        std::fputs("cromo: cannot find the library: lib/ is missing next to the folder of cromo (D237)\n", stderr);
        return 2;
    }
    if (compilation.runtime) {
        // D393: the runtime written in Cromo is std.runtime, which only a file of the
        // library can import; it is compiled once, with the build of cromo, and
        // goes in the archive of the runtime next to the C, as in --release.
        sources.push_back({cromo::libraryFile("runtime-object.cro").string(), "import std.runtime\n"});
        codegen.release = true;
        codegen.runtime = true;
        options.objectOnly = true;
    } else {
        sources.push_back({inputPath, {}});
        if (!cromo::readSourceFile(inputPath, sources[0].text)) {
            std::fprintf(stderr, "cromo: cannot read %s\n", inputPath.c_str());
            return 2;
        }
    }
    sources.push_back({cromo::libraryFile("core.cro").string(), {}});
    if (!cromo::readSourceFile(sources[1].path, sources[1].text)) {
        std::fprintf(stderr, "cromo: cannot read %s\n", sources[1].path.c_str());
        return 2;
    }
    cromo::Diagnostics diag(sources[0]);
    diag.addFile(sources[1]);
    bool tests = codegen.tests;
    cromo::Module module = cromo::loadProgram(sources, diag, options.target, nullptr, tests);
    if (compilation.checkNames) {  // D423: what the pilot of sema checks, and nothing more
        if (!diag.hasErrors()) cromo::checkNames(module, diag);
        return diag.hasErrors() ? 1 : 0;
    }
    bool requireMain = !tests && !compilation.runtime;
    if (diag.hasErrors() || !cromo::check(module, diag, requireMain) || !cromo::checkMoves(module, diag)) return 1;
    if (tests) {  // in the order of the entry point of the tests (codegen.cpp)
        std::string program = module.files[0].name + ".";
        for (const cromo::FunctionDecl& function : module.functions) {
            if (!function.isTest || module.files[function.loc.file].isLibrary) continue;
            bool own = function.symbol.compare(0, program.size(), program) == 0;
            compilation.tests.push_back(own ? function.symbol.substr(program.size()) : function.symbol);
        }
    }

    // The libraries of the extern blocks, with their C functions checked in the DLLs (D136, D137, D143).
    cromo::Libraries libraries = cromo::findLibraries(module, sources, options.target, diag);
    if (diag.hasErrors()) return 1;
    options.archives = libraries.archives;
    options.imports = libraries.imports;
    std::string failure;
    if (options.target == cromo::Target::Linux && !cromo::callsTheCLibrary(options.archives, options.glibc, failure)) {
        std::fprintf(stderr, "cromo: %s\n", failure.c_str());  // D428
        return 1;
    }
    if (options.target == cromo::Target::Windows && !tests && !compilation.runtime) {
        for (const cromo::FunctionDecl& function : module.functions) {
            if (function.isWindow) options.window = true;  // D426
        }
        // D425: the icon next to the file with main, with its name or main.ico.
        fs::path source(inputPath);
        std::error_code ec;
        for (const fs::path& icon : {fs::path(source).replace_extension(".ico"), source.parent_path() / "main.ico"}) {
            if (fs::is_regular_file(icon, ec)) {
                options.icon = icon.string();
                break;
            }
        }
    }

    std::string sourceName = fs::path(sources[0].path).filename().string();
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
    bool test = compilation.codegen.tests;
    const char* command = test ? "test" : run ? "run" : "build";
    for (size_t i = first; i < last; ++i) {
        const std::string& arg = args[i];
        if ((arg == "-o" || arg == "--emit-ir" || arg == "--target") && (run || test)) {
            std::fprintf(stderr, "cromo: %s is an option of cromo build (D159)\n", arg.c_str());
            return false;
        }
        if (arg == "-o" && i + 1 < last) {
            options.outputPath = args[++i];
        } else if (arg == "--target" && i + 1 < last) {
            const std::string& target = args[++i];  // D142, D144, D406
            if (target == "windows") {
                options.target = cromo::Target::Windows;
            } else if (target == "linux") {
                options.target = cromo::Target::Linux;
            } else if (target == "linux-arm64") {
                options.target = cromo::Target::LinuxArm64;
            } else if (target == "android-arm64") {  // P43
                options.target = cromo::Target::AndroidArm64;
            } else if (target == "android-x86_64") {
                options.target = cromo::Target::AndroidX64;
            } else {
                std::fprintf(stderr,
                             "cromo: unknown target: %s (windows, linux, linux-arm64, android-arm64 or android-x86_64)\n",
                             target.c_str());
                return false;
            }
        } else if (arg == "--release") {
            compilation.codegen.release = true;  // D57
        } else if (arg == "--emit-ir") {
            compilation.emitIr = true;
        } else if (arg == "--runtime" && !run && !test) {
            compilation.runtime = true;  // D393: only for the build of cromo, not in the help
        } else if (arg == "--check-names" && !run && !test) {
            compilation.checkNames = true;  // D423: the reference of the pilot, not in the help
        } else if (arg.size() == 3 && arg[0] == '-' && arg[1] == 'O' && arg[2] >= '0' && arg[2] <= '3') {
            options.optLevel = arg[2] - '0';
        } else if (arg[0] != '-' && compilation.inputPath.empty() && (!test || fs::path(arg).extension() == ".cro")) {
            compilation.inputPath = arg;
        } else if (arg[0] != '-' && test) {
            compilation.filters.push_back(arg);  // a part of the names of the tests to run
        } else {
            std::fprintf(stderr, "cromo: unknown option of cromo %s: %s\n\n", command, arg.c_str());
            printUsage(stderr);
            return false;
        }
    }
    bool windows = options.target == cromo::Target::Windows;
    if (compilation.runtime) {
        if (options.outputPath.empty() || !compilation.inputPath.empty()) {
            std::fputs("cromo: cromo build --runtime [--target system] -o <object>, for the build of cromo (D393)\n", stderr);
            return false;
        }
        return true;
    }
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

// ---- cromo test (D349-D352) ----

// A test to run, and what it left.
struct TestRun {
    std::string name;
    size_t index = 0;   // its number for the executable of the tests
    fs::path output;    // its standard output and error
    int code = 0;
    bool done = false;
};

std::string readFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// "ok    name", or "FAIL  name" and what the test wrote, indented.
void reportTest(const TestRun& run) {
    if (run.code == 0) {
        std::printf("ok    %s\n", run.name.c_str());
        return;
    }
    std::printf("FAIL  %s\n", run.name.c_str());
    std::string text = readFile(run.output);
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(start, end - start);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::printf("      %s\n", line.c_str());
        start = end + 1;
    }
    if (run.code != 101 && run.code != 1) {  // not a panic, not an exception: say how it ended
        std::printf("      the test ended with the exit code %d\n", run.code);
    }
}

#ifdef _WIN32

// Starts the executable of the tests for one test, its output to a file.
HANDLE startTest(const fs::path& exe, const TestRun& run) {
    SECURITY_ATTRIBUTES inherit{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE output = CreateFileW(run.output.wstring().c_str(), GENERIC_WRITE, FILE_SHARE_READ, &inherit, CREATE_ALWAYS,
                                FILE_ATTRIBUTE_NORMAL, nullptr);
    HANDLE input = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit, OPEN_EXISTING, 0, nullptr);
    if (output == INVALID_HANDLE_VALUE || input == INVALID_HANDLE_VALUE) return nullptr;
    std::wstring line = quoteArgument(exe.wstring()) + L" " + std::to_wstring(run.index);
    STARTUPINFOW startup{};
    startup.cb = sizeof startup;
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = input;
    startup.hStdOutput = output;
    startup.hStdError = output;
    PROCESS_INFORMATION process{};
    BOOL started = CreateProcessW(nullptr, line.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &startup, &process);
    CloseHandle(output);  // the test has its own: the next one must not inherit it
    CloseHandle(input);
    if (!started) return nullptr;
    CloseHandle(process.hThread);
    return process.hProcess;
}

// D352: each test in its own process, as many at a time as the processors.
void runTests(const fs::path& exe, std::vector<TestRun>& runs) {
    size_t width = std::min<size_t>(std::max(1u, std::thread::hardware_concurrency()), MAXIMUM_WAIT_OBJECTS);
    std::vector<HANDLE> running;
    std::vector<size_t> which;
    size_t next = 0;
    size_t shown = 0;
    while (shown < runs.size()) {
        while (next < runs.size() && running.size() < width) {
            HANDLE process = startTest(exe, runs[next]);
            if (!process) {
                runs[next].code = -1;
                runs[next].done = true;
            } else {
                running.push_back(process);
                which.push_back(next);
            }
            ++next;
        }
        if (!running.empty()) {
            DWORD signaled = WaitForMultipleObjects(static_cast<DWORD>(running.size()), running.data(), FALSE, INFINITE);
            size_t slot = signaled - WAIT_OBJECT_0;
            if (slot < running.size()) {
                DWORD exitCode = 1;
                GetExitCodeProcess(running[slot], &exitCode);
                CloseHandle(running[slot]);
                runs[which[slot]].code = static_cast<int>(exitCode);
                runs[which[slot]].done = true;
                running.erase(running.begin() + slot);
                which.erase(which.begin() + slot);
            }
        }
        while (shown < runs.size() && runs[shown].done) reportTest(runs[shown++]);  // in the order of the file
        std::fflush(stdout);
    }
}

#else

pid_t startTest(const fs::path& exe, const TestRun& run) {
    std::string program = exe.string();
    std::string index = std::to_string(run.index);
    std::string output = run.output.string();
    std::fflush(nullptr);
    pid_t child = fork();
    if (child == 0) {
        int file = open(output.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        int nothing = open("/dev/null", O_RDONLY);
        if (file < 0 || nothing < 0) _exit(127);
        dup2(nothing, 0);
        dup2(file, 1);
        dup2(file, 2);
        char* argv[] = {program.data(), index.data(), nullptr};
        execv(argv[0], argv);
        _exit(127);
    }
    return child;
}

void runTests(const fs::path& exe, std::vector<TestRun>& runs) {
    size_t width = std::max(1u, std::thread::hardware_concurrency());
    std::map<pid_t, size_t> running;
    size_t next = 0;
    size_t shown = 0;
    while (shown < runs.size()) {
        while (next < runs.size() && running.size() < width) {
            pid_t child = startTest(exe, runs[next]);
            if (child < 0) {
                runs[next].code = -1;
                runs[next].done = true;
            } else {
                running[child] = next;
            }
            ++next;
        }
        if (!running.empty()) {
            int status = 0;
            pid_t child = waitpid(-1, &status, 0);
            auto found = running.find(child);
            if (found != running.end()) {
                TestRun& run = runs[found->second];
                run.code = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
                run.done = true;
                running.erase(found);
            }
        }
        while (shown < runs.size() && runs[shown].done) reportTest(runs[shown++]);
        std::fflush(stdout);
    }
}

#endif

// D349-D352: builds the tests of the program in a temporary folder, runs each in
// its own process and says which failed. Returns 0 when all passed.
int testCommand(Compilation& compilation) {
    std::error_code ec;
#ifdef _WIN32
    std::string id = std::to_string(GetCurrentProcessId());
    const char* extension = ".exe";
#else
    std::string id = std::to_string(getpid());
    const char* extension = "";
#endif
    fs::path folder = fs::temp_directory_path(ec) / ("cromo-test-" + id);
    fs::create_directories(folder, ec);
    if (ec) {
        std::fprintf(stderr, "cromo: cannot create the folder %s: %s\n", folder.string().c_str(), ec.message().c_str());
        return 1;
    }
    compilation.options.outputPath = (folder / (std::string("tests") + extension)).string();
    int code = build(compilation);
    if (code != 0) {
        fs::remove_all(folder, ec);
        return code;
    }
    std::vector<TestRun> runs;
    for (size_t i = 0; i < compilation.tests.size(); ++i) {
        const std::string& name = compilation.tests[i];
        bool chosen = compilation.filters.empty();
        for (const std::string& filter : compilation.filters) chosen = chosen || name.find(filter) != std::string::npos;
        if (!chosen) continue;
        TestRun run;
        run.name = name;
        run.index = i;
        run.output = folder / ("test" + std::to_string(i) + ".out");
        runs.push_back(std::move(run));
    }
    if (runs.empty()) {
        std::printf(compilation.tests.empty() ? "no tests: write them as [[test]] void name() (D349)\n"
                                              : "no test has those names\n");
        fs::remove_all(folder, ec);
        return compilation.tests.empty() ? 0 : 1;
    }
    runTests(fs::path(compilation.options.outputPath), runs);
    size_t failed = 0;
    for (const TestRun& run : runs) failed += run.code != 0;
    std::printf("%zu passed, %zu failed\n", runs.size() - failed, failed);
    fs::remove_all(folder, ec);
    return failed == 0 ? 0 : 1;
}

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
        // D403: a program written in Cromo, in the library of this cromo, compiled
        // and run as by cromo run, with the version asked (D289).
        if (args.size() > 3) {
            std::fputs("cromo: cromo update takes at most a version, as cromo update 0.28.0 (D289)\n", stderr);
            return 2;
        }
        Compilation compilation;
        compilation.inputPath = cromo::libraryFile("tools/update.cro").string();
        compilation.options.outputPath = "update";
#ifdef _WIN32
        std::vector<std::wstring> programArgs(wide.begin() + 2, wide.end());
#else
        std::vector<std::string> programArgs(args.begin() + 2, args.end());
#endif
        return run(compilation, programArgs);
    }
    if (command == "fmt") return formatCommand(args);
    if (command == "lsp") return cromo::runLanguageServer();  // D170
    if (command == "rls") {
        std::fputs("cromo: the language server is cromo lsp (D170)\n", stderr);
        return 2;
    }
    if (command != "build" && command != "run" && command != "test") {
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
    compilation.codegen.tests = command == "test";
    if (!readOptions(args, 2, separator, isRun, compilation)) return 2;
    if (compilation.codegen.tests) return testCommand(compilation);
    if (!isRun) return build(compilation);

#ifdef _WIN32
    std::vector<std::wstring> programArgs;
    for (size_t i = separator + 1; i < wide.size(); ++i) programArgs.push_back(wide[i]);
#else
    std::vector<std::string> programArgs(args.begin() + std::min(separator + 1, args.size()), args.end());
#endif
    return run(compilation, programArgs);
}
