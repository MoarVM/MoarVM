#include "moar.h"
#include <math.h>

#ifdef INFINITY
const double MVM_NUM_POSINF =  INFINITY;
const double MVM_NUM_NEGINF = -INFINITY;
#else
const double MVM_NUM_POSINF =  1.0 / 0.0;
const double MVM_NUM_NEGINF = -1.0 / 0.0;
#endif

#ifdef NAN
const double MVM_NUM_NAN = NAN;
#else
const double MVM_NUM_NAN = 0.0 / 0.0;
#endif

MVMint64 MVM_num_isnanorinf(MVMThreadContext *tc, MVMnum64 n) {
    return n == MVM_NUM_POSINF || n == MVM_NUM_NEGINF || n == MVM_NUM_NAN;
}

MVMnum64 MVM_num_posinf(MVMThreadContext *tc) {
    return MVM_NUM_POSINF;
}

MVMnum64 MVM_num_neginf(MVMThreadContext *tc) {
    return MVM_NUM_NEGINF;
}

MVMnum64 MVM_num_nan(MVMThreadContext *tc) {
    return MVM_NUM_NAN;
}
