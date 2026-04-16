#pragma once

#include "ast.h"
#include "lexer.h"

#include <map>
#include <memory>
#include <string>

namespace ml {

class Parser {
public:
    explicit Parser(Lexer& l) : lexer(l) { advance(); }

    // One-token lookahead.
    int current() const { return cur_tok; }
    int advance() { return cur_tok = lexer.gettok(); }

    // Top-level dispatch entry points -- the driver calls these in a loop.
    std::unique_ptr<FunctionAST>  parse_definition();
    std::unique_ptr<PrototypeAST> parse_extern();
    // A bare top-level expression gets wrapped in an anonymous function so
    // the JIT can call it. Caller decides what to name the wrapper.
    std::unique_ptr<FunctionAST>  parse_top_level_expr(const std::string& wrap_name);

    // Used by tests; usually you don't call this directly.
    std::unique_ptr<ExprAST> parse_expression();

    Lexer& get_lexer() { return lexer; }

private:
    Lexer& lexer;
    int cur_tok = 0;

    // Binop precedence table. Higher = binds tighter. '<' / '>' / '==' / '!=' /
    // '<=' / '>=' all sit at level 10; '+' / '-' at 20; '*' / '/' at 40.
    // Assignment '=' is the lowest at 2 (right-associative -- handled by
    // BinaryExprAST codegen, not here).
    static int get_tok_precedence(int tok);

    std::unique_ptr<ExprAST>      parse_primary();
    std::unique_ptr<ExprAST>      parse_number_expr();
    std::unique_ptr<ExprAST>      parse_paren_expr();
    std::unique_ptr<ExprAST>      parse_identifier_expr();
    std::unique_ptr<ExprAST>      parse_if_expr();
    std::unique_ptr<ExprAST>      parse_for_expr();
    std::unique_ptr<ExprAST>      parse_var_expr();
    std::unique_ptr<ExprAST>      parse_binop_rhs(int expr_prec, std::unique_ptr<ExprAST> lhs);
    std::unique_ptr<PrototypeAST> parse_prototype();
};

// Diagnostic helpers. They print to stderr and return null so the parser
// can: `return log_error("expected ')'");` in one line.
std::unique_ptr<ExprAST>      log_error(const char* msg);
std::unique_ptr<PrototypeAST> log_error_p(const char* msg);

}  // namespace ml
