#pragma once

#include "ast.h"

#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"

#include <map>
#include <memory>
#include <string>

namespace ml {

// Codegen owns the LLVMContext + Module + IRBuilder triple, plus a symbol
// table mapping source-level names to AllocaInst*s (post-mem2reg these become
// SSA registers). The driver creates a fresh Codegen per top-level item it
// wants to JIT, so context state never leaks across compilations.
class Codegen {
public:
    Codegen();

    llvm::LLVMContext&  ctx()     { return *context_; }
    llvm::Module&       module()  { return *module_; }
    llvm::IRBuilder<>&  builder() { return *builder_; }

    // Look up an existing function in the current module. If it isn't in
    // the current module but we have a remembered prototype for it, lazily
    // re-declare it in the current module so the IR can reference it; ORC
    // resolves the symbol at link time.
    llvm::Function* get_function(const std::string& name);

    // Symbol table: name -> alloca holding that variable's storage. The
    // values are owned by the IR (AllocaInst lives inside the function's
    // entry block); we just keep raw pointers.
    std::map<std::string, llvm::AllocaInst*>& named_values() { return named_values_; }

    // Prototype memory. *Persists across reset()* -- the driver compiles
    // each top-level item in its own module, but the JIT keeps definitions
    // around. When a fresh module references an old function, we look up
    // its prototype here and re-declare it.
    void remember_proto(std::unique_ptr<PrototypeAST> proto);

    // Helper -- emits an alloca in the entry block, which is the canonical
    // location for stack slots that mem2reg will later promote to SSA.
    llvm::AllocaInst* create_entry_block_alloca(llvm::Function* fn,
                                                const std::string& name);

    // Take ownership of the current module/context and return them. Used by
    // the JIT driver, which adds the module to ORC and then asks Codegen for
    // a fresh one for the next top-level expression.
    std::unique_ptr<llvm::Module>       take_module();
    std::unique_ptr<llvm::LLVMContext>  take_context();

    // Re-create context + module after a take_*. Driver calls this between
    // top-level items.
    void reset(const std::string& module_name = "minilang.module");

    // Pretty-print the current module to stdout (-emit-llvm path).
    void print_module() const;

private:
    std::unique_ptr<llvm::LLVMContext>   context_;
    std::unique_ptr<llvm::Module>        module_;
    // IRBuilder<> isn't copy/move-assignable (it holds a reference to the
    // context), so we hold it through a unique_ptr to make reset() trivial.
    std::unique_ptr<llvm::IRBuilder<>>   builder_;
    std::map<std::string, llvm::AllocaInst*>      named_values_;
    std::map<std::string, std::unique_ptr<PrototypeAST>> protos_;
};

}  // namespace ml
