#include "moarvm.h"
#include <math.h>

static double mp_get_double(mp_int *a) {
    double d    = 0.0;
    double sign = SIGN(a) == MP_NEG ? -1.0 : 1.0;
    int i;
    if (USED(a) == 0)
        return d;
    if (USED(a) == 1)
        return sign * (double) DIGIT(a, 0);

    mp_clamp(a);
    i = USED(a) - 1;
    d = (double) DIGIT(a, i);
    i--;
    if (i == -1) {
        return sign * d;
    }
    d *= pow(2.0, DIGIT_BIT);
    d += (double) DIGIT(a, i);

    if (USED(a) > 2) {
        i--;
        d *= pow(2.0, DIGIT_BIT);
        d += (double) DIGIT(a, i);
    }

    d *= pow(2.0, DIGIT_BIT * i);
    return sign * d;
}

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

void MVM_bigint_pow(MVMObject *c, MVMObject *a, MVMObject *b) {
    mp_int *base = MVM_get_bigint(a);
    mp_int *exponent = MVM_get_bigint(b);
    mp_int *ic = MVM_get_bigint(c);
    mp_digit exponent_d = 0;
    int cmp = mp_cmp_d(exponent, 0);
    mp_init(ic);

    if (((cmp == MP_EQ) || (MP_EQ == mp_cmp_d(base, 1)))) {
        mp_set_int(ic, 1);
    }
    else {
        if ((cmp == MP_GT)) {
            exponent_d = mp_get_int(exponent);
            if ((MP_GT == mp_cmp_d(exponent, exponent_d))) {
                cmp = mp_cmp_d(base, 0);
                if (((MP_EQ == cmp) || (MP_EQ == mp_cmp_d(base, 1)))) {
                    mp_copy(base, ic);
                }
                else {
                    double ZERO = 0.0;
                    if ((MP_GT == cmp)) {
                        mp_set_int(ic, (double)1.0 / ZERO);
                    }
                    else {
                        mp_set_int(ic, (double)(-1.0) / ZERO);
                    }
                }
            }
            else {
                mp_expt_d(base, exponent_d, ic);
            }
        }
        else {
            double f_base = mp_get_double(base);
            double f_exp = mp_get_double(exponent);
            mp_set_int(ic, pow(f_base, f_exp));
        }
    }
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

MVMnum64 MVM_bigint_to_num(MVMThreadContext *tc, MVMObject *a) {
    mp_int *ia = MVM_get_bigint(a);
    return (MVMnum64)mp_get_double(ia);
}
