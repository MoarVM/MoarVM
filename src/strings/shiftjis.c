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
    MVMuint8 *result = NULL;
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
                MVMint16 pointer;
                unsigned int lead, lead_offset, trail, offset;
                /* If code point is U+2212, set it to U+FF0D. */
                if (codepoint == 0x2212) {
                    codepoint = 0xFF0D;
                }
                /* Let pointer be the index Shift_JIS pointer for code point. */
                pointer = shift_jis_cp_to_index(tc, codepoint);
                /* If pointer is null, return error with code point. */
                if (pointer == SHIFTJIS_NULL) {
                    if (replacement) {
                        size_t i;
                        if (result_alloc <= out_pos + repl_length) {
                            result_alloc += repl_length;
                            result = MVM_realloc(result, result_alloc + 1);
                        }
                        for (i = 0; i < repl_length; i++) {
                            result[out_pos++] = repl_bytes[i];
                        }
                        continue;
                    }
                    else {
                        MVM_free(result);
                        MVM_exception_throw_adhoc(tc,
                            "Error encoding shiftjis string: could not encode codepoint %d",
                             codepoint);
                    }
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
#define DECODE_ERROR -1
#define DECODE_CONTINUE -2
#define DECODE_CODEPOINT -4
#define DECODE_PREPEND_TO_STREAM -5
static int decoder_handler (MVMThreadContext *tc, MVMuint8 *Shift_JIS_lead, MVMuint8 byte, MVMCodepoint *out) {
    /* If Shift_JIS lead is not 0x00 */
    if (*Shift_JIS_lead != 0x00) {
        /* let lead be Shift_JIS lead, */
        MVMuint8 lead = *Shift_JIS_lead;
        /* Let pointer be null */
        MVMint16 pointer = SHIFTJIS_NULL;
        /* Let offset be 0x40, if byte is less than 0x7F, and 0x41 otherwise. */
        MVMuint8 offset = byte < 0x7F ? 0x40 : 0x41;
        /* Let lead offset be 0x81, if lead is less than 0xA0, and 0xC1 otherwise. */
        MVMuint8 lead_offset = lead < 0xA0 ? 0x81 : 0xC1;
        MVMGrapheme32 codepoint;
        /* Set Shift_JIS lead to 0x00 */
        *Shift_JIS_lead = 0x00;
        /* 3. If byte is in the range 0x40 to 0x7E, inclusive, or 0x80 to 0xFC,
         * inclusive, set pointer to (lead − lead offset) × 188 + byte − offset. */
        if ((0x40 <= byte && byte <= 0x7E) || (0x80 <= byte && byte <= 0xFC)) {
            pointer = (lead - lead_offset) * 188 + byte - offset;
        }
        /* 4. If pointer is in the range 8836 to 10715, inclusive, return a code
         * point whose value is 0xE000 − 8836 + pointer. */
        if (8836 <= pointer && pointer <= 10715) {
            *out = 0xE000 - 8836 + pointer;
            return DECODE_CODEPOINT;
        }
        /* 5. Let code point be null, if pointer is null */
        if (pointer == SHIFTJIS_NULL) {
            codepoint = SHIFTJIS_NULL;
        }
        /*  And the index code point for pointer in index jis0208 otherwise. */
        else {
            codepoint = shift_jis_index_to_cp(tc, pointer);
        }
        /* 6. If code point is non-null, return a code point whose value is code point. */
        if (codepoint != SHIFTJIS_NULL) {
            *out = codepoint;
            return DECODE_CODEPOINT;
        }
        /* 7. If byte is an ASCII byte, prepend byte to stream. */
        if (byte <= 0x7F) {
            *out = byte;
            return DECODE_PREPEND_TO_STREAM;
        }
        /* 8. Return error. */
        return DECODE_ERROR;
    }
    /* 4. If byte is an ASCII byte or 0x80, return a code point whose value is byte. */
    if (byte <= 0x7F || byte == 0x80) {
        *out = byte;
        return DECODE_CODEPOINT;
    }
    /* 5. If byte is in the range 0xA1 to 0xDF, inclusive, return a code point
     * whose value is 0xFF61 − 0xA1 + byte. */
    if (0xA1 <= byte && byte <= 0xDF) {
        *out = 0xFF61 - 0xA1 + byte;
        return DECODE_CODEPOINT;
    }
    /* 6. If byte is in the range 0x81 to 0x9F, inclusive, or 0xE0 to 0xFC,
     * inclusive, set Shift_JIS lead to byte and return continue. */
    if ((0x81 <= byte && byte <= 0x9F) || (0xE0 <= byte && byte <= 0xFC)) {
        *Shift_JIS_lead = byte;
        return DECODE_CONTINUE;
    }
    return DECODE_ERROR;

}
MVMString * MVM_string_shiftjis_decode(MVMThreadContext *tc,
        const MVMObject *result_type, char *windows125X_c, size_t num_bytes,
        MVMString *replacement, MVMint64 config) {
    MVMuint8 *bytes = (MVMuint8 *)windows125X_c;
    MVMString *result = (MVMString *)REPR(result_type)->allocate(tc, STABLE(result_type));
    size_t pos = 0, result_graphs, additional_bytes = 0;
    MVMStringIndex repl_length = replacement ? MVM_string_graphs(tc, replacement) : 0;
    MVMuint8 Shift_JIS_lead = 0x00;
    /* Stores a byte that we must run through the decoder a second time. Instead
     * of prepending to the last position of the buffer we just store it so as not
     * to modify the buffer. */
    MVMuint8 prepended = 0;
    int is_prepended   = 0;
    MVMStringIndex repl_pos = 0;
    int last_was_cr         = 0;
    MVMStringIndex result_size = num_bytes;

    result->body.storage_type    = MVM_STRING_GRAPHEME_32;
    /* TODO allocate less? */
    result->body.storage.blob_32 = MVM_malloc(sizeof(MVMGrapheme32) * result_size);

    result_graphs = 0;
    while (pos < num_bytes || repl_pos) {
        MVMGrapheme32 graph     = SHIFTJIS_NULL;
        MVMGrapheme32 codepoint = SHIFTJIS_NULL;
        MVMuint8 byte;
        if (repl_pos) {
            graph = MVM_string_get_grapheme_at_nocheck(tc, replacement, repl_pos++);
            if (repl_length <= repl_pos) repl_pos = 0;
        }
        else if (is_prepended) {
            byte = prepended;
            is_prepended = 0;
        }
        else {
            byte = bytes[pos++];
        }
        /* graph will be SHIFTJIS_NULL unless we just grabbed a replacement grapheme */
        if (graph == SHIFTJIS_NULL) {
            int handler_rtrn = decoder_handler(tc, &Shift_JIS_lead, byte, &codepoint);
            if (handler_rtrn == DECODE_CODEPOINT) {
                graph = codepoint;
            }
            else if (handler_rtrn == DECODE_CONTINUE) {
                continue;
            }
            else if (handler_rtrn == DECODE_ERROR) {
                /* Clearing this seems like the right thing to do, in case
                 * a replacement is used. */
                Shift_JIS_lead = 0x00;
                if (replacement) {
                    graph = MVM_string_get_grapheme_at_nocheck(tc, replacement, repl_pos);
                    /* If the replacement is more than one grapheme we need
                     * to set repl_pos++ so we will grab the next grapheme on
                     * the next loop */
                    if (1 < repl_length) repl_pos++;
                }
                else {
                    /* Throw if it's unmapped */
                    MVM_exception_throw_adhoc(tc,
                        "Error decoding shiftjis string: could not decode byte 0x%hhX",
                         byte);
                }
            }
            else if (handler_rtrn == DECODE_PREPEND_TO_STREAM) {
                is_prepended = 1;
                prepended    = codepoint;
                continue;
            }
            else {
                MVM_exception_throw_adhoc(tc, "shiftjis decoder encountered an internal error.\n");
            }
        }
        if (last_was_cr) {
            if (graph == '\n') {
                graph = MVM_nfg_crlf_grapheme(tc);
            }
            else {
                graph = '\r';
                pos--;
            }
            last_was_cr = 0;
        }
        else if (graph == '\r') {
            last_was_cr = 1;
            continue;
        }
        if (result_graphs == result_size) {
            result_size += repl_length;
            result->body.storage.blob_32 = MVM_realloc(result->body.storage.blob_32,
                result_size * sizeof(MVMGrapheme32));
        }
        result->body.storage.blob_32[result_graphs++] = graph;
    }
    /* If we end up with Shift_JIS_lead still set, that means we're missing a byte
     * that should have followed it. */
    if (Shift_JIS_lead != 0x00) {
        MVM_exception_throw_adhoc(tc, "Error, ended decode of shiftjis expecting another byte. "
            "Last byte seen was 0x%hhX\n", Shift_JIS_lead);
    }
    result->body.storage.blob_32 = MVM_realloc(result->body.storage.blob_32,
        result_graphs * sizeof(MVMGrapheme32));
    result->body.num_graphs = result_graphs;

    return result;
}
/* Decodes using a decodestream. Decodes as far as it can with the input
 * buffers, or until a stopper is reached. */
MVMuint32 MVM_string_shiftjis_decodestream(MVMThreadContext *tc, MVMDecodeStream *ds,
                                         const MVMint32 *stopper_chars,
                                         MVMDecodeStreamSeparators *seps) {
    MVMint32 count = 0, total = 0;
    MVMint32 bufsize;
    MVMGrapheme32 *buffer = NULL;
    MVMDecodeStreamBytes *cur_bytes = NULL;
    MVMDecodeStreamBytes *last_accept_bytes = ds->bytes_head;
    MVMint32 last_accept_pos, last_was_cr;
    MVMuint32 reached_stopper;
    MVMStringIndex repl_length = ds->replacement ? MVM_string_graphs(tc, ds->replacement) : 0;
    MVMStringIndex repl_pos = 0;
    MVMuint8 Shift_JIS_lead = 0x00;
    MVMuint8 prepended = 0;
    int is_prepended = 0;
    /* If there's no buffers, we're done. */
    if (!ds->bytes_head)
        return 0;
    last_accept_pos = ds->bytes_head_pos;

    /* If we're asked for zero chars, also done. */
    if (stopper_chars && *stopper_chars == 0)
        return 1;

    bufsize = ds->result_size_guess;
    buffer  = MVM_malloc(bufsize * sizeof(MVMGrapheme32));

    /* Decode each of the buffers. */
    cur_bytes = ds->bytes_head;
    last_was_cr = 0;
    reached_stopper = 0;
    while (cur_bytes) {
        /* Process this buffer. */
        MVMint32  pos = cur_bytes == ds->bytes_head ? ds->bytes_head_pos : 0;
        MVMuint8 *bytes = (MVMuint8 *)cur_bytes->bytes;
        while (pos < cur_bytes->length || repl_pos) {
            MVMGrapheme32 graph = -1;
            MVMCodepoint codepoint = 0;
            MVMuint8 byte;
            int handler_rtrn = 0;
            if (repl_pos) {
                graph = MVM_string_get_grapheme_at_nocheck(tc, ds->replacement, repl_pos++);
                if (repl_length <= repl_pos) repl_pos = 0;
            }
            else if (is_prepended) {
                byte = prepended;
                is_prepended = 0;
            }
            else {
                byte = bytes[pos++];
            }
            /* graph will be -1 unless we just grabbed a replacement grapheme */
            if (graph == -1) {
                handler_rtrn = decoder_handler(tc, &Shift_JIS_lead, byte, &codepoint);
                if (handler_rtrn == DECODE_CODEPOINT) {
                    graph = codepoint;
                }
                else if (handler_rtrn == DECODE_CONTINUE) {
                    continue;
                }
                else if (handler_rtrn == DECODE_ERROR) {
                    /* Clearing this seems like the right thing to do, in case
                     * a replacement is used. */
                    Shift_JIS_lead = 0x00;
                    if (ds->replacement) {
                        graph = MVM_string_get_grapheme_at_nocheck(tc, ds->replacement, repl_pos);
                        /* If the replacement is more than one grapheme we need
                         * to set repl_pos++ so we will grab the next grapheme on
                         * the next loop */
                        if (1 < repl_length) repl_pos++;
                    }
                    else {
                        /* Throw if it's unmapped */
                        MVM_free(buffer);
                        MVM_exception_throw_adhoc(tc,
                            "Error decoding shiftjis string: could not byte 0x%hhx",
                             byte);
                    }
                }
                else if (handler_rtrn == DECODE_PREPEND_TO_STREAM) {
                    is_prepended = 1;
                    prepended    = codepoint;
                    continue;
                }
                else {
                    MVM_exception_throw_adhoc(tc, "shiftjis decoder encountered an internal error. This bug should be reported.\n");
                }
            }
            if (last_was_cr) {
                if (graph == '\n') {
                    graph = MVM_unicode_normalizer_translated_crlf(tc, &(ds->norm));
                }
                else {
                    graph = '\r';
                    pos--;
                }
                last_was_cr = 0;
            }
            else if (graph == '\r') {
                last_was_cr = 1;
                continue;
            }
            if (count == bufsize) {
                /* We filled the buffer. Attach this one to the buffers
                 * linked list, and continue with a new one. */
                MVM_string_decodestream_add_chars(tc, ds, buffer, bufsize);
                buffer = MVM_malloc(bufsize * sizeof(MVMGrapheme32));
                count = 0;
            }
            buffer[count++]   = graph;
            last_accept_bytes = cur_bytes;
            last_accept_pos   = pos;
            total++;
            if (MVM_string_decode_stream_maybe_sep(tc, seps, codepoint)) {
                reached_stopper = 1;
                goto done;
            }
            else if (stopper_chars && *stopper_chars == total) {
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
