# MiniLang -- Grammar (informal EBNF)

```
program       = top_level* ;

top_level     = function_def
              | extern_decl
              | top_expr
              ;

function_def  = "def" IDENT "(" param_list? ")" expr ";" ;
extern_decl   = "extern" IDENT "(" param_list? ")" ";" ;
top_expr      = expr ";" ;

param_list    = IDENT ("," IDENT)* ;

expr          = primary (binop primary)* ;

primary       = NUMBER
              | IDENT ("(" arg_list? ")")?       (* variable or call *)
              | "(" expr ")"
              | if_expr
              | for_expr
              | var_expr
              ;

if_expr       = "if" expr "then" expr "else" expr ;
for_expr      = "for" IDENT "=" expr "," expr ("," expr)? "in" expr ;
var_expr      = "var" IDENT ("=" expr)? ("," IDENT ("=" expr)?)* "in" expr ;

binop         = "+" | "-" | "*" | "/"
              | "<" | ">"
              | "="                              (* assignment *)
              ;

arg_list      = expr ("," expr)* ;

NUMBER        = [0-9]+ ("." [0-9]*)? | "." [0-9]+ ;
IDENT         = [a-zA-Z][a-zA-Z0-9_]* ;
```

## Operator precedence (low to high)

| Level | Operators |
|------:|-----------|
|     2 | `=` (assignment, right-associative -- handled in codegen, not parser) |
|    10 | `<`, `>` |
|    20 | `+`, `-` |
|    40 | `*`, `/` |

## Comments

`#` and `//` both start line comments. Block comments are not supported.

## Reserved words

`def`, `extern`, `if`, `then`, `else`, `for`, `in`, `var`.

## Semantics quick-reference

- The only type is `f64`. `bool` lives as `f64` -- `0.0` is false, anything
  else is true.
- `if`, `for`, `var` are all expressions. `if` returns the chosen branch's
  value; `for` returns `0.0` (it exists for its side effects). `var` returns
  the value of its body.
- `=` requires the LHS to be a `var`-bound variable. It produces the
  assigned value, so chains like `a = b = 0` parse and codegen sensibly.
- Assignment uses the precedence of the leading character (so it binds at
  level 2 alongside the `=`-only relational predicates the parser table
  uses).

## Examples by feature

```ml
# arithmetic + recursion
def fib(n)
    if n < 2 then n
    else fib(n - 1) + fib(n - 2);

# mutable accumulator + for loop
def sum_to(n)
    var s = 0.0 in
    var dummy = (for i = 1, i < n + 1, 1.0 in s = s + i) in
    s;

# extern declarations (linked at JIT time)
extern sin(x);
extern cos(x);
```
