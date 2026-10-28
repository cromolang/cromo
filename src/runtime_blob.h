#pragma once

#include <string_view>

namespace cromo {

// The runtime library (runtime/), embedded in cromo.exe so that it stays a
// single file (D28, D58).
std::string_view runtimeLibrary();

}  // namespace cromo
