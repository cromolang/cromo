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
    return type.isStruct() && type.structDecl->needsDestruction;
}

int sizeOf(Type type) {
    switch (type.kind) {
    case TypeKind::Bool: return 1;
    case TypeKind::Int:
    case TypeKind::Float: return type.bits / 8;
    case TypeKind::Struct: return type.structDecl->size;
    default: return 0;
    }
}

int alignOf(Type type) {
    if (type.isStruct()) return type.structDecl->align;
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
    case TypeKind::Struct: return "%struct." + type.structDecl->name;
    default: return "<invalid>";
    }
}

}  // namespace cromo
