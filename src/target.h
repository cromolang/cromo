#pragma once

namespace cromo {

// The system a program is compiled for: Windows x64 or Linux x64 (D27, D142).
enum class Target { Windows, Linux };

inline const char* targetTriple(Target target) {
    return target == Target::Linux ? "x86_64-unknown-linux-gnu" : "x86_64-pc-windows-msvc";
}

}  // namespace cromo
