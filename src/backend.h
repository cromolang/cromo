#pragma once

#include <string>
#include <vector>

namespace cromo {

struct BackendOptions {
    std::string outputPath;  // executable to produce
    int optLevel = 2;        // from 0 to 3
    bool keepObject = false; // keep the intermediate object file
    std::vector<std::string> libraries;     // of the extern blocks: "kernel32" is kernel32.lib (D128)
    std::vector<std::string> libraryPaths;  // where to look for them, before the Windows SDK
};

// Reads the textual IR with LLVM, optimizes it, generates machine code and links
// the executable with LLD (D28). On failure it returns false and puts the reason
// in `error`.
bool buildExecutable(const std::string& irText, const std::string& moduleName, const BackendOptions& options,
                     std::string& error);

}  // namespace cromo
