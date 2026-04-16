#include "lexer.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace ml {

int Lexer::next_char() {
    return std::getchar();
}

int Lexer::gettok() {
    // Skip whitespace.
    while (std::isspace(last_char)) last_char = next_char();

    // Identifier: [a-zA-Z][a-zA-Z0-9_]*
    if (std::isalpha(last_char)) {
        identifier_str = static_cast<char>(last_char);
        while (std::isalnum((last_char = next_char())) || last_char == '_')
            identifier_str += static_cast<char>(last_char);

        if (identifier_str == "def")    return tok_def;
        if (identifier_str == "extern") return tok_extern;
        if (identifier_str == "if")     return tok_if;
        if (identifier_str == "then")   return tok_then;
        if (identifier_str == "else")   return tok_else;
        if (identifier_str == "for")    return tok_for;
        if (identifier_str == "in")     return tok_in;
        if (identifier_str == "var")    return tok_var;
        return tok_identifier;
    }

    // Number: [0-9.]+ -- accepts a single dot, no exponent (good enough for
    // a teaching language; strtod tolerates the rest if the user really needs
    // 1e3 etc.)
    if (std::isdigit(last_char) || last_char == '.') {
        std::string num;
        do {
            num += static_cast<char>(last_char);
            last_char = next_char();
        } while (std::isdigit(last_char) || last_char == '.');
        number_val = std::strtod(num.c_str(), nullptr);
        return tok_number;
    }

    // Line comment: '#' to end of line.
    if (last_char == '#') {
        do last_char = next_char();
        while (last_char != EOF && last_char != '\n' && last_char != '\r');
        if (last_char == EOF) return tok_eof;
        return gettok();
    }

    // '//' is also a line comment. We only know it's a comment after peeking
    // the next char; if the peek isn't another '/', '/' becomes the division
    // token and we save the peek as the new last_char (no stdin pushback).
    if (last_char == '/') {
        int peek = next_char();
        if (peek == '/') {
            do last_char = next_char();
            while (last_char != EOF && last_char != '\n' && last_char != '\r');
            if (last_char == EOF) return tok_eof;
            return gettok();
        }
        last_char = peek;  // peek becomes the next char in the stream
        return '/';
    }

    if (last_char == EOF) return tok_eof;

    // Otherwise: return the raw character. Parser handles '+', '-', '(', etc.
    int this_char = last_char;
    last_char = next_char();
    return this_char;
}

}  // namespace ml
