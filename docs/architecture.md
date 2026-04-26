# MiniLang -- Architecture

This document is for someone reading the source for the first time. It is
not a spec.

## Pipeline

```
.ml                     UTF-8 source on stdin
  |
  v   src/lexer.cpp
Lexer                   getchar() -> Token stream
  |   include/lexer.h
  v   src/parser.cpp
Parser                  recursive descent + precedence climbing
  |   include/parser.h
  v   include/ast.h
AST                     ExprAST hierarchy + FunctionAST
  |   src/codegen.cpp
  v
Codegen                 IRBuilder<>, named_values_, prototype memory
  |
  v   src/main.cpp + LLVM
Pass Manager            mem2reg + LoopSimplify + (custom passes | -O2)
  |
  +--- -emit-llvm ---> stdout
  |
  v   include/jit/KaleidoscopeJIT.h
ORC JIT                 add module, lookup symbol, call as f64()->f64
  |
  v
top-level result -> stdout (-jit mode)
```

## File map

| Path                                            | Role                                          |
|-------------------------------------------------|-----------------------------------------------|
| `include/lexer.h` + `src/lexer.cpp`             | Tokeniser with two-line-comment styles.       |
| `include/parser.h` + `src/parser.cpp`           | Recursive-descent + precedence climbing.      |
| `include/ast.h`                                 | AST hierarchy. Each node has a `codegen()`.   |
| `include/codegen.h` + `src/codegen.cpp`         | LLVM IR emission. Owns context/module/builder.|
| `include/jit/KaleidoscopeJIT.h`                 | Vendored ORC JIT (LLVM 18 example, trimmed).  |
| `include/passes/*.h` + `src/passes/*.cpp`       | Three custom new-PM passes.                   |
| `src/main.cpp`                                  | Driver: CLI parsing, top-level loop, JIT add. |
| `tests/unit/*.cpp`                              | GoogleTest unit tests.                        |
| `tests/integration/*.ml`                        | FileCheck IR-level integration tests.         |
| `tests/e2e/*.ml` + `*.out`                      | End-to-end stdout-diff tests.                 |
| `tests/run_all.sh`                              | Runs all three test types.                    |
| `examples/*.ml`                                 | Five canonical examples (also run by CI).     |
| `.github/workflows/ci.yml`                      | Ubuntu+macOS, Debug+Release matrix.           |

## Codegen details worth knowing

### Anonymous wrapping

Top-level expressions are wrapped in unique `__anon_expr_N` functions so the
JIT has something concrete to look up, call, and free. Each wrapper lives
in its own module and on its own `ResourceTracker`, which we drop right
after invocation.

### Prototype memory

`Codegen::protos_` survives `reset()`. Why: each top-level item compiles
into a fresh module, but the JITDylib keeps definitions across modules.
When a fresh module's IR references `square`, we need to forward-declare
`square` in that module so the IR verifies; the prototype map gives us the
arity/signature without re-parsing.

### Mutable variables

`var x = init in body` codegens an `alloca` in the entry block, a `store`
of `init`, and treats reads of `x` as `load`. Assignments (`x = e`) stay as
`store` instructions. `mem2reg` then promotes the alloca to SSA so the
custom passes downstream see plain registers.

### For-loops

Header-then-body CFG:

```
preheader  -> header
header     : cond = end-expr; br cond, body, after
body       : body-expr; i = i + step; br header
after      : ...
```

The Kaleidoscope tutorial does body-then-test (i.e. `do { ... } while`).
That loop runs the body at least once, even when the initial condition is
false, which makes recursive loops over empty ranges misbehave. The
header-first layout matches what `LoopSimplify` expects to canonicalise.

### Comparisons return f64

There's only one type, so `<` / `>` lower to `fcmp` followed by `uitofp`
back to `f64` (1.0 for true, 0.0 for false). It's wasteful at -O0 but
`InstCombine` (and our own `ConstFoldPass`) eliminate the round-trip when
the result is consumed immediately.

## What v0.1 deliberately does **not** do

- Integer / boolean types as first-class. Booleans are `f64` (0.0 / non-zero).
- A real type checker. We rely on `verifyFunction` to catch the worst.
- AOT compilation. Adding an `-o foo.o` mode means hooking up a
  `TargetMachine` and emitting object code; it's two days of plumbing and
  not interesting from a teaching standpoint.
- Common-subexpression elimination, function inlining, loop unrolling --
  all useful, all big enough to be their own project.
- Diagnostics with line/column information. `error: expected ')'` is the
  best we do.
- Garbage collection. There's nothing to collect.
- Modules / namespaces. The whole program lives in one symbol table.

## Future work

1. Integer types (`i64` first, then signed/unsigned arithmetic) -- needs
   real type inference, not just defaulting everything to `f64`.
2. CSE pass over a basic-block at a time, keyed by op + operand identity.
3. Function inlining for small (< N IR insts) leaf functions.
4. A real diagnostic system: file/line tracking in the lexer, propagated
   through every AST node.
5. Object-file emission via `LLVMTargetMachine::addPassesToEmitFile`.
6. ELF backend on Linux + Mach-O on macOS for AOT (the JIT already does
   both, but as runtime image, not on-disk).
