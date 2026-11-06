#include "lsp.h"

#include "docs.h"
#include "format.h"
#include "index.h"
#include "json.h"
#include "lexer.h"
#include "loader.h"
#include "ownership.h"
#include "parser.h"
#include "installation.h"
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
    // The documentation comments of each file, read when the editor asks (D473).
    mutable std::map<int, std::unique_ptr<DocComments>> docComments;

    int fileOf(const std::string& key) const {
        for (size_t i = 0; i < keys.size(); ++i) {
            if (keys[i] == key) return static_cast<int>(i);
        }
        return -1;
    }

    const DocComments* docsOf(int file) const {
        if (file < 0 || static_cast<size_t>(file) >= sources.size()) return nullptr;
        std::unique_ptr<DocComments>& docs = docComments[file];
        if (!docs) docs = std::make_unique<DocComments>(sources[file]);
        return docs.get();
    }

    // The documentation of the declaration at `loc`, as Markdown (D474).
    std::string docOf(SourceLocation loc, bool trailing) const {
        const DocComments* docs = docsOf(loc.file);
        return docs ? markdownOf(docs->of(loc.line, trailing)) : std::string();
    }

    // The path of the module of a file, as `std.fs`.
    std::string moduleOf(int file) const {
        if (file < 0 || static_cast<size_t>(file) >= module.files.size()) return {};
        return module.files[file].name;
    }

    // The files of an imported module, with those for the system (D202).
    std::set<int> filesOf(const ImportDecl& import) const {
        std::set<int> files(import.files.begin(), import.files.end());
        for (size_t i = 0; i < module.files.size(); ++i) {
            if (files.count(module.files[i].part)) files.insert(static_cast<int>(i));
        }
        return files;
    }

    // P51: the documentation of the first of its files that has one.
    std::string moduleDoc(const ImportDecl& import) const {
        for (int file : import.files) {
            const DocComments* docs = docsOf(file);
            std::vector<std::string> lines = docs ? docs->module() : std::vector<std::string>();
            if (!lines.empty()) return markdownOf(lines);
        }
        return {};
    }
};

std::vector<std::string> linesOf(const std::string& text) {
    std::vector<std::string> lines;
    size_t start = 0;
    for (;;) {
        size_t end = text.find('\n', start);
        lines.push_back(text.substr(start, end == std::string::npos ? std::string::npos : end - start));
        if (end == std::string::npos) return lines;
        start = end + 1;
    }
}

// The Markdown of a hover or of a signature: Cromo code, then the documentation
// under a line (D475).
std::string withDoc(const std::string& code, const std::string& doc) {
    return code + (doc.empty() ? "" : "\n\n---\n\n" + doc);
}

std::string cromoBlock(const std::string& code) { return "```cromo\n" + code + "\n```"; }

Json markdown(const std::string& value) {
    Json out = Json::object();
    out.set("kind", "markdown");
    out.set("value", value);
    return out;
}

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
            } else if (method == "textDocument/signatureHelp") {
                send(response(id, signatureHelp(params)));
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
        Json signature = Json::object();
        Json opening = Json::array();
        opening.push("(");
        opening.push(",");
        signature.set("triggerCharacters", std::move(opening));
        Json again = Json::array();
        again.push(")");
        signature.set("retriggerCharacters", std::move(again));
        capabilities.set("signatureHelpProvider", std::move(signature));  // D477
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
        analysis->sources.push_back({libraryFile("core.cro").string(), {}});
        readSourceFile(analysis->sources[1].path, analysis->sources[1].text);
        Diagnostics diag(analysis->sources[0]);
        diag.addFile(analysis->sources[1]);
        diag.collectInto(&analysis->messages);
        analysis->module = loadProgram(analysis->sources, diag, hostTarget(), &overlay);
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
        if (!reference) return hoverOfName(params);
        std::string code = cromoBlock(reference->hover);
        // D475: the module above the signature, when it is another one; the core has none.
        if (!reference->docs.empty() && reference->target.file != kLibraryFile) {
            std::string module = analysis->moduleOf(reference->target.file);
            if (!module.empty() && module != analysis->moduleOf(file)) code = cromoBlock(module) + "\n" + code;
        }
        std::string doc;
        for (const Reference::Doc& place : reference->docs) {
            std::string text = analysis->docOf(place.loc, place.trailing);
            if (!text.empty()) doc += (doc.empty() ? "" : "\n\n") + text;
        }
        Json result = Json::object();
        result.set("contents", markdown(withDoc(code, doc)));
        result.set("range", range(analysis->sources[file].text, reference->loc, reference->length));
        return result;
    }

    // D475: the names that the analysis does not index: the built-in functions and
    // types, and the name of an imported module, in its import or before a dot.
    Json hoverOfName(const Json& params) const {
        std::string key = sourceKey(pathFromUri(params["textDocument"]["uri"].asString()));
        auto document = documents_.find(key);
        if (document == documents_.end()) return Json();
        const std::string& text = document->second.text;
        size_t offset = offsetAt(text, params["position"]);
        SourceFile source{document->second.path, text};
        Diagnostics quiet(source);
        quiet.setSilent(true);
        std::vector<Token> tokens = tokenize(source, 0, quiet, true);
        size_t at = 0;
        while (at < tokens.size() && !(tokens[at].begin <= offset && offset < tokens[at].end)) ++at;
        if (at == tokens.size() || tokens[at].kind != TokenKind::Identifier) return Json();
        const Token& token = tokens[at];
        std::string value;
        if (const BuiltinDoc* builtin = builtinDoc(token.text)) {
            value = withDoc(cromoBlock(*builtin->signature ? builtin->signature : builtin->name),
                            markdownOf(linesOf(builtin->text)));
        }
        const Analysis* analysis = analysisOf(key);
        int file = analysis ? analysis->fileOf(key) : -1;
        if (value.empty() && file >= 0 && static_cast<size_t>(file) < analysis->module.files.size()) {
            bool beforeDot = at + 1 < tokens.size() && tokens[at + 1].kind == TokenKind::Dot;
            for (const ImportDecl& import : analysis->module.files[file].imports) {
                bool inImport = import.loc.line == token.loc.line;
                if (!inImport && !(beforeDot && import.qualifier() == token.text)) continue;
                std::string path;
                for (const std::string& part : import.path) path += (path.empty() ? "" : ".") + part;
                value = withDoc(cromoBlock("import " + path), analysis->moduleDoc(import));
                break;
            }
        }
        if (value.empty()) return Json();
        Json result = Json::object();
        result.set("contents", markdown(value));
        result.set("range", range(text, token.loc, static_cast<int>(token.text.size())));
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

        int file = -1;
        std::shared_ptr<Analysis> analysis = analyzeText(document->second, key, repaired, file);
        if (file < 0) return items;

        SourceLocation at = locationOf(repaired, begin, file);
        for (const Reference& reference : analysis->references) {
            if (reference.loc.file != file || reference.loc.line != at.line || reference.loc.column != at.column) continue;
            Type type = reference.type;
            if (type.isStruct()) members(*analysis, *type.structDecl, file, items);
            if (type.isArray()) {  // D196
                items.push(item("len", 2, "i64 len()"));
                if (!needsDestruction(type)) items.push(item("copy", 2, type.name() + " copy()"));
            }
            if (type.isInterface()) {
                for (const FunctionDecl& method : type.interfaceDecl->methods) {
                    items.push(item(method.name, 2, signatureOf(method), analysis->docOf(method.loc, false)));
                }
            }
            return items;
        }
        // D181: after the name of an enum, its cases.
        for (const StructDecl& decl : analysis->module.structs) {
            if (decl.name != name || !decl.isEnum || (decl.loc.file != file && !decl.isPublic)) continue;
            for (const EnumCase& entry : decl.cases) {
                items.push(item(entry.name, 20, decl.name + "." + entry.name, analysis->docOf(entry.loc, true)));
            }
            return items;
        }
        moduleMembers(*analysis, file, name, items);
        return items;
    }

    // The analysis of a text of a document that is not the one in the editor, as
    // the code being written with a call in place of what is missing (D180).
    std::shared_ptr<Analysis> analyzeText(const Document& document, const std::string& key, const std::string& text,
                                          int& file) {
        Document copy = document;
        copy.text = text;
        bool requireMain = true;
        std::string root = findRoot(copy, requireMain);
        std::pair<std::string, std::string> replaced{key, text};
        std::shared_ptr<Analysis> analysis = analyze(root, requireMain, &replaced);
        file = analysis->fileOf(key);
        if (file < 0) {  // a module that main.cro does not import
            analysis = analyze(document.path, false, &replaced);
            file = analysis->fileOf(key);
        }
        return analysis;
    }

    // LSP kinds: 2 method, 3 function, 5 field, 8 interface, 13 enum, 20 enum member,
    // 22 struct. The documentation is shown next to the signature (D475).
    static Json item(const std::string& label, int kind, const std::string& detail, const std::string& doc = {}) {
        Json out = Json::object();
        out.set("label", label);
        out.set("kind", kind);
        out.set("detail", detail);
        if (!doc.empty()) out.set("documentation", markdown(doc));
        return out;
    }

    // D181: what the module of `file` can use: the public members, or all of them in
    // the module of the type (D132); a method that implements an interface too.
    static void members(const Analysis& analysis, const StructDecl& decl, int file, Json& items) {
        bool own = decl.loc.file == file;
        for (const FieldDecl& field : decl.fields) {
            if (own || field.isPublic) {
                items.push(item(field.name, 5, fieldTypeOf(field, decl) + " " + decl.name + "." + field.name,
                                analysis.docOf(field.loc, true)));
            }
        }
        std::set<std::string> fromInterfaces;
        for (const TypeName& name : decl.interfaces) {
            if (!name.resolved.isInterface()) continue;
            for (const FunctionDecl& method : name.resolved.interfaceDecl->methods) fromInterfaces.insert(method.name);
        }
        for (const FunctionDecl& member : decl.members) {
            if (member.kind != FunctionKind::Method) continue;
            if (own || member.isPublic || fromInterfaces.count(member.name)) {
                items.push(item(member.name, 2, signatureOf(member, &decl), analysis.docOf(member.loc, false)));
            }
        }
    }

    static std::string typeParamsOf(const std::vector<TypeParam>& params) {
        if (params.empty()) return {};
        std::string text = "<";
        for (size_t i = 0; i < params.size(); ++i) text += (i ? ", " : "") + params[i].name;
        return text + ">";
    }

    // D476: after the name of an imported module, its public names, without the
    // instances of the generics, the tests and the C functions (P51).
    static void moduleMembers(const Analysis& analysis, int file, const std::string& name, Json& items) {
        if (file < 0 || static_cast<size_t>(file) >= analysis.module.files.size()) return;
        std::set<int> files;
        for (const ImportDecl& import : analysis.module.files[file].imports) {
            if (import.qualifier() != name) continue;
            for (int part : analysis.filesOf(import)) files.insert(part);
        }
        if (files.empty()) return;
        for (const FunctionDecl& function : analysis.module.functions) {
            if (!files.count(function.loc.file) || !function.isPublic || function.isExtern || function.isTest) continue;
            items.push(item(function.name, 3, signatureOf(function), analysis.docOf(function.loc, false)));
        }
        for (const StructDecl& decl : analysis.module.structs) {
            if (!files.count(decl.loc.file) || !decl.isPublic) continue;
            items.push(item(decl.name, decl.isEnum ? 13 : 22, (decl.isEnum ? "enum " : "struct ") + decl.name + typeParamsOf(decl.typeParams),
                            analysis.docOf(decl.loc, false)));
        }
        for (const InterfaceDecl& decl : analysis.module.interfaces) {
            if (!files.count(decl.loc.file) || !decl.isPublic) continue;
            items.push(item(decl.name, 8, "interface " + decl.name, analysis.docOf(decl.loc, false)));
        }
    }

    // ---- The help on the signature (D477) ----

    Json signatureHelp(const Json& params) {
        std::string key = sourceKey(pathFromUri(params["textDocument"]["uri"].asString()));
        auto document = documents_.find(key);
        if (document == documents_.end()) return Json();
        const std::string& text = document->second.text;
        size_t cursor = offsetAt(text, params["position"]);
        SourceFile source{document->second.path, text};
        Diagnostics quiet(source);
        quiet.setSilent(true);
        std::vector<Token> tokens = tokenize(source, 0, quiet);

        // The parenthesis of the call around the cursor, and the commas before it.
        int last = -1;
        for (size_t i = 0; i < tokens.size() && tokens[i].kind != TokenKind::EndOfFile && tokens[i].begin < cursor; ++i) {
            last = static_cast<int>(i);
        }
        int depth = 0;
        int commas = 0;
        int open = -1;
        for (int i = last; i >= 0; --i) {
            TokenKind kind = tokens[i].kind;
            if (kind == TokenKind::RParen || kind == TokenKind::RBracket || kind == TokenKind::RBrace) {
                ++depth;
            } else if (kind == TokenKind::LParen || kind == TokenKind::LBracket || kind == TokenKind::LBrace) {
                if (depth == 0) {
                    if (kind == TokenKind::LParen) open = i;
                    break;
                }
                --depth;
            } else if (depth == 0 && kind == TokenKind::Comma) {
                ++commas;
            } else if (depth == 0 && kind == TokenKind::Newline) {
                break;
            }
        }
        if (open < 1 || tokens[open - 1].kind != TokenKind::Identifier) return Json();
        std::string name = tokens[open - 1].text;
        std::string receiver;
        size_t receiverBegin = 0;
        if (open >= 3 && tokens[open - 2].kind == TokenKind::Dot) {
            if (tokens[open - 3].kind != TokenKind::Identifier) return Json();  // after a call or an index (D180)
            receiver = tokens[open - 3].text;
            receiverBegin = tokens[open - 3].begin;
        }

        // P51: the text with the call without its arguments, which can be analyzed.
        size_t close = std::string::npos;
        int level = 0;
        for (size_t i = open + 1; i < tokens.size() && tokens[i].kind != TokenKind::EndOfFile; ++i) {
            TokenKind kind = tokens[i].kind;
            if (kind == TokenKind::LParen || kind == TokenKind::LBracket || kind == TokenKind::LBrace) {
                ++level;
            } else if (kind == TokenKind::RParen || kind == TokenKind::RBracket || kind == TokenKind::RBrace) {
                if (level == 0) {
                    if (kind == TokenKind::RParen) close = tokens[i].end;
                    break;
                }
                --level;
            } else if (level == 0 && kind == TokenKind::Newline) {
                break;
            }
        }
        if (close == std::string::npos || close < cursor) {
            close = text.find('\n', cursor);
            if (close == std::string::npos) close = text.size();
        }
        std::string repaired = text.substr(0, tokens[open].end) + ")" + text.substr(close);
        int file = -1;
        std::shared_ptr<Analysis> analysis = analyzeText(document->second, key, repaired, file);
        if (file < 0) return Json();

        std::string label;
        std::string doc;
        if (!calleeOf(*analysis, file, receiver, name, locationOf(repaired, receiverBegin, file), label, doc)) return Json();
        Json signature = signatureInformation(label, name, doc);
        int count = static_cast<int>(signature["parameters"].items().size());
        int active = count == 0 ? 0 : std::min(commas, count - 1);
        signature.set("activeParameter", active);
        Json signatures = Json::array();
        signatures.push(std::move(signature));
        Json result = Json::object();
        result.set("signatures", std::move(signatures));
        result.set("activeSignature", 0);
        result.set("activeParameter", active);
        return result;
    }

    // The signature and the documentation of what `receiver.name(` or `name(`
    // calls, found as the completion finds the members (D477).
    static bool calleeOf(const Analysis& analysis, int file, const std::string& receiver, const std::string& name,
                         SourceLocation receiverAt, std::string& label, std::string& doc) {
        const Module& module = analysis.module;
        std::string own = analysis.moduleOf(file);
        auto constructorOf = [&](const StructDecl& decl) {
            for (const FunctionDecl& member : decl.members) {
                if (member.kind != FunctionKind::Constructor) continue;
                label = signatureOf(member);
                doc = analysis.docOf(decl.loc, false);
                std::string more = analysis.docOf(member.loc, false);
                if (!more.empty()) doc += (doc.empty() ? "" : "\n\n") + more;
                return true;
            }
            return false;
        };
        auto function = [&](const FunctionDecl& found) {
            label = signatureOf(found);
            doc = analysis.docOf(found.loc, false);
            return true;
        };
        if (receiver.empty()) {
            if (const BuiltinDoc* builtin = builtinDoc(name)) {
                if (!*builtin->signature) return false;
                label = builtin->signature;
                doc = markdownOf(linesOf(builtin->text));
                return true;
            }
            // The names of the file and of its module come first (D126), then the core,
            // then the public names of the other modules.
            auto rank = [&](int at, bool isPublic) {
                if (at == file) return 0;
                if (analysis.moduleOf(at) == own) return 1;
                if (!isPublic) return 9;
                return at == kLibraryFile ? 2 : 3;
            };
            const FunctionDecl* best = nullptr;
            int bestRank = 9;
            for (const FunctionDecl& candidate : module.functions) {
                if (candidate.name != name || candidate.isTest) continue;
                int r = rank(candidate.loc.file, candidate.isPublic);
                if (r < bestRank) {
                    best = &candidate;
                    bestRank = r;
                }
            }
            if (best) return function(*best);
            const StructDecl* type = nullptr;
            bestRank = 9;
            for (const StructDecl& candidate : module.structs) {
                if (candidate.name != name) continue;
                int r = rank(candidate.loc.file, candidate.isPublic);
                if (r < bestRank) {
                    type = &candidate;
                    bestRank = r;
                }
            }
            return type && constructorOf(*type);
        }
        // `geometry.area(`: a function or a constructor of an imported module (D126).
        if (file >= 0 && static_cast<size_t>(file) < module.files.size()) {
            std::set<int> files;
            for (const ImportDecl& import : module.files[file].imports) {
                if (import.qualifier() != receiver) continue;
                for (int part : analysis.filesOf(import)) files.insert(part);
            }
            if (!files.empty()) {
                for (const FunctionDecl& candidate : module.functions) {
                    if (candidate.name == name && candidate.isPublic && files.count(candidate.loc.file)) return function(candidate);
                }
                for (const StructDecl& candidate : module.structs) {
                    if (candidate.name == name && candidate.isPublic && files.count(candidate.loc.file)) return constructorOf(candidate);
                }
                return false;
            }
        }
        // `Shape.Circle(`: a case with data.
        for (const StructDecl& decl : module.structs) {
            if (decl.name != receiver || !decl.isEnum || (decl.loc.file != file && !decl.isPublic)) continue;
            for (const EnumCase& entry : decl.cases) {
                if (entry.name != name || entry.fields.empty()) continue;
                label = decl.name + "." + entry.name + "(";
                for (size_t i = 0; i < entry.fields.size(); ++i) {
                    const FieldDecl& field = entry.fields[i];
                    label += (i ? ", " : "") + (field.resolved.isError() ? field.type.name : field.resolved.name()) + " " + field.name;
                }
                label += ")";
                doc = analysis.docOf(entry.loc, true);
                return true;
            }
            return false;
        }
        // `c.area(`: a method of the type of the name before the dot (D180).
        for (const Reference& reference : analysis.references) {
            if (reference.loc.file != file || reference.loc.line != receiverAt.line || reference.loc.column != receiverAt.column) continue;
            Type type = reference.type;
            if (type.isStruct()) {
                for (const FunctionDecl& member : type.structDecl->members) {
                    if (member.kind != FunctionKind::Method || member.name != name) continue;
                    label = signatureOf(member, type.structDecl);
                    doc = analysis.docOf(member.loc, false);
                    return true;
                }
            }
            if (type.isInterface()) {
                for (const FunctionDecl& method : type.interfaceDecl->methods) {
                    if (method.name == name) return function(method);
                }
            }
            return false;
        }
        return false;
    }

    // The signature with the places of its parameters in the label.
    static Json signatureInformation(const std::string& label, const std::string& name, const std::string& doc) {
        Json parameters = Json::array();
        size_t open = std::string::npos;
        for (size_t at = label.find(name + "("); at != std::string::npos; at = label.find(name + "(", at + 1)) {
            if (at == 0 || !isIdentChar(label[at - 1])) {
                open = at + name.size();
                break;
            }
        }
        if (open != std::string::npos) {
            int depth = 0;
            size_t start = open + 1;
            for (size_t i = open + 1; i < label.size(); ++i) {
                char c = label[i];
                if (c == '(' || c == '<' || c == '[') {
                    ++depth;
                } else if ((c == ')' || c == '>' || c == ']') && depth > 0) {
                    --depth;
                } else if ((c == ',' || c == ')') && depth == 0) {
                    while (start < i && label[start] == ' ') ++start;
                    if (i > start) {
                        Json place = Json::array();
                        place.push(static_cast<int>(start));
                        place.push(static_cast<int>(i));
                        Json parameter = Json::object();
                        parameter.set("label", std::move(place));
                        parameters.push(std::move(parameter));
                    }
                    start = i + 1;
                    if (c == ')') break;
                }
            }
        }
        Json signature = Json::object();
        signature.set("label", label);
        if (!doc.empty()) signature.set("documentation", markdown(doc));
        signature.set("parameters", std::move(parameters));
        return signature;
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
