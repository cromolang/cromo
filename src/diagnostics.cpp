#include "diagnostics.h"

#include <cstdio>

namespace cromo {

namespace {

// Returns line `line` (starting from 1), without the line terminator.
std::string lineAt(const std::string& text, int line) {
    size_t start = 0;
    for (int i = 1; i < line; ++i) {
        size_t newline = text.find('\n', start);
        if (newline == std::string::npos) return {};
        start = newline + 1;
    }
    size_t end = text.find('\n', start);
    std::string result = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
    if (!result.empty() && result.back() == '\r') result.pop_back();
    return result;
}

}  // namespace

void Diagnostics::error(SourceLocation loc, const std::string& message) {
    if (muted_) return;
    ++errorCount_;
    report("error", loc, message + context_);
}

void Diagnostics::error(const std::string& message) {
    if (muted_) return;
    ++errorCount_;
    std::fprintf(stderr, "%s: error: %s\n", file_.path.c_str(), (message + context_).c_str());
}

void Diagnostics::warning(SourceLocation loc, const std::string& message) {
    if (muted_) return;
    report("warning", loc, message + context_);
}

void Diagnostics::report(const char* kind, SourceLocation loc, const std::string& message) {
    std::fprintf(stderr, "%s:%d:%d: %s: %s\n", file_.path.c_str(), loc.line, loc.column, kind, message.c_str());

    std::string source = lineAt(file_.text, loc.line);
    if (source.empty()) return;
    std::fprintf(stderr, "    %s\n    ", source.c_str());

    // Aligns the marker by counting UTF-8 characters and keeping tabs.
    int column = 1;
    for (size_t i = 0; i < source.size() && column < loc.column; ++i) {
        unsigned char c = static_cast<unsigned char>(source[i]);
        if ((c & 0xC0) == 0x80) continue;  // UTF-8 continuation byte
        std::fputc(c == '\t' ? '\t' : ' ', stderr);
        ++column;
    }
    std::fputs("^\n", stderr);
}

}  // namespace cromo
