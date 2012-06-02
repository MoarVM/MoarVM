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

/* Encodes the specified string to ASCII. Anything outside of ASCII range
 * will become a ?. The result string is NULL terminated, but the specified
 * size is the non-null part. */
MVMuint8 * MVM_string_ascii_encode(MVMThreadContext *tc, MVMString *str, MVMuint64 *output_size) {
    /* ASCII is a single byte encoding, so each grapheme will just become
     * a single byte. */
    MVMuint8 *result = malloc(str->body.graphs + 1);
    size_t i;
    for (i = 0; i < str->body.graphs; i++) {
        MVMint32 ord = str->body.data[i];
        if (ord >= 0 && ord <= 127)
            result[i] = (MVMuint8)ord;
        else
            result[i] = '?';
    }
    *output_size = str->body.graphs;
    return result;
}
