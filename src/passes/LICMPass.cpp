#include "passes/LICMPass.h"

#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/LoopAnalysisManager.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"

using namespace llvm;

namespace ml {

static bool is_loop_invariant(const Instruction& I, const Loop* L) {
    for (const Value* op : I.operands()) {
        if (auto* op_inst = dyn_cast<Instruction>(op)) {
            // If any operand is *defined inside* this loop, the instruction
            // can't be hoisted. Constants, function args, and instructions
            // defined in dominating blocks are all loop-invariant.
            if (L->contains(op_inst->getParent())) return false;
        }
    }
    return true;
}

static bool safe_to_hoist(const Instruction& I) {
    return !I.mayHaveSideEffects()
        && !I.isTerminator()
        && !isa<PHINode>(I)
        && !isa<LandingPadInst>(I);
}

bool LICMPass::process_loop(Loop* L) {
    bool changed = false;

    // Innermost-first. Hoisting from an inner loop into its preheader can
    // make the result invariant for the outer loop too -- processing inside
    // out lets that opportunity propagate without a second pass.
    for (Loop* sub : L->getSubLoops()) changed |= process_loop(sub);

    BasicBlock* preheader = L->getLoopPreheader();
    if (!preheader) return changed;  // multi-entry loop; LoopSimplify didn't run

    SmallVector<Instruction*, 8> to_hoist;
    for (BasicBlock* BB : L->blocks()) {
        for (Instruction& I : *BB) {
            if (is_loop_invariant(I, L) && safe_to_hoist(I)) {
                to_hoist.push_back(&I);
            }
        }
    }

    // moveBefore() preserves use-def edges; we just relocate the instruction
    // to the end of the preheader (immediately before its terminator branch).
    for (Instruction* I : to_hoist) {
        I->moveBefore(preheader->getTerminator());
        changed = true;
    }
    return changed;
}

PreservedAnalyses LICMPass::run(Function& F, FunctionAnalysisManager& FAM) {
    auto& LI = FAM.getResult<LoopAnalysis>(F);

    bool changed = false;
    for (Loop* L : LI) changed |= process_loop(L);

    if (!changed) return PreservedAnalyses::all();

    // We rearranged instructions but didn't change the CFG. Loop info, DT,
    // and DomTree are technically still valid; the new pass manager has a
    // helper for that.
    PreservedAnalyses pa;
    pa.preserve<DominatorTreeAnalysis>();
    pa.preserve<LoopAnalysis>();
    return pa;
}

}  // namespace ml
