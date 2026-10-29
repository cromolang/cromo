#include "loader.h"

#include "lexer.h"
#include "parser.h"
#include "runtime_blob.h"

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

bool isEmbedded(const std::string& path) { return path.rfind("cromo:", 0) == 0; }

namespace {

// The suffix of the files of a system (D202): `fs_windows.cro`.
const char* systemSuffix(Target target) { return target == Target::Linux ? "_linux" : "_windows"; }

// The name without the suffix of a system, and whether it has the one of `target`.
// `other` is set for a file of another system, which is left out.
std::string withoutSuffix(const std::string& stem, Target target, bool& own, bool& other) {
    own = other = false;
    for (Target system : {Target::Windows, Target::Linux}) {
        std::string suffix = systemSuffix(system);
        if (stem.size() > suffix.size() && stem.compare(stem.size() - suffix.size(), suffix.size(), suffix) == 0) {
            (system == target ? own : other) = true;
            return stem.substr(0, stem.size() - suffix.size());
        }
    }
    return stem;
}

// Reads the main file, the standard library and every module they import (D50).
// `import a.b` is the file a/b.cro, or every .cro file of the folder a/b, in the
// folder of the main file (D130). `sources` is a deque: Diagnostics keeps their
// addresses.
class Loader {
public:
    Loader(std::deque<SourceFile>& sources, Diagnostics& diag, Target target, const std::map<std::string, std::string>* overlay)
        : sources_(sources), diag_(diag), target_(target), overlay_(overlay) {
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
    void parseFile(int index, int part = -1) {
        Module module = parse(tokenize(sources_[index], index, diag_), diag_);
        module.files.resize(index + 1);
        module.files[index].name = names_[index];
        module.files[index].part = part;
        program_.append(std::move(module));
    }

    std::vector<int> resolve(const ImportDecl& import) {
        std::string dotted;
        fs::path relative;
        for (const std::string& part : import.path) {
            dotted += (dotted.empty() ? "" : ".") + part;
            relative /= part;
        }
        if (import.path[0] == "std") return resolveLibrary(import, dotted);
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
        if (isFile) {
            int index = loadFile(file, dotted, import.loc);
            fs::path own = root_ / relative;
            own += std::string(systemSuffix(target_)) + ".cro";
            if (fs::is_regular_file(own, ec)) loadFile(own, dotted, import.loc, index);
            return {index};
        }
        if (isFolder) {
            // A file with the suffix of a system joins the file without it, or is a
            // module of its own; the files of the other system are left out (D202).
            std::map<std::string, std::vector<fs::path>> modules;
            for (const fs::directory_entry& entry : fs::directory_iterator(folder, ec)) {
                if (!entry.is_regular_file() || entry.path().extension() != ".cro") continue;
                bool own, other;
                std::string name = withoutSuffix(entry.path().stem().string(), target_, own, other);
                if (!other) modules[name].push_back(entry.path());
            }
            if (modules.empty()) diag_.error(import.loc, "the folder " + shown + " has no .cro files for this system (D130, D202)");
            std::vector<int> files;
            for (auto& [name, paths] : modules) {
                std::sort(paths.begin(), paths.end(), [](const fs::path& a, const fs::path& b) {
                    return a.stem().string().size() < b.stem().string().size();  // the file without the suffix first
                });
                int first = loadFile(paths[0], dotted + "." + name, import.loc);
                for (size_t i = 1; i < paths.size(); ++i) loadFile(paths[i], dotted + "." + name, import.loc, first);
                files.push_back(first);
            }
            return files;
        }
        diag_.error(import.loc, "cannot find the module " + dotted + ": there is no " + shown + ".cro or folder " + shown +
                                    " next to " + fs::path(sources_[0].path).filename().string() + " (D130)");
        return {};
    }

    int loadFile(const fs::path& path, const std::string& name, SourceLocation at, int part = -1) {
        fs::path key = fs::weakly_canonical(path);
        auto found = loaded_.find(key);
        if (found != loaded_.end()) return found->second;
        sources_.push_back({path.string(), {}});
        if (!readText(path.string(), sources_.back().text)) diag_.error(at, "cannot read " + path.string());
        int index = diag_.addFile(sources_.back());
        loaded_[key] = index;
        names_.push_back(name);
        parseFile(index, part);
        return index;
    }

    // `import std.fs` (D201): the module embedded in cromo, with its file for the
    // system (D202).
    std::vector<int> resolveLibrary(const ImportDecl& import, const std::string& dotted) {
        std::string base = "std/" + dotted.substr(4);
        auto found = loaded_.find(fs::path("cromo:" + base));
        if (found != loaded_.end()) return {found->second};
        std::optional<std::string_view> text = libraryModule(base + ".cro");
        if (import.path.size() != 2 || !text) {
            diag_.error(import.loc, "the standard library has no module " + dotted + ": it has std.fs, std.time and std.os, "
                                    "and its core is visible without import (D201)");
            return {};
        }
        int first = embed(base + ".cro", *text, dotted, -1);
        loaded_[fs::path("cromo:" + base)] = first;
        std::string own = base + systemSuffix(target_) + ".cro";
        if (std::optional<std::string_view> part = libraryModule(own)) embed(own, *part, dotted, first);
        return {first};
    }

    int embed(const std::string& name, std::string_view text, const std::string& module, int part) {
        sources_.push_back({"cromo:" + name, std::string(text)});
        int index = diag_.addFile(sources_.back());
        names_.push_back(module);
        parseFile(index, part);
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
    Target target_;
    const std::map<std::string, std::string>* overlay_;
    fs::path root_;
    std::map<fs::path, int> loaded_;
    std::vector<std::string> names_;  // of the modules, by file
    Module program_;
};

}  // namespace

Module loadProgram(std::deque<SourceFile>& sources, Diagnostics& diag, Target target,
                   const std::map<std::string, std::string>* overlay) {
    return Loader(sources, diag, target, overlay).load();
}

}  // namespace cromo
