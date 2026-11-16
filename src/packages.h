#pragma once

#include "target.h"

#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace cromo {

// A line `require name url version` of a cromo.mod (D377), or `require name folder`
// for a package in a folder next to the project (D390).
struct Require {
    std::string name;     // the first name of its imports (D383)
    std::string url;      // "github.com/anna/geometry", or "" for a folder
    std::string version;  // "v1.2.0"
    std::string local;    // the folder, as written
    int line = 0;
};

// A line `native system url sha256:hash file...` of a cromo.mod (D551): the archive
// with the libraries of a package for a system, and the files of it that are taken.
struct Native {
    std::string system;  // "windows-x86_64", "linux-x86_64"
    std::string url;
    std::string sha256;
    std::vector<std::string> files;  // their paths in the archive
    int line = 0;
};

// What a cromo.mod says of the packages.
struct ProjectFile {
    std::string module;
    std::vector<Require> requires_;
    std::vector<Native> natives;
};

// A package of a program, in the version that the selection chose (D378).
struct Package {
    std::string url;      // "" for the project and for a folder
    std::string major;    // "v1"
    std::string version;  // "v1.2.0"
    std::filesystem::path folder;
    bool local = false;
    std::map<std::string, int> requires_;  // the names of its cromo.mod, each the number of its package
    std::vector<Native> natives;
};

// The packages of a program: the first is the project.
struct Resolution {
    std::vector<Package> packages;
};

// Downloads a version of a package into the cache; false, with the reason, when it cannot.
using Fetch = std::function<bool(const std::string& url, const std::string& version, std::string& error)>;

// The cache of the packages (D388): CROMO_PACKAGES, or ~/.cromo/packages.
std::filesystem::path packagesFolder();

// Where a version of a package is in the cache.
std::filesystem::path packageFolder(const std::string& url, const std::string& version);

// The name of a system in a line native of cromo.mod (D551), or "" for a system that has none.
std::string nativeSystem(Target target);

// Where the files taken from the archive of a line native are in the cache: by its
// hash and by the files.
std::filesystem::path nativeFolder(const Native& native);

// The folders of the native libraries of a package for a system, those of its lines.
std::vector<std::string> nativeFolders(const Package& package, Target target);

// The name in front of the modules of a package (D391): its URL, with the major
// version from v2, as "github.com/anna/geometry@v2".
std::string packageModuleName(const Package& package);

// Whether `text` is a version, as v1.2.0, and its major version, as v1.
bool isPackageVersion(const std::string& text);
std::string majorOf(const std::string& version);

// -1, 0 or 1, as version `a` is before `b`, the same or after it.
int comparePackageVersions(const std::string& a, const std::string& b);

// Reads the lines of the packages of a cromo.mod; a file that is not there has none.
// False, with the errors each on a line as "cromo.mod:3: error: ...", when it is wrong.
bool readProjectFile(const std::filesystem::path& file, ProjectFile& out, std::string& error);

// The packages of the project of a folder, chosen as Go does (D378): of each, the
// highest version that the project and the packages chosen require. A version that is
// not in the cache is downloaded with `fetch`; without it, it is an error.
bool resolvePackages(const std::string& projectFolder, Resolution& out, std::string& error, const Fetch* fetch);

// Downloads the archive of a line native and takes its files into the cache.
using FetchNative = std::function<bool(const Native& native, std::string& error)>;

// The native libraries of the packages for a system (D551): those that are not in the
// cache are downloaded with `fetch`.
bool prepareNatives(const Resolution& resolution, Target target, const FetchNative& fetch, std::string& error);

// cromo.sum (D380, D387): the hash of each version chosen is the one written there,
// or it is written there when `write` is set. False when a hash is another.
bool checkPackageSums(const std::string& projectFolder, const Resolution& resolution, bool write, std::string& error);

}  // namespace cromo
