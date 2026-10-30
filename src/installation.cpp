#include "installation.h"

#include <algorithm>
#include <cctype>
#include <system_error>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace cromo {

namespace fs = std::filesystem;

namespace {

fs::path executablePath() {
#ifdef _WIN32
    std::wstring buffer(32768, L'\0');
    DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    buffer.resize(length);
    return fs::path(buffer);
#else
    std::error_code ec;
    return fs::read_symlink("/proc/self/exe", ec);
#endif
}

// The path as a key to compare: with / and, on Windows, in lower case.
std::string comparable(const fs::path& path) {
    std::string text = path.generic_string();
#ifdef _WIN32
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return std::tolower(c); });
#endif
    return text;
}

}  // namespace

const fs::path& libraryFolder() {
    static const fs::path folder = [] {
        fs::path bin = executablePath().parent_path();
        for (const fs::path& candidate : {bin.parent_path() / "lib", bin / "lib"}) {
            std::error_code ec;
            if (fs::exists(candidate / "core.cro", ec)) return fs::weakly_canonical(candidate, ec);
        }
        return fs::path();
    }();
    return folder;
}

fs::path libraryFile(const std::string& name) { return libraryFolder() / fs::path(name); }

fs::path runtimeLibraryPath(Target target) {
    return target == Target::Linux ? libraryFolder() / "linux" / "libcromo_rt.a"
                                   : libraryFolder() / "windows" / "cromo_rt.lib";
}

bool isLibraryFile(const std::string& path) {
    if (libraryFolder().empty()) return false;
    std::string folder = comparable(libraryFolder()) + "/";
    std::error_code ec;
    std::string file = comparable(fs::weakly_canonical(fs::path(path), ec));
    return file.compare(0, folder.size(), folder) == 0;
}

std::string libraryName(const std::string& path) {
    std::error_code ec;
    fs::path relative = fs::weakly_canonical(fs::path(path), ec).lexically_relative(libraryFolder());
    return "cromo:" + relative.generic_string();
}

}  // namespace cromo
