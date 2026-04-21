#include "passes/ConstFoldPass.h"

#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"

using namespace llvm;

namespace ml {

namespace {

// Try to fold a BinaryOperator with two ConstantFP operands. Returns null if
// not foldable (different opcode, divide-by-zero, etc.).
Constant* fold_binop(BinaryOperator& bo) {
    auto* L = dyn_cast<ConstantFP>(bo.getOperand(0));
    auto* R = dyn_cast<ConstantFP>(bo.getOperand(1));
    if (!L || !R) return nullptr;

    APFloat lv = L->getValueAPF();
    APFloat rv = R->getValueAPF();
    APFloat result = lv;  // copy so we have the right semantics

    // rmNearestTiesToEven matches what -ffast-math-free codegen would do.
    const auto rm = APFloat::rmNearestTiesToEven;

    switch (bo.getOpcode()) {
        case Instruction::FAdd: result.add(rv, rm);      break;
        case Instruction::FSub: result.subtract(rv, rm); break;
        case Instruction::FMul: result.multiply(rv, rm); break;
        case Instruction::FDiv:
            // Don't fold div-by-zero. Letting the runtime produce inf/nan is
            // technically valid, but folding it at compile time hides what's
            // almost always a bug.
            if (rv.isZero()) return nullptr;
            result.divide(rv, rm);
            break;
        default:
            return nullptr;
    }
    return ConstantFP::get(bo.getContext(), result);
}

// Try to fold an FCmpInst with two ConstantFP operands into an i1 constant.
Constant* fold_fcmp(FCmpInst& cmp) {
    auto* L = dyn_cast<ConstantFP>(cmp.getOperand(0));
    auto* R = dyn_cast<ConstantFP>(cmp.getOperand(1));
    if (!L || !R) return nullptr;

    APFloat::cmpResult ord = L->getValueAPF().compare(R->getValueAPF());
    bool unordered = (ord == APFloat::cmpUnordered);
    bool result    = false;

    // 'O' predicates are *false* if either operand is NaN; 'U' predicates
    // are *true* in that case. We only fold when neither operand is NaN
    // (cheap to fold, and covers the cases minilang actually emits).
    switch (cmp.getPredicate()) {
        case FCmpInst::FCMP_OEQ: result = !unordered && ord == APFloat::cmpEqual; break;
        case FCmpInst::FCMP_OGT: result = !unordered && ord == APFloat::cmpGreaterThan; break;
        case FCmpInst::FCMP_OGE: result = !unordered && (ord == APFloat::cmpGreaterThan
                                                       || ord == APFloat::cmpEqual); break;
        case FCmpInst::FCMP_OLT: result = !unordered && ord == APFloat::cmpLessThan; break;
        case FCmpInst::FCMP_OLE: result = !unordered && (ord == APFloat::cmpLessThan
                                                       || ord == APFloat::cmpEqual); break;
        case FCmpInst::FCMP_ONE: result = !unordered && ord != APFloat::cmpEqual; break;
        case FCmpInst::FCMP_UEQ: result =  unordered || ord == APFloat::cmpEqual; break;
        case FCmpInst::FCMP_UGT: result =  unordered || ord == APFloat::cmpGreaterThan; break;
        case FCmpInst::FCMP_UGE: result =  unordered || (ord == APFloat::cmpGreaterThan
                                                       || ord == APFloat::cmpEqual); break;
        case FCmpInst::FCMP_ULT: result =  unordered || ord == APFloat::cmpLessThan; break;
        case FCmpInst::FCMP_ULE: result =  unordered || (ord == APFloat::cmpLessThan
                                                       || ord == APFloat::cmpEqual); break;
        case FCmpInst::FCMP_UNE: result =  unordered || ord != APFloat::cmpEqual; break;
        default:
            return nullptr;  // ordered/unordered constant predicates unhandled
    }
    return ConstantInt::getBool(cmp.getContext(), result);
}

}  // namespace

PreservedAnalyses ConstFoldPass::run(Function& F, FunctionAnalysisManager&) {
    bool changed = false;

    for (BasicBlock& BB : F) {
        // We collect (instruction, replacement) pairs first and only mutate
        // afterwards. Erasing during iteration invalidates the iterator;
        // this is the *one* thing every new-PM pass author trips over.
        SmallVector<std::pair<Instruction*, Constant*>, 16> work;

        for (Instruction& I : BB) {
            Constant* folded = nullptr;
            if (auto* bo  = dyn_cast<BinaryOperator>(&I)) folded = fold_binop(*bo);
            else if (auto* cmp = dyn_cast<FCmpInst>(&I))   folded = fold_fcmp(*cmp);

            if (folded) work.emplace_back(&I, folded);
        }

        for (auto& [I, C] : work) {
            I->replaceAllUsesWith(C);
            I->eraseFromParent();
            changed = true;
        }
    }

    // We didn't touch CFG / dominators / loop info, so we *could* declare
    // those preserved -- but the conservative answer is none() if anything
    // changed, since downstream passes can re-analyse cheaply.
    return changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

}  // namespace ml
