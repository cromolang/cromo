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
    int errorCount() const { return errorCount_; }

    // While muted nothing is reported or counted: sema uses it to check an
    // expression only to learn its type.
    bool muted() const { return muted_; }
    void setMuted(bool muted) { muted_ = muted; }

    // Text added to every message, for example the generic being instantiated.
    const std::string& context() const { return context_; }
    void setContext(std::string context) { context_ = std::move(context); }

private:
    void report(const char* kind, SourceLocation loc, const std::string& message);

    const SourceFile& file_;
    int errorCount_ = 0;
    bool muted_ = false;
    std::string context_;
};

}  // namespace cromo
