// Integration test: DCEPass deletes the unused multiply after mem2reg
// lifts the alloca/store/load triple out of the way.
//
// RUN: %minilang -emit-llvm -my-passes < %s | FileCheck %s

def f(x)
    var unused = x * 99999 in
    x + 1;

f(5);

// The fmul disappears; only the fadd remains.
// CHECK-LABEL: define double @f
// CHECK-NOT: fmul
// CHECK: fadd double %x, 1.000000e+00
