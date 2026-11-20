#pragma once

#include "ast.h"
#include "diagnostics.h"

#include <deque>
#include <string>
#include <vector>

namespace cromo {

// D519: the public declarations of the core and of `modules` of the library, with their
// signatures as written and their documentation (D473, D474), as JSON for the pages
// of the site that tools/book.py makes. False with the reason in `error` when the
// file cannot be written.
bool writeReference(const Module& module, const std::deque<SourceFile>& sources, const std::vector<std::string>& modules,
                    const std::string& path, std::string& error);

// D559: the public declarations of one module of the library, `name` as it is imported
// or "std" for the core, as Markdown: what `cromo ai std.fs` prints. False for a
// module that is not among those loaded.
bool moduleReference(const Module& module, const std::deque<SourceFile>& sources, const std::string& name, std::string& out);

}  // namespace cromo
