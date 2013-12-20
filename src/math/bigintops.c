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

static mp_int * get_bigint(MVMThreadContext *tc, MVMObject *obj) {
    return (mp_int *)REPR(obj)->box_funcs.get_boxed_ref(tc, STABLE(obj), obj,
        OBJECT_BODY(obj), MVM_REPR_ID_P6bigint);
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

#define MVM_BIGINT_UNARY_OP(opname) \
void MVM_bigint_##opname(MVMThreadContext *tc, MVMObject *result, MVMObject *source) { \
    mp_int *ia = get_bigint(tc, source); \
    mp_int *ib = get_bigint(tc, result); \
    mp_##opname(ia, ib); \
}

#define MVM_BIGINT_BINARY_OP(opname) \
void MVM_bigint_##opname(MVMThreadContext *tc, MVMObject *result, MVMObject *a, MVMObject *b) { \
    mp_int *ia = get_bigint(tc, a); \
    mp_int *ib = get_bigint(tc, b); \
    mp_int *ic = get_bigint(tc, result); \
    mp_##opname(ia, ib, ic); \
}

#define MVM_BIGINT_BINARY_OP_2(opname) \
void MVM_bigint_##opname(MVMThreadContext *tc, MVMObject *result, MVMObject *a, MVMObject *b) { \
    mp_int *ia = get_bigint(tc, a); \
    mp_int *ib = get_bigint(tc, b); \
    mp_int *ic = get_bigint(tc, result); \
    two_complement_bitop(ia, ib, ic, mp_##opname); \
}

#define MVM_BIGINT_COMPARE_OP(opname) \
MVMint64 MVM_bigint_##opname(MVMThreadContext *tc, MVMObject *a, MVMObject *b) { \
    mp_int *ia = get_bigint(tc, a); \
    mp_int *ib = get_bigint(tc, b); \
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

MVM_BIGINT_BINARY_OP_2(or)
MVM_BIGINT_BINARY_OP_2(xor)
MVM_BIGINT_BINARY_OP_2(and)

MVM_BIGINT_COMPARE_OP(cmp)


void MVM_bigint_div(MVMThreadContext *tc, MVMObject *result, MVMObject *a, MVMObject *b) {
    mp_int *ia = get_bigint(tc, a);
    mp_int *ib = get_bigint(tc, b);
    mp_int *ic = get_bigint(tc, result);
    mp_div(ia, ib, ic, NULL);
}

void MVM_bigint_pow(MVMThreadContext *tc, MVMObject *result, MVMObject *a, MVMObject *b) {
    mp_int *base        = get_bigint(tc, a);
    mp_int *exponent    = get_bigint(tc, b);
    mp_int *ic          = get_bigint(tc, result);
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
}

void MVM_bigint_shl(MVMThreadContext *tc, MVMObject *result, MVMObject *a, MVMint64 n) {
    mp_int *ia = get_bigint(tc, a);
    mp_int *ib = get_bigint(tc, result);
    two_complement_shl(ib, ia, n);
}

void MVM_bigint_shr(MVMThreadContext *tc, MVMObject *result, MVMObject *a, MVMint64 n) {
    mp_int *ia = get_bigint(tc, a);
    mp_int *ib = get_bigint(tc, result);
    two_complement_shl(ib, ia, -n);
}

void MVM_bigint_not(MVMThreadContext *tc, MVMObject *result, MVMObject *a) {
    mp_int *ia = get_bigint(tc, a);
    mp_int *ib = get_bigint(tc, result);
    /* two's complement not: add 1 and negate */
    mp_add_d(ia, 1, ib);
    mp_neg(ib, ib);
}

void MVM_bigint_expmod(MVMThreadContext *tc, MVMObject *result, MVMObject *a, MVMObject *b, MVMObject *c) {
    mp_int *ia = get_bigint(tc, a);
    mp_int *ib = get_bigint(tc, b);
    mp_int *ic = get_bigint(tc, c);
    mp_int *id = get_bigint(tc, result);
    mp_exptmod(ia, ib, ic, id);
}

void MVM_bigint_from_str(MVMThreadContext *tc, MVMObject *a, MVMuint8 *buf) {
    mp_int *i = get_bigint(tc, a);
    mp_read_radix(i, (const char *)buf, 10);
}

/* XXXX: This feels wrongly factored and possibly GC-unsafe */
MVMString * MVM_bigint_to_str(MVMThreadContext *tc, MVMObject *a, int base) {
    mp_int *i = get_bigint(tc, a);
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

MVMnum64 MVM_bigint_to_num(MVMThreadContext *tc, MVMObject *a) {
    mp_int *ia = get_bigint(tc, a);
    return mp_get_double(ia);
}

void MVM_bigint_from_num(MVMThreadContext *tc, MVMObject *a, MVMnum64 n) {
    mp_int *ia = get_bigint(tc, a);
    from_num(n, ia);
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
    return !mp_iszero(get_bigint(tc, a));
}
