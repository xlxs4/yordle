#include <stdint.h>

typedef double LispExpr;

#define T(x) *(uint64_t*)&x >> 48

unsigned ATOM=0x7ff8, PRIM=0x7ff9, CONS=0x7ffa, CLOS=0x7ffb, NIL=0x7ffc;

LispExpr box(unsigned tag, unsigned data) {
    LispExpr x;
    *(uint64_t*)&x = (uint64_t)tag << 48 | data;
    return x;
}

unsigned ord(LispExpr x) { return *(uint64_t*)&x; }

LispExpr num(LispExpr n) { return n; }

unsigned eq(LispExpr x, LispExpr y) { return *(uint64_t*)&x == *(uint64_t*)&y; }

unsigned not(LispExpr x) { return T(x) == NIL; }
