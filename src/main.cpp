// minilang driver. Reads source from stdin, lex+parse+codegen, then either
// pretty-prints the resulting LLVM IR or executes it via the ORC JIT.
//
// Top-level loop is the same shape as the LLVM Kaleidoscope tutorial. The
// interesting bits are:
//   - we run a custom optimisation pipeline (-my-passes) before either
//     emitting or executing, so you can see the effect of ConstFold/DCE/LICM
//     on real input;
//   - each top-level expression is compiled into its own anonymous function
//     and added to the JIT inside its own ResourceTracker, so we can release
//     it immediately afterwards (top-level exprs aren't reachable from named
//     definitions, so keeping them around is just memory pressure).

#include "ast.h"
#include "codegen.h"
#include "lexer.h"
#include "parser.h"

#include "passes/ConstFoldPass.h"
#include "passes/DCEPass.h"
#include "passes/LICMPass.h"

#include "jit/KaleidoscopeJIT.h"

#include "llvm/ExecutionEngine/Orc/ThreadSafeModule.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/LoopSimplify.h"
#include "llvm/Transforms/Utils/Mem2Reg.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

using namespace llvm;
using namespace llvm::orc;

namespace {

enum class Mode  { JIT, EmitLLVM };
enum class OptL  { O0, O2, MyPasses, MyPassesO2 };

struct Options {
    Mode mode    = Mode::JIT;
    OptL opt     = OptL::O0;
    bool dump_ast = false;  // currently a no-op; reserved for future
    bool quiet    = false;  // suppress JIT result prints (used by tests)
};

void print_usage() {
    std::fputs(
        "minilang [flags] < source.ml\n"
        "\n"
        "  -emit-llvm     Emit LLVM IR to stdout (no execution)\n"
        "  -jit           Execute via ORC JIT (default)\n"
        "  -O0            No optimisations\n"
        "  -O2            Standard LLVM -O2 pipeline\n"
        "  -my-passes     Run only our three custom passes (mem2reg, LoopSimplify,\n"
        "                 ConstFoldPass, DCEPass, LICMPass)\n"
        "  -my-passes-O2  Custom passes, then standard -O2\n"
        "  -dump-ast      (reserved)\n"
        "  -q, --quiet    Don't print top-level expression results\n"
        "  -h, --help     This message\n",
        stderr);
}

Options parse_args(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if      (a == "-emit-llvm")    o.mode = Mode::EmitLLVM;
        else if (a == "-jit")          o.mode = Mode::JIT;
        else if (a == "-O0")           o.opt  = OptL::O0;
        else if (a == "-O2")           o.opt  = OptL::O2;
        else if (a == "-my-passes")    o.opt  = OptL::MyPasses;
        else if (a == "-my-passes-O2") o.opt  = OptL::MyPassesO2;
        else if (a == "-dump-ast")     o.dump_ast = true;
        else if (a == "-q" || a == "--quiet") o.quiet = true;
        else if (a == "-h" || a == "--help") { print_usage(); std::exit(0); }
        else { std::fprintf(stderr, "unknown flag: %s\n", a.c_str()); std::exit(2); }
    }
    return o;
}

// Build the optimisation pipeline corresponding to `o.opt`. Returns a
// ModulePassManager you can call .run(M, MAM) on.
ModulePassManager build_pipeline(OptL opt,
                                 PassBuilder& PB,
                                 ModuleAnalysisManager& MAM) {
    if (opt == OptL::O0) return ModulePassManager();
    if (opt == OptL::O2) return PB.buildPerModuleDefaultPipeline(OptimizationLevel::O2);

    // Custom pipeline. mem2reg first because mutable variables in our codegen
    // emit alloca/load/store patterns -- without mem2reg, ConstFold sees no
    // ConstantFP* operands and does nothing. LoopSimplify next so LICM has
    // canonical loops to work with.
    FunctionPassManager FPM;
    FPM.addPass(PromotePass());        // mem2reg
    FPM.addPass(LoopSimplifyPass());   // canonical preheader / single backedge
    FPM.addPass(ml::ConstFoldPass());
    FPM.addPass(ml::DCEPass());
    FPM.addPass(ml::LICMPass());

    ModulePassManager MPM;
    MPM.addPass(createModuleToFunctionPassAdaptor(std::move(FPM)));

    if (opt == OptL::MyPassesO2) {
        // Tack the standard -O2 pipeline on after our custom passes. Useful
        // for sanity-checking that we didn't break anything LLVM expects.
        MPM.addPass(PB.buildPerModuleDefaultPipeline(OptimizationLevel::O2));
    }
    return MPM;
}

}  // namespace

int main(int argc, char** argv) {
    Options opt = parse_args(argc, argv);

    InitializeNativeTarget();
    InitializeNativeTargetAsmPrinter();
    InitializeNativeTargetAsmParser();

    // JIT is created lazily so -emit-llvm doesn't pay startup cost.
    std::unique_ptr<KaleidoscopeJIT> jit;
    if (opt.mode == Mode::JIT) {
        auto j = KaleidoscopeJIT::Create();
        if (!j) {
            errs() << "fatal: " << toString(j.takeError()) << "\n";
            return 1;
        }
        jit = std::move(*j);
    }

    ml::Lexer  lexer;
    ml::Parser parser(lexer);
    ml::Codegen codegen;

    // Helper: spin up a fresh new-PM analysis stack and run the configured
    // pipeline on the current codegen module. Used by both definition and
    // top-level expr paths.
    auto run_pipeline = [&]() {
        PassBuilder PB;
        LoopAnalysisManager LAM;
        FunctionAnalysisManager FAM;
        CGSCCAnalysisManager CGAM;
        ModuleAnalysisManager MAM;
        PB.registerModuleAnalyses(MAM);
        PB.registerCGSCCAnalyses(CGAM);
        PB.registerFunctionAnalyses(FAM);
        PB.registerLoopAnalyses(LAM);
        PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);
        ModulePassManager MPM = build_pipeline(opt.opt, PB, MAM);
        MPM.run(codegen.module(), MAM);
    };

    // For -emit-llvm we accumulate everything in one module and print at EOF.
    // For -jit each top-level item gets its own module that we hand to ORC.
    auto handle_function = [&](std::unique_ptr<ml::FunctionAST> fn) {
        if (!fn) return;
        if (!fn->codegen(codegen)) {
            std::fprintf(stderr, "(codegen failed)\n");
            if (opt.mode == Mode::JIT) codegen.reset();
            return;
        }
        if (opt.mode == Mode::JIT) {
            codegen.module().setDataLayout(jit->getDataLayout());
            run_pipeline();
            // Force evaluation order: module first, then context.
            auto m   = codegen.take_module();
            auto c   = codegen.take_context();
            auto tsm = ThreadSafeModule(std::move(m), std::move(c));
            if (auto err = jit->addModule(std::move(tsm))) {
                errs() << toString(std::move(err)) << "\n";
            }
            codegen.reset();
        }
    };

    // top-level expression handler. Wraps the expression in an anonymous
    // function, codegens, optimises, then either prints or JIT-runs.
    int anon_counter = 0;
    auto handle_top = [&](std::unique_ptr<ml::FunctionAST> fn) {
        if (!fn) return;
        Function* ir_fn = fn->codegen(codegen);
        if (!ir_fn) {
            if (opt.mode == Mode::JIT) codegen.reset();
            return;
        }
        if (opt.mode == Mode::JIT) {
            std::string anon_name = ir_fn->getName().str();

            run_pipeline();

            // The JIT's DataLayout must match the module -- without this, the
            // CompileLayer happily compiles the IR but the resulting symbols
            // are mangled differently and lookup fails. Cost me an evening
            // the first time.
            codegen.module().setDataLayout(jit->getDataLayout());

            // Debug: dump module before adding to JIT (controlled by env var).
            if (std::getenv("ML_DUMP_IR")) {
                errs() << "=== module before JIT add ===\n";
                codegen.module().print(errs(), nullptr);
            }

            // Anonymous expressions go on their own ResourceTracker so we can
            // free them right after we call them -- otherwise we'd leak a
            // module per ';' line.
            auto rt  = jit->getMainJITDylib().createResourceTracker();
            // Force evaluation order: module first, then context.
            auto m   = codegen.take_module();
            auto c   = codegen.take_context();
            auto tsm = ThreadSafeModule(std::move(m), std::move(c));
            if (auto err = jit->addModule(std::move(tsm), rt)) {
                errs() << toString(std::move(err)) << "\n";
                codegen.reset();
                return;
            }

            auto sym = jit->lookup(anon_name);
            if (!sym) {
                errs() << toString(sym.takeError()) << "\n";
                codegen.reset();
                return;
            }

            using AnonFn = double (*)();
            auto* fp = sym->getAddress().toPtr<AnonFn>();
            double result = fp();
            if (!opt.quiet) std::printf("%.6g\n", result);

            cantFail(rt->remove());
            codegen.reset();
        }
        // else: -emit-llvm mode keeps everything in the same module. We
        // print once at EOF.
    };

    // Drive the parser. The grammar lets us see four shapes at top level:
    //   def NAME(...) expr ;
    //   extern NAME(...) ;
    //   <expr> ;
    //   <eof>
    // Parser primes its own lookahead in the constructor.

    while (true) {
        switch (parser.current()) {
            case ml::tok_eof:
                if (opt.mode == Mode::EmitLLVM) {
                    // Run the optimisation pipeline once over the accumulated
                    // module before printing.
                    PassBuilder PB;
                    LoopAnalysisManager LAM;
                    FunctionAnalysisManager FAM;
                    CGSCCAnalysisManager CGAM;
                    ModuleAnalysisManager MAM;
                    PB.registerModuleAnalyses(MAM);
                    PB.registerCGSCCAnalyses(CGAM);
                    PB.registerFunctionAnalyses(FAM);
                    PB.registerLoopAnalyses(LAM);
                    PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);
                    ModulePassManager MPM = build_pipeline(opt.opt, PB, MAM);
                    MPM.run(codegen.module(), MAM);
                    codegen.print_module();
                }
                return 0;
            case ';':
                parser.advance();  // eat trailing/leading semis
                break;
            case ml::tok_def: {
                auto fn = parser.parse_definition();
                if (fn) handle_function(std::move(fn));
                else if (parser.current() != ml::tok_eof) parser.advance();
                break;
            }
            case ml::tok_extern: {
                auto p = parser.parse_extern();
                if (p) {
                    p->codegen(codegen);
                    codegen.remember_proto(std::move(p));
                } else if (parser.current() != ml::tok_eof) {
                    parser.advance();
                }
                break;
            }
            default: {
                std::string anon = "__anon_expr_" + std::to_string(anon_counter++);
                auto fn = parser.parse_top_level_expr(anon);
                if (fn) handle_top(std::move(fn));
                else if (parser.current() != ml::tok_eof) parser.advance();
                break;
            }
        }
    }
}
