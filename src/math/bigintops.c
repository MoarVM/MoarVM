#include "moar.h"
#include "platform/random.h"
#include "tinymt64.h"
#include "ctype.h"

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

#ifndef MPZ_IS_ZERO
    #define MPZ_IS_ZERO(x) (mpz_cmp_ui((x), 0L) == 0)
#endif

#ifndef MPZ_IS_NEG
    #define MPZ_IS_NEG(x) (mpz_sgn((x)) == -1)
#endif

#ifndef MPZ_IS_POS
    #define MPZ_IS_POS(x) (mpz_sgn((x)) == 1)
#endif

/* GMP has an mpz_mod, but we don't use it, redefining it so we can use the MVM_BIGINT_BINARY_OP_2
 * macro for MVM_bigint_mod. */
#undef mpz_mod
#define mpz_mod mpz_fdiv_r

static jmp_buf buf;

void handler(int signal) {
    longjmp(buf, 1);
}

MVM_STATIC_INLINE void adjust_nursery(MVMThreadContext *tc, MVMP6bigintBody *body) {
    if (MVM_BIGINT_IS_BIG(body)) {
        int used = mpz_size(*body->u.bigint) * sizeof(mp_limb_t);
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
static int can_be_smallint(const mpz_t *i) {
    return mpz_fits_sint_p(*i);
}

/* Forces a bigint, even if we only have a smallint. Takes a parameter that
 * indicates where to allocate a temporary mpz_t if needed. */
static mpz_t * force_bigint(MVMThreadContext *tc, const MVMP6bigintBody *body, int idx) {
    if (MVM_BIGINT_IS_BIG(body)) {
        return body->u.bigint;
    }
    else {
        mpz_t *i = tc->temp_bigints[idx];
        mpz_set_si(*i, body->u.smallint.value);
        return i;
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
        mpz_t *i = MVM_malloc(sizeof(mpz_t));
        mpz_init_set_si(*i, result);
        body->u.bigint = i;
    }
}

/* Stores a bigint in a bigint result body, either as a 32-bit smallint if it
 * is in range, or a big integer if not. Clears and frees the passed bigint if
 * it is not being used. */
static void store_bigint_result(MVMP6bigintBody *body, mpz_t *i) {
    if (can_be_smallint(i)) {
        body->u.smallint.flag = MVM_BIGINT_32_FLAG;
        body->u.smallint.value = (MVMint32)mpz_get_si(*i);
        mpz_clear(*i);
        MVM_free(i);
    }
    else {
        body->u.bigint = i;
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
            mpz_t *ia = ba->u.bigint; \
            mpz_t *ib = MVM_malloc(sizeof(mpz_t)); \
            mpz_init(*ib); \
            mpz_##opname(*ib, *ia); \
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
    mpz_t *ia, *ib, *ic; \
    MVMROOT2(tc, a, b, { \
        result = MVM_repr_alloc_init(tc, result_type);\
    }); \
    ba = get_bigint_body(tc, a); \
    bb = get_bigint_body(tc, b); \
    bc = get_bigint_body(tc, result); \
    ia = force_bigint(tc, ba, 0); \
    ib = force_bigint(tc, bb, 1); \
    ic = MVM_malloc(sizeof(mpz_t)); \
    mpz_init(*ic); \
    mpz_##opname(*ic, *ia, *ib); \
    store_bigint_result(bc, ic); \
    adjust_nursery(tc, bc); \
    return result; \
}

#define MVM_BIGINT_BINARY_OP_SIMPLE(opname, SMALLINT_OP) \
void MVM_bigint_fallback_##opname(MVMThreadContext *tc, MVMP6bigintBody *ba, MVMP6bigintBody *bb, \
                                  MVMP6bigintBody *bc) { \
    mpz_t *ia, *ib, *ic; \
    ia = force_bigint(tc, ba, 0); \
    ib = force_bigint(tc, bb, 1); \
    ic = MVM_malloc(sizeof(mpz_t)); \
    mpz_init(*ic); \
    mpz_##opname(*ic, *ia, *ib); \
    store_bigint_result(bc, ic); \
    adjust_nursery(tc, bc); \
} \
MVMObject * MVM_bigint_##opname(MVMThreadContext *tc, MVMObject *result_type, MVMObject *a, MVMObject *b) { \
    MVMP6bigintBody *ba, *bb, *bc; \
    MVMObject *result; \
    ba = get_bigint_body(tc, a); \
    bb = get_bigint_body(tc, b); \
    if (MVM_BIGINT_IS_BIG(ba) || MVM_BIGINT_IS_BIG(bb)) { \
        mpz_t *ia, *ib, *ic; \
        MVMROOT2(tc, a, b, { \
            result = MVM_repr_alloc_init(tc, result_type);\
        }); \
        ba = get_bigint_body(tc, a); \
        bb = get_bigint_body(tc, b); \
        bc = get_bigint_body(tc, result); \
        ia = force_bigint(tc, ba, 0); \
        ib = force_bigint(tc, bb, 1); \
        ic = MVM_malloc(sizeof(mpz_t)); \
        mpz_init(*ic); \
        mpz_##opname(*ic, *ia, *ib); \
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
            mpz_t *ia = force_bigint(tc, ba, 0); \
            mpz_t *ib = force_bigint(tc, bb, 1); \
            mpz_t *ic = MVM_malloc(sizeof(mpz_t)); \
            mpz_init(*ic); \
            mpz_##opname(*ic, *ia, *ib); \
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

MVM_BIGINT_BINARY_OP_2(ior, { sc = sa | sb; })
MVM_BIGINT_BINARY_OP_2(xor, { sc = sa ^ sb; })
MVM_BIGINT_BINARY_OP_2(and, { sc = sa & sb; })
MVM_BIGINT_BINARY_OP_2(gcd, { sa = abs((MVMint32)sa); sb = abs((MVMint32)sb); while (sb != 0) { sc = sb; sb = sa % sb; sa = sc; }; sc = sa; })
MVM_BIGINT_BINARY_OP_2(mod, { sc = ((sa % sb) + sb) % sb; })

MVMint64 MVM_bigint_cmp(MVMThreadContext *tc, MVMObject *a, MVMObject *b) {
    MVMP6bigintBody *ba = get_bigint_body(tc, a);
    MVMP6bigintBody *bb = get_bigint_body(tc, b);
    if (MVM_BIGINT_IS_BIG(ba) || MVM_BIGINT_IS_BIG(bb)) {
        mpz_t *ia = force_bigint(tc, ba, 0);
        mpz_t *ib = force_bigint(tc, bb, 1);
        MVMint64 r = (MVMint64)mpz_cmp(*ia, *ib);
        return r == 0 ? 0 : r < 0 ? -1 : 1;
    }
    else {
        MVMint64 sa = ba->u.smallint.value;
        MVMint64 sb = bb->u.smallint.value;
        return sa == sb ? 0 : sa <  sb ? -1 : 1;
    }
}

MVMObject *MVM_bigint_div(MVMThreadContext *tc, MVMObject *result_type, MVMObject *a, MVMObject *b) {
    MVMP6bigintBody *ba;
    MVMP6bigintBody *bb = get_bigint_body(tc, b);
    MVMP6bigintBody *bc;
    mpz_t *ia, *ib, *ic;
    int cmp_a;
    int cmp_b;
    MVMObject *result;

    if (!MVM_BIGINT_IS_BIG(bb) && bb->u.smallint.value == 1 && STABLE(a) == STABLE(b)) {
        return a;
    }

    MVMROOT2(tc, a, b, {
        result = MVM_repr_alloc_init(tc, result_type);
    });

    ba = get_bigint_body(tc, a);
    bb = get_bigint_body(tc, b);

    bc = get_bigint_body(tc, result);

    if (MVM_BIGINT_IS_BIG(ba) || MVM_BIGINT_IS_BIG(bb)) {
        ia = force_bigint(tc, ba, 0);
        ib = force_bigint(tc, bb, 1);

        ic = MVM_malloc(sizeof(mpz_t));
        mpz_init(*ic);
        mpz_fdiv_q(*ic, *ia, *ib);
        store_bigint_result(bc, ic);
        adjust_nursery(tc, bc);
    } else {
    /* we only care about -1 or !-1, so we give 1 even for 0. */
        cmp_a = ba->u.smallint.value < 0 ? -1 : 1;
        cmp_b = bb->u.smallint.value < 0 ? -1 : 1;

        MVMint32 num   = ba->u.smallint.value;
        MVMint32 denom = bb->u.smallint.value;
        MVMint64 value;
        if ((cmp_a == -1) ^ (cmp_b == -1)) {
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

    mpz_t *base        = force_bigint(tc, ba, 0);
    mpz_t *exponent    = force_bigint(tc, bb, 1);
    unsigned long exponent_d = 0;

    /* base ** 0 || 1 ** exponent == 1 */
    if (MPZ_IS_ZERO(*exponent) || (0 == mpz_cmp_si(*base, 1L))) {
        r = MVM_repr_box_int(tc, int_type, 1);
    }
    /* 0 ** exponent == 0 */
    else if (MPZ_IS_ZERO(*base)) {
        r = MVM_repr_box_int(tc, int_type, 0);
    }
    /* (-)1 ** exponent == (-)1 */
    else if (mpz_get_ui(*base) == 1) {
        r = MVM_repr_box_int(tc, int_type, MPZ_IS_POS(*base) || mpz_even_p(*exponent) ? 1 : -1);
    }
    else if (MPZ_IS_POS(*exponent)) {
        exponent_d = mpz_get_ui(*exponent);
        if (mpz_cmp_ui(*exponent, exponent_d) > 0) {
            MVMnum64 inf;
            if (MPZ_IS_POS(*base) || mpz_even_p(*exponent)) {
                inf = MVM_num_posinf(tc);
            }
            else {
                inf = MVM_num_neginf(tc);
            }
            r = MVM_repr_box_num(tc, num_type, inf);
        }
        else {
            mpz_t *ic = MVM_malloc(sizeof(mpz_t));
            MVMP6bigintBody *resbody;
            mpz_init(*ic);
            MVM_gc_mark_thread_blocked(tc);
            signal(SIGABRT, handler);
            if (setjmp(buf)) {
                signal(SIGABRT, NULL);
                MVM_gc_mark_thread_unblocked(tc);
                mpz_clear(*ic);
                MVM_free(ic);
                MVMnum64 inf;
                if (MPZ_IS_POS(*base) || mpz_even_p(*exponent)) {
                    inf = MVM_num_posinf(tc);
                }
                else {
                    inf = MVM_num_neginf(tc);
                }
                r = MVM_repr_box_num(tc, num_type, inf);
            }
            else {
                mpz_pow_ui(*ic, *base, exponent_d);
                signal(SIGABRT, NULL);
                MVM_gc_mark_thread_unblocked(tc);
                r = MVM_repr_alloc_init(tc, int_type);
                resbody = get_bigint_body(tc, r);
                store_bigint_result(resbody, ic);
                adjust_nursery(tc, resbody);
            }
        }
    }
    else {
        MVMnum64 f_base = mpz_get_d(*base);
        MVMnum64 f_exp = mpz_get_d(*exponent);
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
        mpz_t *ia = force_bigint(tc, ba, 0);
        mpz_t *ib = MVM_malloc(sizeof(mpz_t));
        mpz_init(*ib);
        mpz_mul_2exp(*ib, *ia, n);
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
    if (MVM_BIGINT_IS_BIG(ba)) {
        return MPZ_IS_NEG(*ba->u.bigint);
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
        mpz_t *ia = force_bigint(tc, ba, 0);
        mpz_t *ib = MVM_malloc(sizeof(mpz_t));
        mpz_init(*ib);
        if (n >= 0)
            mpz_fdiv_q_2exp(*ib, *ia, n);
        else
            mpz_mul_2exp(*ib, *ia, -n);
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
        mpz_t *ia = ba->u.bigint;
        mpz_t *ib = MVM_malloc(sizeof(mpz_t));
        mpz_init(*ib);
        /* two's complement not: add 1 and negate */
        mpz_add_ui(*ib, *ia, 1);
        mpz_neg(*ib, *ib);
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

    mpz_t *ia = force_bigint(tc, ba, 0);
    mpz_t *ib = force_bigint(tc, bb, 1);
    mpz_t *ic = force_bigint(tc, bc, 2);
    mpz_t *id = MVM_malloc(sizeof(mpz_t));
    mpz_init(*id);

    if (MPZ_IS_ZERO(*ic) || (MPZ_IS_NEG(*ib) && (MPZ_IS_ZERO(*ic) || mpz_invert(*id, *ia, *ic) == 0))) {
        mpz_clear(*id);
        MVM_free(id);
        MVM_exception_throw_adhoc(tc, "Invalid values for expmod");
    }

    MVMROOT3(tc, a, b, c, {
        result = MVM_repr_alloc_init(tc, result_type);
    });

    bd = get_bigint_body(tc, result);

    mpz_powm(*id, *ia, *ib, *ic);
    store_bigint_result(bd, id);
    adjust_nursery(tc, bd);

    return result;
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

    MVMP6bigintBody *body = get_bigint_body(tc, a);
    mpz_t *ia = MVM_malloc(sizeof(mpz_t));
    if (mpz_init_set_str(*ia, buf, 10) == -1) {
        mpz_clear(*ia);
        MVM_free(ia);
        char *waste[] = { NULL, NULL };
        char *err;
        if (is_malloced) {
            waste[0] = buf;
        }
        else {
            err = MVM_malloc(s->body.num_graphs + 1);
            strcpy(err, buf);
            waste[0] = err;
        }
        MVM_exception_throw_adhoc_free(tc, waste, "Failed to convert '%s' to a bigint", is_malloced ? buf : err);
    }
    store_bigint_result(body, ia);
    adjust_nursery(tc, body);

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
        mpz_t *i = MVM_malloc(sizeof(mpz_t));
        mpz_init_set(*i, *a_body->u.bigint);
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
        mpz_t *i = body->u.bigint;
        char *buf = mpz_get_str(NULL, base, *i);
        int len = strlen(buf);
        if (base > 10)
            for (int i = 0; i < len; i++)
                buf[i] = toupper(buf[i]);
        MVMString *result = MVM_string_ascii_decode(tc, tc->instance->VMString, buf, len);
        MVM_free(buf);
        return result;
    }
    else {
        if (base == 10) {
            return MVM_coerce_i_s(tc, body->u.smallint.value);
        }
        else {
            /* It's small, but shove it through bigint lib, as it knows how to
             * get other bases right. */
            mpz_t i;
            mpz_init_set_si(i, body->u.smallint.value);
            char *buf = mpz_get_str(NULL, base, i);
            int len = strlen(buf);
            if (base > 10)
                for (int i = 0; i < len; i++)
                    buf[i] = toupper(buf[i]);
            MVMString *result = MVM_string_ascii_decode(tc, tc->instance->VMString, buf, len);
            MVM_free(buf);
            mpz_clear(i);
            return result;
        }
    }
}

MVMnum64 MVM_bigint_to_num(MVMThreadContext *tc, MVMObject *a) {
    MVMP6bigintBody *ba = get_bigint_body(tc, a);
    if (MVM_BIGINT_IS_BIG(ba)) {
        return mpz_get_d(*ba->u.bigint);
    } else {
        return (double)ba->u.smallint.value;
    }
}

MVMObject *MVM_bigint_from_num(MVMThreadContext *tc, MVMObject *result_type, MVMnum64 n) {
    MVMObject * const result = MVM_repr_alloc_init(tc, result_type);
    MVMP6bigintBody *ba = get_bigint_body(tc, result);
    mpz_t *ia = MVM_malloc(sizeof(mpz_t));
    mpz_init_set_d(*ia, n);
    store_bigint_result(ba, ia);
    return result;
}

/* Implementation based on the approach described in
 * https://www.exploringbinary.com/correct-decimal-to-floating-point-using-big-integers/
 * but uses `mpz_sizeinbase` to determine the needed scaling within a factor of 2
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

static MVMnum64 bigint_div_num(MVMThreadContext *tc, const mpz_t *numerator, const mpz_t *denominator) {
    int negative = mpz_sgn(*numerator) != mpz_sgn(*denominator);

    if (MPZ_IS_ZERO(*denominator)) {
        /* Comments in the caller imply that the NaN case is unreachable.
         * However, I'd prefer to leave it in place, as that way this code can
         * be extracted and independently tested for all corner cases.
         * (With decimal strings expressed as $numerator / 10 ** $denominator
         * it is bit-for-bit identical with David M. Gay's strtod, including
         * values that overflow, underflow, generate subnormals and negative
         * zero. That laundry list means we just need 0 / 0 => NaN to catch 'em
         * all.) */
        if (MPZ_IS_ZERO(*numerator))
            return MVM_NUM_NAN;
        return negative ? -MVM_NUM_NEGINF : MVM_NUM_POSINF;
    }
    if (MPZ_IS_ZERO(*numerator))
        return 0.0;

    int floor_log2_num = mpz_sizeinbase(*numerator, 2) - 1;
    int floor_log2_den = mpz_sizeinbase(*denominator, 2) - 1;
    int exponent = floor_log2_num - floor_log2_den - 1;
    int in_range = MANTISSA_BITS_IN_DOUBLE - exponent - 1;

    const mpz_t *numerator_scaled;
    const mpz_t *denominator_scaled;

    mpz_t temp;
    mpz_t quotient;
    mpz_t remainder;
    mpz_t *scaled = in_range ? &temp : NULL;

    mpz_inits(quotient, remainder, *scaled, NULL);

    if (!in_range) {
        numerator_scaled = numerator;
        denominator_scaled = denominator;
    }
    else {
        /* We need to multiply by 2**|in_range| */
        int exponent = abs(in_range);

        if (in_range > 0) {
            mpz_mul_2exp(*scaled, *numerator, exponent);

            numerator_scaled = scaled;
            denominator_scaled = denominator;
        }
        else {
            mpz_mul_2exp(*scaled, *denominator, exponent);

            numerator_scaled = numerator;
            denominator_scaled = scaled;
        }
    }

    /* Should not be possible to hit divide-by-zero here. */
    mpz_tdiv_qr(quotient, remainder, *numerator_scaled, *denominator_scaled);

    assert(mpz_sizeinbase(quotient, 2) <= MANTISSA_BITS_IN_DOUBLE + 2);

    uint64_t mantissa = mpz_get_ui(quotient);
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
                if (MPZ_IS_ZERO(remainder)) {
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
        mpz_mul_ui(remainder, remainder, 2L);
        int cmp = mpz_cmpabs(remainder, *denominator_scaled);
        if (cmp >= 0) {
            if (cmp > 0) {
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
                if (rest || !MPZ_IS_ZERO(remainder)) {
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

    mpz_clears(quotient, remainder, *scaled, NULL);

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
        mpz_t *ia = force_bigint(tc, ba, 0);
        mpz_t *ib = force_bigint(tc, bb, 1);

        c = bigint_div_num(tc, ia, ib);
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
    /* This has to be an MVMint64 (and we have to use labs below) because u.smallint.value
     * could be INT_MIN. Calling abs on INT_MIN is UB and negating it is still INT_MIN. */
    MVMint64 smallint_max = 0;

    if (MVM_BIGINT_IS_BIG(bb)) {
        if (can_be_smallint(bb->u.bigint)) {
            use_small_arithmetic = 1;
            smallint_max = mpz_get_ui(*bb->u.bigint);
        }
    }
    else {
        use_small_arithmetic = 1;
        smallint_max = labs(bb->u.smallint.value);
    }

    if (use_small_arithmetic) {
        unsigned long result_int = tinymt64_generate_uint64(tc->rand_state);
        result_int = result_int % smallint_max;
        if (BIGINT_IS_NEGATIVE(bb))
            result_int *= -1;

        MVMROOT2(tc, type, b, {
            result = MVM_repr_alloc_init(tc, type);
        });

        ba = get_bigint_body(tc, result);
        store_int64_result(tc, ba, result_int);
    }

    if (!use_small_arithmetic) {
        mpz_t *rnd = MVM_malloc(sizeof(mpz_t));
        mpz_t *max = force_bigint(tc, bb, 0);

        MVMROOT2(tc, type, b, {
            result = MVM_repr_alloc_init(tc, type);
        });

        ba = get_bigint_body(tc, result);

        mpz_init(*rnd);
        mpz_urandomm(*rnd, tc->gmp_rand_state, *max);
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
        /* We used to do 100 rounds, but that was overkill and we decided 40 was sufficient.
         * TODO:
         * However, GMP > 6.1.2 subtracts 24 from the value passed because it does a Baillie-PSW
         * probable prime test before the Miller-Rabin tests. According to their documentation:
         *     "Reasonable values of reps are between 15 and 50."
         * so we might be safe reducing this value even more. */
        return mpz_probab_prime_p(*ba->u.bigint, 40+24);
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
    MVMObject *result;
    MVMint64 chars  = MVM_string_graphs(tc, str);
    MVMuint16  neg  = 0;
    MVMint64   ch;
    MVMuint32  chars_converted = 0;
    MVMuint32  chars_really_converted = chars_converted;

    mpz_t zvalue;

    MVMObject *value_obj;
    mpz_t *value;
    MVMP6bigintBody *bvalue;

    MVMObject *base_obj;
    mpz_t *base;
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

    mpz_init(zvalue);

    value_obj = MVM_repr_alloc_init(tc, type);
    MVM_repr_push_o(tc, result, value_obj);
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&value_obj);

    base_obj = MVM_repr_alloc_init(tc, type);
    MVM_repr_push_o(tc, result, base_obj);

    bvalue = get_bigint_body(tc, value_obj);
    bbase  = get_bigint_body(tc, base_obj);

    value = MVM_malloc(sizeof(mpz_t));

    mpz_init(*value);

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
        mpz_mul_si(zvalue, zvalue, radix);
        mpz_add_ui(zvalue, zvalue, ch);
        offset++; pos = offset;
        chars_converted++;
        if (ch != 0 || !(flag & 0x04)) {
            chars_really_converted = chars_converted;
            mpz_set(*value, zvalue);
        }
        if (offset >= chars) break;
        ch = MVM_string_get_grapheme_at_nocheck(tc, str, offset);
        if (ch != '_') continue;
        offset++;
        if (offset >= chars) break;
        ch = MVM_string_get_grapheme_at_nocheck(tc, str, offset);
    }

    mpz_clear(zvalue);

    if (neg || flag & 0x01) {
        mpz_neg(*value, *value);
    }

    base = MVM_malloc(sizeof(mpz_t));
    mpz_init_set_ui(*base, chars_really_converted);

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
        return !mpz_fits_sint_p(*ba->u.bigint);
    }
    else {
        /* if it's in a smallint, it's 32 bits big at most and fits into an INTVAL easily. */
        return 0;
    }
}

MVMint64 MVM_bigint_bool(MVMThreadContext *tc, MVMObject *a) {
    MVMP6bigintBody *ba = get_bigint_body(tc, a);
    if (MVM_BIGINT_IS_BIG(ba)) {
        return !MPZ_IS_ZERO(*ba->u.bigint);
    }
    else {
        return ba->u.smallint.value != 0;
    }
}
