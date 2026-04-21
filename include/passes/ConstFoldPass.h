#pragma once

#include "llvm/IR/PassManager.h"

namespace ml {

// Folds binary FP ops and FP comparisons whose operands are both ConstantFPs.
// We do this by hand (rather than relying on InstCombine) for two reasons:
// 1. It's a small, readable example of the new-PM pass interface.
// 2. It lets us demonstrate the "snapshot then erase" pattern that anyone
//    who's tried to delete an instruction mid-iteration learns the hard way.
//
// Operations folded:
//   fadd, fsub, fmul, fdiv (skipped if rhs == 0.0 -- runtime trap is more
//   informative than a silent NaN/Inf), fcmp (olt/ogt/oeq/one/ole/oge/ult/...).
class ConstFoldPass : public llvm::PassInfoMixin<ConstFoldPass> {
public:
    llvm::PreservedAnalyses run(llvm::Function& F,
                                llvm::FunctionAnalysisManager& FAM);
};

}  // namespace ml
