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
    result->body.uint8s = malloc(sizeof(MVMCodepoint8) * bytes);
    for (i = 0; i < bytes; i++)
        if (ascii[i] <= 127)
            result->body.uint8s[i] = ascii[i];
        else
            MVM_exception_throw_adhoc(tc,
                "Will not decode invalid ASCII (code point > 127 found)");
    result->body.flags = MVM_STRING_TYPE_UINT8;

    return result;
}

/* Decodes a NULL-terminated ASCII string into an NFG string, creating
 * a result of the specified type. The type must have the MVMString REPR. */
MVMString * MVM_string_ascii_decode_nt(MVMThreadContext *tc, MVMObject *result_type, char *ascii) {
    return MVM_string_ascii_decode(tc, result_type, ascii, strlen(ascii));
}

/* Encodes the specified substring to ASCII. Anything outside of ASCII range
 * will become a ?. The result string is NULL terminated, but the specified
 * size is the non-null part. */
MVMuint8 * MVM_string_ascii_encode_substr(MVMThreadContext *tc, MVMString *str, MVMuint64 *output_size, MVMint64 start, MVMint64 length) {
    /* ASCII is a single byte encoding, so each grapheme will just become
     * a single byte. */
    MVMuint32 startu = (MVMuint32)start;
    MVMStringIndex strgraphs = NUM_GRAPHS(str);
    MVMuint32 lengthu = (MVMuint32)(length == -1 ? strgraphs - startu : length);
    MVMuint8 *result;
    size_t i;

    /* must check start first since it's used in the length check */
    if (start < 0 || start > strgraphs)
        MVM_exception_throw_adhoc(tc, "start out of range");
    if (length < -1 || start + lengthu > strgraphs)
        MVM_exception_throw_adhoc(tc, "length out of range");

    result = malloc(lengthu + 1);
    for (i = 0; i < lengthu; i++) {
        MVMCodepoint32 ord = MVM_string_get_codepoint_at_nocheck(tc, str, start + i);
        if (ord >= 0 && ord <= 127)
            result[i] = (MVMuint8)ord;
        else
            result[i] = '?';
    }
    result[i] = 0;
    if (output_size)
        *output_size = lengthu;
    return result;
}

/* Encodes the specified string to ASCII.  */
MVMuint8 * MVM_string_ascii_encode(MVMThreadContext *tc, MVMString *str, MVMuint64 *output_size) {
    return MVM_string_ascii_encode_substr(tc, str, output_size, 0, NUM_GRAPHS(str));
}
