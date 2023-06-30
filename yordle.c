#include <readline/readline.h>
#include <readline/history.h>

#include <setjmp.h>

#include <stdint.h>
#include <stdlib.h>

#include <string.h>
#include <stdio.h>

typedef double LispExpr;

typedef enum {
    NO_TRACE,
    TRACE,
    TRACE_INTERACTIVE
} TraceState;

#define TAG_BITS(x) *(uint64_t*)&x >> 48

#define ATOM_HEAP_ADDR (char*)g_cell

#define NCELLS 8192

#define BUFFER_SIZE 80
#define PROMPT_SIZE 20

unsigned g_heap_pointer = 0;
unsigned g_stack_pointer = NCELLS;

unsigned g_ATOM=0x7ff8, g_PRIM=0x7ff9, g_CONS=0x7ffa,
    g_CLOS=0x7ffb, g_MACR=0x7ffc, g_NIL=0x7ffd;

LispExpr g_cell[NCELLS];

LispExpr g_nil, g_true, g_env;

char g_buf[BUFFER_SIZE];
char g_see = ' ';

char *g_curr_line_char_ptr = "";
char *g_line = NULL;
char g_prompt[PROMPT_SIZE];
FILE *g_in = NULL;

TraceState g_trace_state;

jmp_buf g_jmp_context;

LispExpr box(unsigned tag, unsigned data) {
    LispExpr x;
    *(uint64_t*)&x = (uint64_t)tag << 48 | data;
    return x;
}

unsigned ord(LispExpr x) {
    return *(uint64_t*)&x;
}

LispExpr err(int i) {
    longjmp(g_jmp_context, i);
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
        err(6);
    }
    return box(g_ATOM, i);
}

LispExpr cons(LispExpr x, LispExpr y) {
    g_cell[--g_stack_pointer] = x;
    g_cell[--g_stack_pointer] = y;
    if (g_heap_pointer > g_stack_pointer << 3) {
        err(6);
    }
    return box(g_CONS, g_stack_pointer);
}

LispExpr car(LispExpr p) {
    return TAG_BITS(p) == g_CONS || TAG_BITS(p) == g_CLOS || TAG_BITS(p) == g_MACR ?
        g_cell[ord(p) + 1] :
        err(1);
}

LispExpr cdr(LispExpr p) {
    return TAG_BITS(p) == g_CONS || TAG_BITS(p) == g_CLOS || TAG_BITS(p) == g_MACR ?
        g_cell[ord(p)] :
        err(1);
}

LispExpr pair(LispExpr v, LispExpr x, LispExpr e)  {
    return cons(cons(v, x), e);
}

LispExpr closure(LispExpr v, LispExpr x, LispExpr e) {
    return box(g_CLOS, ord(pair(v, x, eq(e, g_env) ? g_nil : e)));
}

LispExpr macro(LispExpr v, LispExpr x) {
    return box(g_MACR, ord(cons(v, x)));
}

LispExpr assoc(LispExpr v, LispExpr e) {
    while (TAG_BITS(e) == g_CONS && !eq(v, car(car(e)))) {
        e = cdr(e);
    }
    return TAG_BITS(e) == g_CONS ? cdr(car(e)) : err(2);
}

unsigned let(LispExpr t) {
    return TAG_BITS(t) != g_NIL && !not(cdr(t));
}

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
    for (s = g_nil, p = &s; TAG_BITS(t) == g_CONS; p = g_cell + g_stack_pointer, t = cdr(t)) {
        *p = cons(eval(car(t), e), g_nil);
    }

    if (TAG_BITS(t) == g_ATOM) {
        *p = assoc(t, e);
    }
    return s;
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

LispExpr f_macro(LispExpr t, LispExpr e) {
    return macro(car(t), car(cdr(t)));
}

LispExpr f_define(LispExpr t, LispExpr e) {
    g_env = pair(car(t), eval(car(cdr(t)), e), g_env);
    return car(t);
}

LispExpr f_assoc(LispExpr t, LispExpr e) {
    t = evlis(t, e);
    return assoc(car(t), car(cdr(t)));
}

LispExpr f_env(LispExpr _, LispExpr e) {
    return e;
}

LispExpr f_let(LispExpr t, LispExpr e) {
    LispExpr d = e;
    for (; let(t); t = cdr(t)) {
        d = pair(car(car(t)), eval(car(cdr(car(t))), e), d);
    }
    return eval(car(t), d);
}

LispExpr f_letreca(LispExpr t, LispExpr e) {
    for (; let(t); t = cdr(t)) {
        e = pair(car(car(t)), g_nil, e); // TODO: change to g_nil after error handling
        g_cell[g_stack_pointer + 2] = eval(car(cdr(car(t))), e);
    }
    return eval(car(t), e);
}

LispExpr f_setq(LispExpr t, LispExpr e) { // TODO: verify this works as intended.
    LispExpr v = car(t);
    LispExpr x = eval(car(cdr(t)), e);
    while (TAG_BITS(e) == g_CONS && !eq(v, car(car(e)))) {
        e = cdr(e);
    }
    return TAG_BITS(e) == g_CONS ? g_cell[ord(car(e))] = x : err(4);
}

LispExpr f_setcar(LispExpr t, LispExpr e) {
    t = evlis(t, e);
    LispExpr p = car(t);
    return (TAG_BITS(p) == g_CONS) ? g_cell[ord(p) + 1] = car(cdr(t)) : err(5);
}

LispExpr f_setcdr(LispExpr t, LispExpr e) {
    t = evlis(t, e);
    LispExpr p = car(t);
    return (TAG_BITS(p) == g_CONS) ? g_cell[ord(p)] = car(cdr(t)) : err(5);
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
    {0}};

LispExpr bind(LispExpr v, LispExpr t, LispExpr e) {
    return TAG_BITS(v) == g_NIL ? e :
        TAG_BITS(v) == g_CONS ? bind(cdr(v), cdr(t), pair(car(v), car(t), e)) :
        pair(v, t, e);
}

LispExpr reduce(LispExpr f, LispExpr t, LispExpr e) {
    return eval(cdr(car(f)), bind(car(car(f)), evlis(t, e), not(cdr(f)) ? g_env : cdr(f)));

}

LispExpr expand(LispExpr f, LispExpr t, LispExpr e) {
    return eval(eval(cdr(f), bind(car(f), t, g_env)), e);
}

LispExpr apply(LispExpr f, LispExpr t, LispExpr e) {
    return TAG_BITS(f) == g_PRIM ? Prim[ord(f)].f(t, e) :
        TAG_BITS(f) == g_CLOS ? reduce(f, t, e) :
        TAG_BITS(f) == g_MACR ? expand(f, t, e) :
        err(3);
}

LispExpr step(LispExpr x, LispExpr e) {
    return TAG_BITS(x) == g_ATOM ? assoc(x, e) :
        TAG_BITS(x) == g_CONS ? apply(eval(car(x), e), cdr(x), e) :
        x;
}

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
    LispExpr t;
    LispExpr *p;
    for (t = g_nil, p = &t; ; *p = cons(parse(), g_nil), p = g_cell + g_stack_pointer) {
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

int main(int argc, char **argv) {
    g_nil = box(g_NIL, 0);
    g_true = atom("#t");
    g_env = pair(g_true, g_true, g_nil);

    g_trace_state = NO_TRACE;

    atom("ERR");

    for (unsigned i = 0; Prim[i].s; ++i) {
        g_env = pair(atom(Prim[i].s), box(g_PRIM, i), g_env);
    }

    g_in = fopen((argc > 1 ? argv[1] : "prelude.lisp"), "r");

    using_history();

    int jmp_status;
    if ((jmp_status = setjmp(g_jmp_context)) != 0) {
        printf("ERR %d", jmp_status);
    }
    while (1) {
        gc();
        putchar('\n');
        snprintf(g_prompt, PROMPT_SIZE, "%u>", g_stack_pointer - g_heap_pointer / 8);
        print(eval(read(), g_env));
    }
}
