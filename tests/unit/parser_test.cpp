// Unit tests for the parser. Same stdin redirection trick as lexer_test.

#include "ast.h"
#include "lexer.h"
#include "parser.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <unistd.h>

namespace {

class StdinFixture {
public:
    explicit StdinFixture(const std::string& src) {
        char tmpl[] = "/tmp/mlpXXXXXX";
        int fd = ::mkstemp(tmpl);
        ::write(fd, src.data(), src.size());
        ::close(fd);
        path = tmpl;
        std::freopen(path.c_str(), "r", stdin);
    }
    ~StdinFixture() {
        std::fclose(stdin);
        ::unlink(path.c_str());
    }
private:
    std::string path;
};

}  // namespace

TEST(Parser, NumberLiteralExpression) {
    StdinFixture sf("42");
    ml::Lexer lex;
    ml::Parser p(lex);
    auto e = p.parse_expression();
    ASSERT_NE(e, nullptr);
    auto* num = dynamic_cast<ml::NumberExprAST*>(e.get());
    ASSERT_NE(num, nullptr);
    EXPECT_DOUBLE_EQ(num->value(), 42.0);
}

TEST(Parser, BinaryAddition) {
    StdinFixture sf("1 + 2");
    ml::Lexer lex;
    ml::Parser p(lex);
    auto e = p.parse_expression();
    ASSERT_NE(e, nullptr);
    auto* bin = dynamic_cast<ml::BinaryExprAST*>(e.get());
    ASSERT_NE(bin, nullptr);
    EXPECT_EQ(bin->get_op(), '+');
}

TEST(Parser, PrecedenceClimbingMultBindsTighter) {
    StdinFixture sf("1 + 2 * 3");
    ml::Lexer lex;
    ml::Parser p(lex);
    auto e = p.parse_expression();
    ASSERT_NE(e, nullptr);
    // Expect: ( + 1 ( * 2 3 ) ).
    auto* outer = dynamic_cast<ml::BinaryExprAST*>(e.get());
    ASSERT_NE(outer, nullptr);
    EXPECT_EQ(outer->get_op(), '+');
    auto* inner = dynamic_cast<ml::BinaryExprAST*>(outer->get_rhs());
    ASSERT_NE(inner, nullptr);
    EXPECT_EQ(inner->get_op(), '*');
}

TEST(Parser, ParenthesisedExpression) {
    StdinFixture sf("(1 + 2) * 3");
    ml::Lexer lex;
    ml::Parser p(lex);
    auto e = p.parse_expression();
    ASSERT_NE(e, nullptr);
    auto* outer = dynamic_cast<ml::BinaryExprAST*>(e.get());
    ASSERT_NE(outer, nullptr);
    EXPECT_EQ(outer->get_op(), '*');
}

TEST(Parser, FunctionDefinition) {
    StdinFixture sf("def square(x) x * x");
    ml::Lexer lex;
    ml::Parser p(lex);
    auto fn = p.parse_definition();
    ASSERT_NE(fn, nullptr);
    EXPECT_EQ(fn->get_proto()->get_name(), "square");
    ASSERT_EQ(fn->get_proto()->get_args().size(), 1u);
    EXPECT_EQ(fn->get_proto()->get_args()[0], "x");
}

TEST(Parser, FunctionWithMultipleArgs) {
    StdinFixture sf("def hot(n, x, y) n");
    ml::Lexer lex;
    ml::Parser p(lex);
    auto fn = p.parse_definition();
    ASSERT_NE(fn, nullptr);
    EXPECT_EQ(fn->get_proto()->get_args().size(), 3u);
}

TEST(Parser, FunctionCall) {
    StdinFixture sf("square(7)");
    ml::Lexer lex;
    ml::Parser p(lex);
    auto e = p.parse_expression();
    ASSERT_NE(e, nullptr);
    EXPECT_NE(dynamic_cast<ml::CallExprAST*>(e.get()), nullptr);
}

TEST(Parser, IfThenElse) {
    StdinFixture sf("if x < 1 then 1 else 0");
    ml::Lexer lex;
    ml::Parser p(lex);
    auto e = p.parse_expression();
    ASSERT_NE(e, nullptr);
    EXPECT_NE(dynamic_cast<ml::IfExprAST*>(e.get()), nullptr);
}

TEST(Parser, ForLoop) {
    StdinFixture sf("for i = 1, i < 10, 1.0 in i");
    ml::Lexer lex;
    ml::Parser p(lex);
    auto e = p.parse_expression();
    ASSERT_NE(e, nullptr);
    auto* fr = dynamic_cast<ml::ForExprAST*>(e.get());
    ASSERT_NE(fr, nullptr);
    EXPECT_EQ(fr->get_var_name(), "i");
}

TEST(Parser, VarBinding) {
    StdinFixture sf("var s = 0 in s");
    ml::Lexer lex;
    ml::Parser p(lex);
    auto e = p.parse_expression();
    ASSERT_NE(e, nullptr);
    auto* v = dynamic_cast<ml::VarExprAST*>(e.get());
    ASSERT_NE(v, nullptr);
    ASSERT_EQ(v->get_bindings().size(), 1u);
    EXPECT_EQ(v->get_bindings()[0].first, "s");
}

TEST(Parser, ExternDeclaration) {
    StdinFixture sf("extern sin(x)");
    ml::Lexer lex;
    ml::Parser p(lex);
    auto proto = p.parse_extern();
    ASSERT_NE(proto, nullptr);
    EXPECT_EQ(proto->get_name(), "sin");
}

TEST(Parser, MissingParenIsDiagnosed) {
    StdinFixture sf("(1 + 2");
    ml::Lexer lex;
    ml::Parser p(lex);
    auto e = p.parse_expression();
    EXPECT_EQ(e, nullptr);  // should fail to parse
}
