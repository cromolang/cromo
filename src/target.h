#pragma once

namespace cromo {

// The system a program is compiled for: Windows x64 or Linux x64 (D27, D142).
enum class Target { Windows, Linux };

// The system cromo runs on: the target when none is given.
inline constexpr Target hostTarget() {
#ifdef _WIN32
    return Target::Windows;
#else
    return Target::Linux;
#endif
}

inline const char* targetTriple(Target target) {
    return target == Target::Linux ? "x86_64-unknown-linux-gnu" : "x86_64-pc-windows-msvc";
}

}  // namespace cromo
