#include "moarvm.h"

static mp_int * MVM_get_bigint(MVMObject *obj) {
  return &((P6bigint *)obj)->body.i;
}

#define MVM_BIGINT_UNARY_OP(opname) \
void MVM_bigint_##opname(MVMObject *b, MVMObject *a) { \
    mp_int *ia = MVM_get_bigint(a); \
    mp_int *ib = MVM_get_bigint(b); \
    mp_##opname(ia, ib); \
}

#define MVM_BIGINT_BINARY_OP(opname) \
void MVM_bigint_##opname(MVMObject *c, MVMObject *a, MVMObject *b) { \
    mp_int *ia = MVM_get_bigint(a); \
    mp_int *ib = MVM_get_bigint(b); \
    mp_int *ic = MVM_get_bigint(c); \
    mp_##opname(ia, ib, ic); \
}

#define MVM_BIGINT_COMPARE_OP(opname) \
MVMint64 MVM_bigint_##opname(MVMObject *a, MVMObject *b) { \
    mp_int *ia = MVM_get_bigint(a); \
    mp_int *ib = MVM_get_bigint(b); \
    return (MVMint64) mp_##opname(ia, ib); \
}

MVM_BIGINT_UNARY_OP(abs)
MVM_BIGINT_UNARY_OP(neg)
/* unused */
/* MVM_BIGINT_UNARY_OP(sqrt) */

MVM_BIGINT_BINARY_OP(add)
MVM_BIGINT_BINARY_OP(sub)
MVM_BIGINT_BINARY_OP(mul)
MVM_BIGINT_BINARY_OP(mod)
MVM_BIGINT_BINARY_OP(gcd)
MVM_BIGINT_BINARY_OP(lcm)

MVM_BIGINT_BINARY_OP(or)
MVM_BIGINT_BINARY_OP(xor)
MVM_BIGINT_BINARY_OP(and)

MVM_BIGINT_COMPARE_OP(cmp)


void MVM_bigint_div(MVMObject *c, MVMObject *a, MVMObject *b) {
    mp_int *ia = MVM_get_bigint(a);
    mp_int *ib = MVM_get_bigint(b);
    mp_int *ic = MVM_get_bigint(c);
    mp_div(ia, ib, ic, NULL);
}

void MVM_bigint_shl(MVMObject *b, MVMObject *a, MVMint64 n) {
    mp_int *ia = MVM_get_bigint(a);
    mp_int *ib = MVM_get_bigint(b);
    mp_mul_2d(ia, n, ib);
}

void MVM_bigint_shr(MVMObject *b, MVMObject *a, MVMint64 n) {
    mp_int *ia = MVM_get_bigint(a);
    mp_int *ib = MVM_get_bigint(b);
    mp_div_2d(ia, n, ib, NULL);
}

void MVM_bigint_not(MVMObject *b, MVMObject *a) {
    mp_int *ia = MVM_get_bigint(a);
    mp_int *ib = MVM_get_bigint(b);
    /* two's complement not: add 1 and negate */
    mp_add_d(ia, 1, ib);
    mp_neg(ib, ib);
}

void MVM_bigint_expmod(MVMObject *d, MVMObject *a, MVMObject *b, MVMObject *c) {
    mp_int *ia = MVM_get_bigint(a);
    mp_int *ib = MVM_get_bigint(b);
    mp_int *ic = MVM_get_bigint(c);
    mp_int *id = MVM_get_bigint(d);
    mp_exptmod(ia, ib, ic, id);
}

void MVM_bigint_from_str(MVMObject *a, MVMuint8 *buf) {
    mp_int *i = MVM_get_bigint(a);
    char   *c = (char *)buf;
    mp_read_radix(i, buf, 10);
}

/* XXXX: This feels wrongly factored and possibly GC-unsafe */
MVMString * MVM_bigint_to_str(MVMThreadContext *tc, MVMObject *a) {
    mp_int *i = MVM_get_bigint(a);
    int len;
    char *buf;
    MVMString *result;
    mp_radix_size(i, 10, &len);
    buf = (char *) malloc(len);
    mp_toradix_n(i, buf, 10, len);
    result = MVM_string_ascii_decode(tc, tc->instance->VMString, buf, len - 1);
    free(buf);
    return result;
}
