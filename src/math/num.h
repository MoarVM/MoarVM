#ifndef MATH_NUM_H
#define MATH_NUM_H

#ifdef _WIN32
#include <float.h>
#endif

#if defined(INFINITY) && !defined(_AIX)
static const MVMnum64 MVM_NUM_POSINF =  INFINITY;
static const MVMnum64 MVM_NUM_NEGINF = -INFINITY;
#else
#  ifdef _MSC_VER
#define MVM_NUM_POSINF  (DBL_MAX+DBL_MAX)
#define MVM_NUM_NEGINF -(DBL_MAX+DBL_MAX)
#  else
static const MVMnum64 MVM_NUM_POSINF =  1.0 / 0.0;
static const MVMnum64 MVM_NUM_NEGINF = -1.0 / 0.0;
#  endif
#endif

#if defined(NAN) && !defined(_AIX)
static const MVMnum64 MVM_NUM_NAN = NAN;
#else
#  ifdef _MSC_VER
#define MVM_NUM_NAN (MVM_NUM_POSINF-MVM_NUM_POSINF)
#  else
static const MVMnum64 MVM_NUM_NAN = 0.0 / 0.0;
#  endif
#endif

MVMint64 MVM_num_isnanorinf(MVMThreadContext *tc, MVMnum64 n);
MVMnum64 MVM_num_posinf(MVMThreadContext *tc);
MVMnum64 MVM_num_neginf(MVMThreadContext *tc);
MVMnum64 MVM_num_nan(MVMThreadContext *tc);

#endif
