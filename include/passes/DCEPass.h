#pragma once

#include "llvm/IR/PassManager.h"

namespace ml {

// Trivial dead-code elimination. Removes instructions whose results have no
// uses and which have no side effects. Iterates to a fixpoint: deleting one
// dead instruction can make its operands dead too, but only after the use
// chain is severed.
//
// This is the *forward* style. ADCE (aggressive DCE) does the opposite --
// assume everything is dead, walk back from terminators marking live --
// which catches more cases but needs more machinery.
class DCEPass : public llvm::PassInfoMixin<DCEPass> {
public:
    llvm::PreservedAnalyses run(llvm::Function& F,
                                llvm::FunctionAnalysisManager& FAM);
};

}  // namespace ml
