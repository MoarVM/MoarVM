#include "moarvm.h"

/* Compares two strings for equality. */
MVMuint32 MVM_string_equal(MVMThreadContext *tc, MVMString *a, MVMString *b) {
    if (a->body.graphs != b->body.graphs)
        return 0;
    return (MVMuint32)memcmp(a->body.data, b->body.data, a->body.graphs * sizeof(MVMint32));
}
