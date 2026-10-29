#pragma once

#include "diagnostics.h"

#include <string>

namespace cromo {

// Formats the source of a module in the only style of cromo fmt (D161-D163).
// Syntax errors are reported to `diag` and give false. If the result would not
// be the same program, or formatting it again would change it, the function
// gives false and explains why in `error` (D169).
bool formatSource(const SourceFile& file, Diagnostics& diag, std::string& out, std::string& error);

}  // namespace cromo
