#include "tommath.h"

#define MVM_BIGINT_32_FLAG      0xFFFFFFFF
#define MVM_BIGINT_IS_BIG(body) ((body)->u.smallint.flag != 0xFFFFFFFF)
#define MVM_IS_32BIT_INT(i)     ((long long)(i) >= -2147483648LL && (long long)(i) <= 2147483647LL)

/* Representation used by big integers; inlined into P6bigint. We store any
 * values in 32-bit signed range without using the big integer library. */
struct MVMP6bigintBody {
    union {
        /* A 32-bit integer and a flag indicating this is not a pointer to a
         * big integer, but instead the 32-bit value should be read. Stored
         * so that the flag sets the lower bits of any 64-bit pointer, which
         * should never happen in a real pointer due to alignment. */
        struct {
#if defined(MVM_BIGENDIAN) && MVM_PTR_SIZE > 4
            MVMint32  value;
            MVMuint32 flag;
#else
            MVMuint32 flag;
            MVMint32  value;
#endif
        } smallint;

        /* Pointer to a libtommath big integer. */
        mp_int *bigint;
    } u;
};
struct MVMP6bigint {
    MVMObject common;
    MVMP6bigintBody body;
};

/* Function for REPR setup. */
const MVMREPROps * MVMP6bigint_initialize(MVMThreadContext *tc);

MVMint64 MVM_p6bigint_get_int64(MVMThreadContext *tc, MVMP6bigintBody *body);
void MVM_p6bigint_store_as_mp_int(MVMThreadContext *tc, MVMP6bigintBody *body, MVMint64 value);
