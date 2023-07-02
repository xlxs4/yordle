### yordle

`yordle` is a small lisp implementation.
That's mostly thanks to NaN-boxing.

To compile `yordle`:

```
cc yordle.c -o yordle -lreadline
```

The default number of Lisp cells allocated is N=4096, which is 32K of memory.
You can change `NCELLS` and recompile yordle as you see fit.

If you want, you can have the interpreter boot with the definitions from the file loaded in the global environment.
To do that, simply pass the filename/path to file: `./yordle swarmalator.lisp`.
If you run yordle without any arguments, nothing gets loaded.
If you run `./yordle p`, it loads a default prelude, found in `prelude.lisp`.
Note that if you load nothing extra, the base takes up 157 cells, while the prelude takes up an extra 2381 cells as-is.

In the IEEE 754 floating-point format, NaNs are represented by specific bit patterns in the fraction part of a double-precision float.
There are two types of NaNs, quite NaNs (qNaNs) and Signaling NaNs (sNaNs).
qNaNs propagate through arithmetic operations without raising exceptions, where sNaNs can raise exceptions.
NaN boxing leverages takes advantage of the IEEE 754 representation and uses NaN values to store additional information.
It leverages the remaining bits in the fraction part of NaNs to store type tags and payload data.
By carefully manipulating these bits, we can encode different types of values within NaNs and perform operations on them.
NaN boxing provides a compact and efficient way to represent different types of Lisp expressions.
Instead of using separate data structures or memory allocations for each type, NaN boxing allows us to store various Lisp expression within the same data type without sacrificing performance or introducing excessive memory overhead.
By type punning the qNaN values, the interpreter can determine the type of the underlying value, e.g., by type punning the NaN value as a pointer to a cons  cell structure.

### Language features

#### Numbers

Double-precision floating point numbers, including `inf`, `-inf` and `nan` (`nan` yields `ERR`).
Numbers may also be entered in hexadecimal `0xh...h` format.

#### Symbols

Lisp symbols consist of a sequence of non-space characters, excluding `(`, `)` and quotes.
When used in a Lisp expression, a symbol is looked up for its value, like a variable typically refers to its value.
Symbols can be quoted (like `'foo`) to use them literally and to pass them to functions.

#### Booleans

The empty list `()` is considered false and anything non-`()` is considered true.
`#t` is a symbol representing true (`#t` evaluates to itself).

#### Lists

Syntactically, a dot may be used for the last list element to construct a pair instead of a list.
For example, `'(1 . 2)` is a pair, whereas `'(1 2)` is a list.
By the nature of linked lists, a list after a dot creates a list, not a pair.
For example, `'(1 . (2 . ()))` is the same as `'(1 2)`.
Note that lists form a chain of pairs ending in a `()`.

#### Function calls

```lisp
(<function> <expr1> <expr2> ... <exprn>)
```

applies a function to the rest of the list of expressions as its arguments.
The following are all builtin functions, called "primitives" and "special forms".

#### Quoting and unquoting

```lisp
(quote <expr>)
```

protects `<expr>` from evaluation by quoting, same as `'<expr>`.

```lisp
(eval <quoted-expr>)
```

evaluates a quoted expression and returns its value.

#### Constructing and deconstructing pairs and lists

```lisp
(cons x y)
```

constructs a pair `(x . y)` for expressions `x` and `y`.
Lists are formed by chaining several cons pairs, with the empty list as the last `y`.

```lisp
(car <pair>)
```

returns the first part `x` of a pair `(x . y)` or list.

```lisp
(cdr <pair>)
```

returns the second part `y` of a pair `(x . y)`.
For lists this returns the rest of the list after the first part.

#### Arithmetic

```lisp
(+ n1 n2 ... nk)
(- n1 n2 ... nk)
(* n1 n2 ... nk)
(/ n1 n2 ... nk)
```

add, subtract, multiply or divide `n1` to `nk`.
This computes `n1 <operator> n2 <operator> ... <operator> nk`.
Note that currently `(- 2)` is 2, not -2.

```lisp
(int n)
```

returns the integer part of a number `n`.

#### Logic

```lisp
(< n1 n2)
```

returns `#t` if *numbers* `n1` < `n2`.
Otherwise, returns `()`.

```lisp
(eq? x y)
```

returns `#t` if values `x` and `y` are identical.
Otherwise, returns `()`.
Numbers and symbols of the same value are always identical, but non-empty lists may or may not be identical even when their values are the same.

```lisp
(not x)
```

returns `#t` if `x` is not `()`.
Otherwise, returns `()`.

```lisp
(or x1 x2 ... xk)
```

returns the value of the first `x` that is not `()`.
Otherwise, returns `()`.
Only evaluates the `x` until the first non-`()`, i.e., it short-circuits.

```lisp
(and x1 x2 ... xk)
```

returns the value of the last `x` if all `x` are not `()`.
Otherwise, returns `()`.
Only evaluates the `x` until the first is `()`, i.e., it short-circuits.

#### Conditionals

```lisp
(cond (x1 y1)
      (x2 y2)
      ...
      (xk yk))
```

returns the value of `y` corresponding to the first `x` that is not `()`.

```lisp
(if x y z)
```

if `x` is not `()`, then return `y` else return `z`.

#### Lambdas

```lisp
(lambda <variables> <expr>)
```

returns an anonymous function closure with a list of variables and an expression as its body.
The variables of a lambda may be a single name (not placed in a list) to pass all arguments as a named list.
For example, `(lambda args args)` returns its arguments as a list.
The pair dot may be used to indicate the rest of the arguments.
For example, `(lambda (f x . args) (f . args))` applies a function argument `f` to the arguments `args`, while ignoring `x`.
The closure includes the lexical scope of the lambda, i.e. local names defined in the outer scope can be used in the body.
For example, `(lambda (f x) (lambda args (f x . args)))` is a function that takes function `f` and argument `x` to return a curried function.

#### Globals

```lisp
(define <symbol> <expr>)
```

globally defines a symbol associated with the value of an expression.
If the expression is a function or a macro, then this globally defines the function or macro.

#### Locals

Locals are declared with the following `let*` special form.
This form differs slightly in syntax from other Lisp and Scheme implementations, with the aim to make let-forms more intuitive to use.

```lisp
(let* (v1 x1)
      (v2 x2)
      ...
      (vk xk)
      y)
```

evaluates `y` with a local scope of bindings for symbols `v` sequentially bound from the first to the last to the corresponding values of `x`.

> Note that most Lisps use a syntax with binding pairs in a list and one or more body expressions:
>
> ```lisp
> (let* ((v1 x1)
>        (v2 x2)
>        ...
>        (vk xk))
>        y1
>        ...
>        yn)
> ```

> In yordle we can do the same by binding all but the last body expression `y` to dummy variables:
>
> ```lisp
> (let* (v1 x1)
>       (v2 x2)
>       ...
>       (vk xk)
>       (_ y1)
>       ...
>       yn)
> ```

#### Extras

```lisp
(assoc <quoted-symbol> <environment>)
```

returns the value associated with the quoted symbol in the given environment.

```lisp
(env)
```

returns the current environment.
When executed in the REPL, returns the global environment.

```lisp
(let (v1 x1)
     (v2 x2)
     ...
     (vk xk)
     y)
```

evaluates `y` with a local scope of bindings for symbols `v` simultaneously bound to the values of `x`.

```lisp
(letrec* (v1 x1)
         (v2 x2)
         ...
         (vk xk)
         y)
```

evaluates `y` with a local scope of recursive bindings for symbols `v` sequentially bound to the values of `x`.

```lisp
(setq <symbol> x)
```

destructively assigns a globally or locally-bound symbol a new value.
Note that garbage collection after `setq` may corrupt the stack if the new value assigned to a global variable is a temporary list (all interactively constructed lists are temporary).
Atomic values are always safe to assign and `setq` is safe to use to assign local variables in the scope of a `lambda` and a `let`.

```lisp
(set-car! <pair> x)
```

destructively assigns a pair a new car value.

```lisp
(set-cdr! <pair> y)
```

destructively assigns a pair a new cdr value.

```lisp
(macro <variables> <expr>)
```

Example:

```lisp
(define defun
  (macro (f v x)
    (list 'define f
      (list 'lambda v x))))
```

```lisp
(read)
```

returns the Lisp expression read from input (unevaluated).

```lisp
(print x1 x2 ... xk)
(println x1 x2 ... xk)
```

print the expressions.

```lisp
(catch <expr>)
```

catches exceptions in the evaluation of an expression, returns the values of the expression or `(ERR . n)` for nonzero error code `n`.

```lisp
(throw n)
```

throws error `n`, where n is a nonzero integer.

```lisp
(trace n)
```

sets trace state to `n`, `n` should be `<0|1|2>`.

### Prelude functions

```lisp
(null? x)
```

same as `not`, returns `#t` if `x` is not `()`.
Otherwise, returns `()`.

```lisp
(err? x)
```

evaluates `x` and returns `#t` if `x` is an `ERR` value.
Otherwise, returns `()`.

```lisp
(number? n)
```

returns `#t` if `n` is numveric.
Otherwise, returns `()`.

```lisp
(pair? x)
```

returns `#t` if `x` is a pair or a non-empty list.
Otherwise, returns `()`.

```lisp
(symbol? a)
```

returns `#t` if a is a symbol.
Otherwise, returns `().`

```lisp
(atom? a)
```

returns `#t` if a is an atom, that is, a symbol or the empty list `()`.
Otherwise, returns `()`.

```lisp
(list? <list>)
```

returns `#t` if `<list>` is a proper list, i.e. either empty `()` or a list of values.
Otherwise, returns `()`.

```lisp
(equal? x y)
```

returns `#t` if `x` and `y` are structurally equal.
Otherwise, returns `()`.

```lisp
(negate n)
```

returns the negative of `n`.

```lisp
(> n1 n2)
```

returns `#t` if numbers `n1` > `n2`.
Otherwise, returns `()`.

```lisp
(<= n1 n2)
```

returns `#t` if numbers `n1` <= `n2`.
Otherwise, returns `()`.

```lisp
(>= n1 n2)
```

returns `#t` if numbers `n1` >= `n2`.
Otherwise, returns `()`.

```lisp
(= n1 n2)
```

returns `#t` if numbers `n1` = `n2`.
Otherwise, returns `()`.

```lisp
(list x1 x2 ... xk)
```

returns the list of `x1`, `x2`, ..., `xk`.
That is, `(x1 x2 ... xk)` with all `x` evaluated.

```lisp
(cadr <list>)
```

returns `(car (cdr <list>))`.

```lisp
(caddr <list>)
```

returns `(car (cdr (cdr <list>)))`.

```lisp
(begin x1 x2 ... xk)
```

evaluates all `x` and returns the value of `xk`.

```lisp
(length <list>)
```

returns the length of the list, `0` for `()`.


```lisp
(append1 <list> x)
```

appends `x` to `<list>` by consing.

```lisp
(append <list1> <list2> ... <listn>)
```

constructs a single list with every value sequentially `append1`-ed to `<list1>`.

```lisp
(nthcdr <list> n)
```

returns a list containing every element of `<list>` after the `n`th element.

```lisp
(nth <list> n)
```

returns the element of `<list>` at index `n`.

```lisp
(rev1 <list>|x <list2>)
```

returns a list with the elements of `<list2>` and then the elements of `<list>` / `x` appended.

```lisp
(reverse <list>)
```

returns `<list>` reversed. `()` reversed is `()`.

```lisp
(member x <list>)
```

is a bit broken right now I think.

```lisp
(foldr <function> acc <list>)
```

folds `<list>` rightwise, applying `<function>`, with accumulator `acc`.

```lisp
(foldl <function> acc <list>)
```

folds `<list>` leftwise, applying `<function>`, with accumulator `acc`.

```lisp
(min n1 n2 ... nk)
```

returns the minimum out of the `n1`, `n2` ... `nk` numbers.

```lisp
(max n1 n2 ... nk)
```

returns the maximum out of the `n1`, `n2` ... `nk` numbers.

```lisp
(filter <function> <list>)
```

returns a list with the elements of `<list>` for which `(<function> elem)` evaluates to `#t`.

```lisp
(all? <function> <list>)
```

is currently broken

```lisp
(any? <function> <list>)
```

returns `#t` if `(<function> elem)` returns `#t` for at least one of the elements of `<list>`.

```lisp
(mapcar <function> <list>)
```

returns a list with its elements the result of evaluating `(<function> elem)` for each element of `<list>`.


```lisp
(map <function> <list1> <list2> ... <listn>)
```

same as `map` but can also apply `<function>` to n lists if `<function>` is n-ary.


```lisp
(zip <list1> <list2> ... <listn>)
```

returns a list each element of which is `(list eleml1 eleml2 ... elemln)` for each element of each list. If the lists are of different length, the returned list has length equal to the length of the smallest list provided.

```lisp
(seq n1 n2)
```

returns a list containing all numbers from `n1` (inclusive) up to `n2` (exclusive).


```lisp
(seqby n1 n2 n3)
```

returns a list containing every number from `n1` (inclusive) up to `n2` (exclusive) with step `n3`.

```lisp
(range n1 n2 <args>)
```

returns a list containing every number from `n1` (inclusive) up to `n2` (exclusive) with step the first of `<args>` if `<args>` where provided.
Otherwise, returns a list containing all numbers from `n1` (inclusive) up to `n2` (exclusive).

```lisp
(abs n)
```

returns the absolute value of `n`.

```lisp
(frac n)
```

returns the fractional part of `n` if `n` is a float.
Otherwise, returns `0`.


```lisp
(truncate n)
```

returns the integer part of `n`.

```lisp
(floor n)
```

returns the closest integer to `n` that is less than or equal to `n`.

```lisp
(ceiling n)
```

returns the closest integer to `n` that is greater than or equal to `n`.

```lisp
(round n)
```

is currently broken.

```lisp
(mod n1 n2)
```

returns `n1` modulo `n2`.


```lisp
(gcd n1 n2)
```

returns the greatest common divisor of `n1` and `n2`.

```lisp
(lcm n1 n2)
```

returns the lowest common multiple of `n1` and `n2`.

```lisp
(even? n)
```

returns `#t` if `n` is even, `()` otherwise.

```lisp
(odd? n)
```

returns `#t` if `n` is odd, `()` otherwise.

```lisp
(curry <function> x)
```

returns a function that applies `<function>` to `x` and the given arguments.


```lisp
(compose <function1> <function2>)
```

returns a function that applies `<function2>` to the arguments followed by the application of `<function1>` to the result.

```lisp
(Y <function>)
```

returns a function that applies `<function>` to `(Y <function>)` that is a copy of itself, and in turn returns a self-applying (recursive) function. The Y combinator can be used for recursion without naming the function.

```lisp
(reveal <function>)
```

returns the arguments and body of a closure of a lambda `<function>`.

```lisp
(defun <function> v x)
```

