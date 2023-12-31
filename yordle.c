#include <readline/history.h>
#include <readline/readline.h>

#include <setjmp.h>

#include <stdint.h>
#include <stdlib.h>

#include <stdio.h>
#include <string.h>

typedef double LispExpr;

/* Should output include tracing?
 *     NO_TRACE          no tracing in REPL
 *     TRACE             after each step, print the expression
 *                       before and afterthe step
 *     TRACE_INTERACTIVE after each step, evaluation halts and resumes
 *                       after specified keypress */
typedef enum { NO_TRACE, TRACE, TRACE_INTERACTIVE } TraceState;

typedef enum {
  INV_CAR_OR_CDR,
  SYM_NOT_FOUND,
  INV_FUN_TYPE,
  OUT_OF_MEMORY,
} ErrorCode;

/* Returns the tag bits of a NaN=boxed Lisp expression x */
#define TAG_BITS(x) *(uint64_t *)&x >> 48

/* Address of the atom heap is at the bottom of the cell stack */
#define ATOM_HEAP_ADDR (char *)g_cell

/* Number of cells to use for the shared stack and atom heap.
 * Increase to preallocate more memory */
#define NCELLS 4096

#define BUFFER_SIZE 80
#define PROMPT_SIZE 20

/* Free bytes available on the heap */
unsigned g_heap_pointer = 0;
/* Top of the stack of Lisp values. The heap grows upward towards the stack.
 * The stack grows downward. Remaining free space sits between the heap and
 * stack */
unsigned g_stack_pointer = NCELLS;

/* Different types of Lisp expressions are encoded using NaN Boxing.
 * These tags are part of the fraction part of a IEEE 754 floating point NaN.
 * We're using quiet NaNs, so we have 51 bits and the sign bits to store the
 * tag, as well as the actual data */
unsigned g_ATOM = 0x7ff8, g_PRIM = 0x7ff9, g_CONS = 0x7ffa, g_CLOS = 0x7ffb,
         g_MACR = 0x7ffc, g_NIL = 0x7ffd;

LispExpr g_cell[NCELLS];

/* nil represents the smpty lisp and is also considred false */
LispExpr g_nil, g_true, g_env;

char g_buf[BUFFER_SIZE];
char g_see = ' ';

char *g_curr_line_char_ptr = "";
char *g_line = NULL;
char g_prompt[PROMPT_SIZE];
FILE *g_in = NULL;

TraceState g_trace_state;

jmp_buf g_jmp_context;

/* Returns a new tagged NaN-boxed double with ordinal content data */
LispExpr box(unsigned tag, unsigned data) {
  LispExpr x;
  *(uint64_t *)&x = (uint64_t)tag << 48 | data;
  return x;
}

/* Returns the ordinal (data/paylaod) of the NaN-boxed x */
unsigned ord(LispExpr x) { return *(uint64_t *)&x; }

LispExpr err(ErrorCode i) { longjmp(g_jmp_context, (int)i); }

/* Returns the NaN-boxed without the tag.
 * This currently passes NaNs to perform arithmetic on, resulting in a NaN.
 * We could check if n is a NaN and take some action, with `if (n != n)` */
LispExpr num(LispExpr n) { return n; }

/* Equality comparisons with NaN values always produce false, so just compare
 * the 64 bits of the values for equality */
unsigned eq(LispExpr x, LispExpr y) {
  return *(uint64_t *)&x == *(uint64_t *)&y;
}

/* Check if a Lisp expression is the empty list (nil) */
unsigned not(LispExpr x) { return TAG_BITS(x) == g_NIL; }

/* Intern atom names (Lisp symbols), returns a unique NaN-boxed ATOM */
LispExpr atom(const char *s) {
  unsigned i = 0;
  while (i < g_heap_pointer &&
         strcmp(ATOM_HEAP_ADDR + i,
                s)) { // search for matching atom name on the heap
    i += strlen(ATOM_HEAP_ADDR + i) + 1;
  }

  if (i == g_heap_pointer && // if not found
      (g_heap_pointer += strlen(strcpy(ATOM_HEAP_ADDR + i, s)) +
                         1) >     // alocate and add a new atom name to the heap
          g_stack_pointer << 3) { // heap ptr points to bytes, stack ptr points
                                  // to 8-byte float
    err(OUT_OF_MEMORY);
  }
  return box(g_ATOM, i);
}

/* A pair p is car(p) and cdr(p). The car is one cell above the cdr.
 * Lisp uses linked lists with the car of a pair containing the list element
 * and the cdr pointing to the next cons pair (or nil) */
LispExpr cons(LispExpr x, LispExpr y) {
  g_cell[--g_stack_pointer] = x; // push the car value in the stack
  g_cell[--g_stack_pointer] = y; // push the cdr value in the stack
  if (g_heap_pointer > g_stack_pointer << 3) {
    err(OUT_OF_MEMORY);
  }
  return box(g_CONS, g_stack_pointer);
}

/* Get the car cell of the pair */
LispExpr car(LispExpr p) {
  return TAG_BITS(p) == g_CONS || TAG_BITS(p) == g_CLOS || TAG_BITS(p) == g_MACR
             ? g_cell[ord(p) + 1]
             : err(INV_CAR_OR_CDR);
}

/* Get the cdr cell of the pair */
LispExpr cdr(LispExpr p) {
  return TAG_BITS(p) == g_CONS || TAG_BITS(p) == g_CLOS || TAG_BITS(p) == g_MACR
             ? g_cell[ord(p)]
             : err(INV_CAR_OR_CDR);
}

/* First construct the name-value Lisp pair (v . x),
 * then place it in front of the Lisp environment list.
 * Returns the list ((v . x) . e) */
LispExpr pair(LispExpr v, LispExpr x, LispExpr e) {
  return cons(cons(v, x), e);
}

/* A closure is a CLOS-tagged pair (v, x, e) representing an instantiation of a
 * Lisp (lambda v x) with either a single atom v as a variable referencing a
 * list of arguments passed to the function, or v is a list of atoms as
 * variables, each referencing the corresponding argument passed to the
 * function. Closures include their static scope as an environment e to
 * reference the bindings of their parent functions, if functions are nested,
 * and to reference the global static scope. The eq(e, g_env) part forces the
 * scope of a closure to be nil if e is the global environment. This is
 * important because when we apply the closure we check if the its environment
 * is nil and use the current global environment. This permits recursive calls
 * and calls to forward-defined functions, because the global environment
 * includes the latest global definitions */
LispExpr closure(LispExpr v, LispExpr x, LispExpr e) {
  return box(g_CLOS, ord(pair(v, x, eq(e, g_env) ? g_nil : e)));
}

/* Construct a macro */
LispExpr macro(LispExpr v, LispExpr x) { return box(g_MACR, ord(cons(v, x))); }

/* Look up a symbol in an environment.
 * An environment in Lisp is implemented as a list of name-value associations,
 * where names are Lisp atoms */
LispExpr assoc(LispExpr v, LispExpr e) {
  while (TAG_BITS(e) == g_CONS && !eq(v, car(car(e)))) {
    e = cdr(e);
  }
  return TAG_BITS(e) == g_CONS ? cdr(car(e)) : err(SYM_NOT_FOUND);
}

unsigned let(LispExpr t) { return TAG_BITS(t) != g_NIL && !not(cdr(t)); }

LispExpr step(LispExpr, LispExpr);

void print(LispExpr);

LispExpr eval(LispExpr x, LispExpr e) {
  LispExpr y = step(x, e);
  if (g_trace_state == NO_TRACE) {
    return y;
  }

  printf("%u: ", g_stack_pointer);
  print(x);
  printf(" => ");
  print(y);

  if (g_trace_state == TRACE_INTERACTIVE) {
    while (getchar() >= ' ') {
      continue;
    }
  }
  return y;
}

LispExpr evlis(LispExpr t, LispExpr e) {
  LispExpr s;
  LispExpr *p;
  for (s = g_nil, p = &s; TAG_BITS(t) == g_CONS;
       p = g_cell + g_stack_pointer, t = cdr(t)) {
    *p = cons(eval(car(t), e), g_nil);
  }

  if (TAG_BITS(t) == g_ATOM) {
    *p = assoc(t, e);
  }
  return s;
}

/* Lisp builtins:
 *     (eval x)
 *     (quote x)
 *     (cons x y)
 *     (car p)
 *     (cdr p)
 *     (add n1 n2 ... nk)  sum of n1 to nk
 *     (sub n1 n2 ... nk)  n1 minus sum of n2 to nk
 *     (mul n1 n2 ... nk)  product of n1 to nk
 *     (div n1 n2 ... nk)  n1 divided by the product of n2 to nk
 *     (int n)             integer part of n
 *     (< n1 n2)           #t if n1<n2, otherwise ()
 *     (eq? x y)           #t if x equals y, otherwise ()
 *     (or x1 x2 ... xk)   first x that is not (), otherwise ()
 *     (and x1 x2 ... xk)  last x if all x are not (), otherwise ()
 *     (not x)             #t if x is (), otherwise ()
 *     (cond (x1 y1)
 *           (x2 y2)
 *           ...
 *           (xk yk))      the first yi for which xi evaluates to non-()
 *     (if x y z)          if x is non-() then y else z
 *     (let* (v1 x1)
 *           (v2 x2)
 *           ...
 *           y)            sequentially binds each variable v1 to xi
 *                         to evaluate y
 *     (lambda v x)        construct a closure
 *     (macro t e)         construct a macro
 *     (define v x)        define a named value globally
 *     (assoc v e)         give the expression associated with v in the
 *                         specified e (v should be quoted)
 *     (env)               return the current environment in which (env)
 *                         is evaluated
 *     (let (v1 x1)
 *          (v2 x2)
 *          ...
 *          y)             similar to let*, evaluates all expressions first
 *                         before binding the values to the variables
 *     (letrec* (v1 x2)
 *              (v2 x2)
 *              ...
 *              y)         similar to let*, allows for local recursion where the
 *                         name may also appear in the
 *                         value of a name-value pair
 *     (setq v e)          set the value of v as a side-effect.
 *                         Garbage collection after setq may corrupt the stack
 *                         if the new value assigned to a global variable
 *                         is a temporary list
 *                         (all interactively constructed lists are temporary).
 *                         Atomic values are always safe to assign and
 *                         setq is safe to use to assign local variables
 *                         in the scope of a lambda and a let
 *     (set-car! p e)      set the value of the car cell of a cons p to e
 *                         as a side-effect
 *     (set-cdr! p e)      set the value of the cdr cell of a cons p to e
 *                         as a side-effect
 *     (read)              return the expression typed in (unevaluated)
 *     (print e)
 *     (println e)
 *     (catch e)           catch exceptions during evaluation of e
 *     (throw n)           throw exception with error code n
 *     (trace n)           change current tracing status, n can be 0|1|2 */

LispExpr f_eval(LispExpr t, LispExpr e) { return eval(car(evlis(t, e)), e); }

LispExpr f_quote(LispExpr t, LispExpr _) { return car(t); }

LispExpr f_cons(LispExpr t, LispExpr e) {
  return t = evlis(t, e), cons(car(t), car(cdr(t)));
}

LispExpr f_car(LispExpr t, LispExpr e) { return car(car(evlis(t, e))); }

LispExpr f_cdr(LispExpr t, LispExpr e) { return cdr(car(evlis(t, e))); }

LispExpr f_add(LispExpr t, LispExpr e) {
  LispExpr n = car(t = evlis(t, e));
  while (!not(t = cdr(t))) {
    n += car(t);
  }
  return num(n);
}

// TODO: negate if single argument has been passed.
LispExpr f_sub(LispExpr t, LispExpr e) {
  LispExpr n = car(t = evlis(t, e));
  while (!not(t = cdr(t))) {
    n -= car(t);
  }
  return num(n);
}

LispExpr f_mul(LispExpr t, LispExpr e) {
  LispExpr n = car(t = evlis(t, e));
  while (!not(t = cdr(t))) {
    n *= car(t);
  }
  return num(n);
}

LispExpr f_div(LispExpr t, LispExpr e) {
  LispExpr n = car(t = evlis(t, e));
  while (!not(t = cdr(t))) {
    n /= car(t);
  }
  return num(n);
}

LispExpr f_int(LispExpr t, LispExpr e) {
  LispExpr n = car(evlis(t, e));
  return n - 1e9 < 0 && n + 1e9 > 0 ? (long)n : n;
}

LispExpr f_lt(LispExpr t, LispExpr e) {
  return t = evlis(t, e), car(t) - car(cdr(t)) < 0 ? g_true : g_nil;
}

LispExpr f_eq(LispExpr t, LispExpr e) {
  return t = evlis(t, e), eq(car(t), car(cdr(t))) ? g_true : g_nil;
}

LispExpr f_not(LispExpr t, LispExpr e) {
  return not(car(evlis(t, e))) ? g_true : g_nil;
}

LispExpr f_or(LispExpr t, LispExpr e) {
  LispExpr x = g_nil;
  while (TAG_BITS(t) != g_NIL && not(x = eval(car(t), e))) {
    t = cdr(t);
  }
  return x;
}

LispExpr f_and(LispExpr t, LispExpr e) {
  LispExpr x = g_nil;
  while (TAG_BITS(t) != g_NIL && !not(x = eval(car(t), e))) {
    t = cdr(t);
  }
  return x;
}

LispExpr f_cond(LispExpr t, LispExpr e) {
  while (TAG_BITS(t) != g_NIL && not(eval(car(car(t)), e))) {
    t = cdr(t);
  }
  return eval(car(cdr(car(t))), e);
}

LispExpr f_if(LispExpr t, LispExpr e) {
  return eval(car(cdr(not(eval(car(t), e)) ? cdr(t) : t)), e);
}

LispExpr f_leta(LispExpr t, LispExpr e) {
  for (; let(t); t = cdr(t)) {
    e = pair(car(car(t)), eval(car(cdr(car(t))), e), e);
  }
  return eval(car(t), e);
}

LispExpr f_lambda(LispExpr t, LispExpr e) {
  return closure(car(t), car(cdr(t)), e);
}

LispExpr f_macro(LispExpr t, LispExpr e) { return macro(car(t), car(cdr(t))); }

LispExpr f_define(LispExpr t, LispExpr e) {
  g_env = pair(car(t), eval(car(cdr(t)), e), g_env);
  return car(t);
}

LispExpr f_assoc(LispExpr t, LispExpr e) {
  t = evlis(t, e);
  return assoc(car(t), car(cdr(t)));
}

LispExpr f_env(LispExpr _, LispExpr e) { return e; }

LispExpr f_let(LispExpr t, LispExpr e) {
  LispExpr d = e;
  for (; let(t); t = cdr(t)) {
    d = pair(car(car(t)), eval(car(cdr(car(t))), e), d);
  }
  return eval(car(t), d);
}

LispExpr f_letreca(LispExpr t, LispExpr e) {
  for (; let(t); t = cdr(t)) {
    e = pair(car(car(t)), g_nil, e);
    g_cell[g_stack_pointer + 2] = eval(car(cdr(car(t))), e);
  }
  return eval(car(t), e);
}

LispExpr f_setq(LispExpr t, LispExpr e) {
  LispExpr v = car(t);
  LispExpr x = eval(car(cdr(t)), e);
  while (TAG_BITS(e) == g_CONS && !eq(v, car(car(e)))) {
    e = cdr(e);
  }
  return TAG_BITS(e) == g_CONS ? g_cell[ord(car(e))] = x : err(SYM_NOT_FOUND);
}

LispExpr f_setcar(LispExpr t, LispExpr e) {
  t = evlis(t, e);
  LispExpr p = car(t);
  return (TAG_BITS(p) == g_CONS) ? g_cell[ord(p) + 1] = car(cdr(t)) : err(SYM_NOT_FOUND);
}

LispExpr f_setcdr(LispExpr t, LispExpr e) {
  t = evlis(t, e);
  LispExpr p = car(t);
  return (TAG_BITS(p) == g_CONS) ? g_cell[ord(p)] = car(cdr(t)) : err(SYM_NOT_FOUND);
}

LispExpr read();

LispExpr f_read(LispExpr t, LispExpr e) {
  LispExpr x;
  char c = g_see;
  g_see = ' ';
  x = read();
  g_see = c;
  return x;
}

void print(LispExpr);

LispExpr f_print(LispExpr t, LispExpr e) {
  for (t = evlis(t, e); TAG_BITS(t) != g_NIL; t = cdr(t)) {
    print(car(t));
  }
  return g_nil;
}

LispExpr f_println(LispExpr t, LispExpr e) {
  f_print(t, e);
  putchar('\n');
  return g_nil;
}

LispExpr f_catch(LispExpr t, LispExpr e) {
  LispExpr x;
  int jmp_status;
  jmp_buf saved_jmp_context;

  memcpy(saved_jmp_context, g_jmp_context, sizeof(g_jmp_context));
  jmp_status = setjmp(g_jmp_context);
  x = jmp_status ? cons(atom("ERR"), jmp_status) : eval(car(t), e);

  memcpy(g_jmp_context, saved_jmp_context, sizeof(g_jmp_context));
  return x;
}

LispExpr f_throw(LispExpr t, LispExpr e) {
  longjmp(g_jmp_context, (int)num(car(t)));
}

LispExpr f_trace(LispExpr t, LispExpr e) {
  g_trace_state = (TraceState) car(t);
  return g_nil;
}

struct {
  const char *s;
  LispExpr (*f)(LispExpr, LispExpr);
} Prim[] = {{"eval", f_eval},
            {"quote", f_quote},
            {"cons", f_cons},
            {"car", f_car},
            {"cdr", f_cdr},
            {"+", f_add},
            {"-", f_sub},
            {"*", f_mul},
            {"/", f_div},
            {"int", f_int},
            {"<", f_lt},
            {"eq?", f_eq},
            {"or", f_or},
            {"and", f_and},
            {"not", f_not},
            {"cond", f_cond},
            {"if", f_if},
            {"let*", f_leta},
            {"lambda", f_lambda},
            {"macro", f_macro},
            {"define", f_define},
            {"assoc", f_assoc},
            {"env", f_env},
            {"let", f_let},
            {"letrec*", f_letreca},
            {"setq", f_setq},
            {"set-car!", f_setcar},
            {"set-cdr!", f_setcdr},
            {"read", f_read},
            {"print", f_print},
            {"println", f_println},
            {"catch", f_catch},
            {"throw", f_throw},
            {"trace", f_trace},
            {0}};

/* Create environment by extending e with variables v bound to values t */
LispExpr bind(LispExpr v, LispExpr t, LispExpr e) {
  return TAG_BITS(v) == g_NIL    ? e
         : TAG_BITS(v) == g_CONS ? bind(cdr(v), cdr(t), pair(car(v), car(t), e))
                                 : pair(v, t, e);
}

/* Apply closure f to the list of arguments t.
 * Notice that we use the fact that closures are constructed
 * to include their static scope or nil as their environment */
LispExpr reduce(LispExpr f, LispExpr t, LispExpr e) {
  return eval(cdr(car(f)),
              bind(car(car(f)), evlis(t, e), not(cdr(f)) ? g_env : cdr(f)));
}

/* Application of macros is similar to lambdas, by they expand instead */
LispExpr expand(LispExpr f, LispExpr t, LispExpr e) {
  return eval(eval(cdr(f), bind(car(f), t, g_env)), e);
}

/* Apply the primitive or the closure f to the list of arguments t in environment e. */
LispExpr apply(LispExpr f, LispExpr t, LispExpr e) {
  return TAG_BITS(f) == g_PRIM   ? Prim[ord(f)].f(t, e)
         : TAG_BITS(f) == g_CLOS ? reduce(f, t, e)
         : TAG_BITS(f) == g_MACR ? expand(f, t, e)
                                 : err(INV_FUN_TYPE);
}

/* The core of `eval`. An expression is either a number, an atom, a primitive,
 * a cons pair, a closure, or nil. Numbers, primitives, closures and nil are constant
 * and returned as they are.
 * Note that an expression x evalutes to the value assoc(x, e) when x is atom, or
 * evaluates to apply(...) if it is a list. */
LispExpr step(LispExpr x, LispExpr e) {
  return TAG_BITS(x) == g_ATOM   ? assoc(x, e)
         : TAG_BITS(x) == g_CONS ? apply(eval(car(x), e), cdr(x), e)
                                 : x;
}

/* Advance to the next character in input buffer, also works when reading from file */
void look() {
  if (g_in) {
    int c = getc(g_in);
    g_see = c;
    if (c != EOF) {
      return;
    }

    fclose(g_in);
    g_in = NULL;
  }

  if (g_see == '\n') {
    if (g_line) {
      free(g_line);
    }

    do {
      g_line = readline(g_prompt);
      g_curr_line_char_ptr = g_line;

      if (g_curr_line_char_ptr == NULL) {
        stdin = freopen("/dev/tty", "r", stdin);
        if (stdin == NULL) {
          fprintf(stderr, "Unable to reopen stdin from /dev/tty\n");
          exit(1);
        }
      }
    } while (g_curr_line_char_ptr == NULL);

    add_history(g_line);
    strcpy(g_prompt, "?");
  }

  if (!(g_see = *g_curr_line_char_ptr++)) {
    g_see = '\n';
  }
}

/* Return non-zero if we're looking at the character c */
unsigned seeing(char c) {
  return c == ' ' ? g_see > 0 && g_see <= c : g_see == c;
}

/* Return the lookahead character from input and advance to the next in buffer */
char get() {
  char c = g_see;
  look();
  return c;
}

/* Tokenize into buffer and return first character of buffer */
char scan() {
  unsigned i = 0;
  while (seeing(' ') || seeing(';')) {
    if (get() == ';') {
      while (!seeing('\n')) {
        look();
      }
    }
  }

  if (seeing('(') || seeing(')') || seeing('\'')) {
    g_buf[i++] = get();
  } else {
    do {
      g_buf[i++] = get();
    } while ((i < BUFFER_SIZE - 1) && !seeing('(') && !seeing(')') &&
             !seeing(' '));
  }

  g_buf[i] = 0;
  return *g_buf;
}

LispExpr parse();

/* Read a Lisp expression from input */
LispExpr read() {
  scan();
  return parse();
}

/* Return a parsed Lisp list */
LispExpr list() {
  LispExpr t;
  LispExpr *p;
  for (t = g_nil, p = &t;;
       *p = cons(parse(), g_nil), p = g_cell + g_stack_pointer) {
    if (scan() == ')') {
      return t;
    }

    if (*g_buf == '.' && !g_buf[1]) {
      *p = read();
      scan();
      return t;
    }
  }
}

/* Reterun a parsed Lisp expression x quoted as (quote x) */
LispExpr quote() { return cons(atom("quote"), cons(read(), g_nil)); }

/* Return a parsed atomic Lisp expression (a number or an atom) */
LispExpr atomic() {
  LispExpr n;
  unsigned i;
  return (sscanf(g_buf, "%lg%n", &n, &i) > 0 && !g_buf[i]) ? n : atom(g_buf);
}

/* Return a parsed Lisp expression */
LispExpr parse() {
  return *g_buf == '(' ? list() : *g_buf == '\'' ? quote() : atomic();
}
/* Print a Lisp list */
void printlist(LispExpr t) {
  for (putchar('(');; putchar(' ')) {
    print(car(t));
    t = cdr(t);

    if (TAG_BITS(t) == g_NIL) {
      break;
    } else if (TAG_BITS(t) != g_CONS) {
      printf(" . ");
      print(t);
      break;
    }
  }

  putchar(')');
}

/* Print a Lisp expression */
void print(LispExpr x) {
  if (TAG_BITS(x) == g_NIL) {
    printf("()");
  } else if (TAG_BITS(x) == g_ATOM) {
    printf("%s", ATOM_HEAP_ADDR + ord(x));
  } else if (TAG_BITS(x) == g_PRIM) {
    printf("<%s>", Prim[ord(x)].s);
  } else if (TAG_BITS(x) == g_CONS) {
    printlist(x);
  } else if (TAG_BITS(x) == g_CLOS) {
    printf("{%u}", ord(x));
  } else {
    printf("%.10lg", x);
  }
}

/* Garbage collection. Remove all temporary cells from the stack.
 * Also removes unused atoms from the heap.
 * Preserves all globally-defined names and functions
 * listed in the global environment */
void gc() {
  g_stack_pointer = ord(g_env); // restore the stack ptr to the point on the stack
                                // where the free space begins

  unsigned i = g_stack_pointer;
  for (g_heap_pointer = 0; i < NCELLS; ++i) { // find the max heap reference among the used ATOM-tagged cells
    if (TAG_BITS(g_cell[i]) == g_ATOM && ord(g_cell[i]) > g_heap_pointer) {
      g_heap_pointer = ord(g_cell[i]);
    }
  }

  g_heap_pointer += strlen(ATOM_HEAP_ADDR + g_heap_pointer) + 1; // adjust the heap ptr accordingly
}

/* Lisp initialization and REPL */
int main(int argc, char **argv) {
  g_nil = box(g_NIL, 0);
  g_true = atom("#t");
  g_env = pair(g_true, g_true, g_nil);

  g_trace_state = NO_TRACE;

  atom("ERR");

  for (unsigned i = 0; Prim[i].s; ++i) {
    g_env = pair(atom(Prim[i].s), box(g_PRIM, i), g_env);
  }

  if (argc > 1) {
    g_in = fopen((strcmp(argv[1], "p") == 0) ? "prelude.lisp" : argv[1], "r");
  }

  using_history();

  int jmp_status;
  if ((jmp_status = setjmp(g_jmp_context)) != 0) {
    printf("ERR %d", jmp_status);
  }
  while (1) {
    gc();
    putchar('\n');
    snprintf(g_prompt, PROMPT_SIZE, "%u>",
             g_stack_pointer - g_heap_pointer / 8);
    print(eval(read(), g_env));
  }
}
