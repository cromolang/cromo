#include "backend.h"
#include "codegen.h"
#include "diagnostics.h"
#include "lexer.h"
#include "ownership.h"
#include "parser.h"
#include "runtime_blob.h"
#include "sema.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

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

    cromo::SourceFile file{inputPath, {}};
    if (!readFile(inputPath, file.text)) {
        std::fprintf(stderr, "cromo: cannot read %s\n", inputPath.c_str());
        return 2;
    }

    // The standard library is compiled together with the program (D97).
    cromo::SourceFile library{"cromo:core.cro", std::string(cromo::coreLibrary())};
    cromo::Diagnostics diag(file);
    int libraryIndex = diag.addFile(library);
    cromo::Module module = cromo::parse(cromo::tokenize(library, libraryIndex, diag), diag);
    cromo::Module program = cromo::parse(cromo::tokenize(file, 0, diag), diag);
    module.append(std::move(program));
    if (diag.hasErrors() || !cromo::check(module, diag) || !cromo::checkMoves(module, diag)) return 1;

    std::string sourceName = std::filesystem::path(inputPath).filename().string();
    codegen.sourcePaths = {inputPath, library.path};
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
