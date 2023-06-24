#include <stdint.h>
#include <string.h>

typedef double LispExpr;

#define TAG_BITS(x) *(uint64_t*)&x >> 48

#define ATOM_HEAP_ADDR (char*)g_cell

#define NCELLS 1024

unsigned g_heap_pointer = 0;
unsigned g_stack_pointer = NCELLS;

unsigned g_ATOM=0x7ff8, g_PRIM=0x7ff9, g_CONS=0x7ffa, g_CLOS=0x7ffb, g_NIL=0x7ffc;

LispExpr g_cell[NCELLS];

LispExpr g_nil, g_tru, g_err;

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
    while (i < g_heap_pointer && strcmp(ATOM_HEAP_ADDR + i, s)) i += strlen(ATOM_HEAP_ADDR + i) + 1;
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

int main() {
    g_nil = box(g_NIL, 0);
    g_tru = atom("#t");
    g_err = atom("ERR");
}
