#pragma once

namespace cromo {

// The system a program is compiled for: Windows x64, Linux x64 (D27, D142) or
// Linux ARM64 (D405).
enum class Target { Windows, Linux, LinuxArm64 };

// The system cromo runs on: the target when none is given.
inline constexpr Target hostTarget() {
#if defined(_WIN32)
    return Target::Windows;
#elif defined(__aarch64__)
    return Target::LinuxArm64;
#else
    return Target::Linux;
#endif
}

inline constexpr bool isLinux(Target target) { return target != Target::Windows; }

inline const char* targetTriple(Target target) {
    switch (target) {
        case Target::Linux: return "x86_64-unknown-linux-gnu";
        case Target::LinuxArm64: return "aarch64-unknown-linux-gnu";
        default: return "x86_64-pc-windows-msvc";
    }
}

// The name of --target (D144, D406).
inline const char* targetName(Target target) {
    switch (target) {
        case Target::Linux: return "linux";
        case Target::LinuxArm64: return "linux-arm64";
        default: return "windows";
    }
}

}  // namespace cromo
