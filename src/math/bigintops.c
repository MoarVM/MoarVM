#include "moar.h"
#include "platform/random.h"
#include "core/jfs64.h"

#define MANTISSA_BITS_IN_DOUBLE 53
#define EXPONENT_SHIFT 52
#define EXPONENT_MIN -1022
#define EXPONENT_MAX 1023
#define EXPONENT_BIAS 1023
#define EXPONENT_ZERO 0

#ifndef MAX
    #define MAX(x,y) ((x)>(y)?(x):(y))
#endif

#ifndef MIN
    #define MIN(x,y) ((x)<(y)?(x):(y))
#endif

MVM_STATIC_INLINE void adjust_nursery(MVMThreadContext *tc, MVMP6bigintBody *body) {
    if (MVM_BIGINT_IS_BIG(body)) {
        int used = body->u.bigint->used;
        int adjustment = MIN(used, 32768) & ~0x7;
        if (adjustment && (char *)tc->nursery_alloc_limit - adjustment > (char *)tc->nursery_alloc) {
            tc->nursery_alloc_limit = (char *)(tc->nursery_alloc_limit) - adjustment;
        }
    }
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
    if (i->used != 1)
        return 0;
    return MVM_IS_32BIT_INT(i->dp[0]);
}

/* Forces a bigint, even if we only have a smallint. Takes a parameter that
 * indicates where to allocate a temporary mp_int if needed. */
static mp_int * force_bigint(MVMThreadContext *tc, const MVMP6bigintBody *body, int idx) {
    if (MVM_BIGINT_IS_BIG(body)) {
        return body->u.bigint;
    }
    else {
        if (sizeof(mp_digit) > 4) {
            mp_int *i = tc->temp_bigints[idx];
            mp_set_i64(i, body->u.smallint.value);
            return i;
        }
        else {
            mp_int *i = tc->temp_bigints[idx];
            mp_set_i32(i, body->u.smallint.value);
            return i;
        }
    }
}

/* Stores an int64 in a bigint result body, either as a 32-bit smallint if it
 * is in range, or a big integer if not. */
static void store_int64_result(MVMThreadContext *tc, MVMP6bigintBody *body, MVMint64 result) {
    if (MVM_IS_32BIT_INT(result)) {
        body->u.smallint.flag = MVM_BIGINT_32_FLAG;
        body->u.smallint.value = (MVMint32)result;
    }
    else {
        mp_err err;
        mp_int *i = MVM_malloc(sizeof(mp_int));
        if ((err = mp_init_i64(i, result)) != MP_OKAY) {
            MVM_free(i);
            MVM_exception_throw_adhoc(tc, "Error creating a big integer from a native integer (%"PRIi64"): %s", result, mp_error_to_string(err));
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
        body->u.smallint.value = i->sign == MP_NEG ? -i->dp[0] : i->dp[0];
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
static void grow_and_negate(MVMThreadContext *tc, const mp_int *a, int size, mp_int *b) {
    mp_err err;
    int i;
    /* Always add an extra DIGIT so we can tell positive values
     * with a one in the highest bit apart from negative values.
     */
    int actual_size = MAX(size, a->used) + 1;

    b->sign = MP_ZPOS;
    if ((err = mp_grow(b, actual_size)) != MP_OKAY) {
        MVM_exception_throw_adhoc(tc, "Error growing a big integer: %s", mp_error_to_string(err));
    }
    b->used = actual_size;
    for (i = 0; i < a->used; i++) {
        b->dp[i] = (~a->dp[i]) & MP_MASK;
    }
    for (; i < actual_size; i++) {
        b->dp[i] = MP_MASK;
    }
    /* Note: This add cannot cause another grow assuming nobody ever
     * tries to use tommath -0 for anything, and nobody tries to use
     * this on positive bigints.
     */
    if ((err = mp_add_d(b, 1, b)) != MP_OKAY) {
        MVM_exception_throw_adhoc(tc, "Error adding a digit to a big integer: %s", mp_error_to_string(err));
    }
}

static void two_complement_bitop(MVMThreadContext *tc, mp_int *a, mp_int *b, mp_int *c,
                                 mp_err (*mp_bitop)(const mp_int *, const mp_int *, mp_int *)) {

    mp_err err;
    mp_int d;
    mp_int e;
    mp_int *f;
    mp_int *g;

    f = a;
    g = b;
    if (MP_NEG == a->sign) {
        if ((err = mp_init(&d)) != MP_OKAY) {
            MVM_exception_throw_adhoc(tc, "Error initializing a big integer: %s", mp_error_to_string(err));
        }
        grow_and_negate(tc, a, b->used, &d);
        f = &d;
    }
    if (MP_NEG == b->sign) {
        if ((err = mp_init(&e)) != MP_OKAY) {
            mp_clear(&d);
            MVM_exception_throw_adhoc(tc, "Error initializing a big integer: %s", mp_error_to_string(err));
        }
        grow_and_negate(tc, b, a->used, &e);
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
    if (c->used > MAX(a->used, b->used)) {
        int i;
        for (i = 0; i < c->used; i++) {
            c->dp[i] = (~c->dp[i]) & MP_MASK;
        }
        if ((err = mp_add_d(c, 1, c)) != MP_OKAY) {
            MVM_exception_throw_adhoc(tc, "Error adding a digit to a big integer: %s", mp_error_to_string(err));
        }
        if ((err = mp_neg(c, c)) != MP_OKAY) {
            MVM_exception_throw_adhoc(tc, "Error negating a big integer: %s", mp_error_to_string(err));
        }
    }
}

static void two_complement_shl(MVMThreadContext *tc, mp_int *result, mp_int *value, MVMint64 count) {
    mp_err err;
    if (count >= 0) {
        if ((err = mp_mul_2d(value, count, result)) != MP_OKAY) {
            MVM_exception_throw_adhoc(tc, "Error in mp_mul_2d: %s", mp_error_to_string(err));
        }
    }
    else if (MP_NEG == value->sign) {
        /* fake two's complement semantics on top of sign-magnitude
         * algorithm appears to work [citation needed]
         */
        if ((err = mp_add_d(value, 1, result)) != MP_OKAY) {
            MVM_exception_throw_adhoc(tc, "Error adding a digit to a big integer: %s", mp_error_to_string(err));
        }
        if ((err = mp_div_2d(result, -count, result, NULL)) != MP_OKAY) {
            MVM_exception_throw_adhoc(tc, "Error in mp_div_2d: %s", mp_error_to_string(err));
        }
        if ((err = mp_sub_d(result, 1, result)) != MP_OKAY) {
            MVM_exception_throw_adhoc(tc, "Error subtracting a digit from a big integer: %s", mp_error_to_string(err));
        }
    }
    else {
        if ((err = mp_div_2d(value, -count, result, NULL)) != MP_OKAY) {
            MVM_exception_throw_adhoc(tc, "Error in mp_div_2d: %s", mp_error_to_string(err));
        }
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
        store_int64_result(tc, bb, 0); \
    } \
    else { \
        MVMP6bigintBody *ba = get_bigint_body(tc, source); \
        if (MVM_BIGINT_IS_BIG(ba)) { \
            mp_err err; \
            mp_int *ia = ba->u.bigint; \
            mp_int *ib = MVM_malloc(sizeof(mp_int)); \
            if ((err = mp_init(ib)) != MP_OKAY) { \
                MVM_free(ib); \
                MVM_exception_throw_adhoc(tc, "Error initializing a big integer: %s", mp_error_to_string(err)); \
            } \
            if ((err = mp_##opname(ia, ib)) != MP_OKAY) { \
                mp_clear(ib); \
                MVM_free(ib); \
                MVM_exception_throw_adhoc(tc, "Error performing %s with a big integer: %s", #opname, mp_error_to_string(err)); \
            } \
            store_bigint_result(bb, ib); \
            adjust_nursery(tc, bb); \
        } \
        else { \
            MVMint64 sb; \
            MVMint64 sa = ba->u.smallint.value; \
            SMALLINT_OP; \
            store_int64_result(tc, bb, sb); \
        } \
    } \
    return result; \
}

#define MVM_BIGINT_BINARY_OP(opname) \
MVMObject * MVM_bigint_##opname(MVMThreadContext *tc, MVMObject *result_type, MVMObject *a, MVMObject *b) { \
    MVMP6bigintBody *ba, *bb, *bc; \
    MVMObject *result; \
    mp_err err; \
    mp_int *ia, *ib, *ic; \
    MVMROOT2(tc, a, b, { \
        result = MVM_repr_alloc_init(tc, result_type);\
    }); \
    ba = get_bigint_body(tc, a); \
    bb = get_bigint_body(tc, b); \
    bc = get_bigint_body(tc, result); \
    ia = force_bigint(tc, ba, 0); \
    ib = force_bigint(tc, bb, 1); \
    ic = MVM_malloc(sizeof(mp_int)); \
    if ((err = mp_init(ic)) != MP_OKAY) { \
        MVM_free(ic); \
        MVM_exception_throw_adhoc(tc, "Error initializing a big integer: %s", mp_error_to_string(err)); \
    } \
    if ((err = mp_##opname(ia, ib, ic)) != MP_OKAY) { \
        mp_clear(ic); \
        MVM_free(ic); \
        MVM_exception_throw_adhoc(tc, "Error performing %s with big integers: %s", #opname, mp_error_to_string(err)); \
    } \
    store_bigint_result(bc, ic); \
    adjust_nursery(tc, bc); \
    return result; \
}

#define MVM_BIGINT_BINARY_OP_SIMPLE(opname, SMALLINT_OP) \
void MVM_bigint_fallback_##opname(MVMThreadContext *tc, MVMP6bigintBody *ba, MVMP6bigintBody *bb, \
                                  MVMP6bigintBody *bc) { \
    mp_err err; \
    mp_int *ia, *ib, *ic; \
    ia = force_bigint(tc, ba, 0); \
    ib = force_bigint(tc, bb, 1); \
    ic = MVM_malloc(sizeof(mp_int)); \
    if ((err = mp_init(ic)) != MP_OKAY) { \
        MVM_free(ic); \
        MVM_exception_throw_adhoc(tc, "Error initializing a big integer: %s", mp_error_to_string(err)); \
    } \
    if ((err = mp_##opname(ia, ib, ic)) != MP_OKAY) { \
        mp_clear(ic); \
        MVM_free(ic); \
        MVM_exception_throw_adhoc(tc, "Error performing %s with big integers: %s", #opname, mp_error_to_string(err)); \
    } \
    store_bigint_result(bc, ic); \
    adjust_nursery(tc, bc); \
} \
MVMObject * MVM_bigint_##opname(MVMThreadContext *tc, MVMObject *result_type, MVMObject *a, MVMObject *b) { \
    MVMP6bigintBody *ba, *bb, *bc; \
    MVMObject *result; \
    ba = get_bigint_body(tc, a); \
    bb = get_bigint_body(tc, b); \
    if (MVM_BIGINT_IS_BIG(ba) || MVM_BIGINT_IS_BIG(bb)) { \
        mp_err err; \
        mp_int *ia, *ib, *ic; \
        MVMROOT2(tc, a, b, { \
            result = MVM_repr_alloc_init(tc, result_type);\
        }); \
        ba = get_bigint_body(tc, a); \
        bb = get_bigint_body(tc, b); \
        bc = get_bigint_body(tc, result); \
        ia = force_bigint(tc, ba, 0); \
        ib = force_bigint(tc, bb, 1); \
        ic = MVM_malloc(sizeof(mp_int)); \
        if ((err = mp_init(ic)) != MP_OKAY) { \
            MVM_free(ic); \
            MVM_exception_throw_adhoc(tc, "Error initializing a big integer: %s", mp_error_to_string(err)); \
        } \
        if ((err = mp_##opname(ia, ib, ic)) != MP_OKAY) { \
            mp_clear(ic); \
            MVM_free(ic); \
            MVM_exception_throw_adhoc(tc, "Error performing %s with big integers: %s", #opname, mp_error_to_string(err)); \
        } \
        store_bigint_result(bc, ic); \
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
        store_int64_result(tc, bc, sc); \
    } \
    return result; \
}

#define MVM_BIGINT_BINARY_OP_2(opname, SMALLINT_OP) \
MVMObject * MVM_bigint_##opname(MVMThreadContext *tc, MVMObject *result_type, MVMObject *a, MVMObject *b) { \
    MVMObject *result; \
    MVMROOT2(tc, a, b, { \
        result = MVM_repr_alloc_init(tc, result_type);\
    }); \
    {\
        MVMP6bigintBody *ba = get_bigint_body(tc, a); \
        MVMP6bigintBody *bb = get_bigint_body(tc, b); \
        MVMP6bigintBody *bc = get_bigint_body(tc, result); \
        if (MVM_BIGINT_IS_BIG(ba) || MVM_BIGINT_IS_BIG(bb)) { \
            mp_err err; \
            mp_int *ia = force_bigint(tc, ba, 0); \
            mp_int *ib = force_bigint(tc, bb, 1); \
            mp_int *ic = MVM_malloc(sizeof(mp_int)); \
            if ((err = mp_init(ic)) != MP_OKAY) { \
                MVM_free(ic); \
                MVM_exception_throw_adhoc(tc, "Error initializing a big integer: %s", mp_error_to_string(err)); \
            } \
            two_complement_bitop(tc, ia, ib, ic, mp_##opname); \
            store_bigint_result(bc, ic); \
            adjust_nursery(tc, bc); \
        } \
        else { \
            MVMint64 sc; \
            MVMint64 sa = ba->u.smallint.value; \
            MVMint64 sb = bb->u.smallint.value; \
            SMALLINT_OP; \
            store_int64_result(tc, bc, sc); \
        } \
        return result; \
    }\
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
    MVMObject       *result;

    MVMROOT2(tc, a, b, {
        result = MVM_repr_alloc_init(tc, result_type);
    });

    {
        MVMP6bigintBody *ba = get_bigint_body(tc, a);
        MVMP6bigintBody *bb = get_bigint_body(tc, b);
        MVMP6bigintBody *bc = get_bigint_body(tc, result);

        if (MVM_BIGINT_IS_BIG(ba) || MVM_BIGINT_IS_BIG(bb)) {
            mp_err err;
            mp_int *ia = force_bigint(tc, ba, 0);
            mp_int *ib = force_bigint(tc, bb, 1);
            mp_int *ic = MVM_malloc(sizeof(mp_int));
            if ((err = mp_init(ic)) != MP_OKAY) {
                MVM_free(ic);
                MVM_exception_throw_adhoc(tc, "Error creating a big integer: %s", mp_error_to_string(err));
            }
            if ((err = mp_gcd(ia, ib, ic)) != MP_OKAY) {
                mp_clear(ic);
                MVM_free(ic);
                MVM_exception_throw_adhoc(tc, "Error getting the GCD of two big integer: %s", mp_error_to_string(err));
            }
            store_bigint_result(bc, ic);
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
            store_int64_result(tc, bc, sa);
        }
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
        mp_int *ia = force_bigint(tc, ba, 0);
        mp_int *ib = force_bigint(tc, bb, 1);
        MVMint64 r = (MVMint64)mp_cmp(ia, ib);
        return r;
    }
    else {
        MVMint64 sa = ba->u.smallint.value;
        MVMint64 sb = bb->u.smallint.value;
        return sa == sb ? 0 : sa <  sb ? -1 : 1;
    }
}

MVMObject * MVM_bigint_mod(MVMThreadContext *tc, MVMObject *result_type, MVMObject *a, MVMObject *b) {

    MVMObject *result;

    MVMROOT2(tc, a, b, {
        result = MVM_repr_alloc_init(tc, result_type);
    });

    {
        MVMP6bigintBody *ba = get_bigint_body(tc, a);
        MVMP6bigintBody *bb = get_bigint_body(tc, b);
        MVMP6bigintBody *bc;
        bc = get_bigint_body(tc, result);

        /* XXX the behavior of C's mod operator is not correct
         * for our purposes. So we rely on mp_mod for all our modulus
         * calculations for now. */
        if (1 || MVM_BIGINT_IS_BIG(ba) || MVM_BIGINT_IS_BIG(bb)) {
            mp_int *ia = force_bigint(tc, ba, 0);
            mp_int *ib = force_bigint(tc, bb, 1);
            mp_int *ic = MVM_malloc(sizeof(mp_int));
            mp_err err;

            if ((err = mp_init(ic)) != MP_OKAY) {
                MVM_free(ic);
                MVM_exception_throw_adhoc(tc, "Error creating a big integer: %s", mp_error_to_string(err));
            }

            if ((err = mp_mod(ia, ib, ic)) != MP_OKAY) {
                mp_clear(ic);
                MVM_free(ic);
                MVM_exception_throw_adhoc(tc, "Error getting the mod of two big integer: %s", mp_error_to_string(err));
            }

            store_bigint_result(bc, ic);
            adjust_nursery(tc, bc);
        } else {
            store_int64_result(tc, bc, ba->u.smallint.value % bb->u.smallint.value);
        }
    }

    return result;
}

MVMObject *MVM_bigint_div(MVMThreadContext *tc, MVMObject *result_type, MVMObject *a, MVMObject *b) {
    MVMP6bigintBody *ba;
    MVMP6bigintBody *bb = get_bigint_body(tc, b);
    MVMP6bigintBody *bc;
    mp_int *ia, *ib, *ic;
    int cmp_a;
    int cmp_b;
    MVMObject *result;

    mp_err err;

    if (!MVM_BIGINT_IS_BIG(bb) && bb->u.smallint.value == 1 && STABLE(a) == STABLE(b)) {
        return a;
    }

    MVMROOT2(tc, a, b, {
        result = MVM_repr_alloc_init(tc, result_type);
    });

    ba = get_bigint_body(tc, a);
    bb = get_bigint_body(tc, b);

    bc = get_bigint_body(tc, result);

    /* we only care about MP_LT or !MP_LT, so we give MP_GT even for 0. */
    if (MVM_BIGINT_IS_BIG(ba)) {
        cmp_a = !mp_iszero(ba->u.bigint) && ba->u.bigint->sign == MP_NEG ? MP_LT : MP_GT;
    } else {
        cmp_a = ba->u.smallint.value < 0 ? MP_LT : MP_GT;
    }
    if (MVM_BIGINT_IS_BIG(bb)) {
        cmp_b = !mp_iszero(bb->u.bigint) && bb->u.bigint->sign == MP_NEG ? MP_LT : MP_GT;
    } else {
        cmp_b = bb->u.smallint.value < 0 ? MP_LT : MP_GT;
    }

    if (MVM_BIGINT_IS_BIG(ba) || MVM_BIGINT_IS_BIG(bb)) {
        ia = force_bigint(tc, ba, 0);
        ib = force_bigint(tc, bb, 1);

        ic = MVM_malloc(sizeof(mp_int));
        if ((err = mp_init(ic)) != MP_OKAY) {
            MVM_free(ic);
            MVM_exception_throw_adhoc(tc, "Error creating a big integer: %s", mp_error_to_string(err));
        }

        /* if we do a div with a negative, we need to make sure
         * the result is floored rather than rounded towards
         * zero, like C and libtommath would do. */
        if ((cmp_a == MP_LT) ^ (cmp_b == MP_LT)) {
            mp_int remainder, intermediate;
            if ((err = mp_init_multi(&remainder, &intermediate, NULL)) != MP_OKAY) {
                mp_clear(ic);
                MVM_free(ic);
                MVM_exception_throw_adhoc(tc, "Error creating big integers: %s", mp_error_to_string(err));
            }
            if ((err = mp_div(ia, ib, &intermediate, &remainder)) != MP_OKAY) {
                mp_clear_multi(ic, &remainder, &intermediate, NULL);
                MVM_free(ic);
                MVM_exception_throw_adhoc(tc, "Error dividing big integers: %s", mp_error_to_string(err));
            }
            if (mp_iszero(&remainder) == 0) {
                if ((err = mp_sub_d(&intermediate, 1, ic)) != MP_OKAY) {
                    mp_clear_multi(ic, &remainder, &intermediate, NULL);
                    MVM_free(ic);
                    MVM_exception_throw_adhoc(tc, "Error subtracting a digit from a big integer: %s", mp_error_to_string(err));
                }
            } else {
                if ((err = mp_copy(&intermediate, ic)) != MP_OKAY) {
                    mp_clear_multi(ic, &remainder, &intermediate, NULL);
                    MVM_free(ic);
                    MVM_exception_throw_adhoc(tc, "Error copying a big integer: %s", mp_error_to_string(err));
                }
            }
            mp_clear_multi(&remainder, &intermediate, NULL);
        } else {
            if ((err = mp_div(ia, ib, ic, NULL)) != MP_OKAY) {
                mp_clear(ic);
                MVM_free(ic);
                MVM_exception_throw_adhoc(tc, "Error dividing big integers: %s", mp_error_to_string(err));
            }
        }
        store_bigint_result(bc, ic);
        adjust_nursery(tc, bc);
    } else {
        MVMint32 num   = ba->u.smallint.value;
        MVMint32 denom = bb->u.smallint.value;
        MVMint64 value;
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
            value = (MVMint64)num / denom;
        }
        store_int64_result(tc, bc, value);
    }

    return result;
}

MVMObject * MVM_bigint_pow(MVMThreadContext *tc, MVMObject *a, MVMObject *b,
        MVMObject *num_type, MVMObject *int_type) {
    MVMP6bigintBody *ba = get_bigint_body(tc, a);
    MVMP6bigintBody *bb = get_bigint_body(tc, b);
    MVMObject       *r  = NULL;

    mp_int *base        = force_bigint(tc, ba, 0);
    mp_int *exponent    = force_bigint(tc, bb, 1);
    mp_digit exponent_d = 0;

    if (mp_iszero(exponent) || (MP_EQ == mp_cmp_d(base, 1))) {
        r = MVM_repr_box_int(tc, int_type, 1);
    }
    else if (exponent->sign == MP_ZPOS) {
        exponent_d = mp_get_u32(exponent);
        if ((MP_GT == mp_cmp_d(exponent, exponent_d))) {
            if (mp_iszero(base)) {
                r = MVM_repr_box_int(tc, int_type, 0);
            }
            else if (mp_get_i32(base) == 1 || mp_get_i32(base) == -1) {
                r = MVM_repr_box_int(tc, int_type, MP_ZPOS == base->sign || mp_iseven(exponent) ? 1 : -1);
            }
            else {
                MVMnum64 inf;
                if (MP_ZPOS == base->sign || mp_iseven(exponent)) {
                    inf = MVM_num_posinf(tc);
                }
                else {
                    inf = MVM_num_neginf(tc);
                }
                r = MVM_repr_box_num(tc, num_type, inf);
            }
        }
        else {
            mp_err err;
            mp_int *ic = MVM_malloc(sizeof(mp_int));
            MVMP6bigintBody *resbody;
            if ((err = mp_init(ic)) != MP_OKAY) {
                MVM_free(ic);
                MVM_exception_throw_adhoc(tc, "Error creating a big integer: %s", mp_error_to_string(err));
            }
            MVM_gc_mark_thread_blocked(tc);
            if ((err = mp_expt_u32(base, exponent_d, ic)) != MP_OKAY) {
                mp_clear(ic);
                MVM_free(ic);
                MVM_exception_throw_adhoc(tc, "Error in mp_expt_u32: %s", mp_error_to_string(err));
            }
            MVM_gc_mark_thread_unblocked(tc);
            r = MVM_repr_alloc_init(tc, int_type);
            resbody = get_bigint_body(tc, r);
            store_bigint_result(resbody, ic);
            adjust_nursery(tc, resbody);
        }
    }
    else {
        MVMnum64 f_base = mp_get_double(base);
        MVMnum64 f_exp = mp_get_double(exponent);
        r = MVM_repr_box_num(tc, num_type, pow(f_base, f_exp));
    }
    return r;
}

MVMObject *MVM_bigint_shl(MVMThreadContext *tc, MVMObject *result_type, MVMObject *a, MVMint64 n) {
    MVMP6bigintBody *ba;
    MVMP6bigintBody *bb;
    MVMObject       *result;

    MVMROOT(tc, a, {
        result = MVM_repr_alloc_init(tc, result_type);
    });

    ba = get_bigint_body(tc, a);
    bb = get_bigint_body(tc, result);

    if (MVM_BIGINT_IS_BIG(ba) || n >= 31) {
        mp_err err;
        mp_int *ia = force_bigint(tc, ba, 0);
        mp_int *ib = MVM_malloc(sizeof(mp_int));
        if ((err = mp_init(ib)) != MP_OKAY) {
            MVM_free(ib);
            MVM_exception_throw_adhoc(tc, "Error creating a big integer: %s", mp_error_to_string(err));
        }
        two_complement_shl(tc, ib, ia, n);
        store_bigint_result(bb, ib);
        adjust_nursery(tc, bb);
    } else {
        MVMint64 value;
        if (n < 0)
            value = ((MVMint64)ba->u.smallint.value) >> -n;
        else
            value = ((MVMint64)ba->u.smallint.value) << n;
        store_int64_result(tc, bb, value);
    }

    return result;
}
/* Checks if a MVMP6bigintBody is negative. Handles cases where it is stored as
 * a small int as well as cases when it is stored as a bigint */
static int BIGINT_IS_NEGATIVE (MVMP6bigintBody *ba) {
    mp_int *mp_a = ba->u.bigint;
    if (MVM_BIGINT_IS_BIG(ba)) {
        return mp_a->sign == MP_NEG;
    }
    else {
        return ba->u.smallint.value < 0;
    }
}
MVMObject *MVM_bigint_shr(MVMThreadContext *tc, MVMObject *result_type, MVMObject *a, MVMint64 n) {
    MVMP6bigintBody *ba;
    MVMP6bigintBody *bb;
    MVMObject       *result;

    MVMROOT(tc, a, {
        result = MVM_repr_alloc_init(tc, result_type);
    });

    ba = get_bigint_body(tc, a);
    bb = get_bigint_body(tc, result);

    if (MVM_BIGINT_IS_BIG(ba) || n < 0) {
        mp_err err;
        mp_int *ia = force_bigint(tc, ba, 0);
        mp_int *ib = MVM_malloc(sizeof(mp_int));
        if ((err = mp_init(ib)) != MP_OKAY) {
            MVM_free(ib);
            MVM_exception_throw_adhoc(tc, "Error creating a big integer: %s", mp_error_to_string(err));
        }
        two_complement_shl(tc, ib, ia, -n);
        store_bigint_result(bb, ib);
        adjust_nursery(tc, bb);
    } else if (n >= 32) {
        store_int64_result(tc, bb, BIGINT_IS_NEGATIVE(ba) ? -1 : 0);
    } else {
        MVMint32 value = ba->u.smallint.value;
        value = value >> n;
        store_int64_result(tc, bb, value);
    }

    return result;
}

MVMObject *MVM_bigint_not(MVMThreadContext *tc, MVMObject *result_type, MVMObject *a) {
    MVMP6bigintBody *ba;
    MVMP6bigintBody *bb;
    MVMObject       *result;

    MVMROOT(tc, a, {
        result = MVM_repr_alloc_init(tc, result_type);
    });

    ba = get_bigint_body(tc, a);
    bb = get_bigint_body(tc, result);

    if (MVM_BIGINT_IS_BIG(ba)) {
        mp_err err;
        mp_int *ia = ba->u.bigint;
        mp_int *ib = MVM_malloc(sizeof(mp_int));
        if ((err = mp_init(ib)) != MP_OKAY) {
            MVM_free(ib);
            MVM_exception_throw_adhoc(tc, "Error creating a big integer: %s", mp_error_to_string(err));
        }
        /* two's complement not: add 1 and negate */
        if ((err = mp_add_d(ia, 1, ib)) != MP_OKAY) {
            mp_clear(ib);
            MVM_free(ib);
            MVM_exception_throw_adhoc(tc, "Error adding a digit to a big integer: %s", mp_error_to_string(err));
        }
        if ((err = mp_neg(ib, ib)) != MP_OKAY) {
            mp_clear(ib);
            MVM_free(ib);
            MVM_exception_throw_adhoc(tc, "Error negating a big integer: %s", mp_error_to_string(err));
        }
        store_bigint_result(bb, ib);
        adjust_nursery(tc, bb);
    } else {
        MVMint32 value = ba->u.smallint.value;
        value = ~value;
        store_int64_result(tc, bb, value);
    }

    return result;
}

MVMObject *MVM_bigint_expmod(MVMThreadContext *tc, MVMObject *result_type, MVMObject *a, MVMObject *b, MVMObject *c) {
    MVMP6bigintBody *ba = get_bigint_body(tc, a);
    MVMP6bigintBody *bb = get_bigint_body(tc, b);
    MVMP6bigintBody *bc = get_bigint_body(tc, c);
    MVMP6bigintBody *bd;
    MVMObject       *result;
    mp_err err;

    mp_int *ia = force_bigint(tc, ba, 0);
    mp_int *ib = force_bigint(tc, bb, 1);
    mp_int *ic = force_bigint(tc, bc, 2);
    mp_int *id = MVM_malloc(sizeof(mp_int));
    if ((err = mp_init(id)) != MP_OKAY) {
        MVM_free(id);
        MVM_exception_throw_adhoc(tc, "Error creating a big integer: %s", mp_error_to_string(err));
    }

    MVMROOT3(tc, a, b, c, {
        result = MVM_repr_alloc_init(tc, result_type);
    });

    bd = get_bigint_body(tc, result);

    if ((err = mp_exptmod(ia, ib, ic, id)) != MP_OKAY) {
        mp_clear(id);
        MVM_free(id);
        MVM_exception_throw_adhoc(tc, "Error in mp_exptmod: %s", mp_error_to_string(err));
    }
    store_bigint_result(bd, id);
    adjust_nursery(tc, bd);

    return result;
}

void MVM_bigint_from_str(MVMThreadContext *tc, MVMObject *a, const char *buf) {
    mp_err err;
    MVMP6bigintBody *body = get_bigint_body(tc, a);
    mp_int *i = alloca(sizeof(mp_int));
    if ((err = mp_init(i)) != MP_OKAY) {
        MVM_exception_throw_adhoc(tc, "Error creating a big integer: %s", mp_error_to_string(err));
    }
    if ((err = mp_read_radix(i, buf, 10)) != MP_OKAY) {
        mp_clear(i);
        MVM_exception_throw_adhoc(tc, "Error reading a big integer from a string: %s", mp_error_to_string(err));
    }
    adjust_nursery(tc, body);
    if (can_be_smallint(i)) {
        body->u.smallint.flag = MVM_BIGINT_32_FLAG;
        body->u.smallint.value = i->sign == MP_NEG ? -i->dp[0] : i->dp[0];
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
    MVMObject *a;
    MVMROOT(tc, s, {
        a = MVM_repr_alloc_init(tc, type);
    });
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
        mp_err err;
        mp_int *i = MVM_malloc(sizeof(mp_int));
        if ((err = mp_init_copy(i, a_body->u.bigint)) != MP_OKAY) {
            MVM_free(i);
            MVM_exception_throw_adhoc(tc, "Error creating a big integer from a copy of another: %s", mp_error_to_string(err));
        }
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


/* returns size of ASCII reprensentation */
static int mp_faster_radix_size (mp_int *a, int radix, int *size)
{
    int     res, digs;
    mp_int  t;
    mp_digit d;

    *size = 0;

    /* make sure the radix is in range */
    if ((radix < 2) || (radix > 64))
        return MP_VAL;

    if (mp_iszero(a) == MP_YES) {
        *size = 2;
        return MP_OKAY;
    }

    /* special case for binary */
    if (radix == 2) {
        *size = mp_count_bits(a) + ((a->sign == MP_NEG) ? 1 : 0) + 1;
        return MP_OKAY;
    }

    digs = 0; /* digit count */

    if (a->sign == MP_NEG)
        ++digs;

    /* init a copy of the input */
    if ((res = mp_init_copy(&t, a)) != MP_OKAY)
        return res;

    /* force temp to positive */
    t.sign = MP_ZPOS;

    /* fetch out all of the digits */

#if MP_DIGIT_BIT == 60
    /* Optimization for base-10 numbers.
     * Logic is designed for 60-bit mp_digit, with 100000000000000000
     * being the largest 10**n that can fit into it, which gives us 17 digits.
     * So we reduce the number in 17 digit chunks, until we get to a number
     * small enough to fit into a single mp_digit.
     */
    if (radix == 10) {
        mp_clamp(&t);
        while ((&t)->used > 1) {
            if ((res = mp_div_d(&t, (mp_digit) 100000000000000000, &t, &d)) != MP_OKAY) {
              mp_clear(&t);
              return res;
            }
            digs += 17;
        }
    }
#endif

    while (mp_iszero(&t) == MP_NO) {
        if ((res = mp_div_d(&t, (mp_digit) radix, &t, &d)) != MP_OKAY) {
            mp_clear(&t);
            return res;
        }
        ++digs;
    }
    mp_clear(&t);

    /* return digs + 1, the 1 is for the NULL byte that would be required. */
    *size = digs + 1;
    return MP_OKAY;
}

MVMString * MVM_bigint_to_str(MVMThreadContext *tc, MVMObject *a, int base) {
    MVMP6bigintBody *body = get_bigint_body(tc, a);
    if (MVM_BIGINT_IS_BIG(body)) {
        mp_err err;
        mp_int *i = body->u.bigint;
        int len;
        char *buf;
        MVMString *result;
        int is_malloced = 0;
        mp_faster_radix_size(i, base, &len);
        if (len < 120) {
            buf = alloca(len);
        }
        else {
            buf = MVM_malloc(len);
            is_malloced = 1;
        }
        if ((err = mp_to_radix(i, buf, len, NULL, base)) != MP_OKAY) {
            if (is_malloced) MVM_free(buf);
            MVM_exception_throw_adhoc(tc, "Error getting the string representation of a big integer: %s", mp_error_to_string(err));
        }
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
            mp_err err;
            mp_int i;
            int len, is_malloced = 0;
            char *buf;
            MVMString *result;

            if ((err = mp_init(&i)) != MP_OKAY) {
                MVM_exception_throw_adhoc(tc, "Error creating a big integer: %s", mp_error_to_string(err));
            }
            mp_set_i64(&i, body->u.smallint.value);

            if ((err = mp_radix_size(&i, base, &len)) != MP_OKAY) {
                mp_clear(&i);
                MVM_exception_throw_adhoc(tc, "Error calculating the size of the string representation of a big integer: %s", mp_error_to_string(err));
            }
            if (len < 120) {
                buf = alloca(len);
            }
            else {
                buf = MVM_malloc(len);
                is_malloced = 1;
            }
            if ((err = mp_to_radix(&i, buf, len, NULL, base)) != MP_OKAY) {
                if (is_malloced) MVM_free(buf);
                mp_clear(&i);
                MVM_exception_throw_adhoc(tc, "Error getting the string representation of a big integer: %s", mp_error_to_string(err));
            }
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
        return mp_get_double(ba->u.bigint);
    } else {
        return (double)ba->u.smallint.value;
    }
}

MVMObject *MVM_bigint_from_num(MVMThreadContext *tc, MVMObject *result_type, MVMnum64 n) {
    mp_err err;
    MVMObject * const result = MVM_repr_alloc_init(tc, result_type);
    MVMP6bigintBody *ba = get_bigint_body(tc, result);
    mp_int *ia = MVM_malloc(sizeof(mp_int));
    if ((err = mp_init(ia)) != MP_OKAY) {
        MVM_free(ia);
        MVM_exception_throw_adhoc(tc, "Error creating a big integer: %s", mp_error_to_string(err));
    }
    if ((err = mp_set_double(ia, n)) != MP_OKAY) {
        mp_clear(ia);
        MVM_free(ia);
        MVM_exception_throw_adhoc(tc, "Error storing an MVMnum64 (%f) in a big integer: %s", n, mp_error_to_string(err));
    }
    store_bigint_result(ba, ia);
    return result;
}

/* Implementation based on the approach described in
 * https://www.exploringbinary.com/correct-decimal-to-floating-point-using-big-integers/
 * but uses `mp_count_bits` to determine the needed scaling within a factor of 2
 * instead of the suggestion on that page of multiplying by two, which implies
 * trial divisions.
 *
 * This might look horrible and long, but it only has one truly expensive
 * operation (the big integer division). The other big integer operations are
 * powers of two, and internally simple loops with bitshifts or even O(1).
 * Most of the rest is bit manipulations on 64 bit integers.
 *
 * The code that this replaced also had big integer division, and scaled up the
 * numerator by 2**64. This code scales up by 2**54 worst case. It should be
 * slightly faster, as well as being as accurate as possible for all the awkward
 * cases.
 */

static MVMnum64 bigint_div_num(MVMThreadContext *tc, const mp_int *numerator, const mp_int *denominator) {
    int negative = numerator->sign != denominator->sign;

    if (mp_iszero(denominator)) {
        /* Comments in the caller imply that the NaN case is unreachable.
         * However, I'd prefer to leave it in place, as that way this code can
         * be extracted and independently tested for all corner cases.
         * (With decimal strings expressed as $numerator / 10 ** $denominator
         * it is bit-for-bit identical with David M. Gay's strtod, including
         * values that overflow, underflow, generate subnormals and negative
         * zero. That laundry list means we just need 0 / 0 => NaN to catch 'em
         * all.) */
        if (mp_iszero(numerator))
            return MVM_NUM_NAN;
        return negative ? -MVM_NUM_NEGINF : MVM_NUM_POSINF;
    }
    if (mp_iszero(numerator))
        return 0.0;

    int floor_log2_num = mp_count_bits(numerator) - 1;
    int floor_log2_den = mp_count_bits(denominator) - 1;
    int exponent = floor_log2_num - floor_log2_den - 1;
    int in_range = MANTISSA_BITS_IN_DOUBLE - exponent - 1;

    const mp_int *numerator_scaled;
    const mp_int *denominator_scaled;

    mp_err err;
    mp_int temp;
    mp_int quotient;
    mp_int remainder;
    mp_int *scaled = in_range ? &temp : NULL;

    err = mp_init_multi(&quotient, &remainder, scaled, NULL);
    if (err != MP_OKAY)
        MVM_exception_throw_adhoc(tc,
                                  "Failed to initialize bigint for division results");

    if (!in_range) {
        numerator_scaled = numerator;
        denominator_scaled = denominator;
    }
    else {
        /* We need to multiply by 2**|in_range| */
        int exponent = abs(in_range);

        if (in_range > 0) {
            err = mp_mul_2d(numerator, exponent, scaled);
            if (err != MP_OKAY) {
                mp_clear_multi(&quotient, &remainder, scaled, NULL);
                MVM_exception_throw_adhoc(tc,
                                          "Failed to scale numerator before division");
            }

            numerator_scaled = scaled;
            denominator_scaled = denominator;
        }
        else {
            err = mp_mul_2d(denominator, exponent, scaled);
            if (err != MP_OKAY) {
                mp_clear_multi(&quotient, &remainder, scaled, NULL);
                MVM_exception_throw_adhoc(tc,
                                          "Failed to scale denominator before division");
            }

            numerator_scaled = numerator;
            denominator_scaled = scaled;
        }
    }

    /* Should not be possible to hit divide-by-zero here. */
    err = mp_div(numerator_scaled, denominator_scaled, &quotient, &remainder);
    if (err != MP_OKAY) {
        mp_clear_multi(&quotient, &remainder, scaled, NULL);
        MVM_exception_throw_adhoc(tc, "Failed to perform bigint division");
    }

    assert(mp_count_bits(&quotient) <= MANTISSA_BITS_IN_DOUBLE + 2);

    uint64_t mantissa = mp_get_mag_u64(&quotient);
    assert(mantissa < UINT64_C(1) << (MANTISSA_BITS_IN_DOUBLE + 1));
    assert(mantissa >= UINT64_C(1) << (MANTISSA_BITS_IN_DOUBLE - 1));

    int carry = 0;
    if (mantissa & UINT64_C(1) << MANTISSA_BITS_IN_DOUBLE) {
        /* Not in range - must be a factor of two too large.
         * This happens because we only have the floor of the base 2 log of each
         * value for the division, so we can end up picking a value one too
         * large for `exponent`, meaning that we don't successfully normalise
         * `quotient` into the correct range. */
        carry = mantissa & 1;
        mantissa >>= 1;
        ++exponent;
        if (exponent >= EXPONENT_MIN) {
            if (carry) {
                if (mp_iszero(&remainder)) {
                    /* The remainder is exactly midway between the two possible
                     * values. For this case, IEEE says round to even. */
                    if (mantissa & 1) {
                        /* Round up to even. */
                        ++mantissa;
                    }
                    /* else round down to even, which is implicit in the shift,
                     * as it discarded the lowest bit. */
                }
                else {
                    /* IEEE says round up. */
                    ++mantissa;
                }
            }
            /* And then to add to the fun, rounding can push us up even further.
             */
            if (mantissa == UINT64_C(1) << MANTISSA_BITS_IN_DOUBLE) {
                mantissa >>= 1;
                ++exponent;
            }
            /* else round down. Which the right shift already did for us thanks
             * to truncation. */
        }
        /* else the value is subnormal. */
    }
    else if (exponent >= EXPONENT_MIN) {
        /* In range already. */
        err = mp_mul_2(&remainder, &remainder);
        if (err != MP_OKAY) {
            mp_clear_multi(&quotient, &remainder, scaled, NULL);
            MVM_exception_throw_adhoc(tc, "Failed to double remainder in bigint division");
        }
        mp_ord cmp = mp_cmp_mag(&remainder, denominator_scaled);
        if (cmp != MP_LT) {
            if (cmp == MP_GT) {
                ++mantissa;
                /* Round up. */
            }
            else {
                /* The remainder is exactly midway between the two possible
                 * values. For this case, IEEE says round to even. */
                if (mantissa & 1) {
                    /* round up to even. */
                    ++mantissa;
                }
                /* else round down to even. */
            }
            if (mantissa == UINT64_C(1) << MANTISSA_BITS_IN_DOUBLE) {
                /* As before, "even further."
                 * Writing this comment after the comment below about "not
                 * specifically tested", I realise that the design of the
                 * floating point bit patterns in memory means that one could
                 * actually perform this rounding after packing mantissa and
                 * exponent together, for all 3 cases, because the LSB of the
                 * exponent is immediately above the Most Significant (Stored)
                 * Bit of the mantissa. So
                 * 1) subnormal rounds up to lowest normal
                 *    (subnormal's exponent is 0x000; smallest normal is 0x001)
                 * 2) 0x(1)fffffffffffffp123 to 0x(1)0000000000000p124
                 * 3) largest possible finite value rounds up to infinity -
                 *    the exponent of Inf is one more than highest float, and
                 *    the bit representation is 0.
                 * I don't think that this code would be any clearer if I did
                 * that, but I can see why it would help hardware design.
                 */
                mantissa >>= 1;
                ++exponent;
            }
        }
        /* else round down. */
    }
    /* else the value is subnormal */

    if (exponent < EXPONENT_MIN) {
        /* Subnormals.
         * Subnormals are numbers too. */

        unsigned int shift_by = EXPONENT_MIN - exponent;
        if (shift_by > MANTISSA_BITS_IN_DOUBLE) {
            /* Really really small - the result rounds to zero. */
            mantissa = 0;
        }
        else {
            /* OK, so the corner case of the corner cases is where we were a
             * factor of two too large (above), and when we correct *that* it
             * turns out that actually we're subnormal.
             * In which case, what we thought was the "carry" bit (the bit we
             * shifted off the over-large mantissa, because it really should be
             * the MSB of the remainder), is actually *not* the true MSB of the
             * remainder, but one of the others.
             * Which, it turns out, isn't *that* complex, because all that
             * matters about all the "other" bits is whether they are all zero.
             * (If carry is 0, then we always round down. If carry is 1, we need
             * to check this the "other" bits - if they are all zero we are
             * exactly halfway, and so round-to-even applies.) */
            assert(shift_by > 0);
            unsigned int shift_up = shift_by - 1;
            MVMuint64 actual_carry_bit_pos = UINT64_C(1) << shift_up;

            /* "smushed up" - `rest` is a 64 bit boolean.
             * `actual_carry_bit_pos - 1` will be 0 if exponent is 52. That's
             * fine. */
            MVMuint64 rest = carry | (mantissa & (actual_carry_bit_pos - 1));
            int actual_carry = (mantissa & actual_carry_bit_pos) ? 1 : 0;

            mantissa >>= shift_by;
            if (actual_carry) {
                if (rest || !mp_iszero(&remainder)) {
                    /* IEEE says round up. */
                    ++mantissa;
                }
                else {
                    /* midway, so round to even. */
                    if (mantissa & 1) {
                        ++mantissa;
                    }
                }
            }
        }
        /* Not specifically tested - I think that there is also a corner case
         * sort of implicitly handled here. If the mantissa is *just* subnormal
         * - ie 0x0FFFFFFFFFFFFF.....
         * - ie would be 0x0.fffffffffffffp-1022 (if truncated)
         * and then it turns out that the '....' causes the mantissa to round up
         * then I think that mantissa here is now actually
         * 0x100000000000000
         * and the bitwise or below means that that 1 bit set at bit 53 actually
         * "leaks" into the lowest bit of exponent, causing the value to
         * (correctly) become 0x1.0000000000000p-10222
         */
        exponent = EXPONENT_ZERO;
    }
    else {
        /* The ^ clears the always-set 53rd bit. */
        assert(mantissa & UINT64_C(1) << (MANTISSA_BITS_IN_DOUBLE - 1));
        mantissa ^= UINT64_C(1) << (MANTISSA_BITS_IN_DOUBLE - 1);
        exponent += EXPONENT_BIAS;
    }

    mp_clear_multi(&quotient, &remainder, scaled, NULL);

    if (exponent > EXPONENT_MAX + EXPONENT_BIAS)
        return negative ? -MVM_NUM_NEGINF : MVM_NUM_POSINF;

    union double_or_uint64 {
        MVMuint64 u;
        double d;
    } result;

    result.u = (negative ? UINT64_C(0x8000000000000000) : 0)
        | (((MVMuint64) exponent) << EXPONENT_SHIFT)
        | mantissa;

    /* This code assumes that the floating point endianness always matches the
     * integer endianness, which is not something that IEEE requires.
     * It isn't true on certain obscure platforms (eg very old ARM with
     * softfloat) but we already don't support those because we won't even be
     * able to correctly deserialised the floating point constants stored in the
     * stage 0 bootstrap files. So don't worry about that here, until someone
     * submits a working patch for the deserialisation code. */
    return result.d;
}

MVMnum64 MVM_bigint_div_num(MVMThreadContext *tc, MVMObject *a, MVMObject *b) {
    MVMP6bigintBody *ba = get_bigint_body(tc, a);
    MVMP6bigintBody *bb = get_bigint_body(tc, b);
    MVMnum64 c;

    if (MVM_BIGINT_IS_BIG(ba) || MVM_BIGINT_IS_BIG(bb)) {
        /* To get here at least one bigint must be big, meaning we can't have
         * 0/0 and hence the NaN case. */
        mp_int *ia = force_bigint(tc, ba, 0);
        mp_int *ib = force_bigint(tc, bb, 1);

        mp_clamp(ia);
        mp_clamp(ib);

        c = bigint_div_num(tc, ia, ib);
    } else {
        c = (double)ba->u.smallint.value / (double)bb->u.smallint.value;
    }
    return c;
}

/* The below function is copied from libtommath and modified to use
 * jfs64 as the source of randomness. As of LTM v1.1.0, mp_rand()
 * uses sources of randomness that can't be seeded. Since we want to
 * be able to do that, for now just copy and modify.
 */

/* LibTomMath, multiple-precision integer library -- Tom St Denis */
/* SPDX-License-Identifier: Unlicense */
mp_err MVM_mp_rand(MVMThreadContext *tc, mp_int *a, int digits)
{
   int i;
   mp_err err;

   mp_zero(a);

   if (digits <= 0) {
      return MP_OKAY;
   }

   if ((err = mp_grow(a, digits)) != MP_OKAY) {
      return err;
   }

   /* TODO: We ensure that the highest digit is nonzero. Should this be removed? */
   while ((a->dp[digits - 1] & MP_MASK) == 0u) {
      a->dp[digits - 1] = jfs64_generate_uint64(tc->rand_state);
   }

   a->used = digits;
   for (i = 0; i < digits; ++i) {
      a->dp[i] = jfs64_generate_uint64(tc->rand_state);
      a->dp[i] &= MP_MASK;
   }

   return MP_OKAY;
}


/* 
    The old version of LibTomMath has it publically defined the new one not,
    so we can take the (non)existance as a marker.
 */
#ifndef MP_GEN_RANDOM_MAX
#define MP_GEN_RANDOM_MAX MP_MASK
#define MP_NEW_LTM_VERSION
#endif

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
            smallint_max = bb->u.bigint->dp[0];
            have_to_negate = bb->u.bigint->sign == MP_NEG;
        }
    } else {
        use_small_arithmetic = 1;
        smallint_max = bb->u.smallint.value;
    }

    if (use_small_arithmetic) {
        if (MP_GEN_RANDOM_MAX >= (unsigned long)abs(smallint_max)) {
            mp_digit result_int = jfs64_generate_uint64(tc->rand_state);
            result_int = result_int % smallint_max;
            if (have_to_negate)
                result_int *= -1;

            MVMROOT2(tc, type, b, {
                result = MVM_repr_alloc_init(tc, type);
            });

            ba = get_bigint_body(tc, result);
            store_int64_result(tc, ba, result_int);
        } else {
            use_small_arithmetic = 0;
        }
    }

    if (!use_small_arithmetic) {
        mp_err err;
        mp_int *rnd = MVM_malloc(sizeof(mp_int));
        mp_int *max = force_bigint(tc, bb, 0);

        MVMROOT2(tc, type, b, {
            result = MVM_repr_alloc_init(tc, type);
        });

        ba = get_bigint_body(tc, result);

        if ((err = mp_init(rnd)) != MP_OKAY) {
            MVM_free(rnd);
            MVM_exception_throw_adhoc(tc, "Error creating a big integer: %s", mp_error_to_string(err));
        }
        if ((err = MVM_mp_rand(tc, rnd, max->used + 1)) != MP_OKAY) {
            mp_clear(rnd);
            MVM_free(rnd);
            MVM_exception_throw_adhoc(tc, "Error randomizing a big integer: %s", mp_error_to_string(err));
        }

        if ((err = mp_mod(rnd, max, rnd)) != MP_OKAY) {
            mp_clear(rnd);
            MVM_free(rnd);
            MVM_exception_throw_adhoc(tc, "Error in mp_mod: %s", mp_error_to_string(err));
        }
        store_bigint_result(ba, rnd);
        adjust_nursery(tc, ba);
    }

    return result;
}

/* Forišek and Jančina, "Fast Primality Testing for Integers That Fit into a Machine Word", 2015
 *  FJ32_256 code slightly modifed to work in MoarVM
 */
static uint16_t bases[]={15591,2018,166,7429,8064,16045,10503,4399,1949,1295,2776,3620,560,3128,5212,
2657,2300,2021,4652,1471,9336,4018,2398,20462,10277,8028,2213,6219,620,3763,4852,5012,3185,
1333,6227,5298,1074,2391,5113,7061,803,1269,3875,422,751,580,4729,10239,746,2951,556,2206,
3778,481,1522,3476,481,2487,3266,5633,488,3373,6441,3344,17,15105,1490,4154,2036,1882,1813,
467,3307,14042,6371,658,1005,903,737,1887,7447,1888,2848,1784,7559,3400,951,13969,4304,177,41,
19875,3110,13221,8726,571,7043,6943,1199,352,6435,165,1169,3315,978,233,3003,2562,2994,10587,
10030,2377,1902,5354,4447,1555,263,27027,2283,305,669,1912,601,6186,429,1930,14873,1784,1661,
524,3577,236,2360,6146,2850,55637,1753,4178,8466,222,2579,2743,2031,2226,2276,374,2132,813,
23788,1610,4422,5159,1725,3597,3366,14336,579,165,1375,10018,12616,9816,1371,536,1867,10864,
857,2206,5788,434,8085,17618,727,3639,1595,4944,2129,2029,8195,8344,6232,9183,8126,1870,3296,
7455,8947,25017,541,19115,368,566,5674,411,522,1027,8215,2050,6544,10049,614,774,2333,3007,
35201,4706,1152,1785,1028,1540,3743,493,4474,2521,26845,8354,864,18915,5465,2447,42,4511,
1660,166,1249,6259,2553,304,272,7286,73,6554,899,2816,5197,13330,7054,2818,3199,811,922,350,
7514,4452,3449,2663,4708,418,1621,1171,3471,88,11345,412,1559,194};

static MVMint64 is_SPRP(uint32_t n, uint32_t a) {
    uint32_t d = n-1, s = 0;
    while ((d&1)==0) ++s, d>>=1;
    uint64_t cur = 1, pw = d;
    while (pw) {
        if (pw & 1) cur = (cur*a) % n;
        a = ((uint64_t)a*a) % n;
        pw >>= 1;
    }
    if (cur == 1) return 1;
    for (uint32_t r=0; r<s; r++) {
        if (cur == n-1) return 1;
        cur = (cur*cur) % n;
    }
    return 0;
}

MVMint64 MVM_bigint_is_prime(MVMThreadContext *tc, MVMObject *a) {
    MVMP6bigintBody *ba = get_bigint_body(tc, a);
    if (MVM_BIGINT_IS_BIG(ba)) {
        mp_int *ia = ba->u.bigint;
        mp_err err;
        mp_bool result;

        if (ia->sign == MP_NEG) {
            return 0;
        }

        if ((err = mp_prime_is_prime(ia, 40, &result)) != MP_OKAY)
            MVM_exception_throw_adhoc(tc, "Error checking primality of a big integer: %s", mp_error_to_string(err));

        return result;
    }
    else {
        MVMint32 x = ba->u.smallint.value;
        if (x==2 || x==3 || x==5 || x==7) return 1;
        if (x%2==0 || x%3==0 || x%5==0 || x%7==0 || x<0) return 0;
        if (x<121) return (x>1);
        uint64_t h = x;
        h = ((h >> 16) ^ h) * 0x45d9f3b;
        h = ((h >> 16) ^ h) * 0x45d9f3b;
        h = ((h >> 16) ^ h) & 255;
        return is_SPRP(x,bases[h]);
    }
}

MVMObject * MVM_bigint_radix(MVMThreadContext *tc, MVMint64 radix, MVMString *str, MVMint64 offset, MVMint64 flag, MVMObject *type) {
    mp_err err;
    MVMObject *result;
    MVMint64 chars  = MVM_string_graphs(tc, str);
    MVMuint16  neg  = 0;
    MVMint64   ch;
    MVMuint32  chars_converted = 0;
    MVMuint32  chars_really_converted = chars_converted;

    mp_int zvalue;

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

    if ((err = mp_init(&zvalue)) != MP_OKAY) {
        MVM_exception_throw_adhoc(tc, "Error creating big integers: %s", mp_error_to_string(err));
    }

    value_obj = MVM_repr_alloc_init(tc, type);
    MVM_repr_push_o(tc, result, value_obj);
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&value_obj);

    base_obj = MVM_repr_alloc_init(tc, type);
    MVM_repr_push_o(tc, result, base_obj);

    bvalue = get_bigint_body(tc, value_obj);
    bbase  = get_bigint_body(tc, base_obj);

    value = MVM_malloc(sizeof(mp_int));

    if ((err = mp_init(value)) != MP_OKAY) {
        mp_clear(&zvalue);
        MVM_free(value);
        MVM_exception_throw_adhoc(tc, "Error creating big integers: %s", mp_error_to_string(err));
    }

    ch = (offset < chars) ? MVM_string_get_grapheme_at_nocheck(tc, str, offset) : 0;
    if ((flag & 0x02) && (ch == '+' || ch == '-' || ch == 0x2212)) {  /* MINUS SIGN */
        neg = (ch == '-' || ch == 0x2212);
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
        if ((err = mp_mul_d(&zvalue, radix, &zvalue)) != MP_OKAY) {
            mp_clear_multi(&zvalue, value, NULL);
            MVM_free(value);
            MVM_exception_throw_adhoc(tc, "Error multiplying a big integer by a digit: %s", mp_error_to_string(err));
        }
        if ((err = mp_add_d(&zvalue, ch, &zvalue)) != MP_OKAY) {
            mp_clear_multi(&zvalue, value, NULL);
            MVM_free(value);
            MVM_exception_throw_adhoc(tc, "Error adding a big integer by a digit: %s", mp_error_to_string(err));
        }
        offset++; pos = offset;
        chars_converted++;
        if (ch != 0 || !(flag & 0x04)) {
            chars_really_converted = chars_converted;
            if ((err = mp_copy(&zvalue, value)) != MP_OKAY) {
                mp_clear_multi(&zvalue, value, NULL);
                MVM_free(value);
                MVM_exception_throw_adhoc(tc, "Error copying a big integer: %s", mp_error_to_string(err));
            }
        }
        if (offset >= chars) break;
        ch = MVM_string_get_grapheme_at_nocheck(tc, str, offset);
        if (ch != '_') continue;
        offset++;
        if (offset >= chars) break;
        ch = MVM_string_get_grapheme_at_nocheck(tc, str, offset);
    }

    mp_clear(&zvalue);

    if (neg || flag & 0x01) {
        if ((err = mp_neg(value, value)) != MP_OKAY) {
            mp_clear(value);
            MVM_free(value);
            MVM_exception_throw_adhoc(tc, "Error negating a big integer: %s", mp_error_to_string(err));
        }
    }

    base = MVM_malloc(sizeof(mp_int));
    if ((err =  mp_init_u32(base, chars_really_converted)) != MP_OKAY) {
        MVM_free(base);
        MVM_exception_throw_adhoc(tc, "Error creating a big integer: %s", mp_error_to_string(err));
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
        if ( sizeof(MVMint64) * 8 > MP_DIGIT_BIT && is_big == 0 && b->dp[0] & ~0x7FFFFFFFUL)
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
