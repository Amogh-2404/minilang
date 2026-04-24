// Unit tests for the lexer. We feed bytes through stdin via freopen on a
// temp file (gettok() reads from getchar). It's a bit ugly but it's the
// least invasive way to drive the lexer without changing its public API.

#include "lexer.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <unistd.h>

namespace {

// Write `src` to a temp file, redirect stdin to it, and run `body` with a
// fresh Lexer. The OS reclaims the temp file after we close it.
class StdinFixture {
public:
    explicit StdinFixture(const std::string& src) {
        char tmpl[] = "/tmp/mlxXXXXXX";
        int fd = ::mkstemp(tmpl);
        ::write(fd, src.data(), src.size());
        ::close(fd);
        path = tmpl;
        saved = std::freopen(path.c_str(), "r", stdin);
    }
    ~StdinFixture() {
        std::fclose(stdin);
        ::unlink(path.c_str());
    }
private:
    std::string path;
    std::FILE*  saved = nullptr;
};

}  // namespace

TEST(Lexer, EmptyInputReturnsEof) {
    StdinFixture sf("");
    ml::Lexer lex;
    EXPECT_EQ(lex.gettok(), ml::tok_eof);
}

TEST(Lexer, NumberLiteral) {
    StdinFixture sf("42.5");
    ml::Lexer lex;
    EXPECT_EQ(lex.gettok(), ml::tok_number);
    EXPECT_DOUBLE_EQ(lex.number_val, 42.5);
}

TEST(Lexer, IdentifierAndKeywords) {
    StdinFixture sf("def extern if then else for in var foo");
    ml::Lexer lex;
    EXPECT_EQ(lex.gettok(), ml::tok_def);
    EXPECT_EQ(lex.gettok(), ml::tok_extern);
    EXPECT_EQ(lex.gettok(), ml::tok_if);
    EXPECT_EQ(lex.gettok(), ml::tok_then);
    EXPECT_EQ(lex.gettok(), ml::tok_else);
    EXPECT_EQ(lex.gettok(), ml::tok_for);
    EXPECT_EQ(lex.gettok(), ml::tok_in);
    EXPECT_EQ(lex.gettok(), ml::tok_var);
    EXPECT_EQ(lex.gettok(), ml::tok_identifier);
    EXPECT_EQ(lex.identifier_str, "foo");
}

TEST(Lexer, OperatorsAndPunctuation) {
    StdinFixture sf("+ - * / < > = ( ) , ;");
    ml::Lexer lex;
    EXPECT_EQ(lex.gettok(), '+');
    EXPECT_EQ(lex.gettok(), '-');
    EXPECT_EQ(lex.gettok(), '*');
    EXPECT_EQ(lex.gettok(), '/');
    EXPECT_EQ(lex.gettok(), '<');
    EXPECT_EQ(lex.gettok(), '>');
    EXPECT_EQ(lex.gettok(), '=');
    EXPECT_EQ(lex.gettok(), '(');
    EXPECT_EQ(lex.gettok(), ')');
    EXPECT_EQ(lex.gettok(), ',');
    EXPECT_EQ(lex.gettok(), ';');
    EXPECT_EQ(lex.gettok(), ml::tok_eof);
}

TEST(Lexer, LineCommentHash) {
    StdinFixture sf("# this is a comment\n42");
    ml::Lexer lex;
    EXPECT_EQ(lex.gettok(), ml::tok_number);
    EXPECT_DOUBLE_EQ(lex.number_val, 42.0);
}

TEST(Lexer, LineCommentDoubleSlash) {
    StdinFixture sf("// also a comment\n7");
    ml::Lexer lex;
    EXPECT_EQ(lex.gettok(), ml::tok_number);
    EXPECT_DOUBLE_EQ(lex.number_val, 7.0);
}

TEST(Lexer, SlashIsDivisionWhenNotComment) {
    StdinFixture sf("6 / 2");
    ml::Lexer lex;
    EXPECT_EQ(lex.gettok(), ml::tok_number);
    EXPECT_EQ(lex.gettok(), '/');
    EXPECT_EQ(lex.gettok(), ml::tok_number);
}

TEST(Lexer, IdentifierWithUnderscoreInside) {
    StdinFixture sf("sum_to_n");
    ml::Lexer lex;
    EXPECT_EQ(lex.gettok(), ml::tok_identifier);
    EXPECT_EQ(lex.identifier_str, "sum_to_n");
}

TEST(Lexer, MultiTokenSequence) {
    StdinFixture sf("def square(x) x * x;");
    ml::Lexer lex;
    EXPECT_EQ(lex.gettok(), ml::tok_def);
    EXPECT_EQ(lex.gettok(), ml::tok_identifier);
    EXPECT_EQ(lex.identifier_str, "square");
    EXPECT_EQ(lex.gettok(), '(');
    EXPECT_EQ(lex.gettok(), ml::tok_identifier);
    EXPECT_EQ(lex.identifier_str, "x");
    EXPECT_EQ(lex.gettok(), ')');
    EXPECT_EQ(lex.gettok(), ml::tok_identifier);
    EXPECT_EQ(lex.gettok(), '*');
    EXPECT_EQ(lex.gettok(), ml::tok_identifier);
    EXPECT_EQ(lex.gettok(), ';');
    EXPECT_EQ(lex.gettok(), ml::tok_eof);
}

TEST(Lexer, WhitespaceIsSkipped) {
    StdinFixture sf("  \t\n  42  ");
    ml::Lexer lex;
    EXPECT_EQ(lex.gettok(), ml::tok_number);
    EXPECT_DOUBLE_EQ(lex.number_val, 42.0);
    EXPECT_EQ(lex.gettok(), ml::tok_eof);
}

TEST(Lexer, NumberWithLeadingDot) {
    StdinFixture sf(".5");
    ml::Lexer lex;
    EXPECT_EQ(lex.gettok(), ml::tok_number);
    EXPECT_DOUBLE_EQ(lex.number_val, 0.5);
}

TEST(Lexer, MultipleNumbersBackToBack) {
    StdinFixture sf("1 2 3");
    ml::Lexer lex;
    EXPECT_EQ(lex.gettok(), ml::tok_number);
    EXPECT_DOUBLE_EQ(lex.number_val, 1.0);
    EXPECT_EQ(lex.gettok(), ml::tok_number);
    EXPECT_DOUBLE_EQ(lex.number_val, 2.0);
    EXPECT_EQ(lex.gettok(), ml::tok_number);
    EXPECT_DOUBLE_EQ(lex.number_val, 3.0);
}

TEST(Lexer, KeywordsAreNotIdentifiers) {
    StdinFixture sf("if iff");  // 'iff' should be tok_identifier, not tok_if
    ml::Lexer lex;
    EXPECT_EQ(lex.gettok(), ml::tok_if);
    EXPECT_EQ(lex.gettok(), ml::tok_identifier);
    EXPECT_EQ(lex.identifier_str, "iff");
}

TEST(Lexer, LongIdentifier) {
    StdinFixture sf("a_really_quite_long_name_with_many_underscores");
    ml::Lexer lex;
    EXPECT_EQ(lex.gettok(), ml::tok_identifier);
    EXPECT_EQ(lex.identifier_str.size(), 46u);
}

TEST(Lexer, CommentAtEofDoesNotLoopForever) {
    StdinFixture sf("# trailing comment with no newline");
    ml::Lexer lex;
    EXPECT_EQ(lex.gettok(), ml::tok_eof);
}
