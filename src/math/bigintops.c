#include "moar.h"
#include <math.h>

#ifndef MANTISSA_BITS_IN_DOUBLE
#define MANTISSA_BITS_IN_DOUBLE 53
#endif
#ifndef MAX_BIGINT_BITS_IN_DOUBLE
#define MAX_BIGINT_BITS_IN_DOUBLE 1023
#endif

#ifndef MAX
    #define MAX(x,y) ((x)>(y)?(x):(y))
#endif

#ifndef MIN
    #define MIN(x,y) ((x)<(y)?(x):(y))
#endif

MVM_STATIC_INLINE void adjust_nursery(MVMThreadContext *tc, MVMP6bigintBody *body) {
    if (MVM_BIGINT_IS_BIG(body)) {
        int used = USED(body->u.bigint);
        int adjustment = MIN(used, 32768) & ~0x7;
        if (adjustment && (char *)tc->nursery_alloc_limit - adjustment > (char *)tc->nursery_alloc) {
            tc->nursery_alloc_limit = (char *)(tc->nursery_alloc_limit) - adjustment;
        }
    }
}

/* Taken from mp_set_long, but portably accepts a 64-bit number. */
int MVM_bigint_mp_set_uint64(mp_int * a, MVMuint64 b) {
  int     x, res;

  mp_zero (a);

  /* set four bits at a time */
  for (x = 0; x < sizeof(MVMuint64) * 2; x++) {
    /* shift the number up four bits */
    if ((res = mp_mul_2d (a, 4, a)) != MP_OKAY) {
      return res;
    }

    /* OR in the top four bits of the source */
    a->dp[0] |= (b >> ((sizeof(MVMuint64)) * 8 - 4)) & 15;

    /* shift the source up to the next four bits */
    b <<= 4;

    /* ensure that digits are not clamped off */
    a->used += 1;
  }
  mp_clamp(a);
  return MP_OKAY;
}

/*
 *  Convert to double, assumes IEEE-754 conforming double. Taken from
 *  https://github.com/czurnieden/libtommath/blob/master/bn_mp_get_double.c
 *  and slightly modified to fit MoarVM's setup.
 */
static const int mp_get_double_digits_needed
= ((MANTISSA_BITS_IN_DOUBLE + DIGIT_BIT) / DIGIT_BIT) + 1;
static const double mp_get_double_multiplier = (double)(MP_MASK + 1);

static MVMnum64 mp_get_double(mp_int *a, int shift) {
    MVMnum64 d;
    int i, limit, final_shift;
    d = 0.0;

    mp_clamp(a);
    i = a->used;
    limit = (i <= mp_get_double_digits_needed)
        ? 0 : i - mp_get_double_digits_needed;

    while (i-- > limit) {
        d += a->dp[i];
        d *= mp_get_double_multiplier;
    }

    if (a->sign == MP_NEG)
        d *= -1.0;
    final_shift = i * DIGIT_BIT - shift;
    if (final_shift < 0) {
        while (final_shift < -1023) {
            d *= pow(2.0, -1023);
            final_shift += 1023;
        }
    }
    else {
        while (final_shift > 1023) {
            d *= pow(2.0, 1023);
            final_shift -= 1023;
        }
    }
    d *= pow(2.0, final_shift);
    return d;
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
        MVM_bigint_mp_set_uint64(a, (MVMuint64) upper);
        mp_mul_2d(a, DIGIT_BIT , a);
        DIGIT(a, 0) = (mp_digit) lower;
        mp_mul_2d(a, DIGIT_BIT , a);
    } else {
        if (lower >= 1) {
            MVM_bigint_mp_set_uint64(a, (MVMuint64) lower);
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

/* Returns the body of a P6bigint, containing the bigint/smallint union, for
 * operations that want to explicitly handle the two. */
static MVMP6bigintBody * get_bigint_body(MVMThreadContext *tc, MVMObject *obj) {
    if (IS_CONCRETE(obj))
        return (MVMP6bigintBody *)REPR(obj)->box_funcs.get_boxed_ref(tc,
            STABLE(obj), obj, OBJECT_BODY(obj), MVM_REPR_ID_P6bigint);
    else
        MVM_exception_throw_adhoc(tc,
            "Can only perform big integer operations on concrete objects");
}

/* Checks if a bigint can be stored small. */
static int can_be_smallint(const mp_int *i) {
    if (USED(i) != 1)
        return 0;
    return MVM_IS_32BIT_INT(DIGIT(i, 0));
}

/* Forces a bigint, even if we only have a smallint. Takes a parameter that
 * indicates where to allocate a temporary mp_int if needed. */
static mp_int * force_bigint(const MVMP6bigintBody *body, mp_int **tmp) {
    if (MVM_BIGINT_IS_BIG(body)) {
        return body->u.bigint;
    }
    else {
        MVMint64 value = body->u.smallint.value;
        mp_int *i = MVM_malloc(sizeof(mp_int));
        mp_init(i);
        if (value >= 0) {
            mp_set_long(i, value);
        }
        else {
            mp_set_long(i, -value);
            mp_neg(i, i);
        }
        while (*tmp)
            tmp++;
        *tmp = i;
        return i;
    }
}

/* Clears an array that may contain tempory big ints. */
static void clear_temp_bigints(mp_int *const *ints, MVMint32 n) {
    MVMint32 i;
    for (i = 0; i < n; i++)
        if (ints[i]) {
            mp_clear(ints[i]);
            MVM_free(ints[i]);
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
        mp_int *i = MVM_malloc(sizeof(mp_int));
        mp_init(i);
        if (result >= 0) {
            MVM_bigint_mp_set_uint64(i, (MVMuint64)result);
        }
        else {
            MVM_bigint_mp_set_uint64(i, (MVMuint64)-result);
            mp_neg(i, i);
        }
        body->u.bigint = i;
    }
}

/* Stores a bigint in a bigint result body, either as a 32-bit smallint if it
 * is in range, or a big integer if not. Clears and frees the passed bigint if
 * it is not being used. */
static void store_bigint_result(MVMP6bigintBody *body, mp_int *i) {
    if (can_be_smallint(i)) {
        body->u.smallint.flag = MVM_BIGINT_32_FLAG;
        body->u.smallint.value = SIGN(i) == MP_NEG ? -DIGIT(i, 0) : DIGIT(i, 0);
        mp_clear(i);
        MVM_free(i);
    }
    else {
        body->u.bigint = i;
    }
}

/* Bitops on libtomath (no two's complement API) are horrendously inefficient and
 * really should be hand-coded to work DIGIT-by-DIGIT with in-loop carry
 * handling.  For now we have these fixups.
 *
 * The following inverts the bits of a negative bigint, adds 1 to that, and
 * appends sign-bit extension DIGITs to it to give us a 2s complement
 * representation in memory.  Do not call it on positive bigints.
 */
static void grow_and_negate(const mp_int *a, int size, mp_int *b) {
    int i;
    /* Always add an extra DIGIT so we can tell positive values
     * with a one in the highest bit apart from negative values.
     */
    int actual_size = MAX(size, USED(a)) + 1;

    SIGN(b) = MP_ZPOS;
    mp_grow(b, actual_size);
    USED(b) = actual_size;
    for (i = 0; i < USED(a); i++) {
        DIGIT(b, i) = (~DIGIT(a, i)) & MP_MASK;
    }
    for (; i < actual_size; i++) {
        DIGIT(b, i) = MP_MASK;
    }
    /* Note: This add cannot cause another grow assuming nobody ever
     * tries to use tommath -0 for anything, and nobody tries to use
     * this on positive bigints.
     */
    mp_add_d(b, 1, b);
}

static void two_complement_bitop(mp_int *a, mp_int *b, mp_int *c,
                                 int (*mp_bitop)(mp_int *, mp_int *, mp_int *)) {

    mp_int d;
    mp_int e;
    mp_int *f;
    mp_int *g;

    f = a;
    g = b;
    if (MP_NEG == SIGN(a)) {
        mp_init(&d);
        grow_and_negate(a, USED(b), &d);
        f = &d;
    }
    if (MP_NEG == SIGN(b)) {
        mp_init(&e);
        grow_and_negate(b, USED(a), &e);
        g = &e;
    }
    /* f and g now guaranteed to each point to positive bigints containing
     * a two's complement representation of the values in a and b.  If either
     * a or b was negative, the representation is one tomath "digit" longer
     * than it need be and sign extended.
     */
    mp_bitop(f, g, c);
    if (f == &d) mp_clear(&d);
    if (g == &e) mp_clear(&e);
    /* Use the fact that tomath clamps to detect results that should be
     * signed.  If we created extra tomath "digits" and they resulted in
     * sign bits of 0, they have been clamped away.  If the resulting sign
     * bits were 1, they remain, and c will have more digits than either of
     * original operands.  Note this only works because we do not
     * support NOR/NAND/NXOR, and so two zero sign bits can never create 1s.
     */
    if (USED(c) > MAX(USED(a),USED(b))) {
        int i;
        for (i = 0; i < USED(c); i++) {
            DIGIT(c, i) = (~DIGIT(c, i)) & MP_MASK;
        }
        mp_add_d(c, 1, c);
        mp_neg(c, c);
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
MVMObject * MVM_bigint_##opname(MVMThreadContext *tc, MVMObject *result_type, MVMObject *source) { \
    MVMP6bigintBody *bb; \
    MVMObject *result; \
    MVMROOT(tc, source, { \
        result = MVM_repr_alloc_init(tc, result_type);\
    }); \
    bb = get_bigint_body(tc, result); \
    if (!IS_CONCRETE(source)) { \
        store_int64_result(bb, 0); \
    } \
    else { \
        MVMP6bigintBody *ba = get_bigint_body(tc, source); \
        if (MVM_BIGINT_IS_BIG(ba)) { \
            mp_int *ia = ba->u.bigint; \
            mp_int *ib = MVM_malloc(sizeof(mp_int)); \
            mp_init(ib); \
            mp_##opname(ia, ib); \
            store_bigint_result(bb, ib); \
            adjust_nursery(tc, bb); \
        } \
        else { \
            MVMint64 sb; \
            MVMint64 sa = ba->u.smallint.value; \
            SMALLINT_OP; \
            store_int64_result(bb, sb); \
        } \
    } \
    return result; \
}

#define MVM_BIGINT_BINARY_OP(opname) \
MVMObject * MVM_bigint_##opname(MVMThreadContext *tc, MVMObject *result_type, MVMObject *a, MVMObject *b) { \
    MVMP6bigintBody *ba, *bb, *bc; \
    MVMObject *result; \
    mp_int *tmp[2] = { NULL, NULL }; \
    mp_int *ia, *ib, *ic; \
    MVMROOT2(tc, a, b, { \
        result = MVM_repr_alloc_init(tc, result_type);\
    }); \
    ba = get_bigint_body(tc, a); \
    bb = get_bigint_body(tc, b); \
    bc = get_bigint_body(tc, result); \
    ia = force_bigint(ba, tmp); \
    ib = force_bigint(bb, tmp); \
    ic = MVM_malloc(sizeof(mp_int)); \
    mp_init(ic); \
    mp_##opname(ia, ib, ic); \
    store_bigint_result(bc, ic); \
    clear_temp_bigints(tmp, 2); \
    adjust_nursery(tc, bc); \
    return result; \
}

#define MVM_BIGINT_BINARY_OP_SIMPLE(opname, SMALLINT_OP) \
void MVM_bigint_fallback_##opname(MVMThreadContext *tc, MVMP6bigintBody *ba, MVMP6bigintBody *bb, \
                                  MVMP6bigintBody *bc) { \
    mp_int *tmp[2] = { NULL, NULL }; \
    mp_int *ia, *ib, *ic; \
    ia = force_bigint(ba, tmp); \
    ib = force_bigint(bb, tmp); \
    ic = MVM_malloc(sizeof(mp_int)); \
    mp_init(ic); \
    mp_##opname(ia, ib, ic); \
    store_bigint_result(bc, ic); \
    clear_temp_bigints(tmp, 2); \
    adjust_nursery(tc, bc); \
} \
MVMObject * MVM_bigint_##opname(MVMThreadContext *tc, MVMObject *result_type, MVMObject *a, MVMObject *b) { \
    MVMP6bigintBody *ba, *bb, *bc; \
    MVMObject *result; \
    ba = get_bigint_body(tc, a); \
    bb = get_bigint_body(tc, b); \
    if (MVM_BIGINT_IS_BIG(ba) || MVM_BIGINT_IS_BIG(bb)) { \
        mp_int *tmp[2] = { NULL, NULL }; \
        mp_int *ia, *ib, *ic; \
        MVMROOT2(tc, a, b, { \
            result = MVM_repr_alloc_init(tc, result_type);\
        }); \
        ba = get_bigint_body(tc, a); \
        bb = get_bigint_body(tc, b); \
        bc = get_bigint_body(tc, result); \
        ia = force_bigint(ba, tmp); \
        ib = force_bigint(bb, tmp); \
        ic = MVM_malloc(sizeof(mp_int)); \
        mp_init(ic); \
        mp_##opname(ia, ib, ic); \
        store_bigint_result(bc, ic); \
        clear_temp_bigints(tmp, 2); \
        adjust_nursery(tc, bc); \
    } \
    else { \
        MVMint64 sc; \
        MVMint64 sa = ba->u.smallint.value; \
        MVMint64 sb = bb->u.smallint.value; \
        SMALLINT_OP; \
        result = MVM_intcache_get(tc, result_type, sc); \
        if (result) \
            return result; \
        result = MVM_repr_alloc_init(tc, result_type);\
        bc = get_bigint_body(tc, result); \
        store_int64_result(bc, sc); \
    } \
    return result; \
}

#define MVM_BIGINT_BINARY_OP_2(opname, SMALLINT_OP) \
MVMObject * MVM_bigint_##opname(MVMThreadContext *tc, MVMObject *result_type, MVMObject *a, MVMObject *b) { \
    MVMP6bigintBody *ba = get_bigint_body(tc, a); \
    MVMP6bigintBody *bb = get_bigint_body(tc, b); \
    MVMP6bigintBody *bc; \
    MVMObject *result; \
    MVMROOT2(tc, a, b, { \
        result = MVM_repr_alloc_init(tc, result_type);\
    }); \
    bc = get_bigint_body(tc, result); \
    if (MVM_BIGINT_IS_BIG(ba) || MVM_BIGINT_IS_BIG(bb)) { \
        mp_int *tmp[2] = { NULL, NULL }; \
        mp_int *ia = force_bigint(ba, tmp); \
        mp_int *ib = force_bigint(bb, tmp); \
        mp_int *ic = MVM_malloc(sizeof(mp_int)); \
        mp_init(ic); \
        two_complement_bitop(ia, ib, ic, mp_##opname); \
        store_bigint_result(bc, ic); \
        clear_temp_bigints(tmp, 2); \
        adjust_nursery(tc, bc); \
    } \
    else { \
        MVMint64 sc; \
        MVMint64 sa = ba->u.smallint.value; \
        MVMint64 sb = bb->u.smallint.value; \
        SMALLINT_OP; \
        store_int64_result(bc, sc); \
    } \
    return result; \
}

MVM_BIGINT_UNARY_OP(abs, { sb = labs(sa); })
MVM_BIGINT_UNARY_OP(neg, { sb = -sa; })

/* unused */
/* MVM_BIGINT_UNARY_OP(sqrt) */

MVM_BIGINT_BINARY_OP_SIMPLE(add, { sc = sa + sb; })
MVM_BIGINT_BINARY_OP_SIMPLE(sub, { sc = sa - sb; })
MVM_BIGINT_BINARY_OP_SIMPLE(mul, { sc = sa * sb; })
MVM_BIGINT_BINARY_OP(lcm)

MVMObject *MVM_bigint_gcd(MVMThreadContext *tc, MVMObject *result_type, MVMObject *a, MVMObject *b) {
    MVMP6bigintBody *ba = get_bigint_body(tc, a);
    MVMP6bigintBody *bb = get_bigint_body(tc, b);
    MVMP6bigintBody *bc;
    MVMObject       *result;

    MVMROOT2(tc, a, b, {
        result = MVM_repr_alloc_init(tc, result_type);
    });

    bc = get_bigint_body(tc, result);

    if (MVM_BIGINT_IS_BIG(ba) || MVM_BIGINT_IS_BIG(bb)) {
        mp_int *tmp[2] = { NULL, NULL };
        mp_int *ia = force_bigint(ba, tmp);
        mp_int *ib = force_bigint(bb, tmp);
        mp_int *ic = MVM_malloc(sizeof(mp_int));
        mp_init(ic);
        mp_gcd(ia, ib, ic);
        store_bigint_result(bc, ic);
        clear_temp_bigints(tmp, 2);
        adjust_nursery(tc, bc);
    } else {
        MVMint32 sa = ba->u.smallint.value;
        MVMint32 sb = bb->u.smallint.value;
        MVMint32 t;
        sa = abs(sa);
        sb = abs(sb);
        while (sb != 0) {
            t  = sb;
            sb = sa % sb;
            sa = t;
        }
        store_int64_result(bc, sa);
    }

    return result;
}

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
        MVMint64 sa = ba->u.smallint.value;
        MVMint64 sb = bb->u.smallint.value;
        return sa == sb ? 0 : sa <  sb ? -1 : 1;
    }
}

MVMObject * MVM_bigint_mod(MVMThreadContext *tc, MVMObject *result_type, MVMObject *a, MVMObject *b) {
    MVMP6bigintBody *ba = get_bigint_body(tc, a);
    MVMP6bigintBody *bb = get_bigint_body(tc, b);
    MVMP6bigintBody *bc;

    MVMObject *result;

    MVMROOT2(tc, a, b, {
        result = MVM_repr_alloc_init(tc, result_type);
    });

    bc = get_bigint_body(tc, result);

    /* XXX the behavior of C's mod operator is not correct
     * for our purposes. So we rely on mp_mod for all our modulus
     * calculations for now. */
    if (1 || MVM_BIGINT_IS_BIG(ba) || MVM_BIGINT_IS_BIG(bb)) {
        mp_int *tmp[2] = { NULL, NULL };
        mp_int *ia = force_bigint(ba, tmp);
        mp_int *ib = force_bigint(bb, tmp);
        mp_int *ic = MVM_malloc(sizeof(mp_int));
        int mp_result;

        mp_init(ic);

        mp_result = mp_mod(ia, ib, ic);
        clear_temp_bigints(tmp, 2);

        if (mp_result == MP_VAL) {
            MVM_exception_throw_adhoc(tc, "Division by zero");
        }
        store_bigint_result(bc, ic);
        adjust_nursery(tc, bc);
    } else {
        store_int64_result(bc, ba->u.smallint.value % bb->u.smallint.value);
    }

    return result;
}

MVMObject *MVM_bigint_div(MVMThreadContext *tc, MVMObject *result_type, MVMObject *a, MVMObject *b) {
    MVMP6bigintBody *ba = get_bigint_body(tc, a);
    MVMP6bigintBody *bb = get_bigint_body(tc, b);
    MVMP6bigintBody *bc;
    mp_int *ia, *ib, *ic;
    int cmp_a;
    int cmp_b;
    mp_int remainder;
    mp_int intermediate;
    MVMObject *result;

    int mp_result;

    if (!MVM_BIGINT_IS_BIG(bb) && bb->u.smallint.value == 1 && STABLE(a) == STABLE(b)) {
        return a;
    }

    MVMROOT2(tc, a, b, {
        result = MVM_repr_alloc_init(tc, result_type);
    });

    bc = get_bigint_body(tc, result);

    /* we only care about MP_LT or !MP_LT, so we give MP_GT even for 0. */
    if (MVM_BIGINT_IS_BIG(ba)) {
        cmp_a = !mp_iszero(ba->u.bigint) && SIGN(ba->u.bigint) == MP_NEG ? MP_LT : MP_GT;
    } else {
        cmp_a = ba->u.smallint.value < 0 ? MP_LT : MP_GT;
    }
    if (MVM_BIGINT_IS_BIG(bb)) {
        cmp_b = !mp_iszero(bb->u.bigint) && SIGN(bb->u.bigint) == MP_NEG ? MP_LT : MP_GT;
    } else {
        cmp_b = bb->u.smallint.value < 0 ? MP_LT : MP_GT;
    }

    if (MVM_BIGINT_IS_BIG(ba) || MVM_BIGINT_IS_BIG(bb)) {
        mp_int *tmp[2] = { NULL, NULL };
        ia = force_bigint(ba, tmp);
        ib = force_bigint(bb, tmp);

        ic = MVM_malloc(sizeof(mp_int));
        mp_init(ic);

        /* if we do a div with a negative, we need to make sure
         * the result is floored rather than rounded towards
         * zero, like C and libtommath would do. */
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
        adjust_nursery(tc, bc);
    } else {
        MVMint32 num   = ba->u.smallint.value;
        MVMint32 denom = bb->u.smallint.value;
        MVMint32 value;
        if ((cmp_a == MP_LT) ^ (cmp_b == MP_LT)) {
            if (denom == 0) {
                MVM_exception_throw_adhoc(tc, "Division by zero");
            }
            if ((num % denom) != 0) {
                value = num / denom - 1;
            } else {
                value = num / denom;
            }
        } else {
            value = num / denom;
        }
        store_int64_result(bc, value);
    }

    return result;
}

MVMObject * MVM_bigint_pow(MVMThreadContext *tc, MVMObject *a, MVMObject *b,
        MVMObject *num_type, MVMObject *int_type) {
    MVMP6bigintBody *ba = get_bigint_body(tc, a);
    MVMP6bigintBody *bb = get_bigint_body(tc, b);
    MVMObject       *r  = NULL;

    mp_int *tmp[2] = { NULL, NULL };
    mp_int *base        = force_bigint(ba, tmp);
    mp_int *exponent    = force_bigint(bb, tmp);
    mp_digit exponent_d = 0;

    if (mp_iszero(exponent) || (MP_EQ == mp_cmp_d(base, 1))) {
        r = MVM_repr_box_int(tc, int_type, 1);
    }
    else if (SIGN(exponent) == MP_ZPOS) {
        exponent_d = mp_get_int(exponent);
        if ((MP_GT == mp_cmp_d(exponent, exponent_d))) {
            if (mp_iszero(base)) {
                r = MVM_repr_box_int(tc, int_type, 0);
            }
            else if (mp_get_int(base) == 1) {
                r = MVM_repr_box_int(tc, int_type, MP_ZPOS == SIGN(base) || mp_iseven(exponent) ? 1 : -1);
            }
            else {
                MVMnum64 inf;
                if (MP_ZPOS == SIGN(base) || mp_iseven(exponent)) {
                    inf = MVM_num_posinf(tc);
                }
                else {
                    inf = MVM_num_neginf(tc);
                }
                r = MVM_repr_box_num(tc, num_type, inf);
            }
        }
        else {
            mp_int *ic = MVM_malloc(sizeof(mp_int));
            MVMP6bigintBody *resbody;
            mp_init(ic);
            MVM_gc_mark_thread_blocked(tc);
            mp_expt_d(base, exponent_d, ic);
            MVM_gc_mark_thread_unblocked(tc);
            r = MVM_repr_alloc_init(tc, int_type);
            resbody = get_bigint_body(tc, r);
            store_bigint_result(resbody, ic);
            adjust_nursery(tc, resbody);
        }
    }
    else {
        MVMnum64 f_base = mp_get_double(base, 0);
        MVMnum64 f_exp = mp_get_double(exponent, 0);
        r = MVM_repr_box_num(tc, num_type, pow(f_base, f_exp));
    }
    clear_temp_bigints(tmp, 2);
    return r;
}

MVMObject *MVM_bigint_shl(MVMThreadContext *tc, MVMObject *result_type, MVMObject *a, MVMint64 n) {
    MVMP6bigintBody *ba = get_bigint_body(tc, a);
    MVMP6bigintBody *bb;
    MVMObject       *result;

    MVMROOT(tc, a, {
        result = MVM_repr_alloc_init(tc, result_type);
    });

    bb = get_bigint_body(tc, result);

    if (MVM_BIGINT_IS_BIG(ba) || n >= 31) {
        mp_int *tmp[1] = { NULL };
        mp_int *ia = force_bigint(ba, tmp);
        mp_int *ib = MVM_malloc(sizeof(mp_int));
        mp_init(ib);
        two_complement_shl(ib, ia, n);
        store_bigint_result(bb, ib);
        clear_temp_bigints(tmp, 1);
        adjust_nursery(tc, bb);
    } else {
        MVMint64 value;
        if (n < 0)
            value = ((MVMint64)ba->u.smallint.value) >> -n;
        else
            value = ((MVMint64)ba->u.smallint.value) << n;
        store_int64_result(bb, value);
    }

    return result;
}
/* Checks if a MVMP6bigintBody is negative. Handles cases where it is stored as
 * a small int as well as cases when it is stored as a bigint */
int BIGINT_IS_NEGATIVE (MVMP6bigintBody *ba) {
    mp_int *mp_a = ba->u.bigint;
    if (MVM_BIGINT_IS_BIG(ba)) {
        return SIGN(mp_a) == MP_NEG;
    }
    else {
        return ba->u.smallint.value < 0;
    }
}
MVMObject *MVM_bigint_shr(MVMThreadContext *tc, MVMObject *result_type, MVMObject *a, MVMint64 n) {
    MVMP6bigintBody *ba = get_bigint_body(tc, a);
    MVMP6bigintBody *bb;
    MVMObject       *result;

    MVMROOT(tc, a, {
        result = MVM_repr_alloc_init(tc, result_type);
    });

    bb = get_bigint_body(tc, result);

    if (MVM_BIGINT_IS_BIG(ba) || n < 0) {
        mp_int *tmp[1] = { NULL };
        mp_int *ia = force_bigint(ba, tmp);
        mp_int *ib = MVM_malloc(sizeof(mp_int));
        mp_init(ib);
        two_complement_shl(ib, ia, -n);
        store_bigint_result(bb, ib);
        clear_temp_bigints(tmp, 1);
        adjust_nursery(tc, bb);
    } else if (n >= 32) {
        store_int64_result(bb, BIGINT_IS_NEGATIVE(ba) ? -1 : 0);
    } else {
        MVMint32 value = ba->u.smallint.value;
        value = value >> n;
        store_int64_result(bb, value);
    }

    return result;
}

MVMObject *MVM_bigint_not(MVMThreadContext *tc, MVMObject *result_type, MVMObject *a) {
    MVMP6bigintBody *ba = get_bigint_body(tc, a);
    MVMP6bigintBody *bb;
    MVMObject       *result;

    MVMROOT(tc, a, {
        result = MVM_repr_alloc_init(tc, result_type);
    });

    bb = get_bigint_body(tc, result);

    if (MVM_BIGINT_IS_BIG(ba)) {
        mp_int *ia = ba->u.bigint;
        mp_int *ib = MVM_malloc(sizeof(mp_int));
        mp_init(ib);
        /* two's complement not: add 1 and negate */
        mp_add_d(ia, 1, ib);
        mp_neg(ib, ib);
        store_bigint_result(bb, ib);
        adjust_nursery(tc, bb);
    } else {
        MVMint32 value = ba->u.smallint.value;
        value = ~value;
        store_int64_result(bb, value);
    }

    return result;
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
    mp_int *id = MVM_malloc(sizeof(mp_int));
    mp_init(id);

    mp_exptmod(ia, ib, ic, id);
    store_bigint_result(bd, id);
    clear_temp_bigints(tmp, 3);
    adjust_nursery(tc, bd);
}

void MVM_bigint_from_str(MVMThreadContext *tc, MVMObject *a, const char *buf) {
    MVMP6bigintBody *body = get_bigint_body(tc, a);
    mp_int *i = alloca(sizeof(mp_int));
    mp_init(i);
    mp_read_radix(i, buf, 10);
    adjust_nursery(tc, body);
    if (can_be_smallint(i)) {
        body->u.smallint.flag = MVM_BIGINT_32_FLAG;
        body->u.smallint.value = SIGN(i) == MP_NEG ? -DIGIT(i, 0) : DIGIT(i, 0);
        mp_clear(i);
    }
    else {
        mp_int *i_cpy = MVM_malloc(sizeof(mp_int));
        memcpy(i_cpy, i, sizeof(mp_int));
        body->u.bigint = i_cpy;
    }
}
#define can_fit_into_8bit(g) ((-128 <= (g) && (g) <= 127))
MVMObject * MVM_coerce_sI(MVMThreadContext *tc, MVMString *s, MVMObject *type) {
    char *buf = NULL;
    int is_malloced = 0;
    MVMStringIndex i;
    MVMObject *a = MVM_repr_alloc_init(tc, type);
    if (s->body.num_graphs < 120) {
        buf = alloca(s->body.num_graphs + 1);
    }
    else {
        buf = MVM_malloc(s->body.num_graphs + 1);
        is_malloced = 1;
    }
    /* We just ignore synthetics since parsing will fail if a synthetic is
     * encountered anyway. */
    switch (s->body.storage_type) {
        case MVM_STRING_GRAPHEME_ASCII:
        case MVM_STRING_GRAPHEME_8:
            memcpy(buf, s->body.storage.blob_8, sizeof(MVMGrapheme8) * s->body.num_graphs);
            break;
        case MVM_STRING_GRAPHEME_32:
            for (i = 0; i < s->body.num_graphs; i++) {
                buf[i] = can_fit_into_8bit(s->body.storage.blob_32[i])
                    ? s->body.storage.blob_32[i]
                    : '?'; /* Add a filler bogus char if it can't fit */
            }
            break;
        case MVM_STRING_STRAND: {
            MVMGraphemeIter gi;
            MVM_string_gi_init(tc, &gi, s);
            for (i = 0; i < s->body.num_graphs; i++) {
                MVMGrapheme32 g = MVM_string_gi_get_grapheme(tc, &gi);
                buf[i] = can_fit_into_8bit(g) ? g : '?';
            }
            break;
        }
        default:
            if (is_malloced) MVM_free(buf);
            MVM_exception_throw_adhoc(tc, "String corruption found in MVM_coerce_sI");
    }
    buf[s->body.num_graphs] = 0;

    MVM_bigint_from_str(tc, a, buf);
    if (is_malloced) MVM_free(buf);
    return a;
}

MVMObject * MVM_bigint_from_bigint(MVMThreadContext *tc, MVMObject *result_type, MVMObject *a) {
    MVMP6bigintBody *a_body;
    MVMP6bigintBody *r_body;
    MVMObject       *result;

    MVMROOT(tc, a, {
        result = MVM_repr_alloc_init(tc, result_type);
    });

    a_body = get_bigint_body(tc, a);
    r_body = get_bigint_body(tc, result);

    if (MVM_BIGINT_IS_BIG(a_body)) {
        mp_int *i = MVM_malloc(sizeof(mp_int));
        mp_init_copy(i, a_body->u.bigint);
        store_bigint_result(r_body, i);
        adjust_nursery(tc, r_body);
    }
    else {
        r_body->u.smallint       = a_body->u.smallint;
        r_body->u.smallint.flag  = a_body->u.smallint.flag;
        r_body->u.smallint.value = a_body->u.smallint.value;
    }

    return result;
}

MVMString * MVM_bigint_to_str(MVMThreadContext *tc, MVMObject *a, int base) {
    MVMP6bigintBody *body = get_bigint_body(tc, a);
    if (MVM_BIGINT_IS_BIG(body)) {
        mp_int *i = body->u.bigint;
        int len;
        char *buf;
        MVMString *result;
        int is_malloced = 0;
        mp_radix_size(i, base, &len);
        if (len < 120) {
            buf = alloca(len);
        }
        else {
            buf = MVM_malloc(len);
            is_malloced = 1;
        }
        mp_toradix_n(i, buf, base, len);
        result = MVM_string_ascii_decode(tc, tc->instance->VMString, buf, len - 1);
        if (is_malloced) MVM_free(buf);
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
            int len, is_malloced = 0;
            char *buf;
            MVMString *result;

            MVMint64 value = body->u.smallint.value;
            mp_init(&i);
            if (value >= 0) {
                mp_set_long(&i, value);
            }
            else {
                mp_set_long(&i, -value);
                mp_neg(&i, &i);
            }

            mp_radix_size(&i, base, &len);
            if (len < 120) {
                buf = alloca(len);
            }
            else {
                buf = MVM_malloc(len);
                is_malloced = 1;
            }
            mp_toradix_n(&i, buf, base, len);
            result = MVM_string_ascii_decode(tc, tc->instance->VMString, buf, len - 1);
            if (is_malloced) MVM_free(buf);
            mp_clear(&i);

            return result;
        }
    }
}

MVMnum64 MVM_bigint_to_num(MVMThreadContext *tc, MVMObject *a) {
    MVMP6bigintBody *ba = get_bigint_body(tc, a);

    if (MVM_BIGINT_IS_BIG(ba)) {
        mp_int *ia = ba->u.bigint;
        return mp_get_double(ia, 0);
    } else {
        return (double)ba->u.smallint.value;
    }
}

MVMObject *MVM_bigint_from_num(MVMThreadContext *tc, MVMObject *result_type, MVMnum64 n) {
    MVMObject * const result = MVM_repr_alloc_init(tc, result_type);
    MVMP6bigintBody *ba = get_bigint_body(tc, result);
    mp_int *ia = MVM_malloc(sizeof(mp_int));
    mp_init(ia);
    from_num(n, ia);
    store_bigint_result(ba, ia);
    return result;
}

MVMnum64 MVM_bigint_div_num(MVMThreadContext *tc, MVMObject *a, MVMObject *b) {
    MVMP6bigintBody *ba = get_bigint_body(tc, a);
    MVMP6bigintBody *bb = get_bigint_body(tc, b);
    MVMnum64 c;

    if (MVM_BIGINT_IS_BIG(ba) || MVM_BIGINT_IS_BIG(bb)) {
        mp_int *tmp[2] = { NULL, NULL };
        mp_int *ia = force_bigint(ba, tmp);
        mp_int *ib = force_bigint(bb, tmp);

        mp_clamp(ib);
        if (ib->used == 0) { /* zero-denominator special case */
            if (ia->sign == MP_NEG)
                c = MVM_NUM_NEGINF;
            else
                c =  MVM_NUM_POSINF;
            /*
             * we won't have NaN case here, since the branch requires at
             * least one bigint to be big
             */
        }
        else {
            mp_int scaled;
            int bbits = mp_count_bits(ib)+64;

            if (mp_init(&scaled) != MP_OKAY)
                MVM_exception_throw_adhoc(tc,
                    "Failed to initialize bigint for scaled divident");
            if (mp_mul_2d(ia, bbits, &scaled) != MP_OKAY)
                MVM_exception_throw_adhoc(tc, "Failed to scale divident");
            // simply re-use &scaled for result
            if (mp_div(&scaled, ib, &scaled, NULL) != MP_OKAY)
                MVM_exception_throw_adhoc(tc,
                    "Failed to preform bigint division");
            c = mp_get_double(&scaled, bbits);
            mp_clear(&scaled);
        }
        clear_temp_bigints(tmp, 2);
    } else {
        c = (double)ba->u.smallint.value / (double)bb->u.smallint.value;
    }
    return c;
}

MVMObject * MVM_bigint_rand(MVMThreadContext *tc, MVMObject *type, MVMObject *b) {
    MVMObject *result;
    MVMP6bigintBody *ba;
    MVMP6bigintBody *bb = get_bigint_body(tc, b);

    MVMint8 use_small_arithmetic = 0;
    MVMint8 have_to_negate = 0;
    MVMint32 smallint_max = 0;

    if (MVM_BIGINT_IS_BIG(bb)) {
        if (can_be_smallint(bb->u.bigint)) {
            use_small_arithmetic = 1;
            smallint_max = DIGIT(bb->u.bigint, 0);
            have_to_negate = SIGN(bb->u.bigint) == MP_NEG;
        }
    } else {
        use_small_arithmetic = 1;
        smallint_max = bb->u.smallint.value;
    }

    if (use_small_arithmetic) {
        if (MP_GEN_RANDOM_MAX >= abs(smallint_max)) {
            mp_digit result_int = MP_GEN_RANDOM();
            result_int = result_int % smallint_max;
            if(have_to_negate)
                result_int *= -1;

            MVMROOT2(tc, type, b, {
                result = MVM_repr_alloc_init(tc, type);
            });

            ba = get_bigint_body(tc, result);
            store_int64_result(ba, result_int);
        } else {
            use_small_arithmetic = 0;
        }
    }

    if (!use_small_arithmetic) {
        mp_int *tmp[1] = { NULL };
        mp_int *rnd = MVM_malloc(sizeof(mp_int));
        mp_int *max = force_bigint(bb, tmp);

        MVMROOT2(tc, type, b, {
            result = MVM_repr_alloc_init(tc, type);
        });

        ba = get_bigint_body(tc, result);

        mp_init(rnd);
        mp_rand(rnd, USED(max) + 1);

        mp_mod(rnd, max, rnd);
        store_bigint_result(ba, rnd);
        clear_temp_bigints(tmp, 1);
        adjust_nursery(tc, ba);
    }

    return result;
}

MVMint64 MVM_bigint_is_prime(MVMThreadContext *tc, MVMObject *a, MVMint64 b) {
    /* mp_prime_is_prime returns True for 1, and I think
     * it's worth special-casing this particular number :-)
     */
    MVMP6bigintBody *ba = get_bigint_body(tc, a);

    if (MVM_BIGINT_IS_BIG(ba) || ba->u.smallint.value != 1) {
        mp_int *tmp[1] = { NULL };
        mp_int *ia = force_bigint(ba, tmp);
        if (mp_cmp_d(ia, 1) == MP_EQ) {
            clear_temp_bigints(tmp, 1);
            return 0;
        }
        else {
            int result;
            mp_prime_is_prime(ia, b, &result);
            clear_temp_bigints(tmp, 1);
            return result;
        }
    } else {
        /* we only reach this if we have a smallint that's equal to 1.
         * which we define as not-prime. */
        return 0;
    }
}

/* concatenating with "" ensures that only literal strings are accepted as argument. */
#define STR_WITH_LEN(str)  ("" str ""), (sizeof(str) - 1)

MVMObject * MVM_bigint_radix(MVMThreadContext *tc, MVMint64 radix, MVMString *str, MVMint64 offset, MVMint64 flag, MVMObject *type) {
    MVMObject *result;
    MVMint64 chars  = MVM_string_graphs(tc, str);
    MVMuint16  neg  = 0;
    MVMint64   ch;

    mp_int zvalue;
    mp_int zbase;

    MVMObject *value_obj;
    mp_int *value;
    MVMP6bigintBody *bvalue;

    MVMObject *base_obj;
    mp_int *base;
    MVMP6bigintBody *bbase;

    MVMObject *pos_obj;
    MVMint64   pos  = -1;

    if (radix > 36) {
        MVM_exception_throw_adhoc(tc, "Cannot convert radix of %"PRId64" (max 36)", radix);
    }

    MVM_gc_root_temp_push(tc, (MVMCollectable **)&str);
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&type);

    /* initialize the object */
    result = MVM_repr_alloc_init(tc, MVM_hll_current(tc)->slurpy_array_type);
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&result);

    mp_init(&zvalue);
    mp_init(&zbase);
    mp_set_int(&zbase, 1);

    value_obj = MVM_repr_alloc_init(tc, type);
    MVM_repr_push_o(tc, result, value_obj);
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&value_obj);

    base_obj = MVM_repr_alloc_init(tc, type);
    MVM_repr_push_o(tc, result, base_obj);

    bvalue = get_bigint_body(tc, value_obj);
    bbase  = get_bigint_body(tc, base_obj);

    value = MVM_malloc(sizeof(mp_int));
    base  = MVM_malloc(sizeof(mp_int));

    mp_init(value);
    mp_init(base);

    mp_set_int(base, 1);

    ch = (offset < chars) ? MVM_string_get_grapheme_at_nocheck(tc, str, offset) : 0;
    if ((flag & 0x02) && (ch == '+' || ch == '-')) {
        neg = (ch == '-');
        offset++;
        ch = (offset < chars) ? MVM_string_get_grapheme_at_nocheck(tc, str, offset) : 0;
    }

    while (offset < chars) {
        if (ch >= '0' && ch <= '9') ch = ch - '0'; /* fast-path for ASCII 0..9 */
        else if (ch >= 'a' && ch <= 'z') ch = ch - 'a' + 10;
        else if (ch >= 'A' && ch <= 'Z') ch = ch - 'A' + 10;
        else if (ch >= 0xFF21 && ch <= 0xFF3A) ch = ch - 0xFF21 + 10; /* uppercase fullwidth */
        else if (ch >= 0xFF41 && ch <= 0xFF5A) ch = ch - 0xFF41 + 10; /* lowercase fullwidth */
        else if (ch > 0 && MVM_unicode_codepoint_get_property_int(tc, ch, MVM_UNICODE_PROPERTY_NUMERIC_TYPE)
         == MVM_UNICODE_PVALUE_Numeric_Type_DECIMAL) {
            /* as of Unicode 9.0.0, characters with the 'de' Numeric Type (and are
             * thus also of General Category Nd, since 4.0.0) are contiguous
             * sequences of 10 chars whose Numeric Values ascend from 0 through 9.
             */

            /* the string returned for NUMERIC_VALUE_NUMERATOR contains an integer
             * value. We can use numerator because they all are from 0-9 and have
             * denominator of 1 */
            ch = fast_atoi(MVM_unicode_codepoint_get_property_cstr(tc, ch, MVM_UNICODE_PROPERTY_NUMERIC_VALUE_NUMERATOR));
        }
        else break;
        if (ch >= radix) break;
        mp_mul_d(&zvalue, radix, &zvalue);
        mp_add_d(&zvalue, ch, &zvalue);
        mp_mul_d(&zbase, radix, &zbase);
        offset++; pos = offset;
        if (ch != 0 || !(flag & 0x04)) { mp_copy(&zvalue, value); mp_copy(&zbase, base); }
        if (offset >= chars) break;
        ch = MVM_string_get_grapheme_at_nocheck(tc, str, offset);
        if (ch != '_') continue;
        offset++;
        if (offset >= chars) break;
        ch = MVM_string_get_grapheme_at_nocheck(tc, str, offset);
    }

    mp_clear(&zvalue);
    mp_clear(&zbase);

    if (neg || flag & 0x01) {
        mp_neg(value, value);
    }

    store_bigint_result(bvalue, value);
    store_bigint_result(bbase, base);

    adjust_nursery(tc, bvalue);
    adjust_nursery(tc, bbase);

    pos_obj = MVM_repr_box_int(tc, type, pos);
    MVM_repr_push_o(tc, result, pos_obj);

    MVM_gc_root_temp_pop_n(tc, 4);

    return result;
}

/* returns 1 if a is too large to fit into an INTVAL without loss of
   information */
MVMint64 MVM_bigint_is_big(MVMThreadContext *tc, MVMObject *a) {
    MVMP6bigintBody *ba = get_bigint_body(tc, a);

    if (MVM_BIGINT_IS_BIG(ba)) {
        mp_int *b = ba->u.bigint;
        MVMint64 is_big = b->used > 1;
        /* XXX somebody please check that on a 32 bit platform */
        if ( sizeof(MVMint64) * 8 > DIGIT_BIT && is_big == 0 && DIGIT(b, 0) & ~0x7FFFFFFFUL)
            is_big = 1;
        return is_big;
    } else {
        /* if it's in a smallint, it's 32 bits big at most and fits into an INTVAL easily. */
        return 0;
    }
}

MVMint64 MVM_bigint_bool(MVMThreadContext *tc, MVMObject *a) {
    MVMP6bigintBody *body = get_bigint_body(tc, a);
    if (MVM_BIGINT_IS_BIG(body))
        return !mp_iszero(body->u.bigint);
    else
        return body->u.smallint.value != 0;
}
