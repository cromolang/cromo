#include "types.h"

#include "ast.h"

namespace cromo {

std::string Type::name() const {
    switch (kind) {
    case TypeKind::Error: return "<error>";
    case TypeKind::Void: return "void";
    case TypeKind::Bool: return "bool";
    case TypeKind::Int: return (isSigned ? "i" : "u") + std::to_string(bits);
    case TypeKind::Float: return "f" + std::to_string(bits);
    case TypeKind::String: return "string";
    case TypeKind::Struct: return structDecl->name;
    case TypeKind::Param: return param->name;
    case TypeKind::Interface: return interfaceDecl->name;
    case TypeKind::SelfType: return "Self";
    case TypeKind::Addr: return "addr";
    case TypeKind::UntypedInt: return "integer literal";
    case TypeKind::UntypedFloat: return "decimal literal";
    }
    return "<unknown>";
}

std::optional<Type> findBuiltinType(const std::string& name) {
    if (name == "bool") return Type::boolType();
    if (name == "f32") return Type::floatType(32);
    if (name == "f64") return Type::floatType(64);
    if (name == "char") return Type::intType(8, false);  // D46
    if (name.size() >= 2 && (name[0] == 'i' || name[0] == 'u')) {
        std::string digits = name.substr(1);
        if (digits == "8" || digits == "16" || digits == "32" || digits == "64") {
            return Type::intType(std::stoi(digits), name[0] == 'i');
        }
    }
    return std::nullopt;
}

bool convertsLosslessly(Type from, Type to) {
    if (from == to) return true;
    if (from.isInt() && to.isInt()) {
        if (from.isSigned == to.isSigned) return to.bits >= from.bits;
        return !from.isSigned && to.isSigned && to.bits > from.bits;  // u8 -> i16, u32 -> i64
    }
    if (from.isInt() && to.isFloat()) {
        int mantissa = to.bits == 64 ? 53 : 24;
        return from.bits < mantissa;  // every value is exactly representable
    }
    if (from.isFloat() && to.isFloat()) return to.bits >= from.bits;
    return false;
}

bool needsDestruction(Type type) {
    return type.isParam() || (type.isStruct() && type.structDecl->needsDestruction);
}

bool isAbstract(Type type) {
    return type.isParam() || type.kind == TypeKind::SelfType || (type.isStruct() && type.structDecl->isAbstract);
}

int sizeOf(Type type) {
    switch (type.kind) {
    case TypeKind::Bool: return 1;
    case TypeKind::Int:
    case TypeKind::Float: return type.bits / 8;
    case TypeKind::Struct: return type.structDecl->size;
    case TypeKind::Interface: return 16;
    case TypeKind::Addr: return 8;
    default: return 0;
    }
}

int alignOf(Type type) {
    if (type.isStruct()) return type.structDecl->align;
    if (type.isInterface()) return 8;
    int size = sizeOf(type);
    return size > 0 ? size : 1;
}

std::string llvmType(Type type) {
    switch (type.kind) {
    case TypeKind::Void: return "void";
    case TypeKind::Bool: return "i1";
    case TypeKind::Int: return "i" + std::to_string(type.bits);
    case TypeKind::Float: return type.bits == 32 ? "float" : "double";
    case TypeKind::String: return "ptr";
    case TypeKind::Struct: return llvmName('%', "struct." + type.structDecl->symbol);
    case TypeKind::Interface: return "{ ptr, ptr }";
    case TypeKind::Addr: return "ptr";
    default: return "<invalid>";
    }
}

std::string llvmName(char sigil, const std::string& name) {
    // A name can start with a digit only when it is all digits, as `%0`: the
    // module `04_lists` must be quoted.
    bool plain = !name.empty() && !(name[0] >= '0' && name[0] <= '9');
    for (char c : name) {
        bool allowed = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '.' ||
                       c == '$' || c == '-';
        plain = plain && allowed;
    }
    if (plain) return sigil + name;
    std::string out = std::string(1, sigil) + "\"";
    for (unsigned char c : name) {
        if (c == '"' || c == '\\' || c < 0x20 || c >= 0x7F) {
            const char* hex = "0123456789ABCDEF";
            out += '\\';
            out += hex[c >> 4];
            out += hex[c & 0xF];
        } else {
            out += static_cast<char>(c);
        }
    }
    return out + "\"";
}

}  // namespace cromo
