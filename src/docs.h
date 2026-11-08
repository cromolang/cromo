#pragma once

#include "diagnostics.h"

#include <map>
#include <string>
#include <vector>

namespace cromo {

// The documentation comments of a source file (D473): the `//` lines right above
// a declaration, the comment at the end of the line of a field or of a case, and
// the comment at the beginning of the file for its module.
class DocComments {
public:
    explicit DocComments(const SourceFile& file);

    // The lines above the declaration on `line`, without `//`; with `trailing`
    // also the comment at the end of that line, after them.
    std::vector<std::string> of(int line, bool trailing) const;

    // The comment at the beginning of the file, unless a declaration follows it.
    std::vector<std::string> module() const;

private:
    struct Line {
        std::string comment;  // the text after `//`, empty when there is none
        int column = 0;       // of the comment
        bool hasComment = false;
        bool code = false;       // a token that is not a comment starts on it
        bool attribute = false;  // it starts with `[[`
        bool import = false;     // it starts with `import`
    };

    const Line* line(int number) const;
    bool aloneComment(int number) const;

    std::map<int, Line> lines_;
};

// The documentation as Markdown for the editor (D474): 'code' becomes a span of
// code and the lines ''' a block of Cromo; `<` is escaped out of them.
std::string markdownOf(const std::vector<std::string>& lines);

// The signature and the documentation of a built-in function, as `println`, or of
// a built-in type, as `i64` (D475); null for the other names.
struct BuiltinDoc {
    const char* name;
    const char* signature;  // empty for a type
    const char* text;       // Markdown with the conventions of D474
};
const BuiltinDoc* builtinDoc(const std::string& name);

// Every function and type of builtinDoc, in their order, without the vectors (D519).
const std::vector<BuiltinDoc>& builtinDocs();

}  // namespace cromo
