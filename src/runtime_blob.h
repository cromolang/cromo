#pragma once

#include <string_view>

namespace cromo {

// The runtime library (runtime/), embedded in cromo.exe so that it stays a
// single file (D28, D58).
std::string_view runtimeLibrary();

// The source of the standard library (library/core.cro), compiled with every
// program (D97).
std::string_view coreLibrary();

}  // namespace cromo
