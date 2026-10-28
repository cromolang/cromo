#pragma once

#include <string>
#include <vector>

namespace cromo {

struct SourceLocation {
    int line = 1;
    int column = 1;  // in characters, not bytes
    int file = 0;    // index of the file in Diagnostics: 0 is the program
};

// The standard library is the second source file of every program (D97).
inline constexpr int kLibraryFile = 1;

struct SourceFile {
    std::string path;
    std::string text;  // UTF-8, without BOM
};

// Prints errors and warnings as file:line:column, followed by the source line
// and a marker under the reported position.
class Diagnostics {
public:
    explicit Diagnostics(const SourceFile& file) : files_{&file} {}

    // Adds another source file and returns its index for SourceLocation::file.
    int addFile(const SourceFile& file) {
        files_.push_back(&file);
        return static_cast<int>(files_.size()) - 1;
    }

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

    std::vector<const SourceFile*> files_;
    int errorCount_ = 0;
    bool muted_ = false;
    std::string context_;
};

}  // namespace cromo
