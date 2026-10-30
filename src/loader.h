#pragma once

#include "ast.h"
#include "diagnostics.h"
#include "target.h"

#include <deque>
#include <map>
#include <string>

namespace cromo {

// Reads a source file, without the UTF-8 BOM.
bool readSourceFile(const std::string& path, std::string& text);

// The key of a file among the texts that replace it: its absolute path, in lower
// case on Windows.
std::string sourceKey(const std::string& path);

// Reads the main file (sources[0], already read), the standard library
// (sources[1]) and every module they import (D50, D130), with the files of the
// system it is compiled for (D202). `sources` is a deque: Diagnostics keeps their
// addresses. The texts of `overlay`, by sourceKey, replace the files on the disk:
// the documents of the editor not yet saved (D173).
Module loadProgram(std::deque<SourceFile>& sources, Diagnostics& diag, Target target,
                   const std::map<std::string, std::string>* overlay = nullptr);

}  // namespace cromo
