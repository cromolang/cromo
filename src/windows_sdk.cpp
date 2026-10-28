#include "windows_sdk.h"

#include <algorithm>
#include <filesystem>
#include <system_error>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace cromo {

namespace {

std::filesystem::path kitsRoot() {
    wchar_t buffer[MAX_PATH];
    DWORD size = sizeof buffer;
    LSTATUS status = RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows Kits\\Installed Roots",
                                  L"KitsRoot10", RRF_RT_REG_SZ | RRF_SUBKEY_WOW6432KEY, nullptr, buffer, &size);
    if (status != ERROR_SUCCESS) return {};
    return std::filesystem::path(buffer);
}

// Splits a version like "10.0.26100.0" into numbers, to compare them one by one.
std::vector<int> versionParts(const std::wstring& text) {
    std::vector<int> parts;
    int current = 0;
    for (wchar_t c : text) {
        if (c == L'.') {
            parts.push_back(current);
            current = 0;
        } else if (c >= L'0' && c <= L'9') {
            current = current * 10 + (c - L'0');
        }
    }
    parts.push_back(current);
    return parts;
}

}  // namespace

std::string findWindowsSdkLibDir() {
    std::filesystem::path root = kitsRoot();
    if (root.empty()) return {};

    std::error_code ec;
    std::vector<std::wstring> versions;
    for (const auto& entry : std::filesystem::directory_iterator(root / L"Lib", ec)) {
        if (entry.is_directory(ec)) versions.push_back(entry.path().filename().wstring());
    }
    std::sort(versions.begin(), versions.end(),
              [](const std::wstring& a, const std::wstring& b) { return versionParts(a) > versionParts(b); });

    for (const std::wstring& version : versions) {
        std::filesystem::path dir = root / L"Lib" / version / L"um" / L"x64";
        if (std::filesystem::exists(dir / L"kernel32.lib", ec)) {
            std::u8string utf8 = dir.u8string();
            return std::string(utf8.begin(), utf8.end());
        }
    }
    return {};
}

}  // namespace cromo
