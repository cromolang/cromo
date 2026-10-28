#pragma once

#include <string>

namespace cromo {

struct BackendOptions {
    std::string outputPath;  // executable to produce
    int optLevel = 2;        // from 0 to 3
    bool keepObject = false; // keep the intermediate object file
};

// Reads the textual IR with LLVM, optimizes it, generates machine code and links
// the executable with LLD (D28). On failure it returns false and puts the reason
// in `error`.
bool buildExecutable(const std::string& irText, const std::string& moduleName, const BackendOptions& options,
                     std::string& error);

}  // namespace cromo
