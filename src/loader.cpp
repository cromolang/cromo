#include "loader.h"

#include "lexer.h"
#include "parser.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <vector>

namespace cromo {

namespace fs = std::filesystem;

bool readSourceFile(const std::string& path, std::string& text) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    if (text.size() >= 3 && text.compare(0, 3, "\xEF\xBB\xBF") == 0) text.erase(0, 3);  // UTF-8 BOM
    return true;
}

std::string sourceKey(const std::string& path) {
    std::error_code ec;
    std::string key = fs::weakly_canonical(fs::absolute(path, ec), ec).string();
#ifdef _WIN32
    std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
#endif
    return key;
}

namespace {

// Reads the main file, the standard library and every module they import (D50).
// `import a.b` is the file a/b.cro, or every .cro file of the folder a/b, in the
// folder of the main file (D130). `sources` is a deque: Diagnostics keeps their
// addresses.
class Loader {
public:
    Loader(std::deque<SourceFile>& sources, Diagnostics& diag, const std::map<std::string, std::string>* overlay)
        : sources_(sources), diag_(diag), overlay_(overlay) {
        fs::path main = fs::absolute(sources[0].path);
        root_ = main.parent_path();
        loaded_[fs::weakly_canonical(main)] = 0;
        names_ = {main.stem().string(), "std"};
    }

    Module load() {
        parseFile(kLibraryFile);
        parseFile(0);
        // The list of files grows while their imports are loaded.
        for (size_t i = 0; i < sources_.size(); ++i) {
            for (size_t k = 0; k < program_.files[i].imports.size(); ++k) {
                std::vector<int> files = resolve(program_.files[i].imports[k]);
                program_.files[i].imports[k].files = std::move(files);
            }
            checkQualifiers(program_.files[i]);
        }
        return std::move(program_);
    }

private:
    void parseFile(int index) {
        Module module = parse(tokenize(sources_[index], index, diag_), diag_);
        module.files.resize(index + 1);
        module.files[index].name = names_[index];
        program_.append(std::move(module));
    }

    std::vector<int> resolve(const ImportDecl& import) {
        std::string dotted;
        fs::path relative;
        for (const std::string& part : import.path) {
            dotted += (dotted.empty() ? "" : ".") + part;
            relative /= part;
        }
        if (import.path[0] == "std") {
            diag_.error(import.loc, "the standard library has no module " + dotted +
                                        " yet: its core is visible without import (D130)");
            return {};
        }
        fs::path file = root_ / relative;
        file += ".cro";
        fs::path folder = root_ / relative;
        std::error_code ec;
        bool isFile = fs::is_regular_file(file, ec) || (overlay_ && overlay_->count(sourceKey(file.string())));
        bool isFolder = fs::is_directory(folder, ec);
        std::string shown = relative.generic_string();
        if (isFile && isFolder) {
            diag_.error(import.loc, "both " + shown + ".cro and the folder " + shown + " exist: rename one of them (D130)");
            return {};
        }
        if (isFile) return {loadFile(file, dotted, import.loc)};
        if (isFolder) {
            std::vector<fs::path> paths;
            for (const fs::directory_entry& entry : fs::directory_iterator(folder, ec)) {
                if (entry.is_regular_file() && entry.path().extension() == ".cro") paths.push_back(entry.path());
            }
            std::sort(paths.begin(), paths.end());
            if (paths.empty()) diag_.error(import.loc, "the folder " + shown + " has no .cro files (D130)");
            std::vector<int> files;
            for (const fs::path& path : paths) files.push_back(loadFile(path, dotted + "." + path.stem().string(), import.loc));
            return files;
        }
        diag_.error(import.loc, "cannot find the module " + dotted + ": there is no " + shown + ".cro or folder " + shown +
                                    " next to " + fs::path(sources_[0].path).filename().string() + " (D130)");
        return {};
    }

    int loadFile(const fs::path& path, const std::string& name, SourceLocation at) {
        fs::path key = fs::weakly_canonical(path);
        auto found = loaded_.find(key);
        if (found != loaded_.end()) return found->second;
        sources_.push_back({path.string(), {}});
        if (!readText(path.string(), sources_.back().text)) diag_.error(at, "cannot read " + path.string());
        int index = diag_.addFile(sources_.back());
        loaded_[key] = index;
        names_.push_back(name);
        parseFile(index);
        return index;
    }

    // Two imports whose names would be written the same way in front (D126).
    void checkQualifiers(const ModuleFile& file) {
        std::set<std::string> seen;
        for (const ImportDecl& import : file.imports) {
            if (!seen.insert(import.qualifier()).second) {
                diag_.error(import.loc, "another import is also called " + import.qualifier() + " (D131)");
            }
        }
    }

    // An open document of the editor comes before the file on the disk (D173).
    bool readText(const std::string& path, std::string& text) const {
        if (overlay_) {
            auto found = overlay_->find(sourceKey(path));
            if (found != overlay_->end()) {
                text = found->second;
                return true;
            }
        }
        return readSourceFile(path, text);
    }

    std::deque<SourceFile>& sources_;
    Diagnostics& diag_;
    const std::map<std::string, std::string>* overlay_;
    fs::path root_;
    std::map<fs::path, int> loaded_;
    std::vector<std::string> names_;  // of the modules, by file
    Module program_;
};

}  // namespace

Module loadProgram(std::deque<SourceFile>& sources, Diagnostics& diag, const std::map<std::string, std::string>* overlay) {
    return Loader(sources, diag, overlay).load();
}

}  // namespace cromo
