#include "parser.h"

#include <cstdio>
#include <utility>
#include <vector>

namespace ml {

std::unique_ptr<ExprAST> log_error(const char* msg) {
    std::fprintf(stderr, "error: %s\n", msg);
    return nullptr;
}

std::unique_ptr<PrototypeAST> log_error_p(const char* msg) {
    log_error(msg);
    return nullptr;
}

int Parser::get_tok_precedence(int tok) {
    switch (tok) {
        case '=':                                            return 2;
        case '<': case '>':                                  return 10;
        case '+': case '-':                                  return 20;
        case '*': case '/':                                  return 40;
        // Two-char comparators are handled by the lexer-less peek inside
        // parse_binop_rhs; we treat the leading char's precedence as 10.
        default:                                             return -1;
    }
}

std::unique_ptr<ExprAST> Parser::parse_number_expr() {
    auto r = std::make_unique<NumberExprAST>(lexer.number_val);
    advance();
    return r;
}

std::unique_ptr<ExprAST> Parser::parse_paren_expr() {
    advance();  // eat (
    auto v = parse_expression();
    if (!v) return nullptr;
    if (cur_tok != ')') return log_error("expected ')'");
    advance();
    return v;
}

std::unique_ptr<ExprAST> Parser::parse_identifier_expr() {
    std::string name = lexer.identifier_str;
    advance();
    if (cur_tok != '(') return std::make_unique<VariableExprAST>(name);

    // Function call.
    advance();  // eat (
    std::vector<std::unique_ptr<ExprAST>> args;
    if (cur_tok != ')') {
        for (;;) {
            if (auto a = parse_expression()) args.push_back(std::move(a));
            else                              return nullptr;
            if (cur_tok == ')') break;
            if (cur_tok != ',') return log_error("expected ')' or ',' in arg list");
            advance();
        }
    }
    advance();  // eat )
    return std::make_unique<CallExprAST>(name, std::move(args));
}

std::unique_ptr<ExprAST> Parser::parse_if_expr() {
    advance();  // eat 'if'
    auto cond = parse_expression();
    if (!cond) return nullptr;
    if (cur_tok != tok_then) return log_error("expected 'then'");
    advance();
    auto then_e = parse_expression();
    if (!then_e) return nullptr;
    if (cur_tok != tok_else) return log_error("expected 'else'");
    advance();
    auto else_e = parse_expression();
    if (!else_e) return nullptr;
    return std::make_unique<IfExprAST>(std::move(cond), std::move(then_e), std::move(else_e));
}

std::unique_ptr<ExprAST> Parser::parse_for_expr() {
    advance();  // eat 'for'
    if (cur_tok != tok_identifier) return log_error("expected identifier after 'for'");
    std::string name = lexer.identifier_str;
    advance();
    if (cur_tok != '=') return log_error("expected '=' after for variable");
    advance();
    auto start = parse_expression();
    if (!start) return nullptr;
    if (cur_tok != ',') return log_error("expected ',' after for start value");
    advance();
    auto end = parse_expression();
    if (!end) return nullptr;

    // Optional step.
    std::unique_ptr<ExprAST> step;
    if (cur_tok == ',') {
        advance();
        step = parse_expression();
        if (!step) return nullptr;
    }
    if (cur_tok != tok_in) return log_error("expected 'in' after for header");
    advance();
    auto body = parse_expression();
    if (!body) return nullptr;
    return std::make_unique<ForExprAST>(std::move(name),
                                        std::move(start), std::move(end),
                                        std::move(step), std::move(body));
}

std::unique_ptr<ExprAST> Parser::parse_var_expr() {
    advance();  // eat 'var'
    std::vector<VarExprAST::Binding> bindings;
    if (cur_tok != tok_identifier)
        return log_error("expected identifier after 'var'");
    for (;;) {
        std::string name = lexer.identifier_str;
        advance();
        std::unique_ptr<ExprAST> init;
        if (cur_tok == '=') {
            advance();
            init = parse_expression();
            if (!init) return nullptr;
        }
        bindings.emplace_back(std::move(name), std::move(init));
        if (cur_tok != ',') break;
        advance();
        if (cur_tok != tok_identifier)
            return log_error("expected identifier after ','");
    }
    if (cur_tok != tok_in) return log_error("expected 'in' in var expr");
    advance();
    auto body = parse_expression();
    if (!body) return nullptr;
    return std::make_unique<VarExprAST>(std::move(bindings), std::move(body));
}

std::unique_ptr<ExprAST> Parser::parse_primary() {
    switch (cur_tok) {
        case tok_identifier: return parse_identifier_expr();
        case tok_number:     return parse_number_expr();
        case '(':            return parse_paren_expr();
        case tok_if:         return parse_if_expr();
        case tok_for:        return parse_for_expr();
        case tok_var:        return parse_var_expr();
        default:             return log_error("unexpected token in primary");
    }
}

std::unique_ptr<ExprAST> Parser::parse_binop_rhs(int expr_prec, std::unique_ptr<ExprAST> lhs) {
    // Precedence-climbing loop. See Eli Bendersky's article -- I copied the
    // shape of his implementation almost verbatim. The trick is the inner
    // recursion only happens when the *next* op binds tighter than the
    // current one; otherwise we just absorb the rhs at this level.
    for (;;) {
        int tok_prec = get_tok_precedence(cur_tok);
        if (tok_prec < expr_prec) return lhs;

        char op = static_cast<char>(cur_tok);
        advance();

        auto rhs = parse_primary();
        if (!rhs) return nullptr;

        int next_prec = get_tok_precedence(cur_tok);
        if (tok_prec < next_prec) {
            rhs = parse_binop_rhs(tok_prec + 1, std::move(rhs));
            if (!rhs) return nullptr;
        }
        lhs = std::make_unique<BinaryExprAST>(op, std::move(lhs), std::move(rhs));
    }
}

std::unique_ptr<ExprAST> Parser::parse_expression() {
    auto lhs = parse_primary();
    if (!lhs) return nullptr;
    return parse_binop_rhs(0, std::move(lhs));
}

std::unique_ptr<PrototypeAST> Parser::parse_prototype() {
    if (cur_tok != tok_identifier)
        return log_error_p("expected function name in prototype");
    std::string fname = lexer.identifier_str;
    advance();
    if (cur_tok != '(') return log_error_p("expected '(' in prototype");
    advance();  // eat (

    // Comma-separated parameter list. Empty list (== "()") is fine.
    std::vector<std::string> args;
    if (cur_tok != ')') {
        for (;;) {
            if (cur_tok != tok_identifier)
                return log_error_p("expected identifier in parameter list");
            args.push_back(lexer.identifier_str);
            advance();
            if (cur_tok == ')') break;
            if (cur_tok != ',')
                return log_error_p("expected ',' or ')' in parameter list");
            advance();
        }
    }
    advance();  // eat )
    return std::make_unique<PrototypeAST>(std::move(fname), std::move(args));
}

std::unique_ptr<FunctionAST> Parser::parse_definition() {
    advance();  // eat 'def'
    auto proto = parse_prototype();
    if (!proto) return nullptr;
    auto body = parse_expression();
    if (!body) return nullptr;
    return std::make_unique<FunctionAST>(std::move(proto), std::move(body));
}

std::unique_ptr<PrototypeAST> Parser::parse_extern() {
    advance();  // eat 'extern'
    return parse_prototype();
}

std::unique_ptr<FunctionAST> Parser::parse_top_level_expr(const std::string& wrap_name) {
    auto e = parse_expression();
    if (!e) return nullptr;
    auto proto = std::make_unique<PrototypeAST>(wrap_name, std::vector<std::string>{});
    return std::make_unique<FunctionAST>(std::move(proto), std::move(e));
}

}  // namespace ml
