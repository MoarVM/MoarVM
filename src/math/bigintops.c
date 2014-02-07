#include "moar.h"
#include <math.h>

static MVMnum64 mp_get_double(mp_int *a) {
    MVMnum64 d    = 0.0;
    MVMnum64 sign = SIGN(a) == MP_NEG ? -1.0 : 1.0;
    int i;
    if (USED(a) == 0)
        return d;
    if (USED(a) == 1)
        return sign * (MVMnum64) DIGIT(a, 0);

    mp_clamp(a);
    i = USED(a) - 1;
    d = (MVMnum64) DIGIT(a, i);
    i--;
    if (i == -1) {
        return sign * d;
    }
    d *= pow(2.0, DIGIT_BIT);
    d += (MVMnum64) DIGIT(a, i);

    if (USED(a) > 2) {
        i--;
        d *= pow(2.0, DIGIT_BIT);
        d += (MVMnum64) DIGIT(a, i);
    }

    d *= pow(2.0, DIGIT_BIT * i);
    return sign * d;
}

static void from_num(MVMnum64 d, mp_int *a) {
    MVMnum64 d_digit = pow(2, DIGIT_BIT);
    MVMnum64 da      = fabs(d);
    MVMnum64 upper;
    MVMnum64 lower;
    MVMnum64 lowest;
    MVMnum64 rest;
    int      digits  = 0;

    mp_zero(a);

    while (da > d_digit * d_digit * d_digit) {;
        da /= d_digit;
        digits++;
    }
    mp_grow(a, digits + 3);

    /* populate the top 3 digits */
    upper = da / (d_digit*d_digit);
    rest = fmod(da, d_digit*d_digit);
    lower = rest / d_digit;
    lowest = fmod(rest,d_digit );
    if (upper >= 1) {
        mp_set_long(a, (unsigned long) upper);
        mp_mul_2d(a, DIGIT_BIT , a);
        DIGIT(a, 0) = (mp_digit) lower;
        mp_mul_2d(a, DIGIT_BIT , a);
    } else {
        if (lower >= 1) {
            mp_set_long(a, (unsigned long) lower);
            mp_mul_2d(a, DIGIT_BIT , a);
            a->used = 2;
        } else {
            a->used = 1;
        }
    }
    DIGIT(a, 0) = (mp_digit) lowest;

    /* shift the rest */
    mp_mul_2d(a, DIGIT_BIT * digits, a);
    if (d < 0)
        mp_neg(a, a);
    mp_clamp(a);
    mp_shrink(a);
}

/* XXX This doesn't work at all on smallint. It survives while we upgrade all
 * the code calling it to cope with the smallint case. Then we toss it. */
static mp_int * get_bigint(MVMThreadContext *tc, MVMObject *obj) {
    MVMP6bigintBody *body = (MVMP6bigintBody *)REPR(obj)->box_funcs.get_boxed_ref(tc,
        STABLE(obj), obj, OBJECT_BODY(obj), MVM_REPR_ID_P6bigint);
    if (MVM_BIGINT_IS_BIG(body))
        return body->u.bigint;
    else
        MVM_exception_throw_adhoc(tc, "Incomplete smallint handling!");
}

/* Returns the body of a P6bigint, containing the bigint/smallint union, for
 * operations that want to explicitly handle the two. */
static MVMP6bigintBody * get_bigint_body(MVMThreadContext *tc, MVMObject *obj) {
    return (MVMP6bigintBody *)REPR(obj)->box_funcs.get_boxed_ref(tc,
        STABLE(obj), obj, OBJECT_BODY(obj), MVM_REPR_ID_P6bigint);
}

/* Checks if a bigint can be stored small. */
static int can_be_smallint(mp_int *i) {
    if (USED(i) != 1)
        return 0;
    return MVM_IS_32BIT_INT(DIGIT(i, 0));
}

/* Forces a bigint, even if we only have a smallint. Takes a parameter that
 * indicates where to allocate a temporary mp_int if needed. */
static mp_int * force_bigint(MVMP6bigintBody *body, mp_int **tmp) {
    if (MVM_BIGINT_IS_BIG(body)) {
        return body->u.bigint;
    }
    else {
        MVMint32 value = body->u.smallint.value;
        mp_int *i = malloc(sizeof(mp_int));
        mp_init(i);
        if (value >= 0) {
            mp_set_long(i, value);
        }
        else {
            mp_set_long(i, -value);
            mp_neg(i, i);
        }
        *tmp = i;
        return i;
    }
}

/* Clears an array that may contain tempory big ints. */
static void clear_temp_bigints(mp_int **ints, MVMint32 n) {
    MVMint32 i;
    for (i = 0; i < n; i++)
        if (ints[i]) {
            mp_clear(ints[i]);
            free(ints[i]);
        }
}

/* Stores an int64 in a bigint result body, either as a 32-bit smallint if it
 * is in range, or a big integer if not. */
static void store_int64_result(MVMP6bigintBody *body, MVMint64 result) {
    if (MVM_IS_32BIT_INT(result)) {
        body->u.smallint.flag = MVM_BIGINT_32_FLAG;
        body->u.smallint.value = (MVMint32)result;
    }
    else {
        mp_int *i = malloc(sizeof(mp_int));
        mp_init(i);
        if (result >= 0) {
            mp_set_long(i, result);
        }
        else {
            mp_set_long(i, -result);
            mp_neg(i, i);
        }
        body->u.bigint = i;
    }
}

/* Stores an bigint in a bigint result body, either as a 32-bit smallint if it
 * is in range, or a big integer if not. Clears and frees the passed bigint if
 * it is not being used. */
static void store_bigint_result(MVMP6bigintBody *body, mp_int *i) {
    if (can_be_smallint(i)) {
        body->u.smallint.flag = MVM_BIGINT_32_FLAG;
        body->u.smallint.value = SIGN(i) ? -DIGIT(i, 0) : DIGIT(i, 0);
        mp_clear(i);
        free(i);
    }
    else {
        body->u.bigint = i;
    }
}

static void grow_and_negate(mp_int *a, int size, mp_int *b) {
    int i;
    int actual_size = MAX(size, USED(a));
    mp_zero(b);
    mp_grow(b, actual_size);
    USED(b) = actual_size;
    for (i = 0; i < actual_size; i++) {
        DIGIT(b, i) = (~DIGIT(a, i)) & MP_MASK;
    }
    mp_add_d(b, 1, b);
}


static void two_complement_bitop(mp_int *a, mp_int *b, mp_int *c,
        int (*mp_bitop)(mp_int *, mp_int *, mp_int *)) {
    mp_int d;
    if (SIGN(a) ^ SIGN(b)) {
        /* exactly one of them is negative, so need to perform
         * some magic. tommath stores a sign bit, but Perl 6 expects
         * 2's complement */
        mp_init(&d);
        if (MP_NEG == SIGN(a)) {
            grow_and_negate(a, USED(b), &d);
            mp_bitop(&d, b, c);
        } else {
            grow_and_negate(b, USED(a), &d);
            mp_bitop(a, &d, c);
        }
        if (DIGIT(c, USED(c) - 1) & ((mp_digit)1<<(mp_digit)(DIGIT_BIT - 1))) {
            grow_and_negate(c, c->used, &d);
            mp_copy(&d, c);
            mp_neg(c, c);
        }
        mp_clear(&d);
    } else {
        mp_bitop(a, b, c);
    }

}

static void two_complement_shl(mp_int *result, mp_int *value, MVMint64 count) {
    if (count >= 0) {
        mp_mul_2d(value, count, result);
    }
    else if (MP_NEG == SIGN(value)) {
        /* fake two's complement semantics on top of sign-magnitude
         * algorithm appears to work [citation needed]
         */
        mp_add_d(value, 1, result);
        mp_div_2d(result, -count, result, NULL);
        mp_sub_d(result, 1, result);
    }
    else {
        mp_div_2d(value, -count, result, NULL);
    }
}

#define MVM_BIGINT_UNARY_OP(opname, SMALLINT_OP) \
void MVM_bigint_##opname(MVMThreadContext *tc, MVMObject *result, MVMObject *source) { \
    MVMP6bigintBody *ba = get_bigint_body(tc, source); \
    MVMP6bigintBody *bb = get_bigint_body(tc, result); \
    if (MVM_BIGINT_IS_BIG(ba)) { \
        mp_int *ia = ba->u.bigint; \
        mp_int *ib = malloc(sizeof(mp_int)); \
        mp_init(ib); \
        mp_##opname(ia, ib); \
        store_bigint_result(bb, ib); \
    } \
    else { \
        MVMint64 sb; \
        MVMint64 sa = ba->u.smallint.value; \
        SMALLINT_OP; \
        store_int64_result(bb, sb); \
    } \
}

#define MVM_BIGINT_BINARY_OP(opname) \
void MVM_bigint_##opname(MVMThreadContext *tc, MVMObject *result, MVMObject *a, MVMObject *b) { \
    mp_int *ia = get_bigint(tc, a); \
    mp_int *ib = get_bigint(tc, b); \
    mp_int *ic = get_bigint(tc, result); \
    mp_##opname(ia, ib, ic); \
}

#define MVM_BIGINT_BINARY_OP_SIMPLE(opname, SMALLINT_OP) \
void MVM_bigint_##opname(MVMThreadContext *tc, MVMObject *result, MVMObject *a, MVMObject *b) { \
    MVMP6bigintBody *ba = get_bigint_body(tc, a); \
    MVMP6bigintBody *bb = get_bigint_body(tc, b); \
    MVMP6bigintBody *bc = get_bigint_body(tc, result); \
    if (MVM_BIGINT_IS_BIG(ba) || MVM_BIGINT_IS_BIG(bb)) { \
        mp_int *tmp[2] = { NULL, NULL }; \
        mp_int *ia = force_bigint(ba, tmp); \
        mp_int *ib = force_bigint(bb, tmp); \
        mp_int *ic = malloc(sizeof(mp_int)); \
        mp_init(ic); \
        mp_##opname(ia, ib, ic); \
        store_bigint_result(bc, ic); \
        clear_temp_bigints(tmp, 2); \
    } \
    else { \
        MVMint64 sc; \
        MVMint64 sa = ba->u.smallint.value; \
        MVMint64 sb = bb->u.smallint.value; \
        SMALLINT_OP; \
        store_int64_result(bc, sc); \
    } \
}

#define MVM_BIGINT_BINARY_OP_2(opname, SMALLINT_OP) \
void MVM_bigint_##opname(MVMThreadContext *tc, MVMObject *result, MVMObject *a, MVMObject *b) { \
    MVMP6bigintBody *ba = get_bigint_body(tc, a); \
    MVMP6bigintBody *bb = get_bigint_body(tc, b); \
    MVMP6bigintBody *bc = get_bigint_body(tc, result); \
    if (MVM_BIGINT_IS_BIG(ba) || MVM_BIGINT_IS_BIG(bb)) { \
        mp_int *tmp[2] = { NULL, NULL }; \
        mp_int *ia = force_bigint(ba, tmp); \
        mp_int *ib = force_bigint(bb, tmp); \
        mp_int *ic = malloc(sizeof(mp_int)); \
        mp_init(ic); \
        two_complement_bitop(ia, ib, ic, mp_##opname); \
        store_bigint_result(bc, ic); \
        clear_temp_bigints(tmp, 2); \
    } \
    else { \
        MVMint64 sc; \
        MVMint64 sa = ba->u.smallint.value; \
        MVMint64 sb = bb->u.smallint.value; \
        SMALLINT_OP; \
        store_int64_result(bc, sc); \
    } \
}

MVM_BIGINT_UNARY_OP(abs, { sb = abs(sa); })
MVM_BIGINT_UNARY_OP(neg, { sb = -sa; })

/* unused */
/* MVM_BIGINT_UNARY_OP(sqrt) */

MVM_BIGINT_BINARY_OP_SIMPLE(add, { sc = sa + sb; })
MVM_BIGINT_BINARY_OP_SIMPLE(sub, { sc = sa - sb; })
MVM_BIGINT_BINARY_OP_SIMPLE(mul, { sc = sa * sb; })
MVM_BIGINT_BINARY_OP(gcd)
MVM_BIGINT_BINARY_OP(lcm)

MVM_BIGINT_BINARY_OP_2(or , { sc = sa | sb; })
MVM_BIGINT_BINARY_OP_2(xor, { sc = sa ^ sb; })
MVM_BIGINT_BINARY_OP_2(and, { sc = sa & sb; })

MVMint64 MVM_bigint_cmp(MVMThreadContext *tc, MVMObject *a, MVMObject *b) {
    MVMP6bigintBody *ba = get_bigint_body(tc, a);
    MVMP6bigintBody *bb = get_bigint_body(tc, b);
    if (MVM_BIGINT_IS_BIG(ba) || MVM_BIGINT_IS_BIG(bb)) {
        mp_int *tmp[2] = { NULL, NULL };
        mp_int *ia = force_bigint(ba, tmp);
        mp_int *ib = force_bigint(bb, tmp);
        MVMint64 r = (MVMint64)mp_cmp(ia, ib);
        clear_temp_bigints(tmp, 2);
        return r;
    }
    else {
        MVMint64 sc;
        MVMint64 sa = ba->u.smallint.value;
        MVMint64 sb = bb->u.smallint.value;
        return sa == sb ? 0 : sa <  sb ? -1 : 1;
    }
}

void MVM_bigint_mod(MVMThreadContext *tc, MVMObject *result, MVMObject *a, MVMObject *b) {
    mp_int *ia = get_bigint(tc, a);
    mp_int *ib = get_bigint(tc, b);
    mp_int *ic = get_bigint(tc, result);
    int mp_result;

    mp_result = mp_mod(ia, ib, ic);
    if (mp_result == MP_VAL)
        MVM_exception_throw_adhoc(tc, "Division by zero");
}

void MVM_bigint_div(MVMThreadContext *tc, MVMObject *result, MVMObject *a, MVMObject *b) {
    MVMP6bigintBody *ba = get_bigint_body(tc, a);
    MVMP6bigintBody *bb = get_bigint_body(tc, b);
    MVMP6bigintBody *bc = get_bigint_body(tc, result);
    mp_int *ia, *ib, *ic;
    int cmp_a;
    int cmp_b;
    mp_int remainder;
    mp_int intermediate;

    int mp_result;

    if (MVM_BIGINT_IS_BIG(ba)) {
        cmp_a = mp_cmp_d(ba->u.bigint, 0);
    } else {
        // we only care about MP_LT or !MP_LT, so we give MP_GT even for 0.
        cmp_a = ba->u.smallint.value < 0 ? MP_LT : MP_GT;
    }
    if (MVM_BIGINT_IS_BIG(bb)) {
        cmp_b = mp_cmp_d(bb->u.bigint, 0);
    } else {
        cmp_b = bb->u.smallint.value < 0 ? MP_LT : MP_GT;
    }

    if (MVM_BIGINT_IS_BIG(ba) || MVM_BIGINT_IS_BIG(bb)) {
        mp_int *tmp[2] = { NULL, NULL };
        ia = force_bigint(ba, tmp);
        ib = force_bigint(bb, tmp);

        ic = malloc(sizeof(mp_int));
        mp_init(ic);

        // if we do a div with a negative, we need to make sure
        // the result is floored rather than rounded towards
        // zero, like C and libtommath would do.
        if ((cmp_a == MP_LT) ^ (cmp_b == MP_LT)) {
            mp_init(&remainder);
            mp_init(&intermediate);
            mp_result = mp_div(ia, ib, &intermediate, &remainder);
            if (mp_result == MP_VAL) {
                mp_clear(&remainder);
                mp_clear(&intermediate);
                clear_temp_bigints(tmp, 2);
                MVM_exception_throw_adhoc(tc, "Division by zero");
            }
            if (mp_iszero(&remainder) == 0) {
                mp_sub_d(&intermediate, 1, ic);
            } else {
                mp_copy(&intermediate, ic);
            }
            mp_clear(&remainder);
            mp_clear(&intermediate);
        } else {
            mp_result = mp_div(ia, ib, ic, NULL);
            if (mp_result == MP_VAL) {
                clear_temp_bigints(tmp, 2);
                MVM_exception_throw_adhoc(tc, "Division by zero");
            }
        }
        store_bigint_result(bc, ic);
        clear_temp_bigints(tmp, 2);
    } else {
        MVMint32 num   = ba->u.smallint.value;
        MVMint32 denom = bb->u.smallint.value;
        MVMint32 result;
        if ((cmp_a == MP_LT) ^ (cmp_b == MP_LT)) {
            if (denom == 0) {
                MVM_exception_throw_adhoc(tc, "Division by zero");
            }
            if ((num % denom) != 0) {
                result = num / denom - 1;
            } else {
                result = num / denom;
            }
        } else {
            result = num / denom;
        }
        store_int64_result(bc, result);
    }
}

void MVM_bigint_pow(MVMThreadContext *tc, MVMObject *result, MVMObject *a, MVMObject *b) {
    MVMP6bigintBody *ba = get_bigint_body(tc, a);
    MVMP6bigintBody *bb = get_bigint_body(tc, b);
    MVMP6bigintBody *bc = get_bigint_body(tc, result);

    mp_int *tmp[2] = { NULL, NULL };
    mp_int *base        = force_bigint(ba, tmp);
    mp_int *exponent    = force_bigint(bb, tmp);
    mp_int *ic          = malloc(sizeof(mp_int));
    mp_digit exponent_d = 0;
    int cmp             = mp_cmp_d(exponent, 0);
    mp_init(ic);

    if ((cmp == MP_EQ) || (MP_EQ == mp_cmp_d(base, 1))) {
        mp_set_int(ic, 1);
    }
    else {
        if (cmp == MP_GT) {
            exponent_d = mp_get_int(exponent);
            if ((MP_GT == mp_cmp_d(exponent, exponent_d))) {
                cmp = mp_cmp_d(base, 0);
                if ((MP_EQ == cmp) || (MP_EQ == mp_cmp_d(base, 1))) {
                    mp_copy(base, ic);
                }
                else {
                    MVMnum64 ZERO = 0.0;
                    if (MP_GT == cmp) {
                        mp_set_int(ic, (MVMnum64)1.0 / ZERO);
                    }
                    else {
                        mp_set_int(ic, (MVMnum64)(-1.0) / ZERO);
                    }
                }
            }
            else {
                mp_expt_d(base, exponent_d, ic);
            }
        }
        else {
            MVMnum64 f_base = mp_get_double(base);
            MVMnum64 f_exp = mp_get_double(exponent);
            mp_set_int(ic, pow(f_base, f_exp));
        }
    }
    store_bigint_result(bc, ic);
    clear_temp_bigints(tmp, 2);
}

void MVM_bigint_shl(MVMThreadContext *tc, MVMObject *result, MVMObject *a, MVMint64 n) {
    MVMP6bigintBody *ba = get_bigint_body(tc, a);
    MVMP6bigintBody *bb = get_bigint_body(tc, result);
    if (MVM_BIGINT_IS_BIG(ba) || n >= 31) {
        mp_int *ia = ba->u.bigint;
        mp_int *ib = malloc(sizeof(mp_int));
        two_complement_shl(ib, ia, n);
        store_bigint_result(bb, ib);
    } else {
        MVMint64 result = ((MVMint64)ba->u.smallint.value) << n;
        store_int64_result(bb, result);
    }
}

void MVM_bigint_shr(MVMThreadContext *tc, MVMObject *result, MVMObject *a, MVMint64 n) {
    MVMP6bigintBody *ba = get_bigint_body(tc, a);
    MVMP6bigintBody *bb = get_bigint_body(tc, result);
    if (MVM_BIGINT_IS_BIG(ba)) {
        mp_int *ia = ba->u.bigint;
        mp_int *ib = malloc(sizeof(mp_int));
        mp_init(ib);
        two_complement_shl(ib, ia, -n);
        store_bigint_result(bb, ib);
    } else {
        MVMint32 value = ba->u.smallint.value;
        MVMint32 result = value >> n;
        store_int64_result(bb, result);
    }
}

void MVM_bigint_not(MVMThreadContext *tc, MVMObject *result, MVMObject *a) {
    MVMP6bigintBody *ba = get_bigint_body(tc, a);
    MVMP6bigintBody *bb = get_bigint_body(tc, result);
    if (MVM_BIGINT_IS_BIG(ba)) {
        mp_int *ia = ba->u.bigint;
        mp_int *ib = malloc(sizeof(mp_int));
        mp_init(ib);
        /* two's complement not: add 1 and negate */
        mp_add_d(ia, 1, ib);
        mp_neg(ib, ib);
        store_bigint_result(bb, ib);
    } else {
        MVMint32 value = ba->u.smallint.value;
        MVMint32 result = ~value;
        store_int64_result(bb, result);
    }
}

void MVM_bigint_expmod(MVMThreadContext *tc, MVMObject *result, MVMObject *a, MVMObject *b, MVMObject *c) {
    MVMP6bigintBody *ba = get_bigint_body(tc, a);
    MVMP6bigintBody *bb = get_bigint_body(tc, b);
    MVMP6bigintBody *bc = get_bigint_body(tc, c);
    MVMP6bigintBody *bd = get_bigint_body(tc, result);

    mp_int *tmp[3] = { NULL, NULL, NULL };

    mp_int *ia = force_bigint(ba, tmp);
    mp_int *ib = force_bigint(bb, tmp);
    mp_int *ic = force_bigint(bc, tmp);
    mp_int *id = malloc(sizeof(mp_int));
    mp_init(id);

    mp_exptmod(ia, ib, ic, id);
    store_bigint_result(bd, id);
    clear_temp_bigints(tmp, 3);
}

void MVM_bigint_from_str(MVMThreadContext *tc, MVMObject *a, MVMuint8 *buf) {
    MVMP6bigintBody *body = get_bigint_body(tc, a);
    mp_int *i = malloc(sizeof(mp_int));
    mp_init(i);
    mp_read_radix(i, (const char *)buf, 10);
    if (can_be_smallint(i)) {
        body->u.smallint.flag = MVM_BIGINT_32_FLAG;
        body->u.smallint.value = SIGN(i) ? -DIGIT(i, 0) : DIGIT(i, 0);
        mp_clear(i);
        free(i);
    }
    else {
        body->u.bigint = i;
    }
}

MVMString * MVM_bigint_to_str(MVMThreadContext *tc, MVMObject *a, int base) {
    MVMP6bigintBody *body = get_bigint_body(tc, a);
    if (MVM_BIGINT_IS_BIG(body)) {
        mp_int *i = body->u.bigint;
        int len;
        char *buf;
        MVMString *result;
        mp_radix_size(i, base, &len);
        buf = (char *) malloc(len);
        mp_toradix_n(i, buf, base, len);
        result = MVM_string_ascii_decode(tc, tc->instance->VMString, buf, len - 1);
        free(buf);
        return result;
    }
    else {
        if (base == 10) {
            return MVM_coerce_i_s(tc, body->u.smallint.value);
        }
        else {
            /* It's small, but shove it through bigint lib, as it knows how to
             * get other bases right. */
            mp_int i;
            int len;
            char *buf;
            MVMString *result;

            MVMint32 value = body->u.smallint.value;
            mp_init(&i);
            if (value >= 0) {
                mp_set_long(&i, value);
            }
            else {
                mp_set_long(&i, -value);
                mp_neg(&i, &i);
            }

            mp_radix_size(&i, base, &len);
            buf = (char *) malloc(len);
            mp_toradix_n(&i, buf, base, len);
            result = MVM_string_ascii_decode(tc, tc->instance->VMString, buf, len - 1);
            free(buf);
            mp_clear(&i);

            return result;
        }
    }
}

MVMnum64 MVM_bigint_to_num(MVMThreadContext *tc, MVMObject *a) {
    mp_int *ia = get_bigint(tc, a);
    return mp_get_double(ia);
}

void MVM_bigint_from_num(MVMThreadContext *tc, MVMObject *a, MVMnum64 n) {
    MVMP6bigintBody *ba = get_bigint_body(tc, a);
    mp_int *ia = malloc(sizeof(mp_int));
    mp_init(ia);
    from_num(n, ia);
    store_bigint_result(ba, ia);
}

MVMnum64 MVM_bigint_div_num(MVMThreadContext *tc, MVMObject *a, MVMObject *b) {
    MVMnum64 c;
    mp_int *ia = get_bigint(tc, a);
    mp_int *ib = get_bigint(tc, b);

    int max_size = DIGIT_BIT * MAX(USED(ia), USED(ib));
    if (max_size > 1023) {
        mp_int reduced_a, reduced_b;
        mp_init(&reduced_a);
        mp_init(&reduced_b);
        mp_div_2d(ia, max_size - 1023, &reduced_a, NULL);
        mp_div_2d(ib, max_size - 1023, &reduced_b, NULL);
        c = mp_get_double(&reduced_a) / mp_get_double(&reduced_b);
        mp_clear(&reduced_a);
        mp_clear(&reduced_b);
    } else {
        c = mp_get_double(ia) / mp_get_double(ib);
    }
    return c;
}

void MVM_bigint_rand(MVMThreadContext *tc, MVMObject *a, MVMObject *b) {
    mp_int *rnd = get_bigint(tc, a);
    mp_int *max = get_bigint(tc, b);
    mp_rand(rnd, USED(max) + 1);
    mp_mod(rnd, max, rnd);
}

MVMint64 MVM_bigint_is_prime(MVMThreadContext *tc, MVMObject *a, MVMint64 b) {
    /* mp_prime_is_prime returns True for 1, and I think
     * it's worth special-casing this particular number :-)
     */
    mp_int *ia = get_bigint(tc, a);
    if (mp_cmp_d(ia, 1) == MP_EQ) {
        return 0;
    }
    else {
        int result;
        mp_prime_is_prime(ia, b, &result);
        return result;
    }
}

MVMObject * MVM_bigint_radix(MVMThreadContext *tc, MVMint64 radix, MVMString *str, MVMint64 offset, MVMint64 flag, MVMObject *type) {
    MVMObject *result;
    MVMint64 chars  = NUM_GRAPHS(str);
    MVMuint16  neg  = 0;
    MVMint64   ch;
    mp_int zvalue;
    mp_int zbase;
    MVMObject *value_obj;
    mp_int *value;
    MVMObject *base_obj;
    mp_int *base;
    MVMObject *pos_obj;
    MVMint64   pos  = -1;

    if (radix > 36) {
        MVM_exception_throw_adhoc(tc, "Cannot convert radix of %d (max 36)", radix);
    }

    MVM_gc_root_temp_push(tc, (MVMCollectable **)&str);
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&type);

    /* initialize the object */
    result = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTArray);
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&result);

    mp_init(&zvalue);
    mp_init(&zbase);
    mp_set_int(&zbase, 1);

    value_obj = MVM_repr_alloc_init(tc, type);
    MVM_repr_push_o(tc, result, value_obj);
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&value_obj);

    base_obj = MVM_repr_alloc_init(tc, type);
    MVM_repr_push_o(tc, result, base_obj);

    value = get_bigint(tc, value_obj);
    base = get_bigint(tc, base_obj);

    mp_set_int(base, 1);

    ch = (offset < chars) ? MVM_string_get_codepoint_at_nocheck(tc, str, offset) : 0;
    if ((flag & 0x02) && (ch == '+' || ch == '-')) {
        neg = (ch == '-');
        offset++;
        ch = (offset < chars) ? MVM_string_get_codepoint_at_nocheck(tc, str, offset) : 0;
    }

   while (offset < chars) {
        if (ch >= '0' && ch <= '9') ch = ch - '0';
        else if (ch >= 'a' && ch <= 'z') ch = ch - 'a' + 10;
        else if (ch >= 'A' && ch <= 'Z') ch = ch - 'A' + 10;
        else break;
        if (ch >= radix) break;
        mp_mul_d(&zvalue, radix, &zvalue);
        mp_add_d(&zvalue, ch, &zvalue);
        mp_mul_d(&zbase, radix, &zbase);
        offset++; pos = offset;
        if (ch != 0 || !(flag & 0x04)) { mp_copy(&zvalue, value); mp_copy(&zbase, base); }
        if (offset >= chars) break;
        ch = MVM_string_get_codepoint_at_nocheck(tc, str, offset);
        if (ch != '_') continue;
        offset++;
        if (offset >= chars) break;
        ch = MVM_string_get_codepoint_at_nocheck(tc, str, offset);
    }

    mp_clear(&zvalue);
    mp_clear(&zbase);

    if (neg || flag & 0x01) {
        mp_neg(value, value);
    }

    pos_obj = MVM_repr_alloc_init(tc, type);
    MVM_repr_set_int(tc, pos_obj, pos);
    MVM_repr_push_o(tc, result, pos_obj);

    MVM_gc_root_temp_pop_n(tc, 4);

    return result;
}

/* returns 1 if a is too large to fit into an INTVAL without loss of
   information */
MVMint64 MVM_bigint_is_big(MVMThreadContext *tc, MVMObject *a) {
    mp_int *b = get_bigint(tc, a);
    MVMint64 is_big = b->used > 1;
    /* XXX somebody please check that on a 32 bit platform */
    if ( sizeof(MVMint64) * 8 < DIGIT_BIT && is_big == 0 && DIGIT(b, 0) & ~0x7FFFFFFFUL)
        is_big = 1;
    return is_big;
}

MVMint64 MVM_bigint_bool(MVMThreadContext *tc, MVMObject *a) {
    MVMP6bigintBody *body = get_bigint_body(tc, a);
    if (MVM_BIGINT_IS_BIG(body))
        return !mp_iszero(body->u.bigint);
    else
        return body->u.smallint.value != 0;
}
