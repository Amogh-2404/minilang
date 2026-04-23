// Integration test: LICMPass hoists `x * y` out of the loop body into the
// preheader (the `entry` block in this codegen).
//
// RUN: %minilang -emit-llvm -my-passes < %s | FileCheck %s

def hot(n, x, y)
    var s = 0.0 in
    var dummy = (for i = 0, i < n, 1.0 in s = s + (x * y)) in
    s;

hot(1000, 3, 4);

// We expect the multiply in `entry` (the preheader) and *not* in
// `loop.body`. FileCheck's CHECK-LABEL keeps the next CHECK / CHECK-NOT
// search inside the next labeled region.
//
// CHECK-LABEL: define double @hot
// CHECK-LABEL: entry:
// CHECK: fmul double %x, %y
// CHECK-LABEL: loop.body:
// CHECK-NOT: fmul
// CHECK-LABEL: loop.after:
