#include "moar.h"

const static MVMuint8 BOM_UTF16LE[2] = { 0xFF, 0xFE };
const static MVMuint8 BOM_UTF16BE[2] = { 0xFE, 0xFF };
#define UTF16_DECODE_BIG_ENDIAN 1
#define UTF16_DECODE_LITTLE_ENDIAN 2
#define UTF16_DECODE_AUTO_ENDIAN 4
MVM_STATIC_INLINE int has_little_endian_bom (MVMuint8 *buf8) {
    return buf8[0] == BOM_UTF16LE[0] && buf8[1] == BOM_UTF16LE[1];
}
MVM_STATIC_INLINE int has_big_endian_bom (MVMuint8 *buf8) {
    return buf8[0] == BOM_UTF16BE[0] && buf8[1] == BOM_UTF16BE[1];
}
MVM_STATIC_INLINE void init_utf16_decoder_state(MVMDecodeStream *ds, int setting) {
    if (!ds->decoder_state) {
        ds->decoder_state = MVM_malloc(sizeof(MVMint32));
    }
    *((MVMint32*)ds->decoder_state) = setting;
}
#define utf16_decoder_state(ds) (*((MVMint32*)(ds)->decoder_state))
MVMuint32 MVM_string_utf16_decodestream_main(MVMThreadContext *tc, MVMDecodeStream *ds,
                                    const MVMint32 *stopper_chars,
                                    MVMDecodeStreamSeparators *seps, int endianess);
MVMuint32 MVM_string_utf16_decodestream(MVMThreadContext *tc, MVMDecodeStream *ds,
                                    const MVMint32 *stopper_chars,
                                    MVMDecodeStreamSeparators *seps) {
    if (!ds->decoder_state) {
#       ifdef MVM_BIGENDIAN
            init_utf16_decoder_state(ds, UTF16_DECODE_BIG_ENDIAN);
#       else
            init_utf16_decoder_state(ds, UTF16_DECODE_LITTLE_ENDIAN);
#       endif
    }
    return MVM_string_utf16_decodestream_main(tc, ds, stopper_chars, seps, UTF16_DECODE_AUTO_ENDIAN);
}
MVMuint32 MVM_string_utf16le_decodestream(MVMThreadContext *tc, MVMDecodeStream *ds,
                                    const MVMint32 *stopper_chars,
                                    MVMDecodeStreamSeparators *seps) {
    init_utf16_decoder_state(ds, UTF16_DECODE_LITTLE_ENDIAN);
    return MVM_string_utf16_decodestream_main(tc, ds, stopper_chars, seps, UTF16_DECODE_LITTLE_ENDIAN);
}
MVMuint32 MVM_string_utf16be_decodestream(MVMThreadContext *tc, MVMDecodeStream *ds,
                                    const MVMint32 *stopper_chars,
                                    MVMDecodeStreamSeparators *seps) {
    init_utf16_decoder_state(ds, UTF16_DECODE_BIG_ENDIAN);
    return MVM_string_utf16_decodestream_main(tc, ds, stopper_chars, seps, UTF16_DECODE_BIG_ENDIAN);
}
/* mostly from YAML-LibYAML */
/* Decodes using a decodestream. Decodes as far as it can with the input
 * buffers, or until a stopper is reached. */
MVMuint32 MVM_string_utf16_decodestream_main(MVMThreadContext *tc, MVMDecodeStream *ds,
                                    const MVMint32 *stopper_chars,
                                    MVMDecodeStreamSeparators *seps, int endianess) {
    MVMint32 count = 0, total = 0;
    MVMint32 bufsize;
    MVMGrapheme32 *buffer;
    MVMDecodeStreamBytes *cur_bytes;
    MVMDecodeStreamBytes *last_accept_bytes = ds->bytes_head;
    MVMint32 last_accept_pos, last_was_cr;
    MVMuint32 reached_stopper;
    int low, high;
    MVMint32  pos = cur_bytes == ds->bytes_head ? ds->bytes_head_pos : 0;
    MVMuint8 *bytes = (unsigned char *)cur_bytes->bytes;
    /* Set to 1 to remove the BOM even when big endian or little endian are
     * explicitly specified. */
    int remove_bom = 0;

    /* If there's no buffers, we're done. */
    if (!ds->bytes_head)
        return 0;
    last_accept_pos = ds->bytes_head_pos;

    /* If we're asked for zero chars, also done. */
    if (stopper_chars && *stopper_chars == 0)
        return 1;

    bufsize = ds->result_size_guess;
    buffer = MVM_malloc(bufsize * sizeof(MVMGrapheme32));

    /* Decode each of the buffers. */
    cur_bytes = ds->bytes_head;
    last_was_cr = 0;
    reached_stopper = 0;
    if (utf16_decoder_state(ds) == UTF16_DECODE_LITTLE_ENDIAN) {
        low = 0;
        high = 1;
    }
    else if (utf16_decoder_state(ds) == UTF16_DECODE_BIG_ENDIAN) {
        low = 1;
        high = 0;
    }
    else {
        MVM_free(buffer);
        MVM_exception_throw_adhoc(tc, "Unknown config setting in utf16 decodestream. This should never happen.");
    }
    while (cur_bytes) {
        /* Process this buffer. */
        MVMint32  pos = cur_bytes == ds->bytes_head ? ds->bytes_head_pos : 0;
        MVMuint8 *bytes = (unsigned char *)cur_bytes->bytes;
        if (ds->abs_byte_pos == 0 && pos + 1 < cur_bytes->length) {
            if (has_little_endian_bom(bytes + pos)) {
                /* Only change the setting if we are using standard 'utf16' decode
                 * which is meant to detect the encoding. */
                if (endianess == UTF16_DECODE_AUTO_ENDIAN) {
                    low = 0;
                    high = 1;
                    last_accept_pos = pos += 2;
                    utf16_decoder_state(ds) = UTF16_DECODE_LITTLE_ENDIAN;
                }
                /* If we see little endian BOM and we're set to utf16le, skip
                 * the BOM. */
                else if (endianess == UTF16_DECODE_LITTLE_ENDIAN && remove_bom) {
                    last_accept_pos = pos += 2;
                }
            }
            else if (has_big_endian_bom(bytes + pos)) {
                if (endianess == UTF16_DECODE_AUTO_ENDIAN) {
                    low = 1;
                    high = 0;
                    last_accept_pos = pos += 2;
                    utf16_decoder_state(ds) = UTF16_DECODE_BIG_ENDIAN;
                }
                /* If we see a big endian BOM and we're set to utf16be, skip
                 * the BOM. */
                else if (endianess == UTF16_DECODE_BIG_ENDIAN && remove_bom) {
                    last_accept_pos = pos += 2;
                }

            }
        }
        while (pos + 1 < cur_bytes->length) {
            MVMuint32 value = (bytes[pos+high] << 8) + bytes[pos+low];
            MVMuint32 value2;
            MVMGrapheme32 g;

            if ((value & 0xFC00) == 0xDC00) {
                MVM_free(buffer);
                MVM_exception_throw_adhoc(tc, "Malformed UTF-16; unexpected low surrogate");
            }

            if ((value & 0xFC00) == 0xD800) { /* high surrogate */
                pos += 2;
                if (pos + 1 >= cur_bytes->length) {
                    MVM_free(buffer);
                    MVM_exception_throw_adhoc(tc, "Malformed UTF-16; incomplete surrogate pair");
                }
                value2 = (bytes[pos+high] << 8) + bytes[pos+low];
                if ((value2 & 0xFC00) != 0xDC00) {
                    MVM_free(buffer);
                    MVM_exception_throw_adhoc(tc, "Malformed UTF-16; incomplete surrogate pair");
                }
                value = 0x10000 + ((value & 0x3FF) << 10) + (value2 & 0x3FF);
            }
            if (count == bufsize) {
                /* We filled the buffer. Attach this one to the buffers
                 * linked list, and continue with a new one. */
                MVM_string_decodestream_add_chars(tc, ds, buffer, bufsize);
                buffer = MVM_malloc(bufsize * sizeof(MVMGrapheme32));
                count = 0;
            }
            buffer[count++] = value;
            last_accept_bytes = cur_bytes;
            last_accept_pos = pos += 2;
            total++;
            if (MVM_string_decode_stream_maybe_sep(tc, seps, value) ||
                    stopper_chars && *stopper_chars == total) {
                reached_stopper = 1;
                goto done;
            }
        }
        cur_bytes = cur_bytes->next;
    }
  done:

    /* Attach what we successfully parsed as a result buffer, and trim away
     * what we chewed through. */
    if (count) {
        MVM_string_decodestream_add_chars(tc, ds, buffer, count);
    }
    else {
        MVM_free(buffer);
    }
    MVM_string_decodestream_discard_to(tc, ds, last_accept_bytes, last_accept_pos);

    return reached_stopper;
}
static MVMString * MVM_string_utf16_decode_main(MVMThreadContext *tc,
        const MVMObject *result_type, MVMuint8 *utf16_chars, size_t bytes, int endianess);
MVMString * MVM_string_utf16be_decode(MVMThreadContext *tc,
        const MVMObject *result_type, char *utf16_chars, size_t bytes) {
    return MVM_string_utf16_decode_main(tc, result_type, (MVMuint8*)utf16_chars, bytes, UTF16_DECODE_BIG_ENDIAN);
}
MVMString * MVM_string_utf16le_decode(MVMThreadContext *tc,
            const MVMObject *result_type, char *utf16_chars, size_t bytes) {
    return MVM_string_utf16_decode_main(tc, result_type, (MVMuint8*)utf16_chars, bytes, UTF16_DECODE_LITTLE_ENDIAN);
}
MVMString * MVM_string_utf16_decode(MVMThreadContext *tc,
            const MVMObject *result_type, char *_utf16_chars, size_t bytes) {
    MVMuint8 *utf16_chars = (MVMuint8*)utf16_chars;
#ifdef MVM_BIGENDIAN
    int mode = UTF16_DECODE_BIG_ENDIAN;
#else
    int mode = UTF16_DECODE_LITTLE_ENDIAN;
#endif
    /* set the byte order if there's a BOM */
    if (2 <= bytes) {
        if (has_little_endian_bom(utf16_chars)) {
            mode = UTF16_DECODE_LITTLE_ENDIAN;
            utf16_chars += 2;
            bytes -= 2;
        }
        else if (has_big_endian_bom(utf16_chars)) {
            mode = UTF16_DECODE_BIG_ENDIAN;
            utf16_chars += 2;
            bytes -= 2;
        }
    }
    return MVM_string_utf16_decode_main(tc, result_type, (MVMuint8*)utf16_chars, bytes, mode);
}
/* Decodes the specified number of bytes of utf16 into an NFG string, creating
 * a result of the specified type. The type must have the MVMString REPR. */
static MVMString * MVM_string_utf16_decode_main(MVMThreadContext *tc,
        const MVMObject *result_type, MVMuint8 *utf16_chars, size_t bytes, int endianess) {
    MVMString *result = (MVMString *)REPR(result_type)->allocate(tc, STABLE(result_type));
    size_t str_pos = 0;
    MVMuint8 *utf16 = (MVMuint8 *)utf16_chars;
    MVMuint8 *utf16_end = NULL;
    int low, high;
    MVMNormalizer norm;
    MVMint32 ready;
    switch (endianess) {
        case UTF16_DECODE_BIG_ENDIAN:
            low  = 1;
            high = 0;
            break;
        case UTF16_DECODE_LITTLE_ENDIAN:
            low  = 0;
            high = 1;
            break;
        default:
            MVM_exception_throw_adhoc(tc, "Unknown mode set in utf16 decode. This should never happen.");
    }

    if (bytes % 2) {
        MVM_exception_throw_adhoc(tc, "Malformed UTF-16; odd number of bytes");
    }

    utf16_end = utf16 + bytes;

    /* possibly allocating extra space; oh well */
    result->body.storage.blob_32 = MVM_malloc(sizeof(MVMGrapheme32) * bytes / 2);

    /* Need to normalize to NFG as we decode. */
    MVM_unicode_normalizer_init(tc, &norm, MVM_NORMALIZE_NFG);

    for (; utf16 < utf16_end; utf16 += 2) {
        MVMuint32 value = (utf16[high] << 8) + utf16[low];
        MVMuint32 value2;
        MVMGrapheme32 g;

        if ((value & 0xFC00) == 0xDC00) {
            MVM_unicode_normalizer_cleanup(tc, &norm);
            MVM_exception_throw_adhoc(tc, "Malformed UTF-16; unexpected low surrogate");
        }

        if ((value & 0xFC00) == 0xD800) { /* high surrogate */
            utf16 += 2;
            if (utf16 == utf16_end) {
                MVM_unicode_normalizer_cleanup(tc, &norm);
                MVM_exception_throw_adhoc(tc, "Malformed UTF-16; incomplete surrogate pair");
            }
            value2 = (utf16[high] << 8) + utf16[low];
            if ((value2 & 0xFC00) != 0xDC00) {
                MVM_unicode_normalizer_cleanup(tc, &norm);
                MVM_exception_throw_adhoc(tc, "Malformed UTF-16; incomplete surrogate pair");
            }
            value = 0x10000 + ((value & 0x3FF) << 10) + (value2 & 0x3FF);
        }

        /* TODO: check for invalid values */
        ready = MVM_unicode_normalizer_process_codepoint_to_grapheme(tc, &norm, value, &g);
        if (ready) {
            result->body.storage.blob_32[str_pos++] = g;
            while (--ready > 0)
                result->body.storage.blob_32[str_pos++] = MVM_unicode_normalizer_get_grapheme(tc, &norm);
        }
    }

    /* Get any final graphemes from the normalizer, and clean it up. */
    MVM_unicode_normalizer_eof(tc, &norm);
    ready = MVM_unicode_normalizer_available(tc, &norm);
    while (ready--)
        result->body.storage.blob_32[str_pos++] = MVM_unicode_normalizer_get_grapheme(tc, &norm);
    MVM_unicode_normalizer_cleanup(tc, &norm);

    result->body.storage_type = MVM_STRING_GRAPHEME_32;
    result->body.num_graphs   = str_pos;

    return result;
}
MVM_STATIC_INLINE MVMuint16 swap_bytes(MVMuint16 uint16, int enable_byte_swap) {
    return enable_byte_swap
        ? (((MVMuint8*) &uint16)[0] << 8)
            + ((MVMuint8*) &uint16)[1]
        : uint16;
}
char * MVM_string_utf16_encode_substr_main(MVMThreadContext *tc, MVMString *str, MVMuint64 *output_size, MVMint64 start, MVMint64 length, MVMString *replacement, MVMint32 translate_newlines, int endianess);
/* Encodes the specified substring to utf16. The result string is NULL terminated, but
 * the specified size is the non-null part. (This being UTF-16, there are 2 null bytes
 * on the end.) */
char * MVM_string_utf16be_encode_substr(MVMThreadContext *tc, MVMString *str, MVMuint64 *output_size, MVMint64 start, MVMint64 length, MVMString *replacement, MVMint32 translate_newlines) {
    return MVM_string_utf16_encode_substr_main(tc, str, output_size, start, length, replacement, translate_newlines, UTF16_DECODE_BIG_ENDIAN);
}
char * MVM_string_utf16le_encode_substr(MVMThreadContext *tc, MVMString *str, MVMuint64 *output_size, MVMint64 start, MVMint64 length, MVMString *replacement, MVMint32 translate_newlines) {
    return MVM_string_utf16_encode_substr_main(tc, str, output_size, start, length, replacement, translate_newlines, UTF16_DECODE_LITTLE_ENDIAN);
}
char * MVM_string_utf16_encode_substr(MVMThreadContext *tc, MVMString *str, MVMuint64 *output_size, MVMint64 start, MVMint64 length, MVMString *replacement, MVMint32 translate_newlines) {
    return MVM_string_utf16_encode_substr_main(tc, str, output_size, start, length, replacement, translate_newlines, UTF16_DECODE_AUTO_ENDIAN);
}

char * MVM_string_utf16_encode_substr_main(MVMThreadContext *tc, MVMString *str, MVMuint64 *output_size, MVMint64 start, MVMint64 length, MVMString *replacement, MVMint32 translate_newlines, int endianess) {
    MVMStringIndex strgraphs = MVM_string_graphs(tc, str);
    MVMuint32 lengthu = (MVMuint32)(length == -1 ? strgraphs - start : length);
    MVMuint16 *result;
    MVMuint16 *result_pos;
    MVMCodepointIter ci;
    MVMuint8 *repl_bytes = NULL;
    MVMuint64 repl_length = 0;
    MVMint32 alloc_size;
    MVMuint64 scratch_space = 0;
    int enable_byte_swap = 0;
#ifdef MVM_BIGENDIAN
    if (endianess == UTF16_DECODE_LITTLE_ENDIAN)
        enable_byte_swap = 1;
#else
    if (endianess == UTF16_DECODE_BIG_ENDIAN)
        enable_byte_swap = 1;
#endif
    /* must check start first since it's used in the length check */
    if (start < 0 || start > strgraphs)
        MVM_exception_throw_adhoc(tc, "start out of range");
    if (start + lengthu > strgraphs)
        MVM_exception_throw_adhoc(tc, "length out of range");

    if (replacement)
        repl_bytes = (MVMuint8 *) MVM_string_utf16_encode_substr(tc,
            replacement, &repl_length, 0, -1, NULL, translate_newlines);

    alloc_size = lengthu * 2;
    result = MVM_malloc(alloc_size + 2);
    result_pos = result;
    MVM_string_ci_init(tc, &ci, str, translate_newlines, 0);
    while (MVM_string_ci_has_more(tc, &ci)) {
        int bytes_needed;
        MVMCodepoint value = MVM_string_ci_get_codepoint(tc, &ci);

        if (value < 0x10000) {
            bytes_needed = 2;
        }
        else if (value <= 0x1FFFFF) {
            bytes_needed = 4;
        }
        else {
            bytes_needed = repl_length;
        }

        while ((alloc_size - 2 * (result_pos - result)) < bytes_needed) {
            MVMuint16 *new_result;

            alloc_size *= 2;
            new_result  = MVM_realloc(result, alloc_size + 2);

            result_pos = new_result + (result_pos - result);
            result     = new_result;
        }

        if (value < 0x10000) {
            result_pos[0] = swap_bytes(value, enable_byte_swap);
            result_pos++;
        }
        else if (value <= 0x1FFFFF) {
            value -= 0x10000;
            result_pos[0] = swap_bytes(0xD800 + (value >> 10), enable_byte_swap);
            result_pos[1] = swap_bytes(0xDC00 + (value & 0x3FF), enable_byte_swap);
            result_pos += 2;
        }
        else if (replacement) {
            memcpy(result_pos, repl_bytes, repl_length);
            result_pos += repl_length/2;
        }
        else {
            MVM_free(result);
            MVM_free(repl_bytes);
            MVM_exception_throw_adhoc(tc,
                "Error encoding UTF-16 string: could not encode codepoint %d",
                value);
        }
    }
    result_pos[0] = 0;
    if (!output_size)
        output_size = &scratch_space;
    *output_size = (char *)result_pos - (char *)result;
    result = MVM_realloc(result, *output_size);
    MVM_free(repl_bytes);
    return (char *)result;
}

/* Encodes the whole string, double-NULL terminated. */
char * MVM_string_utf16_encode(MVMThreadContext *tc, MVMString *str, MVMint32 translate_newlines) {
    return MVM_string_utf16_encode_substr(tc, str, NULL, 0, -1, NULL, translate_newlines);
}
