#include "moar.h"

/* Decodes the specified number of bytes of latin1 into an NFG string,
 * creating a result of the specified type. The type must have the MVMString
 * REPR. */
MVMString * MVM_string_latin1_decode(MVMThreadContext *tc, MVMObject *result_type, 
                                     MVMuint8 *latin1, size_t bytes) {
    MVMString *result = (MVMString *)REPR(result_type)->allocate(tc, STABLE(result_type));
    size_t i;

    result->body.codes  = bytes;
    result->body.graphs = bytes;
    result->body.flags  = MVM_STRING_TYPE_INT32;
    result->body.int32s = malloc(sizeof(MVMint32) * bytes);

    for (i = 0; i < bytes; i++)
        result->body.int32s[i] = latin1[i];
    
    return result;
}

/* Encodes the specified substring to latin-1. Anything outside of latin-1 range
 * will become a ?. The result string is NULL terminated, but the specified
 * size is the non-null part. */
MVMuint8 * MVM_string_latin1_encode_substr(MVMThreadContext *tc, MVMString *str, MVMuint64 *output_size, MVMint64 start, MVMint64 length) {
    /* latin-1 is a single byte encoding, so each grapheme will just become
     * a single byte. */
    MVMuint32 startu = (MVMuint32)start;
    MVMStringIndex strgraphs = NUM_GRAPHS(str);
    MVMuint32 lengthu = (MVMuint32)(length == -1 ? strgraphs - startu : length);
    MVMuint8 *result;
    size_t i;

    /* must check start first since it's used in the length check */
    if (start < 0 || start > strgraphs)
        MVM_exception_throw_adhoc(tc, "start out of range");
    if (length < 0 || start + length > strgraphs)
        MVM_exception_throw_adhoc(tc, "length out of range");

    result = malloc(length + 1);
    for (i = 0; i < length; i++) {
        MVMint32 codepoint = MVM_string_get_codepoint_at_nocheck(tc, str, start + i);
        if (codepoint >= 0 && codepoint < 256)
            result[i] = (MVMuint8)codepoint;
        else
            result[i] = '?';
    }
    result[i] = 0;
    if (output_size)
        *output_size = length;
    return result;
}
