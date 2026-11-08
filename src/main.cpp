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
#include <cctype>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <set>
#include <sstream>
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
#include <signal.h>
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
        "  build      compiles file.cro into an executable; without a file, the executables\n"
        "             of the cromo.mod of this folder, or its main.cro; with a name, that\n"
        "             executable of cromo.mod\n"
        "  test       runs the [[test]] functions of file.cro, or of main.cro of this folder, and\n"
        "             of the files name_test.cro of its modules; the names after the file choose\n"
        "             the tests that contain them\n"
        "  run        compiles and runs the program, or an executable of cromo.mod by its name;\n"
        "             its arguments come after --\n"
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
        "  --target     the system of the executable: windows, linux, linux-arm64, android-arm64,\n"
        "               android-x86_64, or android for the APK of an app (build; default: the one\n"
        "               cromo runs on); cromo run --target android runs the app on the phone\n"
        "  --cpu        the processor of x86-64 whose instructions the program uses: x86-64,\n"
        "               x86-64-v2, x86-64-v3 (AVX2), x86-64-v4 (AVX-512), native for the one of\n"
        "               this computer, or a name as haswell (default: x86-64); a processor\n"
        "               without them stops the program at the start, with a message\n"
        "  --release    no overflow checks: integers wrap around (default: debug, with checks)\n"
        "  -O<n>        optimization level (default: -O2)\n"
        "  --emit-ir    print the generated LLVM IR instead of producing the executable (build)\n",
        out);
}

namespace fs = std::filesystem;

// D517: an executable of cromo.mod, its name and its main file.
struct Executable {
    std::string name;
    std::string path;
};

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
    bool apk = false;  // --target android: the APK with both architectures (D453)
    std::string androidPackage;  // of [[android(...)]] on main (D455), then that of the APK
    std::string androidName;
    bool network = false;  // the program uses std.net: the permission INTERNET (D463)
    std::vector<Executable> executables;  // D517: those of cromo.mod, without a file
};

// D517: a name of cromo.mod, of letters, digits and the characters of `others`.
bool projectName(const std::string& name, const char* others) {
    if (name.empty()) return false;
    for (char c : name) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && !std::strchr(others, c)) return false;
    }
    return true;
}

// D516, D517: the executables of the cromo.mod of this folder (P68). False, after
// printing the errors with their lines, when it is wrong.
bool readProject(std::vector<Executable>& executables) {
    std::string text;
    if (!cromo::readSourceFile("cromo.mod", text)) {
        std::fputs("cromo: cannot read cromo.mod\n", stderr);
        return false;
    }
    bool good = true;
    bool first = true;
    int number = 0;
    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line)) {
        ++number;
        size_t comment = line.find("//");
        if (comment != std::string::npos) line.resize(comment);
        std::istringstream split(line);
        std::vector<std::string> words;
        for (std::string word; split >> word;) words.push_back(word);
        if (words.empty()) continue;
        auto fail = [&](const std::string& message) {
            std::fprintf(stderr, "cromo.mod:%d: error: %s\n", number, message.c_str());
            good = false;
        };
        bool wasFirst = first;
        first = false;
        if (words[0] == "module") {
            if (!wasFirst) {
                fail("the line module comes first, once");
            } else if (words.size() != 2 || !projectName(words[1], "._-/")) {
                fail("module takes the name of the project, as module example.com/app");
            }
            continue;
        }
        if (wasFirst) fail("cromo.mod starts with the line module, with the name of the project, as module example.com/app");
        if (words[0] == "bin") {
            if (words.size() != 3 || !projectName(words[1], "_-") || fs::path(words[2]).extension() != ".cro") {
                fail("bin takes the name of an executable and its main file, as bin server cmd/server.cro");
                continue;
            }
            std::error_code ec;
            if (!fs::is_regular_file(words[2], ec)) fail("there is no file " + words[2]);
            for (const Executable& other : executables) {
                if (other.name == words[1]) fail("another executable is called " + words[1]);
            }
            executables.push_back({words[1], words[2]});
        } else if (words[0] == "require") {
            fail("the packages are not available yet: require comes later");
        } else {
            fail("unknown line " + words[0] + ": cromo.mod has the lines module and bin");
        }
    }
    if (first) {
        std::fputs("cromo.mod:1: error: cromo.mod starts with the line module, with the name of the project, as module "
                   "example.com/app\n",
                   stderr);
        good = false;
    }
    return good;
}

// "server, client and tool": the names of the executables.
std::string executableNames(const std::vector<Executable>& executables) {
    std::string text;
    for (size_t i = 0; i < executables.size(); ++i) {
        text += (i == 0 ? "" : i + 1 == executables.size() ? " and " : ", ") + executables[i].name;
    }
    return text;
}

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
    for (const cromo::ModuleFile& file : module.files) compilation.network = compilation.network || file.name == "std.net";
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
    for (const cromo::FunctionDecl& function : module.functions) {
        if (!function.androidPackage.empty()) {  // D455
            compilation.androidPackage = function.androidPackage;
            compilation.androidName = function.androidName;
        }
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
        bool android = arg == "--target" && run && i + 1 < last && args[i + 1] == "android";  // D464
        if (arg == "--target" && run && !android) {
            std::fputs("cromo: cromo run takes only --target android, which runs the app on the phone (D159, D464)\n",
                       stderr);
            return false;
        }
        if ((arg == "-o" || arg == "--emit-ir" || arg == "--target") && (run || test) && !android) {
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
            } else if (target == "android" && !test) {  // D453: the APK, and cromo run on the phone (D464)
                options.target = cromo::Target::AndroidArm64;
                compilation.apk = true;
            } else if (target == "android-arm64") {  // P43
                options.target = cromo::Target::AndroidArm64;
            } else if (target == "android-x86_64") {
                options.target = cromo::Target::AndroidX64;
            } else {
                std::fprintf(stderr,
                             "cromo: unknown target: %s (windows, linux, linux-arm64, android, android-arm64 or "
                             "android-x86_64)\n",
                             target.c_str());
                return false;
            }
        } else if (arg == "--cpu" && i + 1 < last) {
            options.cpu = args[++i];  // D513
            std::string error;
            if (!cromo::validCpu(options.cpu, error)) {
                std::fprintf(stderr, "cromo: %s\n", error.c_str());
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
    if (!options.cpu.empty() && cromo::isArm64(options.target) && !compilation.apk) {
        std::fprintf(stderr, "cromo: --cpu names a processor of x86-64, and %s is ARM64\n",
                     cromo::targetName(options.target));
        return false;
    }
    if (compilation.runtime) {
        if (options.outputPath.empty() || !compilation.inputPath.empty()) {
            std::fputs("cromo: cromo build --runtime [--target system] -o <object>, for the build of cromo (D393)\n", stderr);
            return false;
        }
        return true;
    }
    const char* extension = compilation.apk ? ".apk" : windows ? ".exe" : "";
    std::error_code ec;
    if (compilation.inputPath.empty() && fs::is_regular_file("cromo.mod", ec)) {
        // D517: the executables of cromo.mod, all of them, or the only one for run.
        if (!readProject(compilation.executables)) return false;
        if (!compilation.executables.empty() && !options.outputPath.empty()) {
            std::fputs("cromo: -o names one executable, and those of cromo.mod are named after their lines\n", stderr);
            return false;
        }
        if (run && compilation.executables.size() > 1) {
            std::fprintf(stderr, "cromo: cromo.mod has the executables %s: write which one, as cromo run %s\n",
                         executableNames(compilation.executables).c_str(), compilation.executables[0].name.c_str());
            return false;
        }
        if (run && compilation.executables.size() == 1) {
            compilation.inputPath = compilation.executables[0].path;
            options.outputPath = compilation.executables[0].name + extension;
            compilation.executables.clear();
        }
        if (!compilation.executables.empty()) return true;
    } else if (!compilation.inputPath.empty() && fs::path(compilation.inputPath).extension().empty() &&
               !fs::exists(compilation.inputPath, ec)) {
        // D517: an executable of cromo.mod by its name.
        std::vector<Executable> executables;
        if (fs::is_regular_file("cromo.mod", ec) && !readProject(executables)) return false;
        auto found = std::find_if(executables.begin(), executables.end(),
                                  [&](const Executable& e) { return e.name == compilation.inputPath; });
        if (found == executables.end()) {
            if (executables.empty()) {
                std::fprintf(stderr, "cromo: there is no file %s, and no cromo.mod here with an executable of that name\n",
                             compilation.inputPath.c_str());
            } else {
                std::fprintf(stderr, "cromo: there is no file %s, and cromo.mod has the executables %s\n",
                             compilation.inputPath.c_str(), executableNames(executables).c_str());
            }
            return false;
        }
        if (options.outputPath.empty()) options.outputPath = found->name + extension;
        compilation.inputPath = found->path;
    }
    if (compilation.inputPath.empty()) {
        // D155: main.cro of this folder, and the executable named after the folder.
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
            options.outputPath = (folder.empty() ? std::string("main") : folder) + (compilation.apk ? ".apk" : windows ? ".exe" : "");
        }
    }
    if (options.outputPath.empty()) {
        options.outputPath = fs::path(compilation.inputPath).replace_extension(extension).string();
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

// D453: the APK of an app. libmain.so of the program for each architecture, then
// lib/tools/apk.cro writes and signs the APK with them, libSDL3.so of lib/ and the
// classes of Java of SDL, the package and the name of [[android(...)]], or those
// of the name of the file, and the icon next to it (D455).
int buildApk(Compilation& compilation) {
    std::error_code ec;
#ifdef _WIN32
    std::string id = std::to_string(GetCurrentProcessId());
#else
    std::string id = std::to_string(getpid());
#endif
    fs::path folder = fs::temp_directory_path(ec) / ("cromo-apk-" + id);
    fs::path source(compilation.inputPath);
    std::string output = fs::absolute(compilation.options.outputPath, ec).string();
    std::vector<std::string> entries;
    fs::path dex = cromo::libraryFolder() / "android" / "classes.dex";
    if (!fs::is_regular_file(dex, ec)) {
        std::fprintf(stderr, "cromo: the library has no %s: this cromo was built without the SDK of Android (D453)\n",
                     dex.string().c_str());
        return 1;
    }
    entries.push_back("classes.dex=" + dex.string());
    for (cromo::Target target : {cromo::Target::AndroidArm64, cromo::Target::AndroidX64}) {
        const char* abi = target == cromo::Target::AndroidArm64 ? "arm64-v8a" : "x86_64";
        fs::path sdl = cromo::runtimeLibraryPath(target).parent_path() / "libSDL3.so";
        if (!fs::is_regular_file(sdl, ec)) {
            std::fprintf(stderr, "cromo: the library has no %s: this cromo was built without the NDK (D453)\n",
                         sdl.string().c_str());
            fs::remove_all(folder, ec);
            return 1;
        }
        fs::create_directories(folder / abi, ec);
        Compilation part = compilation;
        part.apk = false;
        part.options.target = target;
        if (target == cromo::Target::AndroidArm64) part.options.cpu.clear();  // --cpu is for x86-64 (D513)
        part.options.sharedLibrary = true;
        part.options.outputPath = (folder / abi / "libmain.so").string();
        int code = build(part);
        if (code != 0) {
            fs::remove_all(folder, ec);
            return code;
        }
        compilation.androidPackage = part.androidPackage;
        compilation.androidName = part.androidName;
        compilation.network = part.network;
        entries.push_back(std::string("lib/") + abi + "/libSDL3.so=" + sdl.string());
        entries.push_back(std::string("lib/") + abi + "/libmain.so=" + part.options.outputPath);
    }
    std::string stem = source.stem().string();
    std::string package = compilation.androidPackage;
    if (package.empty()) {  // org.cromo. and the name of the file, as a name of Java
        std::string name;
        for (char c : stem) name += std::isalnum(static_cast<unsigned char>(c)) ? static_cast<char>(std::tolower(c)) : '_';
        if (name.empty() || !std::isalpha(static_cast<unsigned char>(name[0]))) name = "app" + name;
        package = "org.cromo." + name;
    }
    compilation.androidPackage = package;
    std::string name = compilation.androidName.empty() ? stem : compilation.androidName;
    std::string icon = "-";
    for (const fs::path& candidate : {fs::path(source).replace_extension(".png"), source.parent_path() / "main.png"}) {
        if (fs::is_regular_file(candidate, ec)) {
            icon = fs::absolute(candidate, ec).string();
            break;
        }
    }

    Compilation tool;
    tool.inputPath = cromo::libraryFile("tools/apk.cro").string();
    tool.options.outputPath = "apk";
    std::vector<std::string> args = {output, package, name, icon, compilation.network ? "INTERNET" : "-"};
    args.insert(args.end(), entries.begin(), entries.end());
#ifdef _WIN32
    std::vector<std::wstring> programArgs;
    for (const std::string& arg : args) programArgs.push_back(fs::path(arg).wstring());  // in the code page, as narrow()
#else
    std::vector<std::string> programArgs = args;
#endif
    int code = run(tool, programArgs);
    fs::remove_all(folder, ec);
    return code;
}

// The adb of ANDROID_HOME or ANDROID_SDK_ROOT, in platform-tools, else the one in
// PATH (P47); "" without it.
std::string findAdb() {
    std::error_code ec;
#ifdef _WIN32
    const char* name = "adb.exe";
    const char separator = ';';
#else
    const char* name = "adb";
    const char separator = ':';
#endif
    for (const char* variable : {"ANDROID_HOME", "ANDROID_SDK_ROOT"}) {
        const char* sdk = std::getenv(variable);
        if (!sdk || !*sdk) continue;
        fs::path adb = fs::path(sdk) / "platform-tools" / name;
        if (fs::is_regular_file(adb, ec)) return adb.string();
    }
    const char* path = std::getenv("PATH");
    std::string folders = path ? path : "";
    size_t start = 0;
    while (start <= folders.size()) {
        size_t end = folders.find(separator, start);
        if (end == std::string::npos) end = folders.size();
        if (end > start) {
            fs::path adb = fs::path(folders.substr(start, end - start)) / name;
            if (fs::is_regular_file(adb, ec)) return adb.string();
        }
        start = end + 1;
    }
    return "";
}

// Ctrl+C during cromo run --target android: the program of adb ends, and cromo then
// stops the app (D464).
#ifdef _WIN32
volatile LONG interrupted = 0;
BOOL WINAPI onInterrupt(DWORD kind) {
    if (kind != CTRL_C_EVENT && kind != CTRL_BREAK_EVENT) return FALSE;
    interrupted = 1;
    return TRUE;
}
#else
volatile sig_atomic_t interrupted = 0;
void onInterrupt(int) { interrupted = 1; }
#endif

// Runs a program and waits for it, with the output of cromo.
int runAndWait(const std::vector<std::string>& args) {
#ifdef _WIN32
    std::wstring line;
    for (const std::string& arg : args) line += (line.empty() ? L"" : L" ") + quoteArgument(fs::path(arg).wstring());
    STARTUPINFOW startup{};
    startup.cb = sizeof startup;
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(nullptr, line.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &startup, &process)) return 1;
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exitCode = 1;
    GetExitCodeProcess(process.hProcess, &exitCode);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return static_cast<int>(exitCode);
#else
    std::vector<std::string> copies = args;
    std::vector<char*> argv;
    for (std::string& arg : copies) argv.push_back(arg.data());
    argv.push_back(nullptr);
    std::fflush(nullptr);
    pid_t child = fork();
    if (child == 0) {
        execvp(argv[0], argv.data());
        _exit(127);
    }
    int status = 0;
    while (child > 0 && waitpid(child, &status, 0) < 0) {
    }
    return child > 0 && WIFEXITED(status) ? WEXITSTATUS(status) : 1;
#endif
}

// D464, D465, P47: cromo run --target android. The APK in a temporary folder, then
// lib/tools/android.cro chooses the device, installs and starts the app and shows
// its log until it ends, with its exit code. Ctrl+C ends the tool and adb, and
// cromo stops the app.
int runAndroid(Compilation& compilation) {
    std::string adb = findAdb();
    if (adb.empty()) {
        std::fputs("cromo: adb not found: install the platform tools of the SDK of Android, and set ANDROID_HOME or "
                   "put adb in PATH (P47)\n",
                   stderr);
        return 1;
    }
    std::error_code ec;
#ifdef _WIN32
    std::string id = std::to_string(GetCurrentProcessId());
#else
    std::string id = std::to_string(getpid());
#endif
    fs::path folder = fs::temp_directory_path(ec) / ("cromo-android-" + id);
    fs::create_directories(folder, ec);
    if (ec) {
        std::fprintf(stderr, "cromo: cannot create the folder %s: %s\n", folder.string().c_str(), ec.message().c_str());
        return 1;
    }
    std::string apk = (folder / (fs::path(compilation.inputPath).stem().string() + ".apk")).string();
    compilation.options.outputPath = apk;
    int code = buildApk(compilation);
    if (code == 0) {
        Compilation tool;
        tool.inputPath = cromo::libraryFile("tools/android.cro").string();
        tool.options.outputPath = "android";
#ifdef _WIN32
        std::vector<std::wstring> toolArgs = {fs::path(adb).wstring(), fs::path(apk).wstring(),
                                              fs::path(compilation.androidPackage).wstring()};
        SetConsoleCtrlHandler(onInterrupt, TRUE);
#else
        std::vector<std::string> toolArgs = {adb, apk, compilation.androidPackage};
        struct sigaction action {};
        action.sa_handler = onInterrupt;
        sigaction(SIGINT, &action, nullptr);
#endif
        code = run(tool, toolArgs);
        if (interrupted) {
            runAndWait({adb, "shell", "am", "force-stop", compilation.androidPackage});
            code = 130;  // as the shells say Ctrl+C
        }
    }
    fs::remove_all(folder, ec);
    return code;
}

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
    // D517: each executable of cromo.mod, with the same options.
    std::vector<Executable> executables = std::move(compilation.executables);
    compilation.executables.clear();
    if (compilation.codegen.tests) {
        if (executables.empty()) return testCommand(compilation);
        int result = 0;
        for (const Executable& executable : executables) {
            Compilation one = compilation;
            one.inputPath = executable.path;
            std::printf("%s (%s)\n", executable.name.c_str(), executable.path.c_str());
            std::fflush(stdout);
            if (testCommand(one) != 0) result = 1;
        }
        return result;
    }
    if (!isRun && !executables.empty()) {
        for (const Executable& executable : executables) {
            Compilation one = compilation;
            one.inputPath = executable.path;
            one.options.outputPath =
                executable.name + (one.apk ? ".apk" : one.options.target == cromo::Target::Windows ? ".exe" : "");
            int code = one.apk ? buildApk(one) : build(one);
            if (code != 0) return code;
        }
        return 0;
    }
    if (!isRun) return compilation.apk ? buildApk(compilation) : build(compilation);
    if (compilation.apk) {
        if (separator != args.size()) {
            std::fputs("cromo: an app of Android has no arguments (P47)\n", stderr);
            return 2;
        }
        return runAndroid(compilation);
    }

#ifdef _WIN32
    std::vector<std::wstring> programArgs;
    for (size_t i = separator + 1; i < wide.size(); ++i) programArgs.push_back(wide[i]);
#else
    std::vector<std::string> programArgs(args.begin() + std::min(separator + 1, args.size()), args.end());
#endif
    return run(compilation, programArgs);
}
