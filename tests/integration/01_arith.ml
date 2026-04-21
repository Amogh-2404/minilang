// Integration test: ConstFoldPass folds 2.0 * 3.0 to 6.0 once mem2reg has
// lifted the stored constants out of the alloca.
//
// RUN: %minilang -emit-llvm -my-passes < %s | FileCheck %s

def cf()
    var a = 2.0 in
    var b = 3.0 in
    a * b;

cf();

// CHECK-LABEL: define double @cf
// CHECK: ret double 6.000000e+00
