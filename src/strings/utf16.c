#include "moarvm.h"

#define BOM_UTF16LE "\xff\xfe"
#define BOM_UTF16BE "\xfe\xff"

/* mostly from YAML-LibYAML */

/* Decodes the specified number of bytes of utf16
 into an NFG string, creating
 * a result of the specified type. The type must have the MVMString REPR. */
MVMString * MVM_string_utf16_decode(MVMThreadContext *tc,
        MVMObject *result_type, MVMuint8 *utf16, size_t bytes) {
    MVMString *result = (MVMString *)REPR(result_type)->allocate(tc, STABLE(result_type));
    size_t byte_pos = 0;
    size_t str_pos = 0;
    MVMuint8 *utf16_end;
    /* set the default byte order */
#ifdef MVM_BIGENDIAN
    int low = 1;
    int high = 0;
#else
    int low = 0;
    int high = 1;
#endif

    if (bytes % 2) {
        MVM_exception_throw_adhoc(tc, "Malformed UTF-16; odd number of bytes");
    }

    /* set the byte order if there's a BOM */
    if (bytes >= 2) {
        if (!memcmp(utf16, BOM_UTF16LE, 2)) {
            low = 0;
            high = 1;
            utf16 += 2;
        }
        else if (!memcmp(utf16, BOM_UTF16BE, 2)) {
            low = 1;
            high = 0;
            utf16 += 2;
        }
    }

    utf16_end = utf16 + bytes;

    /* possibly allocating extra space; oh well */
    result->body.int32s = malloc(sizeof(MVMint32) * bytes / 2);

    for (; utf16 < utf16_end; utf16 += 2) {

        MVMuint32 value = (utf16[high] << 8) + utf16[low];
        MVMuint32 value2;

        if ((value & 0xFC00) == 0xDC00) {
            MVM_exception_throw_adhoc(tc, "Malformed UTF-16; unexpected low surrogate");
        }

        if ((value & 0xFC00) == 0xD800) { /* high surrogate */

            utf16 += 2;

            if (utf16 == utf16_end) {
                MVM_exception_throw_adhoc(tc, "Malformed UTF-16; incomplete surrogate pair");
            }

            value2 = (utf16[high] << 8) + utf16[low];

            if ((value2 & 0xFC00) != 0xDC00) {
                MVM_exception_throw_adhoc(tc, "Malformed UTF-16; incomplete surrogate pair");
            }

            value = 0x10000 + ((value & 0x3FF) << 10) + (value2 & 0x3FF);
        }
        /* TODO: check for invalid values */
        result->body.int32s[str_pos++] = (MVMint32)value;
    }

    /* result->body.codes  = str_pos; */
    result->body.flags = MVM_STRING_TYPE_INT32;
    result->body.graphs = str_pos;

    return result;
}

/* Encodes the specified substring to utf16. Anything outside of utf16 range
 * will become a ?. The result string is NULL terminated, but the specified
 * size is the non-null part. */
MVMuint8 * MVM_string_utf16_encode_substr(MVMThreadContext *tc, MVMString *str, MVMuint64 *output_size, MVMint64 start, MVMint64 length) {
    /* latin-1 is a single byte encoding, so each grapheme will just become
     * a single byte. */
    MVMuint32 startu = (MVMuint32)start;
    MVMStringIndex strgraphs = NUM_GRAPHS(str);
    MVMuint32 lengthu = (MVMuint32)(length == -1 ? strgraphs - start : length);
    MVMuint8 *result;
    size_t str_pos;
    MVMuint8 * result_pos;
#ifdef MVM_BIGENDIAN
    int low = 1;
    int high = 0;
#else
    int low = 0;
    int high = 1;
#endif

    /* must check start first since it's used in the length check */
    if (start < 0 || start > strgraphs)
        MVM_exception_throw_adhoc(tc, "start out of range");
    if (length < 0 || start + length > strgraphs)
        MVM_exception_throw_adhoc(tc, "length out of range");

    /* make the result grow as needed instead of allocating so much to start? */
    result = malloc(length * 4 + 1);
    for (str_pos = 0; str_pos < length; str_pos++) {
        MVMCodepoint32 value = MVM_string_get_codepoint_at_nocheck(tc, str, start + str_pos);

        if (value < 0x10000) {
            result_pos[high] = value >> 8;
            result_pos[low] = value & 0xFF;
            result_pos += 2;
        }
        else {
            value -= 0x10000;
            result_pos[high] = 0xD8 + (value >> 18);
            result_pos[low] = (value >> 10) & 0xFF;
            result_pos[high+2] = 0xDC + ((value >> 8) & 0xFF);
            result_pos[low+2] = value & 0xFF;
            result_pos += 4;
        }
    }
    result[str_pos] = 0;
    if (output_size)
        *output_size = result_pos - result;
    return result;
}
