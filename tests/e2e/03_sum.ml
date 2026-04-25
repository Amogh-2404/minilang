def sum_to(n)
    var s = 0.0 in
    var d = (for i = 1, i < n + 1, 1.0 in s = s + i) in
    s;

sum_to(100);
