#include "moarvm.h"

/* Decodes the specified number of bytes of ASCII into an NFG string, creating
 * a result of the specified type. The type must have the MVMString REPR. */
MVMString * MVM_string_ascii_decode(MVMThreadContext *tc, MVMObject *result_type, char *ascii, size_t bytes) {
    MVMString *result = (MVMString *)REPR(result_type)->allocate(tc, STABLE(result_type));
    size_t i;
    
    /* There's no combining chars and such stuff in ASCII, so the codes
     * count and grapheme count are trivially the same as the buffer
     * length. */
    result->body.codes  = bytes;
    result->body.graphs = bytes;
    
    /* Allocate grapheme buffer and decode the ASCII string. */
    result->body.data = malloc(sizeof(MVMint32) * bytes);
    for (i = 0; i < bytes; i++)
        if (ascii[i] <= 127)
            result->body.data[i] = ascii[i];
        else
            MVM_exception_throw_adhoc(tc,
                "Will not decode invalid ASCII (code point > 127 found)");
    
    return result;
}

/* Decodes a NULL-terminated ASCII string into an NFG string, creating
 * a result of the specified type. The type must have the MVMString REPR. */
MVMString * MVM_string_ascii_decode_nt(MVMThreadContext *tc, MVMObject *result_type, char *ascii) {
    return MVM_string_ascii_decode(tc, result_type, ascii, strlen(ascii));
}
