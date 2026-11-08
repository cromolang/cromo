#include "loader.h"

#include "installation.h"
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

std::string projectFolder(const std::string& folder) {
    std::error_code ec;
    fs::path at = fs::weakly_canonical(fs::absolute(folder, ec), ec);
    for (;;) {
        if (fs::is_regular_file(at / "cromo.mod", ec)) return at.string();
        fs::path up = at.parent_path();
        if (up.empty() || up == at) return {};
        at = up;
    }
}

namespace {

// The modules of the library that serve only the library: std.winsock declares
// `connect` and `listen` of C apart from those of std.net; std.runtime is the
// runtime written in Cromo, which cromo build --runtime compiles into the archive
// of the runtime (D393).
const char* const kInternalModules[] = {"std.winsock", "std.runtime"};

// The suffixes of the files of a system and of an architecture (D202, D408):
// `fs_windows.cro`, `fs_arm64.cro`, `fs_linux_arm64.cro`.
const char* const kSystemSuffixes[] = {"_windows", "_linux"};
const char* const kArchSuffixes[] = {"_x86_64", "_arm64"};  // P23

std::string systemSuffix(Target target) { return isLinux(target) ? "_linux" : "_windows"; }
std::string archSuffix(Target target) { return isArm64(target) ? "_arm64" : "_x86_64"; }

// The suffixes of the files that join a module for `target`, in the order they
// are read.
std::vector<std::string> ownSuffixes(Target target) {
    return {systemSuffix(target), archSuffix(target), systemSuffix(target) + archSuffix(target)};
}

// `name` without the suffix `suffix` at its end, if it has it.
bool removeSuffix(std::string& name, const std::string& suffix) {
    if (name.size() <= suffix.size() || name.compare(name.size() - suffix.size(), suffix.size(), suffix) != 0) return false;
    name.resize(name.size() - suffix.size());
    return true;
}

// The name without the suffixes of a system and of an architecture, and whether
// they are those of `target`. `other` is set for a file of another system or
// architecture, which is left out.
std::string withoutSuffix(const std::string& stem, Target target, bool& own, bool& other) {
    own = other = false;
    std::string name = stem;
    bool mine = true;
    bool found = false;
    for (const char* arch : kArchSuffixes) {
        if (removeSuffix(name, arch)) {
            mine = arch == archSuffix(target);
            found = true;
            break;
        }
    }
    for (const char* system : kSystemSuffixes) {
        if (removeSuffix(name, system)) {
            mine = mine && system == systemSuffix(target);
            found = true;
            break;
        }
    }
    if (found) (mine ? own : other) = true;
    return name;
}

// Reads the main file, the standard library and every module they import (D50).
// `import a.b` is the file a/b.cro, or the folder a/b, whose files are one module
// (D515), in the folder of cromo.mod (D516) or of the main file (D130). `sources` is
// a deque: Diagnostics keeps their addresses.
class Loader {
public:
    Loader(std::deque<SourceFile>& sources, Diagnostics& diag, Target target, const std::map<std::string, std::string>* overlay,
           bool tests)
        : sources_(sources), diag_(diag), target_(target), overlay_(overlay), tests_(tests) {
        fs::path main = fs::absolute(sources[0].path);
        std::string project = projectFolder(main.parent_path().string());
        root_ = project.empty() ? main.parent_path() : fs::path(project);
        inProject_ = !project.empty();
        loaded_[fs::weakly_canonical(main)] = 0;
        names_ = {main.stem().string(), "std"};
    }

    Module load() {
        parseFile(kLibraryFile);
        parseFile(0);
        loadTests(fs::absolute(sources_[0].path), names_[0], 0);
        // The list of files grows while their imports are loaded.
        for (size_t i = 0; i < sources_.size(); ++i) {
            for (size_t k = 0; k < program_.files[i].imports.size(); ++k) {
                std::vector<int> files = resolve(program_.files[i].imports[k], isLibraryFile(sources_[i].path));
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
        module.files[index].isLibrary = isLibraryFile(sources_[index].path);
        program_.append(std::move(module));
    }

    std::vector<int> resolve(const ImportDecl& import, bool fromLibrary) {
        std::string dotted;
        fs::path relative;
        for (const std::string& part : import.path) {
            dotted += (dotted.empty() ? "" : ".") + part;
            relative /= part;
        }
        if (import.path[0] == "std") return resolveLibrary(import, dotted, fromLibrary);
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
            for (const std::string& suffix : ownSuffixes(target_)) {
                fs::path own = root_ / relative;
                own += suffix + ".cro";
                if (fs::is_regular_file(own, ec)) loadFile(own, dotted, import.loc, index);
            }
            loadTests(file, dotted, index);
            return {index};
        }
        if (isFolder) {
            // D515: one module of the files of the folder, in the order of their names,
            // the first holding its names; those of another system or architecture are
            // left out (D202, D408), and the tests join it for cromo test (D350).
            std::vector<fs::path> paths;
            std::vector<fs::path> tests;
            for (const fs::directory_entry& entry : fs::directory_iterator(folder, ec)) {
                if (!entry.is_regular_file() || entry.path().extension() != ".cro") continue;
                if (isTestFile(entry.path())) {
                    tests.push_back(entry.path());
                    continue;
                }
                bool own, other;
                withoutSuffix(entry.path().stem().string(), target_, own, other);
                if (!other) paths.push_back(entry.path());
            }
            if (paths.empty()) {
                diag_.error(import.loc, "the folder " + shown + " has no .cro files for this system (D130, D202)");
                return {};
            }
            auto byName = [](const fs::path& a, const fs::path& b) { return a.filename().string() < b.filename().string(); };
            std::sort(paths.begin(), paths.end(), byName);
            std::sort(tests.begin(), tests.end(), byName);
            int first = loadFile(paths[0], dotted, import.loc);
            for (size_t i = 1; i < paths.size(); ++i) loadFile(paths[i], dotted, import.loc, first);
            if (tests_) {
                for (const fs::path& test : tests) loadFile(test, dotted, import.loc, first);
            }
            return {first};
        }
        diag_.error(import.loc, "cannot find the module " + dotted + ": there is no " + shown + ".cro or folder " + shown +
                                    " next to " +
                                    (inProject_ ? std::string("cromo.mod") : fs::path(sources_[0].path).filename().string()) +
                                    (inProject_ ? " (D516)" : " (D130)"));
        return {};
    }

    static bool isTestFile(const fs::path& path) {
        std::string stem = path.stem().string();
        return stem.size() > 5 && stem.compare(stem.size() - 5, 5, "_test") == 0;
    }

    // D350: `geometry_test.cro` is a part of the module of `geometry.cro`, only for
    // cromo test.
    void loadTests(const fs::path& file, const std::string& name, int index) {
        if (!tests_ || isTestFile(file)) return;
        fs::path tests = file.parent_path() / (file.stem().string() + "_test.cro");
        std::error_code ec;
        if (fs::is_regular_file(tests, ec) || (overlay_ && overlay_->count(sourceKey(tests.string())))) {
            loadFile(tests, name, SourceLocation{1, 1, index}, index);
        }
    }

    int loadFile(const fs::path& path, const std::string& name, SourceLocation at, int part = -1) {
        fs::path key = fs::weakly_canonical(path);
        auto found = loaded_.find(key);
        if (found != loaded_.end()) {
            // D515: a file of a folder imported by itself too, before or after.
            const std::string& had = names_[found->second];
            if (found->second != 0 && had != name) {
                const std::string& file = had.size() > name.size() ? had : name;
                const std::string& folder = had.size() > name.size() ? name : had;
                std::string shown = file;
                std::replace(shown.begin(), shown.end(), '.', '/');
                diag_.error(at, "the file " + shown + ".cro is a part of the module " + folder + ": import " + folder + " (D515)");
            }
            return found->second;
        }
        sources_.push_back({path.string(), {}});
        if (!readText(path.string(), sources_.back().text)) diag_.error(at, "cannot read " + path.string());
        int index = diag_.addFile(sources_.back());
        loaded_[key] = index;
        names_.push_back(name);
        parseFile(index, part);
        return index;
    }

    // `import std.fs` (D201): the module in lib/std of the installation (D237), with
    // its file for the system (D202).
    std::vector<int> resolveLibrary(const ImportDecl& import, const std::string& dotted, bool fromLibrary) {
        std::string base = "std/" + dotted.substr(4);
        bool hidden = std::find(std::begin(kInternalModules), std::end(kInternalModules), dotted) != std::end(kInternalModules);
        fs::path path = libraryFile(base + ".cro");
        std::error_code ec;
        if (import.path.size() != 2 || !fs::is_regular_file(path, ec) || (hidden && !fromLibrary)) {
            diag_.error(import.loc, "the standard library has no module " + dotted + ": it has " + libraryModules() +
                                        ", and its core is visible without import (D201)");
            return {};
        }
        int first = loadFile(path, dotted, import.loc);
        for (const std::string& suffix : ownSuffixes(target_)) {
            fs::path own = libraryFile(base + suffix + ".cro");
            if (fs::is_regular_file(own, ec)) loadFile(own, dotted, import.loc, first);
        }
        return {first};
    }

    // "std.crypto, std.fs and std.http": the modules of lib/std for the programs.
    static std::string libraryModules() {
        std::vector<std::string> names = libraryModuleNames();
        std::string text;
        for (size_t i = 0; i < names.size(); ++i) {
            text += (i == 0 ? "" : i + 1 == names.size() ? " and " : ", ") + names[i];
        }
        return text;
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
    bool tests_;
    fs::path root_;
    bool inProject_ = false;  // the root is the folder of cromo.mod (D516)
    std::map<fs::path, int> loaded_;
    std::vector<std::string> names_;  // of the modules, by file
    Module program_;
};

}  // namespace

std::vector<std::string> libraryModuleNames() {
    std::vector<std::string> names;
    std::error_code ec;
    for (const fs::directory_entry& entry : fs::directory_iterator(libraryFile("std"), ec)) {
        if (entry.path().extension() != ".cro") continue;
        bool own = false;
        bool other = false;
        std::string name = "std." + withoutSuffix(entry.path().stem().string(), Target::Windows, own, other);
        if (own || other) continue;
        if (std::find(std::begin(kInternalModules), std::end(kInternalModules), name) != std::end(kInternalModules)) continue;
        names.push_back(name);
    }
    std::sort(names.begin(), names.end());
    return names;
}

Module loadProgram(std::deque<SourceFile>& sources, Diagnostics& diag, Target target,
                   const std::map<std::string, std::string>* overlay, bool tests) {
    return Loader(sources, diag, target, overlay, tests).load();
}

}  // namespace cromo
