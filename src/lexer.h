#pragma once

#include "diagnostics.h"

#include <cstdint>
#include <string>
#include <vector>

namespace cromo {

enum class TokenKind {
    EndOfFile,
    Newline,  // end of statement (D17, D32)
    Comment,  // only with keepComments, for cromo fmt (D161)
    Identifier,
    IntegerLiteral,
    FloatLiteral,
    StringLiteral,
    CharLiteral,

    // Keywords
    KwBreak, KwCase, KwCatch, KwConst, KwContinue, KwDefault, KwElse, KwEnum, KwExtern,
    KwFalse, KwFinally, KwFor, KwIf, KwImport, KwIn, KwInterface, KwMove, KwPublic,
    KwRef, KwRet, KwStruct, KwSwitch, KwThrow, KwTrue, KwTry, KwUnsafe, KwVoid,

    // Punctuation and operators
    LParen, RParen, LBrace, RBrace, LBracket, RBracket,
    Comma, Dot, Colon, Semicolon, Tilde,
    Plus, Minus, Star, Slash, Percent,
    PlusPlus, MinusMinus,
    Assign, PlusAssign, MinusAssign, StarAssign, SlashAssign, PercentAssign,
    Equal, NotEqual, Less, LessEqual, Greater, GreaterEqual,
    AndAnd, OrOr, Not,
};

struct Token {
    TokenKind kind = TokenKind::EndOfFile;
    std::string text;      // source text; for strings, the already decoded content
    SourceLocation loc;
    uint64_t intValue = 0; // value of integer and character literals
    size_t begin = 0;      // byte offsets of the source text, end excluded
    size_t end = 0;
};

// Readable name of a token kind, for error messages.
std::string describe(TokenKind kind);

// Splits the source into tokens. Newlines become Newline tokens, except when the
// statement continues on the next line (D32). With keepComments the comments are
// tokens too, with their text, for cromo fmt.
std::vector<Token> tokenize(const SourceFile& file, int fileIndex, Diagnostics& diag, bool keepComments = false);

// A token at the end of a line that makes the statement continue on the next one (D32).
bool continuesOnNextLine(TokenKind kind);

}  // namespace cromo
