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

// The runtime library of a system (D58, D142): lib/windows/cromo_rt.lib,
// lib/linux/libcromo_rt.a, lib/android_arm64/libcromo_rt.a...
std::filesystem::path runtimeLibraryPath(Target target);

// For the executables for Linux linked with glibc (D428): the runtime,
// lib/linux/libcromo_rt_glibc.a, and the folder of the stubs of glibc.
std::filesystem::path glibcRuntimePath();
std::filesystem::path glibcFolder();

// The libraries of glibc whose stubs are in glibcFolder(), in the order of the linker.
inline constexpr const char* kGlibcLibraries[] = {"libc.so.6",  "libm.so.6",  "libpthread.so.0",
                                                  "libdl.so.2", "librt.so.1", "ld-linux-x86-64.so.2"};

// For Android (P43): the folder of the stubs of Bionic, lib/android_arm64/bionic or
// lib/android_x86_64/bionic, and the libraries whose stubs it has.
std::filesystem::path bionicFolder(Target target);
inline constexpr const char* kBionicLibraries[] = {"libc.so", "libm.so", "libdl.so", "liblog.so"};

// Whether a source file is part of the standard library, which can use the
// dialect unsafe and the modules that serve only the library.
bool isLibraryFile(const std::string& path);

// The name of a file of the library for the messages of a program, as
// "cromo:std/fs.cro", without the folder where cromo is installed.
std::string libraryName(const std::string& path);

}  // namespace cromo
