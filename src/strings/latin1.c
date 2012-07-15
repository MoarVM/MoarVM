#include "moarvm.h"

/* Decodes the specified number of bytes of latin1 (well, really Windows 1252)
 into an NFG string, creating
 * a result of the specified type. The type must have the MVMString REPR. */
MVMString * MVM_string_latin1_decode(MVMThreadContext *tc,
        MVMObject *result_type, MVMuint8 *latin1, size_t bytes) {
    MVMString *result = (MVMString *)REPR(result_type)->allocate(tc, STABLE(result_type));
    size_t i;
    
    result->body.codes  = bytes;
    result->body.graphs = bytes;
    
    result->body.data = malloc(sizeof(MVMint32) * bytes);
    for (i = 0; i < bytes; i++)
        /* actually decode like Windows-1252, since that is mostly a superset,
           and is recommended by the HTML5 standard when latin1 is claimed */
        result->body.data[i] = latin1_char_to_cp(latin1[i]);
    
    return result;
}

/* Encodes the specified substring to latin-1. Anything outside of latin-1 range
 * will become a ?. The result string is NULL terminated, but the specified
 * size is the non-null part. */
MVMuint8 * MVM_string_latin1_encode_substr(MVMThreadContext *tc, MVMString *str, MVMuint64 *output_size, MVMint64 start, MVMint64 length) {
    /* latin-1 is a single byte encoding, so each grapheme will just become
     * a single byte. */
    MVMuint32 startu = (MVMuint32)start;
    MVMuint32 lengthu = (MVMuint32)(length == -1 ? str->body.graphs : length);
    MVMuint8 *result;
    size_t i;
    
    /* must check start first since it's used in the length check */
    if (start < 0 || start > str->body.graphs)
        MVM_exception_throw_adhoc(tc, "start out of range");
    if (length < 0 || start + length > str->body.graphs)
        MVM_exception_throw_adhoc(tc, "length out of range");
    
    result = malloc(length + 1);
    for (i = 0; i < length; i++) {
        MVMint32 ord = latin1_cp_to_char(str->body.data[start + i]);
        if (ord != 256)
            result[i] = (MVMuint8)ord;
        else
            result[i] = '?';
    }
    result[i] = 0;
    if (output_size)
        *output_size = length;
    return result;
}
