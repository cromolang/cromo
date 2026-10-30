#pragma once

#include "target.h"

#include <filesystem>
#include <string>

namespace cromo {

// The folder lib/ of the installation (D237), with the standard library and the
// runtime of each system: next to the folder bin/ of cromo in an archive, inside
// the folder of cromo in a build. Empty if there is none.
const std::filesystem::path& libraryFolder();

// A file of the library: "core.cro" (D97), or a module as "std/fs.cro" (D201).
std::filesystem::path libraryFile(const std::string& name);

// The runtime library of a system (D58, D142): lib/windows/cromo_rt.lib or
// lib/linux/libcromo_rt.a.
std::filesystem::path runtimeLibraryPath(Target target);

// Whether a source file is part of the standard library, which can use the
// dialect unsafe and the modules that serve only the library.
bool isLibraryFile(const std::string& path);

// The name of a file of the library for the messages of a program, as
// "cromo:std/fs.cro", without the folder where cromo is installed.
std::string libraryName(const std::string& path);

}  // namespace cromo
