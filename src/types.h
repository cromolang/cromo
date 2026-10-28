#pragma once

#include <optional>
#include <string>

namespace cromo {

enum class TypeKind {
    Error,         // a type error has already been reported
    Void,
    Bool,
    Int,
    Float,
    String,        // only string literals for now (D65)
    UntypedInt,    // integer literal still waiting for its type (D55)
    UntypedFloat,  // floating-point literal still waiting for its type (D55)
};

struct Type {
    TypeKind kind = TypeKind::Error;
    int bits = 0;          // Int and Float
    bool isSigned = false; // Int

    static Type error() { return {}; }
    static Type voidType() { return {TypeKind::Void, 0, false}; }
    static Type boolType() { return {TypeKind::Bool, 1, false}; }
    static Type intType(int bits, bool isSigned) { return {TypeKind::Int, bits, isSigned}; }
    static Type floatType(int bits) { return {TypeKind::Float, bits, false}; }
    static Type stringType() { return {TypeKind::String, 0, false}; }
    static Type untypedInt() { return {TypeKind::UntypedInt, 0, false}; }
    static Type untypedFloat() { return {TypeKind::UntypedFloat, 0, false}; }

    bool isError() const { return kind == TypeKind::Error; }
    bool isVoid() const { return kind == TypeKind::Void; }
    bool isBool() const { return kind == TypeKind::Bool; }
    bool isInt() const { return kind == TypeKind::Int; }
    bool isFloat() const { return kind == TypeKind::Float; }
    bool isNumeric() const { return isInt() || isFloat(); }
    bool isUntyped() const { return kind == TypeKind::UntypedInt || kind == TypeKind::UntypedFloat; }

    bool operator==(const Type&) const = default;

    std::string name() const;  // as written in Cromo: "i32", "f64", "bool", ...
};

// Built-in types that can be named in the source (D9, D46). `char` is u8.
std::optional<Type> findBuiltinType(const std::string& name);

// True if a value of type `from` fits in `to` without any loss (D33).
bool convertsLosslessly(Type from, Type to);

// LLVM IR spelling of the type.
std::string llvmType(Type type);

}  // namespace cromo
