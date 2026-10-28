#pragma once

#include "ast.h"

#include <cstdint>
#include <optional>
#include <string>

namespace cromo {

// Primitive integer type (D9, D46).
struct IntType {
    std::string name;  // canonical name: "char" is a synonym of "u8"
    int bits;
    bool isSigned;
};

std::optional<IntType> findIntType(const std::string& name);

// Constant integer value, with the sign kept apart to cover the whole range
// from -2^63 to 2^64-1.
struct ConstInt {
    bool negative = false;
    uint64_t magnitude = 0;
};

// Computes the value of a constant expression (at this milestone all of them are).
ConstInt evaluateConstant(const Expr& expr);

// Checks types, values and the `ret` rules (D47). Returns false if there are errors.
bool check(const Module& module, Diagnostics& diag);

}  // namespace cromo
