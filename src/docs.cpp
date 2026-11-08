#include "docs.h"

#include <map>
#include <optional>

#include "lexer.h"
#include "types.h"

namespace cromo {

namespace {

// The text of a comment `// text`, without `//` and the space after it.
std::string commentText(const std::string& comment) {
    std::string text = comment.substr(2);
    if (!text.empty() && text[0] == ' ') text.erase(0, 1);
    return text;
}

// A letter, a digit or `_`; a byte of UTF-8 counts as a letter. A quote next to
// one of them is an apostrophe, not the start or the end of code (D474).
bool isWordChar(char c) {
    unsigned char u = static_cast<unsigned char>(c);
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || u >= 0x80;
}

std::string trimmed(const std::string& text) {
    size_t begin = text.find_first_not_of(" \t");
    if (begin == std::string::npos) return {};
    size_t end = text.find_last_not_of(" \t");
    return text.substr(begin, end - begin + 1);
}

// A line of text: 'code' becomes a span of code, `<` is escaped (P51).
std::string inlineMarkdown(const std::string& line) {
    std::string out;
    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];
        if (c == '\'' && (i == 0 || !isWordChar(line[i - 1]))) {
            size_t close = std::string::npos;
            for (size_t j = i + 2; j < line.size(); ++j) {
                if (line[j] == '\'' && (j + 1 == line.size() || !isWordChar(line[j + 1]))) {
                    close = j;
                    break;
                }
            }
            if (close != std::string::npos) {
                std::string code = line.substr(i + 1, close - i - 1);
                if (code.find('`') == std::string::npos) {
                    out += "`" + code + "`";
                } else {
                    out += "`` " + code + " ``";
                }
                i = close;
                continue;
            }
        }
        if (c == '<') {
            out += "\\<";
        } else {
            out += c;
        }
    }
    return out;
}

}  // namespace

DocComments::DocComments(const SourceFile& file) {
    Diagnostics quiet(file);
    quiet.setSilent(true);
    std::vector<Token> tokens = tokenize(file, 0, quiet, true);
    for (size_t i = 0; i < tokens.size(); ++i) {
        const Token& token = tokens[i];
        if (token.kind == TokenKind::Newline || token.kind == TokenKind::EndOfFile) continue;
        Line& line = lines_[token.loc.line];
        if (token.kind == TokenKind::Comment && token.text.rfind("//", 0) == 0) {
            line.hasComment = true;
            line.comment = commentText(token.text);
            line.column = token.loc.column;
            continue;
        }
        if (!line.code) {  // the first token of the line; a comment /* */ counts as code
            line.attribute = token.kind == TokenKind::LBracket && i + 1 < tokens.size() &&
                             tokens[i + 1].kind == TokenKind::LBracket && tokens[i + 1].loc.line == token.loc.line;
            line.import = token.kind == TokenKind::KwImport;
        }
        line.code = true;
    }
}

const DocComments::Line* DocComments::line(int number) const {
    auto found = lines_.find(number);
    return found == lines_.end() ? nullptr : &found->second;
}

bool DocComments::aloneComment(int number) const {
    const Line* found = line(number);
    return found && found->hasComment && !found->code;
}

std::vector<std::string> DocComments::of(int number, bool trailing) const {
    std::vector<std::string> out;
    int bottom = number - 1;
    while (line(bottom) && line(bottom)->attribute && !line(bottom)->hasComment) --bottom;  // [[test]]
    int top = bottom + 1;
    while (aloneComment(top - 1)) --top;
    // P51: the lines that go on with the comment at the end of the line above, in
    // its column (D168), belong to that one.
    const Line* above = line(top - 1);
    if (above && above->hasComment && above->code) {
        while (top <= bottom && line(top)->column == above->column) ++top;
    }
    for (int i = top; i <= bottom; ++i) out.push_back(line(i)->comment);

    const Line* own = line(number);
    if (trailing && own && own->hasComment && own->code) {
        if (!out.empty()) out.push_back("");
        out.push_back(own->comment);
        for (int next = number + 1; aloneComment(next) && line(next)->column == own->column; ++next) {
            out.push_back(line(next)->comment);
        }
    }
    return out;
}

std::vector<std::string> DocComments::module() const {
    std::vector<std::string> out;
    if (lines_.empty()) return out;
    int number = lines_.begin()->first;
    for (; aloneComment(number); ++number) out.push_back(line(number)->comment);
    const Line* after = line(number);
    if (after && after->code && !after->import) out.clear();  // the documentation of a declaration
    return out;
}

std::string markdownOf(const std::vector<std::string>& lines) {
    std::string out;
    bool block = false;
    for (const std::string& line : lines) {
        if (trimmed(line) == "'''") {
            out += block ? "```\n" : "```cromo\n";
            block = !block;
        } else if (block) {
            out += line + "\n";
        } else {
            out += inlineMarkdown(line) + "\n";
        }
    }
    if (block) out += "```\n";
    while (!out.empty() && (out.back() == '\n' || out.back() == ' ')) out.pop_back();
    return out;
}

const std::vector<BuiltinDoc>& builtinDocs() {
    static const std::vector<BuiltinDoc> docs = {
        {"print", "void print(string format, ...)",
         "Writes 'format' on the standard output, with the arguments in place of its placeholders, and no newline.\n"
         "The format is a string literal, which the compiler checks against the arguments, as 'std::format' of C++:\n"
         "\n"
         "- '{}' a value as it is; a struct or an enum with 'string text() const' as its text\n"
         "- '{0}' the argument at that position, from 0\n"
         "- '{:>8}', '{:<8}', '{:^8}' the value in 8 characters, on the right, on the left or at the center\n"
         "- '{:.2f}' a number with 2 decimals; '{:x}', '{:b}', '{:o}' an integer in hexadecimal, binary or octal\n"
         "- '{:?}' the form for debugging of any value, as 'Point { x: 1, y: 2 }' or a string between quotes\n"
         "- '{{' and '}}' the braces themselves"},
        {"println", "void println(string format, ...)",
         "Writes 'format' on the standard output as 'print' does, and then a newline.\n"
         "\n"
         "'''\n"
         "println(\"Hello {}, you are {} years old\", name, age)\n"
         "println(\"average: {:.2f}\", average)\n"
         "'''"},
        {"eprint", "void eprint(string format, ...)",
         "Writes 'format' on the standard error as 'print' does, without a newline."},
        {"eprintln", "void eprintln(string format, ...)",
         "Writes 'format' on the standard error as 'println' does, with a newline: for errors and messages that must "
         "not mix with the output of the program."},
        {"format", "string format(string format, ...)",
         "The text that 'print' would write, with the same placeholders, in a new string.\n"
         "\n"
         "'''\n"
         "string s = format(\"{} + {} = {}\", 2, 3, 5)   // \"2 + 3 = 5\"\n"
         "'''"},
        {"panic", "void panic(string reason)",
         "Stops the program on purpose, as an index out of range does: it writes 'panic:' with the reason, the place "
         "and the calls that led there on the standard error, and ends with the exit code 101. It is for the errors "
         "of the program, and cannot be caught; for an error that the caller can handle, throw an exception."},
        {"assert", "void assert(bool condition)",
         "Stops the program with a panic when 'condition' is false. The panic writes the condition as it is written "
         "and, for a comparison of numbers, the values of its sides: 'assertion failed: total == 7 (total is 6)'. "
         "It works also with '--release', and the tests use it to check their results."},
        {"wrapAdd", "T wrapAdd(T a, T b)",
         "The sum of two integers of the same type, keeping the low bits when it goes out of the range, without the "
         "check of overflow, also in debug: for hashes, checksums and random numbers."},
        {"wrapSub", "T wrapSub(T a, T b)",
         "The difference of two integers of the same type, keeping the low bits when it goes out of the range, "
         "without the check of overflow, also in debug."},
        {"wrapMul", "T wrapMul(T a, T b)",
         "The product of two integers of the same type, keeping the low bits when it goes out of the range, without "
         "the check of overflow, also in debug: for hashes, checksums and random numbers."},
        {"countOnes", "i32 countOnes(T x)",
         "The bits at 1 of an integer, of any type: 'countOnes(0b1011)' is 3. It is one instruction of the processor, "
         "for the boards of bits and the sets of bits."},
        {"trailingZeros", "i32 trailingZeros(T x)",
         "The bits at 0 under the lowest bit at 1 of an integer: the index of that bit, from 0. 'trailingZeros(8)' "
         "is 3, and for 0 it is the bits of the type, 64 for an i64."},
        {"leadingZeros", "i32 leadingZeros(T x)",
         "The bits at 0 over the highest bit at 1 of an integer: 'leadingZeros(u32(1))' is 31, and for 0 it is the "
         "bits of the type."},
        {"rotateLeft", "T rotateLeft(T x, n)",
         "The bits of an integer turned to the left by n: those that go out on the left come in on the right. n is "
         "an integer of any type, taken modulo the bits of the type, so a negative n turns to the right."},
        {"rotateRight", "T rotateRight(T x, n)",
         "The bits of an integer turned to the right by n: those that go out on the right come in on the left. n is "
         "an integer of any type, taken modulo the bits of the type."},
        {"embed", "list<u8> embed(string path)",
         "The bytes of a file that the compiler reads and puts in the program: images and data that the program "
         "carries with it, also in the APK of Android. 'path' is a string literal, from the folder of the source file "
         "that writes it; a file that is not there is an error of the compiler, and a file changed needs the program "
         "compiled again. Each call gives a copy of its own, which the program can change. For a text: "
         "'string(embed(\"help.txt\"))'.\n"
         "\n"
         "'''\n"
         "list<u8> logo = embed(\"images/logo.png\")\n"
         "'''"},
        {"bool", "", "A truth value, 'true' or 'false', in a byte."},
        {"char", "",
         "A byte, the same type as 'u8', for the characters of ASCII. Its literals are written between single quotes, "
         "as in C, with the same escapes for a newline, a tab or a byte in hexadecimal."},
        {"i8", "", "An integer with sign of 8 bits, from -128 to 127. Out of the range, an operation stops the program "
                   "in debug, and wraps around with '--release'."},
        {"i16", "", "An integer with sign of 16 bits, from -32768 to 32767. Out of the range, an operation stops the "
                    "program in debug, and wraps around with '--release'."},
        {"i32", "", "An integer with sign of 32 bits, from -2147483648 to 2147483647, 'int' of C. Out of the range, an "
                    "operation stops the program in debug, and wraps around with '--release'."},
        {"i64", "", "An integer with sign of 64 bits, from -9223372036854775808 to 9223372036854775807: the lengths and "
                    "the indices of lists and strings. Out of the range, an operation stops the program in debug, and "
                    "wraps around with '--release'."},
        {"u8", "", "An integer without sign of 8 bits, from 0 to 255: a byte. Out of the range, an operation stops the "
                   "program in debug, and wraps around with '--release'."},
        {"u16", "", "An integer without sign of 16 bits, from 0 to 65535. Out of the range, an operation stops the "
                    "program in debug, and wraps around with '--release'."},
        {"u32", "", "An integer without sign of 32 bits, from 0 to 4294967295. Out of the range, an operation stops the "
                    "program in debug, and wraps around with '--release'."},
        {"u64", "", "An integer without sign of 64 bits, from 0 to 18446744073709551615. Out of the range, an operation "
                    "stops the program in debug, and wraps around with '--release'."},
        {"f32", "", "A floating-point number of 32 bits, 'float' of C, with about 7 digits."},
        {"f64", "", "A floating-point number of 64 bits, 'double' of C, with about 16 digits."},
    };
    return docs;
}

const BuiltinDoc* builtinDoc(const std::string& name) {
    for (const BuiltinDoc& doc : builtinDocs()) {
        if (name == doc.name) return &doc;
    }
    // A vector of SIMD (D505): one text for all of them, as `i16x16` or `f32x8`.
    std::optional<Type> type = findBuiltinType(name);
    if (type && type->isVector()) {
        static std::map<std::string, std::pair<std::string, std::string>> vectors;
        auto& made = vectors[name];
        if (made.second.empty()) {
            made.first = name;
            made.second = "A vector of SIMD: " + std::to_string(type->array->count) + " numbers " + type->array->element.name() +
                          ", " + std::to_string(sizeOf(*type) * 8) + " bits, on which the operators act number by number "
                          "in one instruction of the processor, AVX2, SSE or NEON. '+', '-', '*', '/' of the floats, the "
                          "operators of the bits of the integers and the shifts; a number on one side is in each number. "
                          "A comparison gives a mask, -1 where true and 0 where false, with 'all()', 'any()' and "
                          "'select(a, b)'.\n"
                          "\n"
                          "'" + name + "(x)' has x in each number, '" + name + ".load(values, at)' reads from a list or an "
                          "array, 'v.store(ref values, at)' writes; 'v[i]' is a number. 'sum()', 'smallest()', "
                          "'largest()', 'abs()', 'min(w)', 'max(w)', 'len()', and 'low()' and 'high()' for the halves "
                          "of 256 bits. The integers wrap around, without the checks of overflow.";
        }
        static thread_local BuiltinDoc doc;
        doc = BuiltinDoc{made.first.c_str(), "", made.second.c_str()};
        return &doc;
    }
    return nullptr;
}

}  // namespace cromo
