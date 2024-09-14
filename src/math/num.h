#ifndef MATH_NUM_H
#define MATH_NUM_H

#ifdef _WIN32
#include <float.h>
#endif
#include <math.h>

#if defined(INFINITY) && !defined(_AIX)
#  ifdef _MSC_VER
#define MVM_NUM_POSINF  INFINITY
#define MVM_NUM_NEGINF -INFINITY
#  else
static const MVMnum64 MVM_NUM_POSINF = INFINITY;
static const MVMnum64 MVM_NUM_NEGINF = -INFINITY;
#  endif
#else
#  ifdef _MSC_VER
#define MVM_NUM_POSINF  (DBL_MAX+DBL_MAX)
#define MVM_NUM_NEGINF -(DBL_MAX+DBL_MAX)
#  else
static const MVMnum64 MVM_NUM_POSINF = 1.0 / 0.0;
static const MVMnum64 MVM_NUM_NEGINF = -1.0 / 0.0;
#  endif
#endif

#if defined(NAN) && !defined(_AIX)
#  ifdef _MSC_VER
#define MVM_NUM_NAN NAN
#  else
static const MVMnum64 MVM_NUM_NAN = NAN;
#  endif
#else
#  ifdef _MSC_VER
#define MVM_NUM_NAN (MVM_NUM_POSINF-MVM_NUM_POSINF)
#  else
static const MVMnum64 MVM_NUM_NAN = 0.0 / 0.0;
#  endif
#endif

MVM_STATIC_INLINE int MVM_num_isnanorinf(MVMThreadContext *tc, MVMnum64 n) {
#if defined(MVM_HAS_ISINF) && defined(MVM_HAS_ISNAN)
    return isinf(n) || isnan(n);
#else
    return n == MVM_NUM_POSINF || n == MVM_NUM_NEGINF || n != n;
#endif
}

MVM_STATIC_INLINE MVMnum64 MVM_num_posinf(MVMThreadContext *tc) {
    return MVM_NUM_POSINF;
}

MVM_STATIC_INLINE MVMnum64 MVM_num_neginf(MVMThreadContext *tc) {
    return MVM_NUM_NEGINF;
}

MVM_STATIC_INLINE MVMnum64 MVM_num_nan(MVMThreadContext *tc) {
    return MVM_NUM_NAN;
}

MVM_STATIC_INLINE int MVM_num_isnegzero(MVMThreadContext *tc, MVMnum64 n) {
#ifdef MVM_HAS_SIGNBIT
    return n == 0 && signbit(n);
#else
    return n == 0 && 1.0 / n == MVM_NUM_NEGINF;
#endif
}

#endif
