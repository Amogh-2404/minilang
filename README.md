# MiniLang

A small statically-typed language compiled to native code via LLVM 18, with
hand-written optimisation passes.

```ml
def fib(n)
    if n < 2 then n
    else fib(n - 1) + fib(n - 2);

fib(10);
```

```
$ cmake -G Ninja -B build && ninja -C build
$ echo "fib(10);" | ./build/minilang -jit
55
```

## Why this exists

This is a teaching compiler. It is small enough to read end-to-end in an
afternoon but exercises the real LLVM 18 toolchain: the IRBuilder, the new
pass manager, the ORC v2 JIT, and three custom analysis/transform passes
written from scratch. The comments explain *why*, not just *what* -- so
the source can stand in as a worked example of `PassInfoMixin` and the
new-PM analysis-preservation contract.

The language itself is modelled on the LLVM Kaleidoscope tutorial -- f64 is
the only type, control flow is expression-oriented, mutable variables are
introduced via `var ... in`. Where MiniLang differs:

- Comma-separated parameter lists (the tutorial uses whitespace).
- Header-then-body for-loop CFG (the tutorial places the test after the
  body, which means the loop runs at least once even if the initial
  condition is false).
- Three custom passes that are wired through `-my-passes` and have FileCheck
  integration tests.

## Architecture

```
.ml source -> Lexer -> Parser -> AST -> Codegen -> LLVM IR -> Pass Manager -> ORC JIT (or text emit)
```

| Component        | Files                                              |
|------------------|----------------------------------------------------|
| Lexer            | `include/lexer.h`, `src/lexer.cpp`                 |
| Parser           | `include/parser.h`, `src/parser.cpp`               |
| AST              | `include/ast.h`                                    |
| Codegen          | `include/codegen.h`, `src/codegen.cpp`             |
| JIT              | `include/jit/KaleidoscopeJIT.h` (vendored, LLVM 18)|
| Custom passes    | `include/passes/*.h`, `src/passes/*.cpp`           |
| Driver           | `src/main.cpp`                                     |

A more detailed write-up lives in [`docs/architecture.md`](docs/architecture.md);
the grammar is in [`docs/grammar.md`](docs/grammar.md).

## Custom optimisation passes

All three are `FunctionPass`es on the **new** pass manager
(`PassInfoMixin<...>`). The legacy PM is deprecated in LLVM 18; we don't use
it anywhere.

| Pass            | Source                                       | What it does                                                      |
|-----------------|----------------------------------------------|-------------------------------------------------------------------|
| `ConstFoldPass` | [`src/passes/ConstFoldPass.cpp`](src/passes/ConstFoldPass.cpp) | Folds binary FP ops and `fcmp` whose operands are both `ConstantFP`. |
| `DCEPass`       | [`src/passes/DCEPass.cpp`](src/passes/DCEPass.cpp)             | Trivial dead-code elimination iterated to a fixpoint.             |
| `LICMPass`      | [`src/passes/LICMPass.cpp`](src/passes/LICMPass.cpp)           | Hoists loop-invariant instructions into the preheader.            |

Each pass has a one-paragraph theory note in its source file plus a
FileCheck integration test under `tests/integration/`.

### Pipeline composition

| Flag              | Pipeline                                                                             |
|-------------------|--------------------------------------------------------------------------------------|
| `-O0`             | (nothing)                                                                            |
| `-O2`             | `PassBuilder::buildPerModuleDefaultPipeline(O2)`                                     |
| `-my-passes`      | `mem2reg` → `LoopSimplify` → `ConstFoldPass` → `DCEPass` → `LICMPass`                |
| `-my-passes-O2`   | custom passes, then standard `-O2`                                                   |

`mem2reg` runs first because `var x = ...` codegens an
`alloca` + `store` + `load` triple. Without `mem2reg`, `ConstFoldPass` sees
no `ConstantFP` operands and does nothing. `LoopSimplify` canonicalises
loops into single-preheader form so `LICMPass` has a unique place to hoist
to.

## Results

Proof the passes actually do something. IR line counts, `-O0` vs `-my-passes`
(lower is better):

| Example                    | -O0 | -my-passes |     Δ |
|----------------------------|----:|-----------:|------:|
| `examples/01_arith.ml`     |  18 |         14 |  −22% |
| `examples/02_fib.ml`       |  37 |         31 |  −16% |
| `examples/03_loop.ml`      |  44 |         30 |  −32% |
| `examples/04_dead.ml`      |  21 |         14 |  −33% |
| `examples/05_invariant.ml` |  49 |         30 |  −39% |

Apple M4, LLVM 18.1.8, Release build. Where the savings come from (mem2reg +
DCE deleting unread bindings, LICM hoisting `fmul x, y` out of the inner
loop, ConstFold collapsing `2.0 * 3.0` after mem2reg lifts the constants
into SSA), plus wall-clock numbers and a `n = 10_000_000` LICM win, are
in [`BENCHMARKS.md`](BENCHMARKS.md).

## Building

Tested on macOS 14 (Apple Silicon) and Ubuntu 22.04. Requirements:

- CMake 3.20+
- Ninja
- LLVM 18 (`brew install llvm@18` or `apt install llvm-18-dev`)
- GoogleTest (optional; enables unit tests)

```
cmake -G Ninja -B build \
    -DLLVM_DIR=$(brew --prefix llvm@18)/lib/cmake/llvm \
    -DCMAKE_BUILD_TYPE=Release
ninja -C build
./tests/run_all.sh
```

On Linux drop the `-DLLVM_DIR=...`; the apt package installs to the default
search path.

## CLI

```
minilang [flags] < source.ml

  -emit-llvm     Emit LLVM IR to stdout (no execution)
  -jit           Execute via ORC JIT (default)
  -O0            No optimisations
  -O2            Standard LLVM -O2 pipeline
  -my-passes     Run only our three custom passes (mem2reg, LoopSimplify,
                 ConstFoldPass, DCEPass, LICMPass)
  -my-passes-O2  Custom passes, then standard -O2
  -q, --quiet    Don't print top-level expression results
  -h, --help     Print usage
```

## Theory it touches

- Recursive descent + precedence climbing for parsing.
- SSA construction via `mem2reg` lifting `alloca`/`store`/`load`.
- Dominator tree, dataflow liveness (implicit, through the new-PM analysis
  manager).
- Natural loops, dominance frontiers, loop preheaders.
- The new pass manager's analysis-preservation contract.

## What this does **not** do

- Integer types, strings, or any user-defined type. Everything is `f64`.
- A real type-checker. Type errors surface as `verifyFunction` failures.
- Garbage collection, modules, generics, error messages with line numbers.
- AOT compilation to an object file. v0.1 is JIT-only (or `-emit-llvm`).
- Aggressive constant folding -- LLVM's `IRBuilder` already folds at
  build time, and `InstCombine` handles the rest. Our `ConstFoldPass` is
  the small readable version.
- Production-grade LICM (alias analysis, sinking, exception safety).
  LLVM's `LICM.cpp` is ~1000 lines; ours is ~50.

See `docs/architecture.md` for the full "future work" list.

## References

- The [LLVM Kaleidoscope tutorial](https://llvm.org/docs/tutorial/) -- the
  parsing and basic codegen are lifted nearly verbatim from chapters 2-7.
- LLVM's [Writing An LLVM New PM Pass](https://llvm.org/docs/WritingAnLLVMNewPMPass.html).
- Cooper & Torczon, *Engineering a Compiler* (2nd ed.), chapters 8 and 10
  for SSA, dataflow, and loop optimisations.
- Cytron, Ferrante, Rosen, Wegman, Zadeck (1991), *Efficiently computing
  static single assignment form and the control dependence graph* -- the
  original SSA paper.

## License

MIT. See `LICENSE`.

The vendored `include/jit/KaleidoscopeJIT.h` retains its upstream
Apache-2.0-with-LLVM-exception licence as required.
