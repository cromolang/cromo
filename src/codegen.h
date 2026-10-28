#pragma once

#include "ast.h"

#include <string>

namespace cromo {

// Target triple. Only Windows x64 for now (D27).
inline constexpr const char* kTargetTriple = "x86_64-pc-windows-msvc";

struct CodegenOptions {
    std::string sourcePath;  // shown in panic messages (D62)
    bool release = false;    // D57: no overflow checks, integers wrap around
};

// Translates a module already checked by sema into textual LLVM IR (D53).
// Adds the `cromo_start` entry point, which replaces the C runtime startup
// (D19): it calls main and exits the process with its result.
std::string generateIR(const Module& module, const CodegenOptions& options);

}  // namespace cromo
