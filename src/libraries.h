#pragma once

#include "ast.h"
#include "backend.h"
#include "diagnostics.h"

#include <deque>
#include <string>
#include <vector>

namespace cromo {

// The C libraries of the extern blocks (D136-D138, D143).
struct Libraries {
    std::vector<std::string> archives;   // the static libraries next to the modules, given to the linker as they are
    std::vector<ImportLibrary> imports;  // the DLLs whose import library cromo writes
    std::vector<std::string> copies;     // the DLLs next to the modules, to copy next to the executable
};

// Finds the library of every extern block and checks its C functions in the
// DLL (D137); for Linux, the static library libname.a (D143). The problems are
// reported to `diag`.
Libraries findLibraries(const Module& module, const std::deque<SourceFile>& sources, Target target, Diagnostics& diag);

// Whether the static libraries of the extern blocks for Linux call functions of
// the C library that the runtime does not have, so that the executable is
// linked with glibc (D428). On failure it returns false and puts the reason in `error`.
bool callsTheCLibrary(const std::vector<std::string>& archives, bool& calls, std::string& error);

// Copies the DLLs next to the executable, when it is in another folder (D138).
// On failure it returns false and puts the reason in `error`.
bool copyLibraries(const std::vector<std::string>& dlls, const std::string& executable, std::string& error);

}  // namespace cromo
