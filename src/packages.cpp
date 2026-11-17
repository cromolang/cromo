#include "packages.h"

#include "loader.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <set>
#include <sstream>

namespace cromo {

namespace fs = std::filesystem;

namespace {

bool nameOf(const std::string& text, const char* others) {
    if (text.empty()) return false;
    for (char c : text) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && !std::strchr(others, c)) return false;
    }
    return true;
}

// "github.com/anna/geometry": a host with a dot, then a path.
bool isPackageUrl(const std::string& text) {
    size_t slash = text.find('/');
    if (slash == std::string::npos || slash == 0 || slash + 1 == text.size()) return false;
    if (text.find('.') > slash && text.compare(0, 10, "localhost:") != 0 && text.compare(0, 10, "127.0.0.1:") != 0) return false;
    return nameOf(text, "._-/:") && text.find("//") == std::string::npos && text.back() != '/';
}

std::vector<int> numbersOf(const std::string& version) {
    std::vector<int> numbers;
    std::istringstream parts(version.substr(1));
    for (std::string part; std::getline(parts, part, '.');) numbers.push_back(std::atoi(part.c_str()));
    numbers.resize(3);
    return numbers;
}

std::string keyOf(const std::string& url, const std::string& major) { return url + "@" + major; }

}  // namespace

bool isPackageVersion(const std::string& text) {
    if (text.size() < 6 || text[0] != 'v') return false;
    int dots = 0;
    bool digit = false;
    for (size_t i = 1; i < text.size(); ++i) {
        if (text[i] == '.') {
            if (!digit) return false;
            ++dots;
            digit = false;
        } else if (std::isdigit(static_cast<unsigned char>(text[i]))) {
            digit = true;
        } else {
            return false;
        }
    }
    return dots == 2 && digit;
}

std::string majorOf(const std::string& version) { return version.substr(0, version.find('.')); }

int comparePackageVersions(const std::string& a, const std::string& b) {
    std::vector<int> x = numbersOf(a);
    std::vector<int> y = numbersOf(b);
    return x < y ? -1 : x > y ? 1 : 0;
}

fs::path packagesFolder() {
    if (const char* folder = std::getenv("CROMO_PACKAGES"); folder && *folder) return fs::path(folder);
#ifdef _WIN32
    const char* home = std::getenv("USERPROFILE");
#else
    const char* home = std::getenv("HOME");
#endif
    return fs::path(home ? home : ".") / ".cromo" / "packages";
}

fs::path packageFolder(const std::string& url, const std::string& version) {
    std::string name = url;
    std::replace(name.begin(), name.end(), ':', '_');  // the port of a host of a test
    return packagesFolder() / fs::path(name + "@" + version);
}

std::string packageModuleName(const Package& package) {
    if (package.url.empty()) return package.folder.filename().string();
    return package.major == "v0" || package.major == "v1" ? package.url : package.url + "@" + package.major;
}

std::string nativeSystem(Target target) {
    if (target == Target::Windows) return "windows-x86_64";
    if (target == Target::Linux) return "linux-x86_64";
    return "";
}

// By the hash of the archive and by the files taken of it: another list of files is another folder.
fs::path nativeFolder(const Native& native) {
    unsigned long long mix = 1469598103934665603ull;  // FNV-1a
    for (const std::string& file : native.files) {
        for (char c : file + "\n") mix = (mix ^ static_cast<unsigned char>(c)) * 1099511628211ull;
    }
    char text[20];
    std::snprintf(text, sizeof text, "%016llx", mix);
    return packagesFolder() / ".native" / (native.sha256.substr(0, 16) + "-" + text);
}

std::vector<std::string> nativeFolders(const Package& package, Target target) {
    std::vector<std::string> folders;
    std::string system = nativeSystem(target);
    for (const Native& native : package.natives) {
        if (native.system == system) folders.push_back(nativeFolder(native).string());
    }
    return folders;
}

bool prepareNatives(const Resolution& resolution, Target target, const FetchNative& fetch, std::string& error) {
    std::string system = nativeSystem(target);
    for (const Package& package : resolution.packages) {
        for (const Native& native : package.natives) {
            std::error_code ec;
            if (native.system != system || fs::is_directory(nativeFolder(native), ec)) continue;
            if (!fetch(native, error)) return false;
            if (!fs::is_directory(nativeFolder(native), ec)) {
                error = "the libraries of " + native.url + " were not downloaded";
                return false;
            }
        }
    }
    return true;
}

bool readProjectFile(const fs::path& file, ProjectFile& out, std::string& error) {
    std::string text;
    std::error_code ec;
    if (!fs::is_regular_file(file, ec)) return true;
    if (!readSourceFile(file.string(), text)) {
        error = "cannot read " + file.string();
        return false;
    }
    bool good = true;
    int number = 0;
    std::istringstream lines(text);
    std::string line;
    std::set<std::string> names;
    while (std::getline(lines, line)) {
        ++number;
        size_t comment = line.find("//");
        if (comment != std::string::npos && (comment == 0 || line[comment - 1] != ':')) line.resize(comment);  // not the one of https://
        std::istringstream split(line);
        std::vector<std::string> words;
        for (std::string word; split >> word;) words.push_back(word);
        if (words.empty()) continue;
        auto fail = [&](const std::string& message) {
            error += (error.empty() ? "" : "\n") + file.filename().string() + ":" + std::to_string(number) + ": error: " + message;
            good = false;
        };
        if (words[0] == "module" && words.size() == 2) {
            out.module = words[1];
        } else if (words[0] == "require") {
            Require require;
            require.line = number;
            if (words.size() == 4 && nameOf(words[1], "_") && isPackageUrl(words[2]) && isPackageVersion(words[3])) {
                require.name = words[1];
                require.url = words[2];
                require.version = words[3];
            } else if (words.size() == 3 && nameOf(words[1], "_")) {
                require.name = words[1];
                require.local = words[2];
            } else {
                fail("require takes a name, the address of a package and its version, as require geometry "
                     "github.com/anna/geometry v1.2.0, or a name and a folder, as require geometry ../geometry");
                continue;
            }
            if (require.name == "std" || require.name == "unsafe") {
                fail("a package cannot be called " + require.name);
            } else if (!names.insert(require.name).second) {
                fail("another package is called " + require.name);
            } else {
                out.requires_.push_back(require);
            }
        } else if (words[0] == "native") {
            Native native;
            native.line = number;
            bool system = words.size() > 1 && (words[1] == "windows-x86_64" || words[1] == "linux-x86_64");
            if (words.size() < 5 || !system || words[3].compare(0, 7, "sha256:") != 0 || words[3].size() != 71) {
                fail("native takes a system, windows-x86_64 or linux-x86_64, the address of an archive, its hash as "
                     "sha256: and 64 digits, and the files of it to take");
                continue;
            }
            native.system = words[1];
            native.url = words[2];
            native.sha256 = words[3].substr(7);
            native.files.assign(words.begin() + 4, words.end());
            out.natives.push_back(native);
        } else if (words[0] == "version") {  // D557
            bool valid = words.size() == 2 && out.version.empty();
            int parts = 1;
            size_t digits = 0;
            for (size_t i = 0; valid && i < words[1].size(); ++i) {
                char c = words[1][i];
                if (c == '.') {
                    valid = digits > 0 && ++parts <= 4;
                    digits = 0;
                } else {
                    valid = std::isdigit(static_cast<unsigned char>(c)) && ++digits <= 5;
                    size_t start = i + 1 - digits;
                    if (valid && std::stoi(words[1].substr(start, digits)) > 65535) valid = false;
                }
            }
            if (valid && digits > 0) {
                out.version = words[1];
            } else {
                fail(out.version.empty() ? "version takes up to four numbers, each up to 65535, as version 1.2.0"
                                         : "the line version is written once");
            }
        } else if (words[0] == "company" || words[0] == "description" || words[0] == "copyright") {  // D557
            std::string& field = words[0] == "company" ? out.company : words[0] == "description" ? out.description : out.copyright;
            size_t start = line.find(words[0]) + words[0].size();
            start = line.find_first_not_of(" \t", start);
            size_t end = line.find_last_not_of(" \t\r");
            if (start == std::string::npos) {
                fail(words[0] + " takes a text, to the end of the line");
            } else if (!field.empty()) {
                fail("the line " + words[0] + " is written once");
            } else {
                field = line.substr(start, end - start + 1);
            }
        } else if (words[0] == "sign") {  // D558: words, and "words with spaces"
            std::vector<std::string> command;
            size_t at = line.find("sign") + 4;
            bool closed = true;
            while (true) {
                at = line.find_first_not_of(" \t\r", at);
                if (at == std::string::npos) break;
                std::string word;
                if (line[at] == '"') {
                    size_t end = line.find('"', at + 1);
                    closed = end != std::string::npos;
                    if (!closed) break;
                    word = line.substr(at + 1, end - at - 1);
                    at = end + 1;
                } else {
                    size_t end = line.find_first_of(" \t\r", at);
                    word = line.substr(at, end == std::string::npos ? end : end - at);
                    at = end;
                }
                command.push_back(word);
                if (at == std::string::npos) break;
            }
            if (!closed || command.empty()) {
                fail("sign takes the command that signs an executable, as sign signtool sign /fd SHA256 /a");
            } else if (!out.sign.empty()) {
                fail("the line sign is written once");
            } else {
                out.sign = command;
            }
        }
    }
    return good;
}

bool resolvePackages(const std::string& projectFolder, Resolution& out, std::string& error, const Fetch* fetch) {
    out.packages.clear();
    fs::path root(projectFolder);
    ProjectFile project;
    if (!readProjectFile(root / "cromo.mod", project, error)) return false;

    // What is wanted of each package, by its address and its major version: the
    // highest version among those that the project and the packages chosen require.
    struct Wanted {
        std::string url;
        std::string version;
        fs::path folder;  // of a package in a folder (D390)
        ProjectFile file;
        bool read = false;
    };
    std::map<std::string, Wanted> wanted;
    auto want = [&](const Require& require, const fs::path& from, bool fromProject) -> bool {
        if (!require.local.empty()) {
            if (!fromProject) {
                error = "the package in " + from.string() + " requires the folder " + require.local +
                        ": a folder in place of a version holds only in the project that is built";
                return false;
            }
            std::error_code ec;
            fs::path folder = fs::weakly_canonical(from / require.local, ec);
            if (!fs::is_directory(folder, ec)) {
                error = "cromo.mod:" + std::to_string(require.line) + ": error: there is no folder " + require.local;
                return false;
            }
            Wanted& entry = wanted["folder:" + folder.string()];
            entry.folder = folder;
            return true;
        }
        Wanted& entry = wanted[keyOf(require.url, majorOf(require.version))];
        if (entry.version.empty() || comparePackageVersions(require.version, entry.version) > 0) {
            entry.url = require.url;
            entry.version = require.version;
            entry.read = false;
        }
        return true;
    };
    for (const Require& require : project.requires_) {
        if (!want(require, root, true)) return false;
    }
    for (bool again = true; again;) {
        again = false;
        for (auto& [key, entry] : wanted) {
            if (entry.read) continue;
            entry.read = true;
            again = true;
            if (entry.folder.empty()) {
                fs::path folder = packageFolder(entry.url, entry.version);
                std::error_code ec;
                if (!fs::is_directory(folder, ec)) {
                    if (!fetch) {
                        error = "the package " + entry.url + " " + entry.version + " is not downloaded yet: cromo build downloads it";
                        return false;
                    }
                    if (!(*fetch)(entry.url, entry.version, error)) return false;
                    if (!fs::is_directory(folder, ec)) {
                        error = "the package " + entry.url + " " + entry.version + " was not downloaded";
                        return false;
                    }
                }
            }
            fs::path folder = entry.folder.empty() ? packageFolder(entry.url, entry.version) : entry.folder;
            entry.file = ProjectFile();
            std::string problems;
            if (!readProjectFile(folder / "cromo.mod", entry.file, problems)) {
                error = "the package in " + folder.string() + " has a wrong cromo.mod:\n" + problems;
                return false;
            }
            if (entry.folder.empty() && !entry.file.module.empty() && entry.file.module != entry.url) {
                error = "the package downloaded from " + entry.url + " says in its cromo.mod that it is " + entry.file.module +
                        ": require it by that address";
                return false;
            }
            for (const Require& require : entry.file.requires_) {
                if (!want(require, folder, false)) return false;
            }
            break;  // the map may have changed: from its start again
        }
    }

    // The packages, the project first, each with the names of its cromo.mod.
    std::map<std::string, int> numbers;
    Package first;
    first.folder = root;
    first.natives = project.natives;
    out.packages.push_back(first);
    for (auto& [key, entry] : wanted) {
        Package package;
        package.url = entry.url;
        package.version = entry.version;
        package.major = entry.version.empty() ? "" : majorOf(entry.version);
        package.local = !entry.folder.empty();
        package.folder = package.local ? entry.folder : packageFolder(entry.url, entry.version);
        package.natives = entry.file.natives;
        numbers[key] = static_cast<int>(out.packages.size());
        out.packages.push_back(package);
    }
    auto name = [&](Package& package, const std::vector<Require>& requires_, const fs::path& from) {
        for (const Require& require : requires_) {
            std::error_code ec;
            std::string key = require.local.empty() ? keyOf(require.url, majorOf(require.version))
                                                    : "folder:" + fs::weakly_canonical(from / require.local, ec).string();
            package.requires_[require.name] = numbers[key];
        }
    };
    name(out.packages[0], project.requires_, root);
    for (auto& [key, entry] : wanted) name(out.packages[numbers[key]], entry.file.requires_, out.packages[numbers[key]].folder);
    return true;
}

bool checkPackageSums(const std::string& projectFolder, const Resolution& resolution, bool write, std::string& error) {
    fs::path file = fs::path(projectFolder) / "cromo.sum";
    std::map<std::string, std::string> written;  // "url version" and its hash
    std::string before;
    std::error_code ec;
    if (fs::is_regular_file(file, ec)) {
        readSourceFile(file.string(), before);
        std::istringstream lines(before);
        for (std::string line; std::getline(lines, line);) {
            std::istringstream split(line);
            std::string url, version, hash;
            if (split >> url >> version >> hash) written[url + " " + version] = hash;
        }
    }
    std::string text;
    std::vector<std::string> rows;
    for (size_t i = 1; i < resolution.packages.size(); ++i) {
        const Package& package = resolution.packages[i];
        if (package.local) continue;
        fs::path sum = packageFolder(package.url, package.version);
        sum += ".sum";
        std::string hash;
        if (!readSourceFile(sum.string(), hash)) {
            error = "the cache has no hash of " + package.url + " " + package.version + ": delete its folder, " +
                    packageFolder(package.url, package.version).string() + ", and build again";
            return false;
        }
        while (!hash.empty() && std::isspace(static_cast<unsigned char>(hash.back()))) hash.pop_back();
        auto found = written.find(package.url + " " + package.version);
        if (found != written.end() && found->second != hash) {
            error = "the files of " + package.url + " " + package.version + " are not those of cromo.sum: its tag was moved, or "
                    "the download is wrong. cromo.sum has " + found->second + ", the files give " + hash;
            return false;
        }
        rows.push_back(package.url + " " + package.version + " " + hash);
    }
    std::sort(rows.begin(), rows.end());
    for (const std::string& row : rows) text += row + "\n";
    if (write && text != before && !(text.empty() && before.empty())) {
        std::ofstream out(file, std::ios::binary);
        out << text;
        if (!out) {
            error = "cannot write " + file.string();
            return false;
        }
    }
    return true;
}

}  // namespace cromo
