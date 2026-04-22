#include "passes/DCEPass.h"

#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"

using namespace llvm;

namespace ml {

PreservedAnalyses DCEPass::run(Function& F, FunctionAnalysisManager&) {
    bool ever_changed = false;
    bool changed = true;

    // Fixpoint loop. Why? Deleting %1 = fmul %x, 999.0 makes %2 = fadd %1, 0.0
    // dead too, but only after %1 is gone. A single forward sweep would miss
    // %2; the second sweep catches it.
    while (changed) {
        changed = false;
        SmallVector<Instruction*, 16> dead;

        for (BasicBlock& BB : F) {
            for (Instruction& I : BB) {
                // What counts as "side effect" per llvm::Instruction:
                //   - call to non-readonly/readnone function
                //   - any store, volatile load, atomic, fence
                //   - terminators (handled separately below)
                // mayHaveSideEffects() bundles all of those.
                if (I.use_empty() && !I.isTerminator() && !I.mayHaveSideEffects()) {
                    dead.push_back(&I);
                }
            }
        }

        for (Instruction* I : dead) {
            I->eraseFromParent();
            changed = true;
            ever_changed = true;
        }
    }

    return ever_changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

}  // namespace ml
