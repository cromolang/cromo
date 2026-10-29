#pragma once

#include "target.h"

#include <string_view>

namespace cromo {

// The runtime library (runtime/) of a system, embedded in cromo.exe so that it
// stays a single file (D28, D58, D142).
std::string_view runtimeLibrary(Target target);

// The source of the standard library (library/core.cro), compiled with every
// program (D97).
std::string_view coreLibrary();

}  // namespace cromo
