#pragma once

#include "target.h"

#include <string>
#include <utility>
#include <vector>

namespace cromo {

// A DLL and the functions imported from it: cromo writes its import library (D136).
struct ImportLibrary {
    std::string dll;                     // "user32.dll"
    std::vector<std::string> functions;
};

struct BackendOptions {
    std::string outputPath;  // executable to produce
    Target target = hostTarget();
    int optLevel = 2;        // from 0 to 3
    bool keepObject = false; // keep the intermediate object file
    bool objectOnly = false; // the runtime in Cromo (D393): only the object, in outputPath, for its archive
    std::vector<std::string> archives;   // static libraries of the extern blocks: name.lib, libname.a (D136, D143)
    std::vector<ImportLibrary> imports;  // DLLs of the extern blocks; kernel32 is always there, for the runtime
    bool debugInfo = true;  // D338: a PDB next to the executable on Windows, DWARF kept in it on Linux
    // D339: the path of each source file in the debug information, and its name in a stack trace.
    std::vector<std::pair<std::string, std::string>> fileNames;
};

// Reads the textual IR with LLVM, optimizes it, generates machine code and links
// the executable with LLD (D28). On failure it returns false and puts the reason
// in `error`.
bool buildExecutable(const std::string& irText, const std::string& moduleName, const BackendOptions& options,
                     std::string& error);

}  // namespace cromo
