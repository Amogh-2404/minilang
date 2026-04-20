// 05_invariant.ml -- LICM target. (x*y) does not depend on the induction
// variable, so a smart pass can hoist it out of the loop entirely.

def hot(n, x, y)
    var s = 0.0 in
    var dummy = (for i = 0, i < n, 1.0 in s = s + (x * y)) in
    s;

hot(1000, 3, 4);  // expected: 12000
