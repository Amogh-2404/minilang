// 03_loop.ml -- for-loop + a mutable variable.
//
// var-dummy idiom: the for-loop returns 0.0 (it's evaluated for its
// side-effects on `s`), so we bind the loop's value to `dummy` and then
// evaluate `s` as the actual result of the function.

def sum_to(n)
    var s = 0.0 in
    var dummy = (for i = 1, i < n + 1, 1.0 in s = s + i) in
    s;

sum_to(100);  // expected: 5050
