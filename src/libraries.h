#pragma once

#include "ast.h"
#include "backend.h"
#include "diagnostics.h"

#include <deque>
#include <string>
#include <vector>

namespace cromo {

// The C libraries of the extern blocks (D136-D138).
struct Libraries {
    std::vector<std::string> archives;   // the .lib files next to the modules, given to the linker as they are
    std::vector<ImportLibrary> imports;  // the DLLs whose import library cromo writes
    std::vector<std::string> copies;     // the DLLs next to the modules, to copy next to the executable
};

// Finds the library of every extern block and checks its C functions in the
// DLL (D137). The problems are reported to `diag`.
Libraries findLibraries(const Module& module, const std::deque<SourceFile>& sources, Diagnostics& diag);

// Copies the DLLs next to the executable, when it is in another folder (D138).
// On failure it returns false and puts the reason in `error`.
bool copyLibraries(const std::vector<std::string>& dlls, const std::string& executable, std::string& error);

}  // namespace cromo
