#pragma once

#include "ast.h"
#include "diagnostics.h"
#include "target.h"

#include <deque>
#include <map>
#include <string>
#include <vector>

namespace cromo {

// Reads a source file, without the UTF-8 BOM.
bool readSourceFile(const std::string& path, std::string& text);

// The key of a file among the texts that replace it: its absolute path, in lower
// case on Windows.
std::string sourceKey(const std::string& path);

// The modules of lib/std that a program can import, as std.fs, in the order of their
// names (D201).
std::vector<std::string> libraryModuleNames();

// D516: the folder of the cromo.mod of a program, looked for from `folder`, that of
// its main file, up to the root of the drive; "" without one.
std::string projectFolder(const std::string& folder);

// Reads the main file (sources[0], already read), the standard library
// (sources[1]) and every module they import (D50, D130), with the files of the
// system it is compiled for (D202). `sources` is a deque: Diagnostics keeps their
// addresses. The texts of `overlay`, by sourceKey, replace the files on the disk:
// the documents of the editor not yet saved (D173). With `tests` each module also
// has its file `name_test.cro`, when there is one (D350).
Module loadProgram(std::deque<SourceFile>& sources, Diagnostics& diag, Target target,
                   const std::map<std::string, std::string>* overlay = nullptr, bool tests = false);

}  // namespace cromo
