#include "moar.h"
#include "shiftjis_codeindex.h"
/* Encodes the specified substring to ShiftJIS as specified here:
 * https://encoding.spec.whatwg.org/#shift_jis-decoder
 * The result string is NULL terminated, but the specified size is the non-null part. */
char * MVM_string_shiftjis_encode_substr(MVMThreadContext *tc, MVMString *str,
        MVMuint64 *output_size, MVMint64 start, MVMint64 length, MVMString *replacement,
        MVMint32 translate_newlines, MVMint64 config) {
    MVMuint32 startu = (MVMuint32)start;
    MVMStringIndex strgraphs = MVM_string_graphs(tc, str);
    MVMuint32 lengthu = (MVMuint32)(length == -1 ? strgraphs - startu : length);
    MVMuint8 *result;
    size_t result_alloc;
    MVMuint8 *repl_bytes = NULL;
    MVMuint64 repl_length;

    /* must check start first since it's used in the length check */
    if (start < 0 || start > strgraphs)
        MVM_exception_throw_adhoc(tc, "start out of range");
    if (length < -1 || start + lengthu > strgraphs)
        MVM_exception_throw_adhoc(tc, "length out of range");

    if (replacement)
        repl_bytes = (MVMuint8 *) MVM_string_shiftjis_encode_substr(tc,
            replacement, &repl_length, 0, -1, NULL, translate_newlines, config);

    result_alloc = lengthu;
    result = MVM_malloc(result_alloc + 1);
    if (str->body.storage_type == MVM_STRING_GRAPHEME_ASCII) {
        /* No encoding needed; directly copy. */
        memcpy(result, str->body.storage.blob_ascii, lengthu);
        result[lengthu] = 0;
        if (output_size)
            *output_size = lengthu;
    }
    else {
        MVMuint32 out_pos = 0;
        MVMCodepointIter ci;
        MVM_string_ci_init(tc, &ci, str, translate_newlines, 0);
        while (MVM_string_ci_has_more(tc, &ci)) {
            MVMCodepoint codepoint = MVM_string_ci_get_codepoint(tc, &ci);
            if (result_alloc <= out_pos + 1) {
                result_alloc += 8;
                result = MVM_realloc(result, result_alloc + 2);
            }
            /* If code point is an ASCII code point or U+0080, return a byte
             * whose value is code point. */
            if (codepoint <= 0x7F || codepoint == 0x80) {
                result[out_pos++] = codepoint;
            }
            /* If code point is U+00A5, return byte 0x5C. */
            else if (codepoint == 0xA5) {
                result[out_pos++] = 0x5C;
            }
            /* If code point is U+203E, return byte 0x7E. */
            else if (codepoint == 0x203E) {
                result[out_pos++] = 0x7E;
            }
            /* If code point is in the range U+FF61 to U+FF9F, inclusive, return
             * a byte whose value is code point − 0xFF61 + 0xA1. */
            else if (0xFF61 <= codepoint && codepoint <= 0xFF9F) {
                result[out_pos++] = codepoint - 0xFF61 + 0xA1;
            }
            else {
                unsigned int pointer;
                unsigned int lead, lead_offset, trail, offset;
                /* If code point is U+2212, set it to U+FF0D. */
                if (codepoint == 0x2212) {
                    codepoint = 0xFF0D;
                }
                /* Let pointer be the index Shift_JIS pointer for code point. */
                pointer = shift_jis_cp_to_index(codepoint);
                /* If pointer is null, return error with code point. */
                if (!pointer) {
                    MVM_free(result);
                    MVM_exception_throw_adhoc(tc,
                        "Error encoding shiftjis string: could not encode codepoint %d",
                         codepoint);
                }
                /* Let lead be pointer / 188. */
                lead = pointer/188;
                /* Let lead offset be 0x81, if lead is less than 0x1F, and 0xC1
                 * otherwise. */
                lead_offset = lead < 0x1F ? 0x81 : 0xC1;
                /* Let trail be pointer % 188 */
                trail = pointer % 188;
                /* Let offset be 0x40, if trail is less than 0x3F, and 0x41 otherwise. */
                offset = trail < 0x3F ? 0x40 : 0x41;
                /* Return two bytes whose values are lead + lead offset and
                 * trail + offset. */
                result[out_pos++] = lead + lead_offset;
                result[out_pos++] = trail + offset;
            }

        }
        result[out_pos] = 0;
        if (output_size)
            *output_size = out_pos;
    }

    MVM_free(repl_bytes);
    return (char *)result;
}
