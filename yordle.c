#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef double LispExpr;

#define TAG_BITS(x) *(uint64_t*)&x >> 48

#define ATOM_HEAP_ADDR (char*)g_cell

#define NCELLS 8192

#define BUFFER_SIZE 80

unsigned g_heap_pointer = 0;
unsigned g_stack_pointer = NCELLS;

unsigned g_ATOM=0x7ff8, g_PRIM=0x7ff9, g_CONS=0x7ffa, g_CLOS=0x7ffb, g_NIL=0x7ffc;

LispExpr g_cell[NCELLS];

LispExpr g_nil, g_true, g_err, g_env;

LispExpr box(unsigned tag, unsigned data) {
    LispExpr x;
    *(uint64_t*)&x = (uint64_t)tag << 48 | data;
    return x;
}

unsigned ord(LispExpr x) {
    return *(uint64_t*)&x;
}

LispExpr num(LispExpr n) {
    return n;
}

unsigned eq(LispExpr x, LispExpr y) {
    return *(uint64_t*)&x == *(uint64_t*)&y;
}

unsigned not(LispExpr x) {
    return TAG_BITS(x) == g_NIL;
}

LispExpr atom(const char *s) {
    unsigned i = 0;
    while (i < g_heap_pointer && strcmp(ATOM_HEAP_ADDR + i, s)) {
        i += strlen(ATOM_HEAP_ADDR + i) + 1;
    }

    if (i == g_heap_pointer && (g_heap_pointer += strlen(strcpy(ATOM_HEAP_ADDR + i, s)) + 1) > g_stack_pointer << 3) {
        abort();
    }
    return box(g_ATOM, i);
}

LispExpr cons(LispExpr x, LispExpr y) {
    g_cell[--g_stack_pointer] = x;
    g_cell[--g_stack_pointer] = y;
    if (g_heap_pointer > g_stack_pointer << 3) {
        abort();
    }
    return box(g_CONS, g_stack_pointer);
}

LispExpr car(LispExpr p) {
    return (TAG_BITS(p) & ~(g_CONS^g_CLOS)) == g_CONS ? g_cell[ord(p) + 1] : g_err;
}

LispExpr cdr(LispExpr p) {
    return (TAG_BITS(p) & ~(g_CONS^g_CLOS)) == g_CONS ? g_cell[ord(p)] : g_err;
}

LispExpr pair(LispExpr v, LispExpr x, LispExpr e)  {
    return cons(cons(v, x), e);
}

LispExpr closure(LispExpr v, LispExpr x, LispExpr e) {
    return box(g_CLOS, ord(pair(v, x, eq(e, g_env) ? g_nil : e)));
}

LispExpr assoc(LispExpr v, LispExpr e) {
    while (TAG_BITS(e) == g_CONS && !eq(v, car(car(e)))) {
        e = cdr(e);
    }
    return TAG_BITS(e) == g_CONS ? cdr(car(e)) : g_err;
}

unsigned let(LispExpr t) {
    return TAG_BITS(t) != g_NIL && !not(cdr(t));
}

LispExpr eval(LispExpr, LispExpr);

LispExpr evlis(LispExpr t, LispExpr e) {
    return TAG_BITS(t) == g_CONS ? cons(eval(car(t), e), evlis(cdr(t), e)) :
        TAG_BITS(t) == g_ATOM ? assoc(t, e) :
        g_nil;
}

LispExpr f_eval(LispExpr t, LispExpr e) {
    return eval(car(evlis(t, e)), e);
}

LispExpr f_quote(LispExpr t, LispExpr _) {
    return car(t);
}

LispExpr f_cons(LispExpr t, LispExpr e) {
    return t = evlis(t, e), cons(car(t), car(cdr(t)));
}

LispExpr f_car(LispExpr t, LispExpr e) {
    return car(car(evlis(t, e)));
}

LispExpr f_cdr(LispExpr t, LispExpr e) {
    return cdr(car(evlis(t, e)));
}

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

LispExpr f_define(LispExpr t, LispExpr e) {
    g_env = pair(car(t), eval(car(cdr(t)), e), g_env);
    return car(t);
}

struct {
    const char *s;
    LispExpr (*f)(LispExpr, LispExpr);
} Prim[] = {
    {"eval", f_eval},
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
    {"define", f_define},
    {0}};

LispExpr bind(LispExpr v, LispExpr t, LispExpr e) {
    return TAG_BITS(v) == g_NIL ? e :
        TAG_BITS(v) == g_CONS ? bind(cdr(v), cdr(t), pair(car(v), car(t), e)) :
        pair(v, t, e);
}

LispExpr reduce(LispExpr f, LispExpr t, LispExpr e) {
    return eval(cdr(car(f)), bind(car(car(f)), evlis(t, e), not(cdr(f)) ? g_env : cdr(f)));

}

LispExpr apply(LispExpr f, LispExpr t, LispExpr e) {
    return TAG_BITS(f) == g_PRIM ? Prim[ord(f)].f(t, e) :
        TAG_BITS(f) == g_CLOS ? reduce(f, t, e) :
        g_err;
}

LispExpr eval(LispExpr x, LispExpr e) {
    return TAG_BITS(x) == g_ATOM ? assoc(x, e) :
        TAG_BITS(x) == g_CONS ? apply(eval(car(x), e), cdr(x), e) :
        x;
}

char g_buf[BUFFER_SIZE];
char g_see = ' ';

void look() {
    int c = getchar();
    g_see = c;
    if (c == EOF) {
        exit(0);
    }
}

unsigned seeing(char c) {
    return c == ' ' ? g_see > 0 && g_see <= c : g_see == c;
}

char get() {
    char c = g_see;
    look();
    return c;
}

char scan() {
    unsigned i = 0;
    while (seeing(' ') || seeing(';')) {
        if (get() == ';') {
            while(!seeing('\n')) {
                look();
            }
        }
    }

    if (seeing('(') || seeing(')') || seeing('\'')) {
        g_buf[i++] = get();
    } else {
        do {
            g_buf[i++] = get();
        } while ((i < BUFFER_SIZE - 1) && !seeing('(') && !seeing(')') && !seeing(' '));
    }

    g_buf[i] = 0;
    return *g_buf;
}

LispExpr parse();

LispExpr read() {
    scan();
    return parse();
}

LispExpr list() {
    LispExpr x;
    if (scan() == ')') {
        return g_nil;
    } else if (!strcmp(g_buf, ".")) {
        x = read();
        scan();
        return x;
    } else {
        x = parse();
        return cons(x, list());
    }
}

LispExpr quote() {
    return cons(atom("quote"), cons(read(), g_nil));
}

LispExpr atomic() {
    LispExpr n;
    unsigned i;
    return (sscanf(g_buf, "%lg%n", &n, &i) > 0 && !g_buf[i]) ? n :
        atom(g_buf);
}

LispExpr parse() {
    return *g_buf == '(' ? list() :
        *g_buf == '\'' ? quote() :
        atomic();
}

void print(LispExpr);

void printlist(LispExpr t) {
    for (putchar('('); ; putchar(' ')) {
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

void gc() {
    g_stack_pointer = ord(g_env);

    unsigned i = g_stack_pointer;
    for (g_heap_pointer = 0; i < NCELLS; ++i) {
        if (TAG_BITS(g_cell[i]) == g_ATOM && ord(g_cell[i]) > g_heap_pointer) {
            g_heap_pointer = ord(g_cell[i]);
        }
    }

    g_heap_pointer += strlen(ATOM_HEAP_ADDR + g_heap_pointer) + 1;
}

int main() {
    g_nil = box(g_NIL, 0);
    g_true = atom("#t");
    g_err = atom("ERR");
    g_env = pair(g_true, g_true, g_nil);

    for (unsigned i = 0; Prim[i].s; ++i) {
        g_env = pair(atom(Prim[i].s), box(g_PRIM, i), g_env);
    }

    while (1) {
        printf("\n%u>", g_stack_pointer - g_heap_pointer / 8);
        print(eval(read(), g_env));
        gc();
    }
}
