#include "codegen.h"

#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/raw_ostream.h"

#include <cstdio>

using namespace llvm;

namespace ml {

// ---------- Codegen lifecycle ----------

Codegen::Codegen()
    : context_(std::make_unique<LLVMContext>()),
      module_(std::make_unique<Module>("minilang.module", *context_)),
      builder_(std::make_unique<IRBuilder<>>(*context_)) {}

void Codegen::reset(const std::string& module_name) {
    context_ = std::make_unique<LLVMContext>();
    module_  = std::make_unique<Module>(module_name, *context_);
    builder_ = std::make_unique<IRBuilder<>>(*context_);
    named_values_.clear();
}

std::unique_ptr<Module>      Codegen::take_module()  { return std::move(module_); }
std::unique_ptr<LLVMContext> Codegen::take_context() { return std::move(context_); }

Function* Codegen::get_function(const std::string& name) {
    if (auto* fn = module_->getFunction(name)) return fn;
    auto it = protos_.find(name);
    if (it == protos_.end()) return nullptr;
    return it->second->codegen(*this);
}

void Codegen::remember_proto(std::unique_ptr<PrototypeAST> proto) {
    protos_[proto->get_name()] = std::move(proto);
}

AllocaInst* Codegen::create_entry_block_alloca(Function* fn, const std::string& name) {
    // Insert at the *very top* of the entry block, before any other
    // instructions. mem2reg expects all allocas in the entry block; if we
    // insert them later they get treated as dynamic stack allocations and
    // are not promoted.
    IRBuilder<> tmp(&fn->getEntryBlock(), fn->getEntryBlock().begin());
    return tmp.CreateAlloca(Type::getDoubleTy(*context_), nullptr, name);
}

void Codegen::print_module() const {
    module_->print(outs(), nullptr);
}

// ---------- AST::codegen overrides ----------
//
// Each codegen() returns the LLVM Value* representing the computation. For
// expressions that don't really produce a useful value (the "unit" case),
// we synthesise a 0.0 -- because v0.1 only has f64.

Value* NumberExprAST::codegen(Codegen& cg) {
    return ConstantFP::get(cg.ctx(), APFloat(val));
}

Value* VariableExprAST::codegen(Codegen& cg) {
    auto it = cg.named_values().find(name);
    if (it == cg.named_values().end()) {
        std::fprintf(stderr, "error: unknown variable '%s'\n", name.c_str());
        return nullptr;
    }
    // Read through the alloca. mem2reg will collapse load/store pairs into
    // SSA registers, so this is essentially free at -O2.
    return cg.builder().CreateLoad(Type::getDoubleTy(cg.ctx()),
                                   it->second, name.c_str());
}

Value* BinaryExprAST::codegen(Codegen& cg) {
    // Special-case: assignment '=' is the only binop that doesn't follow the
    // generic pattern, because it needs the lhs as an *l-value* (an alloca
    // we can store into) rather than as a loaded value.
    if (op == '=') {
        auto* lhs_var = dynamic_cast<VariableExprAST*>(lhs.get());
        if (!lhs_var) {
            std::fprintf(stderr, "error: '=' requires a variable on the left\n");
            return nullptr;
        }
        Value* rval = rhs->codegen(cg);
        if (!rval) return nullptr;
        auto it = cg.named_values().find(lhs_var->get_name());
        if (it == cg.named_values().end()) {
            std::fprintf(stderr, "error: unknown variable '%s' on lhs of '='\n",
                         lhs_var->get_name().c_str());
            return nullptr;
        }
        cg.builder().CreateStore(rval, it->second);
        return rval;
    }

    Value* L = lhs->codegen(cg);
    Value* R = rhs->codegen(cg);
    if (!L || !R) return nullptr;

    // Comparisons return i1 from fcmp; we widen back to double via uitofp
    // because everything in v0.1 is f64. Yes, this leaves a trivially-foldable
    // pattern in IR -- our ConstFoldPass picks it up if both inputs are
    // constants, and -O2's instcombine does the rest.
    auto& B = cg.builder();
    switch (op) {
        case '+': return B.CreateFAdd(L, R, "addtmp");
        case '-': return B.CreateFSub(L, R, "subtmp");
        case '*': return B.CreateFMul(L, R, "multmp");
        case '/': return B.CreateFDiv(L, R, "divtmp");
        case '<': {
            Value* c = B.CreateFCmpULT(L, R, "cmptmp");
            return B.CreateUIToFP(c, Type::getDoubleTy(cg.ctx()), "booltmp");
        }
        case '>': {
            Value* c = B.CreateFCmpUGT(L, R, "cmptmp");
            return B.CreateUIToFP(c, Type::getDoubleTy(cg.ctx()), "booltmp");
        }
        default:
            std::fprintf(stderr, "error: unknown binop '%c'\n", op);
            return nullptr;
    }
}

Value* CallExprAST::codegen(Codegen& cg) {
    Function* fn = cg.get_function(callee);
    if (!fn) {
        std::fprintf(stderr, "error: unknown function '%s'\n", callee.c_str());
        return nullptr;
    }
    if (fn->arg_size() != args.size()) {
        std::fprintf(stderr, "error: arg count mismatch on '%s' (expected %zu, got %zu)\n",
                     callee.c_str(), fn->arg_size(), args.size());
        return nullptr;
    }
    std::vector<Value*> arg_vals;
    arg_vals.reserve(args.size());
    for (auto& a : args) {
        arg_vals.push_back(a->codegen(cg));
        if (!arg_vals.back()) return nullptr;
    }
    return cg.builder().CreateCall(fn, arg_vals, "calltmp");
}

Value* IfExprAST::codegen(Codegen& cg) {
    Value* cond_v = cond->codegen(cg);
    if (!cond_v) return nullptr;

    // Convert f64 condition to bool by comparing to 0.0 (truthy = nonzero).
    auto& B = cg.builder();
    cond_v = B.CreateFCmpONE(cond_v,
                             ConstantFP::get(cg.ctx(), APFloat(0.0)),
                             "ifcond");

    Function* fn = B.GetInsertBlock()->getParent();

    // Three blocks: then, else, merge. We insert 'then' immediately so the
    // builder has a place to land; 'else' and 'merge' get appended after we
    // emit 'then' (otherwise nested if-exprs end up with weirdly ordered
    // basic blocks in the function).
    BasicBlock* then_bb  = BasicBlock::Create(cg.ctx(), "then", fn);
    BasicBlock* else_bb  = BasicBlock::Create(cg.ctx(), "else");
    BasicBlock* merge_bb = BasicBlock::Create(cg.ctx(), "ifcont");

    B.CreateCondBr(cond_v, then_bb, else_bb);

    // 'then' branch.
    B.SetInsertPoint(then_bb);
    Value* then_v = then_e->codegen(cg);
    if (!then_v) return nullptr;
    B.CreateBr(merge_bb);
    // codegen of 'then' may have changed the current block (e.g. nested
    // control flow); re-fetch for the phi.
    then_bb = B.GetInsertBlock();

    // 'else' branch.
    fn->insert(fn->end(), else_bb);
    B.SetInsertPoint(else_bb);
    Value* else_v = else_e->codegen(cg);
    if (!else_v) return nullptr;
    B.CreateBr(merge_bb);
    else_bb = B.GetInsertBlock();

    // Merge.
    fn->insert(fn->end(), merge_bb);
    B.SetInsertPoint(merge_bb);
    PHINode* phi = B.CreatePHI(Type::getDoubleTy(cg.ctx()), 2, "iftmp");
    phi->addIncoming(then_v, then_bb);
    phi->addIncoming(else_v, else_bb);
    return phi;
}

Value* ForExprAST::codegen(Codegen& cg) {
    auto& B = cg.builder();
    Function* fn = B.GetInsertBlock()->getParent();

    // Loop-counter alloca in the entry block (so mem2reg can promote it).
    AllocaInst* alloca = cg.create_entry_block_alloca(fn, var_name);

    Value* start_v = start->codegen(cg);
    if (!start_v) return nullptr;
    B.CreateStore(start_v, alloca);

    // Header-then-body CFG. We test the condition *before* the body so a
    // loop with a falsy initial condition runs zero times -- the
    // Kaleidoscope tutorial's body-then-test layout has the opposite bug.
    //
    //   preheader        (current insertion point)
    //     -> header
    //   header
    //     cond = eval(end)
    //     br cond, body, after
    //   body
    //     eval(body); i = i + step
    //     -> header
    //   after
    BasicBlock* header_bb = BasicBlock::Create(cg.ctx(), "loop.header", fn);
    BasicBlock* body_bb   = BasicBlock::Create(cg.ctx(), "loop.body",   fn);
    BasicBlock* after_bb  = BasicBlock::Create(cg.ctx(), "loop.after",  fn);

    // Shadow any prior binding of var_name; we restore it on exit.
    AllocaInst* saved = cg.named_values()[var_name];
    cg.named_values()[var_name] = alloca;

    B.CreateBr(header_bb);

    // header: re-evaluate end on each iteration, branch.
    B.SetInsertPoint(header_bb);
    Value* end_v = end->codegen(cg);
    if (!end_v) return nullptr;
    Value* cond_v = B.CreateFCmpONE(
        end_v, ConstantFP::get(cg.ctx(), APFloat(0.0)), "loopcond");
    B.CreateCondBr(cond_v, body_bb, after_bb);

    // body: run body, increment, branch back to header.
    B.SetInsertPoint(body_bb);
    if (!body->codegen(cg)) return nullptr;

    Value* step_v;
    if (step) {
        step_v = step->codegen(cg);
        if (!step_v) return nullptr;
    } else {
        step_v = ConstantFP::get(cg.ctx(), APFloat(1.0));
    }

    Value* cur  = B.CreateLoad(Type::getDoubleTy(cg.ctx()), alloca, var_name.c_str());
    Value* next = B.CreateFAdd(cur, step_v, "nextvar");
    B.CreateStore(next, alloca);
    B.CreateBr(header_bb);

    // after: restore shadowed binding, return a unit value (0.0).
    B.SetInsertPoint(after_bb);
    if (saved) cg.named_values()[var_name] = saved;
    else        cg.named_values().erase(var_name);

    return ConstantFP::get(cg.ctx(), APFloat(0.0));
}

Value* VarExprAST::codegen(Codegen& cg) {
    auto& B = cg.builder();
    Function* fn = B.GetInsertBlock()->getParent();

    // Save shadowed bindings so they come back in scope after the 'in' body.
    std::vector<std::pair<std::string, AllocaInst*>> saved;
    saved.reserve(bindings.size());

    for (auto& [name, init] : bindings) {
        Value* init_v;
        if (init) {
            init_v = init->codegen(cg);
            if (!init_v) return nullptr;
        } else {
            init_v = ConstantFP::get(cg.ctx(), APFloat(0.0));
        }
        AllocaInst* alloca = cg.create_entry_block_alloca(fn, name);
        B.CreateStore(init_v, alloca);

        saved.emplace_back(name, cg.named_values()[name]);
        cg.named_values()[name] = alloca;
    }

    Value* body_v = body->codegen(cg);
    if (!body_v) return nullptr;

    // Restore shadowed bindings.
    for (auto& [name, prev] : saved) {
        if (prev) cg.named_values()[name] = prev;
        else      cg.named_values().erase(name);
    }
    return body_v;
}

Function* PrototypeAST::codegen(Codegen& cg) {
    std::vector<Type*> doubles(args.size(), Type::getDoubleTy(cg.ctx()));
    FunctionType* ft = FunctionType::get(Type::getDoubleTy(cg.ctx()), doubles, false);
    Function* f = Function::Create(ft, Function::ExternalLinkage, name, &cg.module());
    unsigned i = 0;
    for (auto& a : f->args()) a.setName(args[i++]);
    return f;
}

Function* FunctionAST::codegen(Codegen& cg) {
    // Remember the prototype before codegen so future modules can re-declare
    // this function via Codegen::get_function. We clone the proto because
    // FunctionAST still needs it for codegen below; the original `proto`
    // unique_ptr stays owned by this AST node.
    cg.remember_proto(std::make_unique<PrototypeAST>(*proto));

    Function* fn = cg.get_function(proto->get_name());
    if (!fn) return nullptr;  // get_function should have re-declared it
    if (!fn->empty()) {
        std::fprintf(stderr, "error: function '%s' redefined\n",
                     proto->get_name().c_str());
        return nullptr;
    }

    BasicBlock* bb = BasicBlock::Create(cg.ctx(), "entry", fn);
    cg.builder().SetInsertPoint(bb);

    cg.named_values().clear();
    for (auto& a : fn->args()) {
        AllocaInst* alloca = cg.create_entry_block_alloca(fn, std::string(a.getName()));
        cg.builder().CreateStore(&a, alloca);
        cg.named_values()[std::string(a.getName())] = alloca;
    }

    if (Value* ret = body->codegen(cg)) {
        cg.builder().CreateRet(ret);
        if (verifyFunction(*fn, &errs())) {
            std::fprintf(stderr, "error: verifyFunction failed for '%s'\n",
                         proto->get_name().c_str());
            fn->eraseFromParent();
            return nullptr;
        }
        return fn;
    }
    fn->eraseFromParent();
    return nullptr;
}

}  // namespace ml
