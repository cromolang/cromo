#pragma once

#include <string>

namespace cromo {

struct SourceLocation {
    int line = 1;
    int column = 1;  // in characters, not bytes
};

struct SourceFile {
    std::string path;
    std::string text;  // UTF-8, without BOM
};

// Prints errors and warnings as file:line:column, followed by the source line
// and a marker under the reported position.
class Diagnostics {
public:
    explicit Diagnostics(const SourceFile& file) : file_(file) {}

    void error(SourceLocation loc, const std::string& message);
    void error(const std::string& message);  // error about the whole file
    void warning(SourceLocation loc, const std::string& message);

    bool hasErrors() const { return errorCount_ > 0; }

private:
    void report(const char* kind, SourceLocation loc, const std::string& message);

    const SourceFile& file_;
    int errorCount_ = 0;
};

}  // namespace cromo
