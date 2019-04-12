#include "tommath.h"

#define MVM_BIGINT_32_FLAG      0xFFFFFFFF
#define MVM_BIGINT_IS_BIG(body) ((body)->u.smallint.flag != 0xFFFFFFFF)
#define MVM_IS_32BIT_INT(i)     ((long long)(i) >= -2147483648LL && (long long)(i) <= 2147483647LL)

/* Body is defined elsewhere, to be available early enough to put into the
 * interp register union, for the case where we optimize out the enclosing
 * object of a big integer using EA. */
struct MVMP6bigint {
    MVMObject common;
    MVMP6bigintBody body;
};

/* Function for REPR setup. */
const MVMREPROps * MVMP6bigint_initialize(MVMThreadContext *tc);

MVMint64 MVM_p6bigint_get_int64(MVMThreadContext *tc, MVMP6bigintBody *body);
void MVM_p6bigint_store_as_mp_int(MVMP6bigintBody *body, MVMint64 value);
