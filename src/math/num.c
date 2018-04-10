#include "moar.h"
#include <math.h>

MVMint64 MVM_num_isnanorinf(MVMThreadContext *tc, MVMnum64 n) {
    return n == MVM_NUM_POSINF || n == MVM_NUM_NEGINF || n != n;
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
