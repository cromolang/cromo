#include "reference.h"

#include "docs.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <map>
#include <memory>
#include <tuple>

namespace cromo {

namespace {

// A type as the library writes it: `list<T>`, `func(i64, ref T) bool`, `u8[4]`.
std::string written(const TypeName& type) {
    std::string text;
    if (type.isFunction) {
        text = "func(";
        for (size_t i = 0; i < type.args.size(); ++i) {
            int mode = i < type.modes.size() ? type.modes[i] : 0;
            text += (i ? ", " : "") + std::string(mode == int(ParamMode::Ref) ? "ref " : mode == int(ParamMode::Move) ? "move " : "") +
                    written(type.args[i]);
        }
        text += ")";
        if (!type.result.empty()) text += " " + written(type.result[0]);
    } else {
        text = (type.module.empty() ? "" : type.module + ".") + (type.name.empty() ? "void" : type.name);
        if (!type.args.empty()) {
            text += "<";
            for (size_t i = 0; i < type.args.size(); ++i) text += (i ? ", " : "") + written(type.args[i]);
            text += ">";
        }
    }
    for (int64_t size : type.dimensions) text += "[" + std::to_string(size) + "]";
    return text;
}

std::string typeParams(const std::vector<TypeParam>& params) {
    if (params.empty()) return {};
    std::string text = "<";
    for (size_t i = 0; i < params.size(); ++i) {
        text += (i ? ", " : "") + params[i].name;
        for (size_t k = 0; k < params[i].constraints.size(); ++k) {
            text += (k ? " + " : " : ") + written(params[i].constraints[k]);
        }
    }
    return text + ">";
}

std::string parameters(const FunctionDecl& function) {
    std::string text = "(";
    for (size_t i = 0; i < function.params.size(); ++i) {
        const Param& param = function.params[i];
        std::string mode = param.mode == ParamMode::Ref    ? "ref "
                           : param.mode == ParamMode::Move ? (param.isConst ? "move const " : "move ")
                                                           : (param.isConst ? "const " : "");
        text += (i ? ", " : "") + mode + written(param.type) + " " + param.name;
    }
    return text + ")";
}

// A function, a method or a constructor as it is declared, without `public`.
std::string signature(const FunctionDecl& function, const std::string& owner) {
    if (function.kind == FunctionKind::Constructor) return owner + parameters(function);
    std::string text = written(function.returnType) + " " + function.name + typeParams(function.typeParams) + parameters(function);
    if (function.isConst) text += " const";
    return text;
}

std::string json(const std::string& text) {
    std::string out = "\"";
    for (unsigned char c : text) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) {
                char buffer[8];
                std::snprintf(buffer, sizeof buffer, "\\u%04x", c);
                out += buffer;
            } else {
                out += static_cast<char>(c);
            }
        }
    }
    return out + "\"";
}

std::vector<std::string> lines(const std::string& text) {
    std::vector<std::string> out;
    size_t start = 0;
    for (size_t at = text.find('\n'); at != std::string::npos; at = text.find('\n', start)) {
        out.push_back(text.substr(start, at - start));
        start = at + 1;
    }
    out.push_back(text.substr(start));
    return out;
}

// A declaration of the reference, with its members.
struct Item {
    std::string kind;  // function, struct, enum, interface, field, case, constructor, method
    std::string name;
    std::string signature;
    std::string doc;
    std::vector<Item> members;
    std::tuple<bool, int, int> order;  // in a file of a system, its file, its line
};

std::string itemJson(const Item& item) {
    std::string out = "{\"kind\":" + json(item.kind) + ",\"name\":" + json(item.name) + ",\"signature\":" + json(item.signature) +
                      ",\"doc\":" + json(item.doc);
    if (!item.members.empty()) {
        out += ",\"members\":[";
        for (size_t i = 0; i < item.members.size(); ++i) out += (i ? "," : "") + itemJson(item.members[i]);
        out += "]";
    }
    return out + "}";
}

class Reference {
public:
    Reference(const Module& module, const std::deque<SourceFile>& sources, const std::vector<std::string>& modules)
        : module_(module), sources_(sources), modules_(modules) {}

    std::string run() {
        // The modules of the library, by the first of their files (D202).
        std::map<std::string, int> firsts;
        for (size_t i = 1; i < module_.files.size(); ++i) {  // 0 is the program that imports them
            const ModuleFile& file = module_.files[i];
            bool shown = file.name == "std" || std::find(modules_.begin(), modules_.end(), file.name) != modules_.end();
            if (file.isLibrary && file.part < 0 && shown) firsts.emplace(file.name, int(i));
        }
        std::map<std::string, std::vector<Item>> items;
        // The functions and the types that the compiler knows, in the core (D475).
        for (const BuiltinDoc& builtin : builtinDocs()) {
            Item item;
            item.kind = *builtin.signature ? "builtin" : "type";
            item.name = builtin.name;
            item.signature = *builtin.signature ? builtin.signature : builtin.name;
            item.doc = markdownOf(lines(builtin.text));
            item.order = {false, -1, 0};
            items["std"].push_back(std::move(item));
        }
        for (const FunctionDecl& function : module_.functions) {
            if (!function.isPublic || function.isExtern || function.isTest || function.isGenerated || !library(function.loc.file)) continue;
            Item item = make("function", function.name, signature(function, ""), function.loc, false);
            items[moduleOf(function.loc.file)].push_back(std::move(item));
        }
        for (const StructDecl& decl : module_.structs) {
            if (!decl.isPublic || !library(decl.loc.file)) continue;
            std::string head = (decl.isEnum ? "enum " : "struct ") + decl.name + typeParams(decl.typeParams);
            for (size_t i = 0; i < decl.interfaces.size(); ++i) head += (i ? ", " : " : ") + written(decl.interfaces[i]);
            Item item = make(decl.isEnum ? "enum" : "struct", decl.name, head, decl.loc, false);
            for (const EnumCase& c : decl.cases) {
                std::string text = c.name;
                if (!c.fields.empty()) {
                    text += "(";
                    for (size_t i = 0; i < c.fields.size(); ++i) text += (i ? ", " : "") + written(c.fields[i].type) + " " + c.fields[i].name;
                    text += ")";
                }
                item.members.push_back(make("case", c.name, text, c.loc, true));
            }
            for (const FieldDecl& field : decl.fields) {
                if (field.isPublic) item.members.push_back(make("field", field.name, written(field.type) + " " + field.name, field.loc, true));
            }
            for (const FunctionDecl& member : decl.members) {
                if (!member.isPublic || member.isGenerated || member.kind == FunctionKind::Destructor) continue;
                const char* kind = member.kind == FunctionKind::Constructor ? "constructor" : "method";
                item.members.push_back(make(kind, member.name, signature(member, decl.name), member.loc, false));
            }
            items[moduleOf(decl.loc.file)].push_back(std::move(item));
        }
        for (const InterfaceDecl& decl : module_.interfaces) {
            if (!decl.isPublic || !library(decl.loc.file)) continue;
            Item item = make("interface", decl.name, "interface " + decl.name, decl.loc, false);
            for (const FunctionDecl& method : decl.methods) {
                item.members.push_back(make("method", method.name, signature(method, decl.name), method.loc, false));
            }
            items[moduleOf(decl.loc.file)].push_back(std::move(item));
        }

        std::string out = "{\"modules\":[";
        bool firstModule = true;
        for (const auto& [name, file] : firsts) {
            std::vector<Item>& list = items[name];
            std::stable_sort(list.begin(), list.end(), [](const Item& a, const Item& b) { return a.order < b.order; });
            out += std::string(firstModule ? "" : ",") + "{\"name\":" + json(name) + ",\"doc\":" + json(markdownOf(docs(file).module())) +
                   ",\"items\":[";
            for (size_t i = 0; i < list.size(); ++i) out += (i ? "," : "") + itemJson(list[i]);
            out += "]}";
            firstModule = false;
        }
        return out + "]}\n";
    }

private:
    bool library(int file) const {
        return file >= 0 && size_t(file) < module_.files.size() && module_.files[file].isLibrary && !moduleOf(file).empty();
    }

    const std::string& moduleOf(int file) const {
        const ModuleFile& own = module_.files[file];
        return own.part >= 0 ? module_.files[own.part].name : own.name;
    }

    const DocComments& docs(int file) {
        auto found = docs_.find(file);
        if (found == docs_.end()) found = docs_.emplace(file, std::make_unique<DocComments>(sources_[file])).first;
        return *found->second;
    }

    Item make(const char* kind, const std::string& name, const std::string& signature, SourceLocation loc, bool trailing) {
        Item item;
        item.kind = kind;
        item.name = name;
        item.signature = signature;
        item.doc = markdownOf(docs(loc.file).of(loc.line, trailing));
        item.order = {module_.files[loc.file].part >= 0, loc.file, loc.line};
        return item;
    }

    const Module& module_;
    const std::deque<SourceFile>& sources_;
    const std::vector<std::string>& modules_;
    std::map<int, std::unique_ptr<DocComments>> docs_;
};

}  // namespace

bool writeReference(const Module& module, const std::deque<SourceFile>& sources, const std::vector<std::string>& modules,
                    const std::string& path, std::string& error) {
    std::string text = Reference(module, sources, modules).run();
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        error = "cannot write " + path;
        return false;
    }
    out << text;
    return true;
}

}  // namespace cromo
