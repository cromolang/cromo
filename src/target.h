#pragma once

namespace cromo {

// The system a program is compiled for: Windows x64, Linux x64 (D27, D142),
// Linux ARM64 (D405), or Android on ARM64 and on x86-64 (D452).
enum class Target { Windows, Linux, LinuxArm64, AndroidArm64, AndroidX64 };

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

// Android has the kernel of Linux: its programs use the files of the library for
// Linux (P43).
inline constexpr bool isLinux(Target target) { return target != Target::Windows; }

inline constexpr bool isAndroid(Target target) { return target == Target::AndroidArm64 || target == Target::AndroidX64; }

inline constexpr bool isArm64(Target target) { return target == Target::LinuxArm64 || target == Target::AndroidArm64; }

// Android from API 26 (D454).
inline const char* targetTriple(Target target) {
    switch (target) {
        case Target::Linux: return "x86_64-unknown-linux-gnu";
        case Target::LinuxArm64: return "aarch64-unknown-linux-gnu";
        case Target::AndroidArm64: return "aarch64-unknown-linux-android26";
        case Target::AndroidX64: return "x86_64-unknown-linux-android26";
        default: return "x86_64-pc-windows-msvc";
    }
}

// The name of --target (D144, D406, P43).
inline const char* targetName(Target target) {
    switch (target) {
        case Target::Linux: return "linux";
        case Target::LinuxArm64: return "linux-arm64";
        case Target::AndroidArm64: return "android-arm64";
        case Target::AndroidX64: return "android-x86_64";
        default: return "windows";
    }
}

}  // namespace cromo
