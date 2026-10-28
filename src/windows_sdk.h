#pragma once

#include <string>

namespace cromo {

// Folder of the Windows SDK with the x64 import libraries (kernel32.lib).
// Returns an empty string if the SDK is not installed. Needed until `cromo`
// generates its own import libraries (milestone M8).
std::string findWindowsSdkLibDir();

}  // namespace cromo
