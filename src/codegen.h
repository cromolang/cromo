#pragma once

#include "ast.h"
#include "target.h"

#include <string>
#include <vector>

namespace cromo {

struct CodegenOptions {
    std::vector<std::string> sourcePaths;  // by SourceLocation::file, shown in panic messages (D62)
    std::vector<std::string> debugPaths;   // by SourceLocation::file: the full paths, for a debugger (D338)
    bool release = false;    // D57: no overflow checks, integers wrap around; D338: only the lines
    bool optimized = true;
    Target target = hostTarget();
};

// Translates a module already checked by sema into textual LLVM IR (D53).
// Adds the `cromo_start` entry point, which replaces the C runtime startup
// (D19): it calls main and exits the process with its result.
std::string generateIR(const Module& module, const CodegenOptions& options);

}  // namespace cromo
