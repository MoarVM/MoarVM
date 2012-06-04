#include "moarvm.h"

/* Compares two strings for equality. */
MVMint64 MVM_string_equal(MVMThreadContext *tc, MVMString *a, MVMString *b) {
    if (a->body.graphs != b->body.graphs)
        return 0;
    return (MVMint64)(memcmp(a->body.data, b->body.data, a->body.graphs * sizeof(MVMint32))?0:1);
}

/* Returns the location of one string in another or -1  */
MVMint64 MVM_string_index(MVMThreadContext *tc, MVMString *haystack, MVMString *needle) {
    MVMint64 result = -1;
    size_t index    = 0;
    if (needle->body.graphs > haystack->body.graphs || needle->body.graphs < 1)
        return -1;
    /* brute force for now. */
    while (index <= haystack->body.graphs - needle->body.graphs) {
        if (0 == memcmp(needle->body.data,
                haystack->body.data + index,
                needle->body.graphs * sizeof(MVMint32))) {
            result = (MVMint64)index;
            break;
        }
        index++;
    }
    return result;
}