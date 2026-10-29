#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace cromo {

struct StructDecl;
struct InterfaceDecl;
struct TypeParam;
struct FunctionSig;
struct ArrayInfo;

enum class TypeKind {
    Error,         // a type error has already been reported
    Void,
    Bool,
    Int,
    Float,
    String,        // only string literals for now (D65)
    Struct,
    Param,         // a type parameter inside a generic: `T` (D39)
    Interface,     // only for parameters: a reference with its method table (D49)
    SelfType,      // `Self` in the methods of an interface (D84)
    Addr,          // an address without a type: only in the standard library (D37, D98)
    UntypedInt,    // integer literal still waiting for its type (D55)
    UntypedFloat,  // floating-point literal still waiting for its type (D55)
    Function,      // `func(i32, i32) bool`: a pair of captured data and code (D185, D186)
    Array,         // `i32[4]`: a fixed number of elements, kept where the array is (D196)
};

struct Type {
    TypeKind kind = TypeKind::Error;
    int bits = 0;          // Int and Float
    bool isSigned = false; // Int
    const StructDecl* structDecl = nullptr;  // Struct
    const TypeParam* param = nullptr;        // Param
    const InterfaceDecl* interfaceDecl = nullptr;  // Interface
    const FunctionSig* sig = nullptr;              // Function, one for each signature
    const ArrayInfo* array = nullptr;              // Array, one for each element type and count

    static Type error() { return {}; }
    static Type voidType() { return {TypeKind::Void, 0, false}; }
    static Type boolType() { return {TypeKind::Bool, 1, false}; }
    static Type intType(int bits, bool isSigned) { return {TypeKind::Int, bits, isSigned}; }
    static Type floatType(int bits) { return {TypeKind::Float, bits, false}; }
    static Type stringType() { return {TypeKind::String, 0, false}; }
    static Type structType(const StructDecl* decl) { return {TypeKind::Struct, 0, false, decl}; }
    static Type paramType(const TypeParam* param) { return {TypeKind::Param, 0, false, nullptr, param}; }
    static Type interfaceType(const InterfaceDecl* decl) { return {TypeKind::Interface, 0, false, nullptr, nullptr, decl}; }
    static Type selfType() { return {TypeKind::SelfType, 0, false}; }
    static Type addrType() { return {TypeKind::Addr, 64, false}; }
    static Type untypedInt() { return {TypeKind::UntypedInt, 0, false}; }
    static Type untypedFloat() { return {TypeKind::UntypedFloat, 0, false}; }
    static Type functionType(const FunctionSig* sig) { return {TypeKind::Function, 0, false, nullptr, nullptr, nullptr, sig}; }
    static Type arrayType(const ArrayInfo* array) {
        return {TypeKind::Array, 0, false, nullptr, nullptr, nullptr, nullptr, array};
    }

    bool isError() const { return kind == TypeKind::Error; }
    bool isVoid() const { return kind == TypeKind::Void; }
    bool isBool() const { return kind == TypeKind::Bool; }
    bool isInt() const { return kind == TypeKind::Int; }
    bool isFloat() const { return kind == TypeKind::Float; }
    bool isNumeric() const { return isInt() || isFloat(); }
    bool isStruct() const { return kind == TypeKind::Struct; }
    bool isParam() const { return kind == TypeKind::Param; }
    bool isInterface() const { return kind == TypeKind::Interface; }
    bool isFunction() const { return kind == TypeKind::Function; }
    bool isArray() const { return kind == TypeKind::Array; }
    bool isAggregate() const { return isStruct() || isArray(); }  // kept in memory
    bool isUntyped() const { return kind == TypeKind::UntypedInt || kind == TypeKind::UntypedFloat; }

    bool operator==(const Type&) const = default;

    std::string name() const;  // as written in Cromo: "i32", "f64", "bool", ...
};

enum class ParamMode;

// The signature of a function type: the same signature is always the same
// object, so that two types are equal when their pointers are (D185).
struct FunctionSig {
    std::vector<Type> params;
    std::vector<ParamMode> modes;
    Type result;
};

const FunctionSig* internSignature(const std::vector<Type>& params, const std::vector<ParamMode>& modes, Type result);

// An array type: the same element type and count are always the same object (D196).
struct ArrayInfo {
    Type element;
    int64_t count;
};

const ArrayInfo* internArray(Type element, int64_t count);

// The type of the elements of the innermost array: i32 for i32[3][4].
Type baseElement(Type type);

// Built-in types that can be named in the source (D9, D46). `char` is u8.
std::optional<Type> findBuiltinType(const std::string& name);

// True if a value of type `from` fits in `to` without any loss (D33).
bool convertsLosslessly(Type from, Type to);

// True for structs that need destruction: with a destructor, or with a field that
// needs one. They are not copied implicitly (D23). A type parameter may need it.
bool needsDestruction(Type type);

// True if the type mentions a type parameter: such an instance is never emitted.
bool isAbstract(Type type);

// Size in bytes with the natural alignment of C (D74).
int sizeOf(Type type);
int alignOf(Type type);

// LLVM IR spelling of the type.
std::string llvmType(Type type);

// An LLVM name with its sigil, quoted when it has characters such as `<`:
// `%"struct.Pair<i32>"`.
std::string llvmName(char sigil, const std::string& name);

}  // namespace cromo
