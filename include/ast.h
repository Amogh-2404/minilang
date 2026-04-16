#pragma once

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace llvm {
class Value;
class Function;
}

namespace ml {

class Codegen;  // forward; codegen lives in codegen.h

// Base class. Each AST node knows how to lower itself to LLVM IR via the
// codegen visitor. I went with virtual codegen() rather than a real visitor
// pattern because (a) only one backend (LLVM), (b) the Kaleidoscope tutorial
// uses this style and it's easy to read.
class ExprAST {
public:
    virtual ~ExprAST() = default;
    virtual llvm::Value* codegen(Codegen&) = 0;
};

class NumberExprAST : public ExprAST {
public:
    explicit NumberExprAST(double v) : val(v) {}
    llvm::Value* codegen(Codegen&) override;
    double value() const { return val; }
private:
    double val;
};

class VariableExprAST : public ExprAST {
public:
    explicit VariableExprAST(std::string n) : name(std::move(n)) {}
    llvm::Value* codegen(Codegen&) override;
    const std::string& get_name() const { return name; }
private:
    std::string name;
};

class BinaryExprAST : public ExprAST {
public:
    BinaryExprAST(char o, std::unique_ptr<ExprAST> l, std::unique_ptr<ExprAST> r)
        : op(o), lhs(std::move(l)), rhs(std::move(r)) {}
    llvm::Value* codegen(Codegen&) override;
    char get_op() const { return op; }
    ExprAST* get_lhs() const { return lhs.get(); }
    ExprAST* get_rhs() const { return rhs.get(); }
private:
    char op;
    std::unique_ptr<ExprAST> lhs, rhs;
};

class CallExprAST : public ExprAST {
public:
    CallExprAST(std::string c, std::vector<std::unique_ptr<ExprAST>> a)
        : callee(std::move(c)), args(std::move(a)) {}
    llvm::Value* codegen(Codegen&) override;
    const std::string& get_callee() const { return callee; }
private:
    std::string callee;
    std::vector<std::unique_ptr<ExprAST>> args;
};

class IfExprAST : public ExprAST {
public:
    IfExprAST(std::unique_ptr<ExprAST> c,
              std::unique_ptr<ExprAST> t,
              std::unique_ptr<ExprAST> e)
        : cond(std::move(c)), then_e(std::move(t)), else_e(std::move(e)) {}
    llvm::Value* codegen(Codegen&) override;
    ExprAST* get_cond() const { return cond.get(); }
    ExprAST* get_then() const { return then_e.get(); }
    ExprAST* get_else() const { return else_e.get(); }
private:
    std::unique_ptr<ExprAST> cond, then_e, else_e;
};

class ForExprAST : public ExprAST {
public:
    ForExprAST(std::string n,
               std::unique_ptr<ExprAST> s,
               std::unique_ptr<ExprAST> e,
               std::unique_ptr<ExprAST> st,  // step (may be null)
               std::unique_ptr<ExprAST> b)
        : var_name(std::move(n)), start(std::move(s)), end(std::move(e)),
          step(std::move(st)), body(std::move(b)) {}
    llvm::Value* codegen(Codegen&) override;
    const std::string& get_var_name() const { return var_name; }
    ExprAST* get_start() const { return start.get(); }
    ExprAST* get_end()   const { return end.get(); }
    ExprAST* get_step()  const { return step.get(); }
    ExprAST* get_body()  const { return body.get(); }
private:
    std::string var_name;
    std::unique_ptr<ExprAST> start, end, step, body;
};

class VarExprAST : public ExprAST {
public:
    using Binding = std::pair<std::string, std::unique_ptr<ExprAST>>;
    VarExprAST(std::vector<Binding> b, std::unique_ptr<ExprAST> body)
        : bindings(std::move(b)), body(std::move(body)) {}
    llvm::Value* codegen(Codegen&) override;
    const std::vector<Binding>& get_bindings() const { return bindings; }
    ExprAST* get_body() const { return body.get(); }
private:
    std::vector<Binding> bindings;
    std::unique_ptr<ExprAST> body;
};

// Function signature: name + param names. We use double for everything in
// v0.1, so we don't need explicit types here.
class PrototypeAST {
public:
    PrototypeAST(std::string n, std::vector<std::string> a)
        : name(std::move(n)), args(std::move(a)) {}
    const std::string& get_name() const { return name; }
    const std::vector<std::string>& get_args() const { return args; }
    llvm::Function* codegen(Codegen&);
private:
    std::string name;
    std::vector<std::string> args;
};

class FunctionAST {
public:
    FunctionAST(std::unique_ptr<PrototypeAST> p, std::unique_ptr<ExprAST> b)
        : proto(std::move(p)), body(std::move(b)) {}
    PrototypeAST* get_proto() const { return proto.get(); }
    ExprAST* get_body() const { return body.get(); }
    llvm::Function* codegen(Codegen&);
private:
    std::unique_ptr<PrototypeAST> proto;
    std::unique_ptr<ExprAST> body;
};

}  // namespace ml
