// 02_fib.ml -- recursion + control flow.

def fib(n)
    if n < 2 then
        n
    else
        fib(n - 1) + fib(n - 2);

fib(10);  // expected: 55
