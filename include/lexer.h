#pragma once

#include <string>

namespace ml {

// Token enum. Negative values for keywords/identifiers/numbers; positive
// values are the ASCII code of single-character tokens like '+', '(', ';'.
// (Same convention the LLVM Kaleidoscope tutorial uses -- it makes the
// switch in the parser one-liner-friendly.)
enum Token : int {
    tok_eof    = -1,

    // top-level
    tok_def    = -2,
    tok_extern = -3,

    // primary
    tok_identifier = -4,
    tok_number     = -5,

    // control
    tok_if   = -6,
    tok_then = -7,
    tok_else = -8,
    tok_for  = -9,
    tok_in   = -10,

    // var / assignment
    tok_var = -11,
};

// Lexer state. Held in a struct rather than file-static globals so that the
// parser/unit tests can spin up multiple independent lexers if they want to.
struct Lexer {
    std::string identifier_str;  // filled when tok == tok_identifier
    double      number_val = 0;  // filled when tok == tok_number

    // Returns the next token. Caller should call this once before parsing
    // begins (parser keeps a 1-token lookahead).
    int gettok();

private:
    int last_char = ' ';
    int next_char();
};

}  // namespace ml
