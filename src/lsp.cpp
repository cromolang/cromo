#include "lsp.h"

#include "format.h"
#include "index.h"
#include "json.h"
#include "lexer.h"
#include "loader.h"
#include "ownership.h"
#include "parser.h"
#include "runtime_blob.h"
#include "sema.h"

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

namespace cromo {

namespace {

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

// ---- Messages: JSON with a Content-Length header (LSP, D170) ----

// Reads the messages in a thread of its own, so that the server can wait for
// the next one with a time limit (D173).
class Inbox {
public:
    enum class Wait { Message, Timeout, End };

    void start() {
        std::thread([this] { read(); }).detach();
    }

    Wait next(std::optional<Clock::time_point> deadline, Json& out) {
        std::unique_lock<std::mutex> lock(mutex_);
        auto ready = [this] { return !queue_.empty() || ended_; };
        if (deadline) {
            if (!ready_.wait_until(lock, *deadline, ready)) return Wait::Timeout;
        } else {
            ready_.wait(lock, ready);
        }
        if (queue_.empty()) return Wait::End;
        out = std::move(queue_.front());
        queue_.pop_front();
        return Wait::Message;
    }

private:
    void read() {
        for (;;) {
            size_t length = 0;
            for (;;) {
                std::string line;
                int c;
                while ((c = std::fgetc(stdin)) != EOF && c != '\n') line += static_cast<char>(c);
                if (c == EOF) return finish();
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (line.empty()) break;
                // Anywhere in the line: some clients write a UTF-8 BOM before the first header.
                size_t at = line.find("Content-Length:");
                if (at != std::string::npos) length = std::strtoul(line.c_str() + at + 15, nullptr, 10);
            }
            std::string body(length, '\0');
            if (length && std::fread(body.data(), 1, length, stdin) != length) return finish();
            Json message;
            if (!Json::parse(body, message)) continue;
            std::lock_guard<std::mutex> lock(mutex_);
            queue_.push_back(std::move(message));
            ready_.notify_one();
        }
    }

    void finish() {
        std::lock_guard<std::mutex> lock(mutex_);
        ended_ = true;
        ready_.notify_one();
    }

    std::mutex mutex_;
    std::condition_variable ready_;
    std::deque<Json> queue_;
    bool ended_ = false;
};

void send(const Json& message) {
    std::string body = message.dump();
    std::fprintf(stdout, "Content-Length: %zu\r\n\r\n", body.size());
    std::fwrite(body.data(), 1, body.size(), stdout);
    std::fflush(stdout);
}

Json response(const Json& id, Json result) {
    Json message = Json::object();
    message.set("jsonrpc", "2.0");
    message.set("id", id);
    message.set("result", std::move(result));
    return message;
}

// ---- Files and positions ----

std::string pathFromUri(const std::string& uri) {
    std::string text = uri.rfind("file://", 0) == 0 ? uri.substr(7) : uri;
    std::string decoded;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '%' && i + 2 < text.size()) {
            decoded += static_cast<char>(std::strtol(text.substr(i + 1, 2).c_str(), nullptr, 16));
            i += 2;
        } else {
            decoded += text[i];
        }
    }
    if (decoded.size() > 2 && decoded[0] == '/' && decoded[2] == ':') decoded.erase(0, 1);  // /c:/dati
    return fs::path(std::u8string(decoded.begin(), decoded.end())).make_preferred().string();
}

std::string uriFromPath(const std::string& path) {
    std::error_code ec;
    std::u8string generic = fs::absolute(path, ec).generic_u8string();
    std::string out = "file://";
    if (generic.empty() || generic[0] != u8'/') out += '/';
    const char* hex = "0123456789ABCDEF";
    for (size_t i = 0; i < generic.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(generic[i]);
        if (i == 0 && c >= 'A' && c <= 'Z') c = static_cast<unsigned char>(c - 'A' + 'a');  // the drive, as VS Code writes it
        bool plain = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '.' ||
                     c == '_' || c == '~' || c == '/';
        if (plain) {
            out += static_cast<char>(c);
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 0xF];
        }
    }
    return out;
}

// The start of a line of a text, from 1, or the end of the text.
size_t lineStart(const std::string& text, int line) {
    size_t pos = 0;
    for (int i = 1; i < line && pos < text.size(); ++i) {
        size_t next = text.find('\n', pos);
        if (next == std::string::npos) return text.size();
        pos = next + 1;
    }
    return pos;
}

size_t utf8Length(unsigned char lead) { return lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1; }

// LSP counts lines from 0 and UTF-16 units; cromo counts lines and characters from 1.
Json position(const std::string& text, SourceLocation loc) {
    size_t pos = lineStart(text, loc.line);
    int units = 0;
    for (int column = 1; column < loc.column && pos < text.size() && text[pos] != '\n'; ++column) {
        size_t size = utf8Length(static_cast<unsigned char>(text[pos]));
        units += size == 4 ? 2 : 1;
        pos += size;
    }
    Json out = Json::object();
    out.set("line", loc.line - 1);
    out.set("character", units);
    return out;
}

SourceLocation locationAt(const std::string& text, const Json& position, int file) {
    SourceLocation loc{position["line"].asInt() + 1, 1, file};
    size_t pos = lineStart(text, loc.line);
    int units = position["character"].asInt();
    while (units > 0 && pos < text.size() && text[pos] != '\n') {
        size_t size = utf8Length(static_cast<unsigned char>(text[pos]));
        units -= size == 4 ? 2 : 1;
        pos += size;
        ++loc.column;
    }
    return loc;
}

// The range of `length` characters from `loc`.
Json range(const std::string& text, SourceLocation loc, int length) {
    SourceLocation end = loc;
    end.column += length;
    Json out = Json::object();
    out.set("start", position(text, loc));
    out.set("end", position(text, end));
    return out;
}

bool isIdentChar(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

// The byte of a text at an LSP position.
size_t offsetAt(const std::string& text, const Json& position) {
    size_t pos = lineStart(text, position["line"].asInt() + 1);
    int units = position["character"].asInt();
    while (units > 0 && pos < text.size() && text[pos] != '\n') {
        size_t size = utf8Length(static_cast<unsigned char>(text[pos]));
        units -= size == 4 ? 2 : 1;
        pos += size;
    }
    return pos;
}

// The line and the column, from 1, of a byte of a text.
SourceLocation locationOf(const std::string& text, size_t offset, int file) {
    SourceLocation loc{1, 1, file};
    for (size_t i = 0; i < offset && i < text.size(); i += utf8Length(static_cast<unsigned char>(text[i]))) {
        if (text[i] == '\n') {
            ++loc.line;
            loc.column = 1;
        } else {
            ++loc.column;
        }
    }
    return loc;
}

// The length of the name at `loc`, or 1 for anything else.
int tokenLength(const std::string& text, SourceLocation loc) {
    size_t pos = lineStart(text, loc.line);
    for (int column = 1; column < loc.column && pos < text.size() && text[pos] != '\n'; ++column) {
        pos += utf8Length(static_cast<unsigned char>(text[pos]));
    }
    int length = 0;
    while (pos < text.size()) {
        char c = text[pos];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')) break;
        ++pos;
        ++length;
    }
    return length > 0 ? length : 1;
}

// ---- The analysis of a program (D172) ----

struct Document {
    std::string uri;
    std::string path;
    std::string text;
};

struct Analysis {
    std::string rootPath;
    bool requireMain = true;
    std::deque<SourceFile> sources;
    Module module;
    std::vector<DiagnosticMessage> messages;
    std::vector<Reference> references;
    std::vector<std::string> keys;  // sourceKey of each file; empty for the standard library
    bool checked = false;           // it reached sema
    std::set<std::string> published;

    int fileOf(const std::string& key) const {
        for (size_t i = 0; i < keys.size(); ++i) {
            if (keys[i] == key) return static_cast<int>(i);
        }
        return -1;
    }
};

class Server {
public:
    int run() {
#ifdef _WIN32
        _setmode(_fileno(stdin), _O_BINARY);
        _setmode(_fileno(stdout), _O_BINARY);
#endif
        inbox_.start();
        std::optional<Clock::time_point> deadline;
        for (;;) {
            Json message;
            Inbox::Wait wait = inbox_.next(deadline, message);
            if (wait == Inbox::Wait::End) return shutdown_ ? 0 : 1;
            if (wait == Inbox::Wait::Timeout) {  // D173: the pause after the last change
                deadline.reset();
                analyzeDirty();
                continue;
            }
            const std::string& method = message["method"].asString();
            const Json& id = message["id"];
            const Json& params = message["params"];
            if (method == "exit") return shutdown_ ? 0 : 1;
            if (method == "textDocument/didChange") {
                change(params);
                deadline = Clock::now() + std::chrono::milliseconds(300);
                continue;
            }
            if (method == "textDocument/didOpen") {
                open(params);
                analyzeDirty();  // D176: at once
                continue;
            }
            if (method == "textDocument/didClose") {
                close(params);
                continue;
            }
            if (id.isNull()) continue;  // the other notifications

            analyzeDirty();  // D176: a request sees the last text
            deadline.reset();
            if (method == "initialize") {
                send(response(id, initialize(params)));
            } else if (method == "shutdown") {
                shutdown_ = true;
                send(response(id, Json()));
            } else if (method == "textDocument/hover") {
                send(response(id, hover(params)));
            } else if (method == "textDocument/definition") {
                send(response(id, definition(params)));
            } else if (method == "textDocument/documentSymbol") {
                send(response(id, symbols(params)));
            } else if (method == "textDocument/formatting") {
                send(response(id, formatting(params)));
            } else if (method == "textDocument/completion") {
                send(response(id, completion(params)));
            } else {
                Json error = Json::object();
                error.set("code", -32601);
                error.set("message", "cromo lsp does not support " + method);
                Json reply = Json::object();
                reply.set("jsonrpc", "2.0");
                reply.set("id", id);
                reply.set("error", std::move(error));
                send(reply);
            }
        }
    }

private:
    Json initialize(const Json& params) {
        const Json& root = params["rootUri"];
        if (!root.isNull()) {
            workspace_ = pathFromUri(root.asString());
        } else if (!params["workspaceFolders"].items().empty()) {
            workspace_ = pathFromUri(params["workspaceFolders"].items()[0]["uri"].asString());
        }
        Json sync = Json::object();
        sync.set("openClose", true);
        sync.set("change", 1);  // D174: the whole text
        Json capabilities = Json::object();
        capabilities.set("textDocumentSync", std::move(sync));
        capabilities.set("hoverProvider", true);
        capabilities.set("definitionProvider", true);
        capabilities.set("documentSymbolProvider", true);
        capabilities.set("documentFormattingProvider", true);
        Json completion = Json::object();
        Json triggers = Json::array();
        triggers.push(".");
        completion.set("triggerCharacters", std::move(triggers));
        capabilities.set("completionProvider", std::move(completion));  // D179
        Json info = Json::object();
        info.set("name", "cromo");
        info.set("version", CROMO_VERSION);
        Json result = Json::object();
        result.set("capabilities", std::move(capabilities));
        result.set("serverInfo", std::move(info));
        return result;
    }

    void open(const Json& params) {
        const Json& item = params["textDocument"];
        Document document{item["uri"].asString(), pathFromUri(item["uri"].asString()), item["text"].asString()};
        std::string key = sourceKey(document.path);
        documents_[key] = std::move(document);
        dirty_.insert(key);
    }

    void change(const Json& params) {
        std::string key = sourceKey(pathFromUri(params["textDocument"]["uri"].asString()));
        auto found = documents_.find(key);
        const std::vector<Json>& changes = params["contentChanges"].items();
        if (found == documents_.end() || changes.empty()) return;
        found->second.text = changes.back()["text"].asString();
        dirty_.insert(key);
    }

    void close(const Json& params) {
        std::string uri = params["textDocument"]["uri"].asString();
        std::string key = sourceKey(pathFromUri(uri));
        documents_.erase(key);
        dirty_.erase(key);
        publish(uri, Json::array());
        // The programs that no open document needs are forgotten.
        for (auto it = analyses_.begin(); it != analyses_.end();) {
            bool needed = false;
            for (const auto& [documentKey, document] : documents_) needed = needed || it->second->fileOf(documentKey) >= 0;
            if (needed) {
                ++it;
            } else {
                for (const std::string& published : it->second->published) {
                    if (published != key) publish(uriOf(published, *it->second), Json::array());
                }
                it = analyses_.erase(it);
            }
        }
    }

    // D172: the file itself if it has main, else the nearest main.cro above it,
    // else the file alone.
    std::string findRoot(const Document& document, bool& requireMain) const {
        SourceFile file{document.path, document.text};
        Diagnostics quiet(file);
        quiet.setSilent(true);
        Module alone = parse(tokenize(file, 0, quiet), quiet);
        for (const FunctionDecl& function : alone.functions) {
            if (function.name == "main" && !function.isExtern) {
                requireMain = true;
                return document.path;
            }
        }
        std::string documentKey = sourceKey(document.path);
        std::string workspaceKey = workspace_.empty() ? std::string() : sourceKey(workspace_);
        fs::path folder = fs::path(document.path).parent_path();
        for (;;) {
            fs::path candidate = folder / "main.cro";
            std::string key = sourceKey(candidate.string());
            std::error_code ec;
            if (key != documentKey && (documents_.count(key) || fs::is_regular_file(candidate, ec))) {
                requireMain = true;
                return candidate.string();
            }
            if (sourceKey(folder.string()) == workspaceKey) break;  // not above the workspace
            fs::path parent = folder.parent_path();
            if (parent == folder) break;
            folder = parent;
        }
        requireMain = false;
        return document.path;
    }

    // `replaced`, if given, is a document with another text: the completion analyzes
    // a text that is not in the editor (D179).
    std::shared_ptr<Analysis> analyze(const std::string& rootPath, bool requireMain,
                                      const std::pair<std::string, std::string>* replaced = nullptr) {
        auto analysis = std::make_shared<Analysis>();
        analysis->rootPath = rootPath;
        analysis->requireMain = requireMain;
        std::map<std::string, std::string> overlay;
        for (const auto& [key, document] : documents_) overlay[key] = document.text;
        if (replaced) overlay[replaced->first] = replaced->second;

        analysis->sources.push_back({rootPath, {}});
        auto found = overlay.find(sourceKey(rootPath));
        if (found != overlay.end()) {
            analysis->sources[0].text = found->second;
        } else {
            readSourceFile(rootPath, analysis->sources[0].text);
        }
        analysis->sources.push_back({"cromo:core.cro", std::string(coreLibrary())});
        Diagnostics diag(analysis->sources[0]);
        diag.addFile(analysis->sources[1]);
        diag.collectInto(&analysis->messages);
        analysis->module = loadProgram(analysis->sources, diag, Target::Windows, &overlay);
        if (!diag.hasErrors()) {
            analysis->checked = true;
            if (check(analysis->module, diag, requireMain)) checkMoves(analysis->module, diag);
            analysis->references = indexProgram(analysis->module, analysis->sources);
        }
        for (size_t i = 0; i < analysis->sources.size(); ++i) {
            analysis->keys.push_back(static_cast<int>(i) == kLibraryFile ? std::string() : sourceKey(analysis->sources[i].path));
        }
        return analysis;
    }

    void store(const std::string& rootKey, std::shared_ptr<Analysis> analysis) {
        auto old = analyses_.find(rootKey);
        if (old != analyses_.end()) {
            // D177: with syntax errors, hover and definition use the last analysis without them.
            if (!analysis->checked && old->second->checked) analysis->references = old->second->references;
            for (const std::string& key : old->second->published) {
                if (analysis->fileOf(key) < 0) publish(uriOf(key, *old->second), Json::array());
            }
        }
        publishAll(*analysis);
        analyses_[rootKey] = std::move(analysis);
    }

    void analyzeDirty() {
        if (dirty_.empty()) return;
        std::map<std::string, std::pair<std::string, bool>> roots;  // root key: path, requireMain
        for (const std::string& key : dirty_) {
            for (const auto& [rootKey, analysis] : analyses_) {
                if (analysis->fileOf(key) >= 0) roots[rootKey] = {analysis->rootPath, analysis->requireMain};
            }
            auto document = documents_.find(key);
            if (document == documents_.end()) continue;
            bool requireMain = true;
            std::string root = findRoot(document->second, requireMain);
            roots[sourceKey(root)] = {root, requireMain};
        }
        dirty_.clear();
        for (const auto& [rootKey, root] : roots) store(rootKey, analyze(root.first, root.second));
        // A module that its main.cro does not import is analyzed alone.
        for (const auto& [key, document] : documents_) {
            if (analysisOf(key)) continue;
            store(key, analyze(document.path, false));
        }
    }

    // The analysis with the document as its main file, or another that contains it.
    const Analysis* analysisOf(const std::string& key) const {
        auto own = analyses_.find(key);
        if (own != analyses_.end() && own->second->fileOf(key) >= 0) return own->second.get();
        for (const auto& [rootKey, analysis] : analyses_) {
            if (analysis->fileOf(key) >= 0) return analysis.get();
        }
        return nullptr;
    }

    std::string uriOf(const std::string& key, const Analysis& analysis) const {
        auto document = documents_.find(key);
        if (document != documents_.end()) return document->second.uri;
        int file = analysis.fileOf(key);
        return uriFromPath(file >= 0 ? analysis.sources[file].path : key);
    }

    void publish(const std::string& uri, Json diagnostics) {
        Json params = Json::object();
        params.set("uri", uri);
        params.set("diagnostics", std::move(diagnostics));
        Json message = Json::object();
        message.set("jsonrpc", "2.0");
        message.set("method", "textDocument/publishDiagnostics");
        message.set("params", std::move(params));
        send(message);
    }

    // D175: every file of the program gets its errors, also when it is not open.
    void publishAll(Analysis& analysis) {
        for (size_t file = 0; file < analysis.sources.size(); ++file) {
            if (static_cast<int>(file) == kLibraryFile) continue;
            const std::string& text = analysis.sources[file].text;
            Json diagnostics = Json::array();
            for (const DiagnosticMessage& message : analysis.messages) {
                if (static_cast<size_t>(message.loc.file) != file) continue;
                Json item = Json::object();
                item.set("range", range(text, message.loc, tokenLength(text, message.loc)));
                item.set("severity", message.warning ? 2 : 1);
                item.set("source", "cromo");
                item.set("message", message.text);
                diagnostics.push(std::move(item));
            }
            publish(uriOf(analysis.keys[file], analysis), std::move(diagnostics));
            analysis.published.insert(analysis.keys[file]);
        }
    }

    // The name under the cursor of a request, in the analysis that contains its document.
    const Reference* referenceAt(const Json& params, const Analysis*& analysis, int& file) const {
        std::string key = sourceKey(pathFromUri(params["textDocument"]["uri"].asString()));
        analysis = analysisOf(key);
        if (!analysis) return nullptr;
        file = analysis->fileOf(key);
        SourceLocation at = locationAt(analysis->sources[file].text, params["position"], file);
        for (const Reference& reference : analysis->references) {
            if (reference.loc.file == file && reference.loc.line == at.line && reference.loc.column <= at.column &&
                at.column < reference.loc.column + reference.length) {
                return &reference;
            }
        }
        return nullptr;
    }

    Json hover(const Json& params) const {
        const Analysis* analysis = nullptr;
        int file = 0;
        const Reference* reference = referenceAt(params, analysis, file);
        if (!reference) return Json();
        Json contents = Json::object();
        contents.set("kind", "markdown");
        contents.set("value", "```cromo\n" + reference->hover + "\n```");
        Json result = Json::object();
        result.set("contents", std::move(contents));
        result.set("range", range(analysis->sources[file].text, reference->loc, reference->length));
        return result;
    }

    Json definition(const Json& params) const {
        const Analysis* analysis = nullptr;
        int file = 0;
        const Reference* reference = referenceAt(params, analysis, file);
        // D177: the standard library is inside cromo, it has no file to open.
        if (!reference || !reference->hasTarget || reference->target.file == kLibraryFile) return Json();
        int target = reference->target.file;
        const std::string& text = analysis->sources[target].text;
        Json location = Json::object();
        location.set("uri", uriOf(analysis->keys[target], *analysis));
        location.set("range", range(text, reference->target, tokenLength(text, reference->target)));
        return location;
    }

    // D179: the members of the type of the name before the dot. The text with the
    // dot is not complete: it is analyzed with a call to a method that does not
    // exist in place of the member being written (D180).
    Json completion(const Json& params) {
        Json items = Json::array();
        std::string key = sourceKey(pathFromUri(params["textDocument"]["uri"].asString()));
        auto document = documents_.find(key);
        if (document == documents_.end()) return items;
        const std::string& text = document->second.text;
        size_t cursor = offsetAt(text, params["position"]);
        size_t start = cursor;
        while (start > 0 && isIdentChar(text[start - 1])) --start;
        if (start == 0 || text[start - 1] != '.') return items;  // D182: only after a dot, for now
        size_t dot = start - 1;
        size_t begin = dot;
        while (begin > 0 && isIdentChar(text[begin - 1])) --begin;
        if (begin == dot) return items;  // D180: not after a call or an index
        size_t end = cursor;
        while (end < text.size() && isIdentChar(text[end])) ++end;
        std::string name = text.substr(begin, dot - begin);
        std::string repaired = text.substr(0, dot) + ".cromo_complete" + (end < text.size() && text[end] == '(' ? "" : "()") +
                               text.substr(end);

        Document copy = document->second;
        copy.text = repaired;
        bool requireMain = true;
        std::string root = findRoot(copy, requireMain);
        std::pair<std::string, std::string> replaced{key, repaired};
        std::shared_ptr<Analysis> analysis = analyze(root, requireMain, &replaced);
        int file = analysis->fileOf(key);
        if (file < 0) analysis = analyze(document->second.path, false, &replaced);  // a module that main.cro does not import
        file = analysis->fileOf(key);
        if (file < 0) return items;

        SourceLocation at = locationOf(repaired, begin, file);
        for (const Reference& reference : analysis->references) {
            if (reference.loc.file != file || reference.loc.line != at.line || reference.loc.column != at.column) continue;
            Type type = reference.type;
            if (type.isStruct()) members(*type.structDecl, file, items);
            if (type.isArray()) {  // D196
                items.push(item("len", 2, "i64 len()"));
                if (!needsDestruction(type)) items.push(item("copy", 2, type.name() + " copy()"));
            }
            if (type.isInterface()) {
                for (const FunctionDecl& method : type.interfaceDecl->methods) items.push(item(method.name, 2, signatureOf(method)));
            }
            return items;
        }
        // D181: after the name of an enum, its cases.
        for (const StructDecl& decl : analysis->module.structs) {
            if (decl.name != name || !decl.isEnum || (decl.loc.file != file && !decl.isPublic)) continue;
            for (const EnumCase& entry : decl.cases) items.push(item(entry.name, 20, decl.name + "." + entry.name));
            break;
        }
        return items;
    }

    static Json item(const std::string& label, int kind, const std::string& detail) {
        Json out = Json::object();
        out.set("label", label);
        out.set("kind", kind);  // LSP kinds: 2 method, 5 field, 20 enum member
        out.set("detail", detail);
        return out;
    }

    // D181: what the module of `file` can use: the public members, or all of them in
    // the module of the type (D132); a method that implements an interface too.
    static void members(const StructDecl& decl, int file, Json& items) {
        bool own = decl.loc.file == file;
        for (const FieldDecl& field : decl.fields) {
            if (own || field.isPublic) items.push(item(field.name, 5, fieldTypeOf(field, decl) + " " + decl.name + "." + field.name));
        }
        std::set<std::string> fromInterfaces;
        for (const TypeName& name : decl.interfaces) {
            if (!name.resolved.isInterface()) continue;
            for (const FunctionDecl& method : name.resolved.interfaceDecl->methods) fromInterfaces.insert(method.name);
        }
        for (const FunctionDecl& member : decl.members) {
            if (member.kind != FunctionKind::Method) continue;
            if (own || member.isPublic || fromInterfaces.count(member.name)) items.push(item(member.name, 2, signatureOf(member, &decl)));
        }
    }

    static Json symbol(const std::string& text, const std::string& name, int kind, SourceLocation loc,
                       const std::string& detail = {}) {
        Json item = Json::object();
        item.set("name", name);
        if (!detail.empty()) item.set("detail", detail);
        item.set("kind", kind);
        Json place = range(text, loc, tokenLength(text, loc));
        item.set("range", place);
        item.set("selectionRange", std::move(place));
        item.set("children", Json::array());
        return item;
    }

    // D175: from the document alone, so they are there also when the program has errors.
    Json symbols(const Json& params) const {
        Json result = Json::array();
        auto document = documents_.find(sourceKey(pathFromUri(params["textDocument"]["uri"].asString())));
        if (document == documents_.end()) return result;
        const std::string& text = document->second.text;
        SourceFile file{document->second.path, text};
        Diagnostics quiet(file);
        quiet.setSilent(true);
        Module module = parse(tokenize(file, 0, quiet), quiet);
        // LSP kinds: 6 method, 8 field, 9 constructor, 10 enum, 11 interface, 12 function, 22 enum member, 23 struct.
        for (const InterfaceDecl& decl : module.interfaces) {
            Json item = symbol(text, decl.name, 11, decl.loc);
            Json children = Json::array();
            for (const FunctionDecl& method : decl.methods) children.push(symbol(text, method.name, 6, method.loc, signatureOf(method)));
            item.set("children", std::move(children));
            result.push(std::move(item));
        }
        for (const StructDecl& decl : module.structs) {
            Json item = symbol(text, decl.name, decl.isEnum ? 10 : 23, decl.loc);
            Json children = Json::array();
            for (const EnumCase& entry : decl.cases) children.push(symbol(text, entry.name, 22, entry.loc));
            for (const FieldDecl& field : decl.fields) children.push(symbol(text, field.name, 8, field.loc, writtenType(field.type)));
            for (const FunctionDecl& member : decl.members) {
                bool constructor = member.kind == FunctionKind::Constructor;
                std::string name = constructor ? decl.name : member.kind == FunctionKind::Destructor ? "~" + decl.name : member.name;
                children.push(symbol(text, name, constructor ? 9 : 6, member.loc, signatureOf(member)));
            }
            item.set("children", std::move(children));
            result.push(std::move(item));
        }
        for (const FunctionDecl& function : module.functions) {
            result.push(symbol(text, function.name, 12, function.loc, signatureOf(function)));
        }
        return result;
    }

    static std::string writtenType(const TypeName& name) {
        std::string text = (name.module.empty() ? "" : name.module + ".") + name.name;
        if (!name.args.empty()) {
            text += "<";
            for (size_t i = 0; i < name.args.size(); ++i) text += (i ? ", " : "") + writtenType(name.args[i]);
            text += ">";
        }
        return text;
    }

    Json formatting(const Json& params) const {
        Json edits = Json::array();
        auto document = documents_.find(sourceKey(pathFromUri(params["textDocument"]["uri"].asString())));
        if (document == documents_.end()) return edits;
        const std::string& text = document->second.text;
        SourceFile file{document->second.path, text};
        Diagnostics quiet(file);
        quiet.setSilent(true);
        std::string out;
        std::string error;
        if (!formatSource(file, quiet, out, error) || out == text) return edits;
        // The whole text: from the start to after its last character.
        SourceLocation end{1, 1, 0};
        for (size_t i = 0; i < text.size(); i += utf8Length(static_cast<unsigned char>(text[i]))) {
            if (text[i] == '\n') {
                ++end.line;
                end.column = 1;
            } else {
                ++end.column;
            }
        }
        Json whole = Json::object();
        whole.set("start", position(text, SourceLocation{1, 1, 0}));
        whole.set("end", position(text, end));
        Json edit = Json::object();
        edit.set("range", std::move(whole));
        edit.set("newText", out);
        edits.push(std::move(edit));
        return edits;
    }

    Inbox inbox_;
    bool shutdown_ = false;
    std::string workspace_;
    std::map<std::string, Document> documents_;                   // by sourceKey
    std::set<std::string> dirty_;                                 // changed since the last analysis
    std::map<std::string, std::shared_ptr<Analysis>> analyses_;  // by sourceKey of the main file
};

}  // namespace

int runLanguageServer() { return Server().run(); }

}  // namespace cromo
