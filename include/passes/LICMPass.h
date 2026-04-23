#pragma once

#include "llvm/IR/PassManager.h"

namespace llvm { class Loop; }

namespace ml {

// Loop-Invariant Code Motion -- the textbook version. For each natural loop
// with a unique preheader, hoist instructions whose operands are all defined
// outside the loop and which have no side effects.
//
// LLVM's production LICM (~1000 lines, in lib/Transforms/Scalar/LICM.cpp)
// also handles aliasing of loads/stores, sinking, must-execute analysis,
// invariant-load promotion, and exception safety. We don't. The point of
// this pass is pedagogy.
//
// Requires LoopAnalysis. LoopSimplify in front of us guarantees loops are in
// canonical form (single preheader, single backedge, dedicated exits).
class LICMPass : public llvm::PassInfoMixin<LICMPass> {
public:
    llvm::PreservedAnalyses run(llvm::Function& F,
                                llvm::FunctionAnalysisManager& FAM);

private:
    // Recursive driver. Returns true if it hoisted anything.
    bool process_loop(llvm::Loop* L);
};

}  // namespace ml
