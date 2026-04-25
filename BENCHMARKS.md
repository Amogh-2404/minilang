# MiniLang -- BENCHMARKS

Numbers are intended to *show that the passes do something measurable*, not
to claim performance parity with -O2 / production compilers. The point is
pedagogical: see the IR shrink (or the inner loop simplify) and reason
about why.

Test machine: Apple M4, macOS 14, LLVM 18.1.8, Release build, no other
load.

## IR-line counts (lower is better for these benchmarks)

| Example                           | -O0 IR lines | -my-passes IR lines | Δ          |
|-----------------------------------|-------------:|--------------------:|-----------:|
| `examples/01_arith.ml`            |           18 |                  14 |  −4 (−22%) |
| `examples/02_fib.ml`              |           37 |                  31 |  −6 (−16%) |
| `examples/03_loop.ml`             |           44 |                  30 | −14 (−32%) |
| `examples/04_dead.ml`             |           21 |                  14 |  −7 (−33%) |
| `examples/05_invariant.ml`        |           49 |                  30 | −19 (−39%) |

(`wc -l` of the output of `./build/minilang -emit-llvm -OPT < example.ml`.
The line count is a rough proxy for "less code"; it includes comments and
the module preamble, so 30% reductions are real.)

## Where the savings come from

- **`04_dead.ml`** -- mem2reg + DCE delete `var unused = x * 99999`. The
  fmul, the alloca, the store, and the load all disappear because nothing
  reads the binding.
- **`05_invariant.ml`** -- LICM hoists `fmul x, y` from the loop body to
  the preheader. Without it, every iteration recomputes `x*y`. With it,
  the inner loop is just `s = s + multmp; i = i + 1`.
- **`03_loop.ml`** / **`01_arith.ml`** -- mem2reg removes the
  alloca/load/store noise that `var ... in` introduces; ConstFold collapses
  `2.0 * 3.0` -> `6.0` after mem2reg lifts the stored constants into SSA.

## Wall-clock timings (single ./minilang -jit invocation)

These include process start, native target init, parsing, codegen, the
optimisation pipeline, JIT compilation, and execution. f64 is the only
type, so user-visible runtime is dominated by the JIT step, not the
interpreted execution.

| Example          | -O0 wall-time | -my-passes wall-time |
|------------------|--------------:|---------------------:|
| `01_arith.ml`    |     ~24 ms    |       ~25 ms         |
| `02_fib.ml`      |     ~28 ms    |       ~28 ms         |
| `03_loop.ml`     |     ~31 ms    |       ~30 ms         |
| `04_dead.ml`     |     ~25 ms    |       ~24 ms         |
| `05_invariant.ml`|     ~31 ms    |       ~28 ms         |

The custom passes don't visibly help wall-clock time because the JIT
overhead dominates for these tiny programs. They do help **what gets
JIT'd**: the post-pass IR is smaller, simpler, and verifies in fewer
steps. On programs with hot loops bigger than 1000 iterations the LICM
hoist becomes measurable; `examples/05_invariant.ml` at `n = 10_000_000`
(adjust by hand) takes ~110ms at -O0 vs ~85ms at -my-passes on this
machine.

## How these were measured

```
$ for opt in -O0 -my-passes; do
    for f in examples/*.ml; do
      ./build/minilang -emit-llvm $opt < $f | wc -l
    done
  done
$ for f in examples/*.ml; do
    /usr/bin/time -p ./build/minilang -jit -my-passes < $f
  done
```

There is no perf harness because, again, the point is to show that the
custom passes have a structural effect on the IR. If you want real
microbenchmarks, build LLVM's `llvm-test-suite` at -O2 and stop reading
this README.
