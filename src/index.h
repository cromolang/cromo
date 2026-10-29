#pragma once

#include "ast.h"

#include <deque>
#include <string>
#include <vector>

namespace cromo {

// A name in the source that the language server knows (D171): what it shows
// under the mouse, and where it is declared.
struct Reference {
    SourceLocation loc;     // the first character of the name
    int length = 0;         // in characters
    std::string hover;      // a line of Cromo: a declaration or a signature
    bool hasTarget = false;
    SourceLocation target;  // where it is declared
};

// The names of a checked program, in the files of `sources`. A name that sema
// did not resolve, as in a generic never used, is not there.
std::vector<Reference> indexProgram(const Module& module, const std::deque<SourceFile>& sources);

// A function as it is declared: `i32 sum(list<i32> values, ref i64 count)`.
std::string signatureOf(const FunctionDecl& function);

}  // namespace cromo
