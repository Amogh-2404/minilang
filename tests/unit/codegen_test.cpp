// Unit tests for codegen. We don't try to *execute* the IR here; we just
// build a Module and verify it through llvm::verifyModule. End-to-end
// execution is tested via the e2e harness instead.

#include "ast.h"
#include "codegen.h"
#include "lexer.h"
#include "parser.h"

#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/raw_ostream.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <string>
#include <unistd.h>

namespace {

class StdinFixture {
public:
    explicit StdinFixture(const std::string& src) {
        char tmpl[] = "/tmp/mlcXXXXXX";
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

void compile_one(const std::string& src, ml::Codegen& cg) {
    StdinFixture sf(src);
    ml::Lexer  lex;
    ml::Parser p(lex);
    int counter = 0;
    while (p.current() != ml::tok_eof) {
        switch (p.current()) {
            case ';': p.advance(); break;
            case ml::tok_def:
                if (auto fn = p.parse_definition()) fn->codegen(cg);
                break;
            case ml::tok_extern:
                if (auto proto = p.parse_extern()) proto->codegen(cg);
                break;
            default: {
                std::string n = "__t_" + std::to_string(counter++);
                if (auto fn = p.parse_top_level_expr(n)) fn->codegen(cg);
                break;
            }
        }
    }
}

}  // namespace

TEST(Codegen, ArithmeticVerifies) {
    ml::Codegen cg;
    compile_one("def f() 1 + 2 * 3;", cg);
    EXPECT_FALSE(llvm::verifyModule(cg.module(), &llvm::errs()));
}

TEST(Codegen, IfElseVerifies) {
    ml::Codegen cg;
    compile_one("def f(x) if x < 1 then 1 else 0;", cg);
    EXPECT_FALSE(llvm::verifyModule(cg.module(), &llvm::errs()));
}

TEST(Codegen, ForLoopVerifies) {
    ml::Codegen cg;
    compile_one("def f(n) for i = 0, i < n, 1.0 in i;", cg);
    EXPECT_FALSE(llvm::verifyModule(cg.module(), &llvm::errs()));
}

TEST(Codegen, VarBindingVerifies) {
    ml::Codegen cg;
    compile_one("def f(x) var s = 0.0 in s + x;", cg);
    EXPECT_FALSE(llvm::verifyModule(cg.module(), &llvm::errs()));
}

TEST(Codegen, RecursionVerifies) {
    ml::Codegen cg;
    compile_one("def fib(n) if n < 2 then n else fib(n - 1) + fib(n - 2);", cg);
    EXPECT_FALSE(llvm::verifyModule(cg.module(), &llvm::errs()));
}

TEST(Codegen, FunctionWithMultipleArgs) {
    ml::Codegen cg;
    compile_one("def hot(n, x, y) x + y * n;", cg);
    EXPECT_FALSE(llvm::verifyModule(cg.module(), &llvm::errs()));
}
