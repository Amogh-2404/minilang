// 04_dead.ml -- DCE target.
//
// `unused` is bound to a multiplication that nothing reads. After mem2reg
// promotes the alloca to SSA, the fmul has no users -- our DCEPass deletes
// it on the first sweep.

def f(x)
    var unused = x * 99999 in
    x + 1;

f(5);  // expected: 6
