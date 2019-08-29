#include "moar.h"
#include "gb2312_codeindex.h"

MVMString * MVM_string_gb2312_decode(MVMThreadContext *tc, const MVMObject *result_type, const char *gb2312, size_t bytes) {
    size_t i, result_graphs;

    MVMString *result = (MVMString *)REPR(result_type)->allocate(tc, STABLE(result_type));

    result->body.storage_type = MVM_STRING_GRAPHEME_32;
    result->body.storage.blob_32 = MVM_malloc(sizeof(MVMGrapheme32) * bytes);

    result_graphs = 0;

    for (i = 0; i < bytes; i++) {
        if (0 <= gb2312[i] && gb2312[i] <= 127) {
            /* Ascii character */
            if (gb2312[i] == '\r' && i + 1 < bytes && gb2312[i + 1] == '\n') {
                result->body.storage.blob_32[result_graphs++] = MVM_nfg_crlf_grapheme(tc);
                i++;
            }
            else {
                result->body.storage.blob_32[result_graphs++] = gb2312[i];
            }
        }
        else {
            if (i + 1 < bytes && (gb2312[i + 1] > 127 || gb2312[i + 1] < 0)) {
                MVMuint8 byte1 = gb2312[i];
                MVMuint8 byte2 = gb2312[i + 1];
                MVMuint16 codepoint = (MVMuint16)byte1 * 256 + byte2;
                MVMGrapheme32 index = gb2312_index_to_cp(codepoint);
                if (index != GB2312_NULL) {
                    result->body.storage.blob_32[result_graphs++] = index;
                    i++;
                }
                else {
                    MVM_exception_throw_adhoc(tc, "Error decoding gb2312 string: could not decode codepoint 0x%x", codepoint);
                }
            }
            else {
                MVM_exception_throw_adhoc(tc, 
                "Error decoding gb2312 string: invalid gb2312 format (two bytes for a gb2312 character). Last byte seen was 0x%hhX\n", 
                (MVMuint8)gb2312[i]);
            }
        }
    }

    result->body.num_graphs = result_graphs;

    return result;
}

#define GB2312_DECODE_FORMAT_EXCEPTION -1
#define GB2312_DECODE_ASCII_CODEPOINT -2
#define GB2312_DECODE_CONTINUE -3
#define GB2312_DECODE_CODEPOINT_EXCEPTION -4
#define GB2312_DECODE_CHINESE_CODEPOINT -5

int gb2312_decode_handler(MVMThreadContext *tc, MVMint32 last_was_first_byte, 
                          MVMuint16 codepoint, MVMuint16 last_codepoint, MVMGrapheme32 *out) {
    MVMGrapheme32 graph;
    if (codepoint <= 127) {
        if (last_was_first_byte) {
            return GB2312_DECODE_FORMAT_EXCEPTION;
        }
        graph = (MVMGrapheme32)codepoint;
        *out = graph;
        return GB2312_DECODE_ASCII_CODEPOINT;
    }
    else {
        if (last_was_first_byte) {
            MVMuint16 combined_codepoint = last_codepoint * 256 + codepoint;
            graph = gb2312_index_to_cp(combined_codepoint);
            *out = graph;
            if (graph == GB2312_NULL) {
                return GB2312_DECODE_CODEPOINT_EXCEPTION;
            }
            return GB2312_DECODE_CHINESE_CODEPOINT;
        }
        else {
            return GB2312_DECODE_CONTINUE;
        }
    }
}

MVMuint32 MVM_string_gb2312_decodestream(MVMThreadContext *tc, MVMDecodeStream *ds,
                                         const MVMint32 *stopper_chars, MVMDecodeStreamSeparators *seps) {
    MVMint32 count = 0, total = 0;
    MVMint32 bufsize;
    MVMGrapheme32 *buffer = NULL;
    MVMDecodeStreamBytes *cur_bytes = NULL;
    MVMDecodeStreamBytes *last_accept_bytes = ds->bytes_head;
    MVMint32 last_accept_pos, last_was_cr;
    MVMuint32 reached_stopper;

    MVMint32 last_was_first_byte;
    MVMuint16 last_codepoint;
    
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

    last_was_first_byte = 0;
    last_codepoint = 0;

    while (cur_bytes) {
        /* Process this buffer. */
        MVMint32 pos = cur_bytes == ds->bytes_head ? ds->bytes_head_pos : 0;
        MVMuint8 *bytes = (MVMuint8 *)cur_bytes->bytes;

        while (pos < cur_bytes->length) {
            MVMGrapheme32 graph;
            MVMuint16 codepoint = (MVMuint16) bytes[pos++];

            int handler_rtrn = gb2312_decode_handler(tc, last_was_first_byte, codepoint, last_codepoint, &graph);

            if (handler_rtrn == GB2312_DECODE_FORMAT_EXCEPTION) {
                MVM_exception_throw_adhoc(tc, 
                "Error decoding gb2312 string: invalid gb2312 format (two bytes for a gb2312 character). Last byte seen was 0x%x\n", 
                last_codepoint);
            }
            else if (handler_rtrn == GB2312_DECODE_CODEPOINT_EXCEPTION) {
                MVM_exception_throw_adhoc(tc, "Error decoding gb2312 string: could not decode codepoint 0x%x", 
                last_codepoint * 256 + codepoint);
            }
            else if (handler_rtrn == GB2312_DECODE_CONTINUE) {
                last_codepoint = codepoint;
                last_was_first_byte = 1;
                continue;
            }
            else if (handler_rtrn == GB2312_DECODE_CHINESE_CODEPOINT) {
                last_was_first_byte = 0;
            }
            if (last_was_cr) {
                if (codepoint == '\n') {
                    graph = MVM_unicode_normalizer_translated_crlf(tc, &(ds->norm));
                }
                else {
                    graph = '\r';
                    pos--;
                }
                last_was_cr = 0;
            }
            else if (codepoint == '\r') {
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

            buffer[count++] = graph;
            last_accept_bytes = cur_bytes;
            last_accept_pos = pos;
            total++;

            if (MVM_string_decode_stream_maybe_sep(tc, seps, codepoint) ||
                    (stopper_chars && *stopper_chars == total)) {
                reached_stopper = 1;
                goto done;
            }
        }

        cur_bytes = cur_bytes -> next;
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

char * MVM_string_gb2312_encode_substr(MVMThreadContext *tc, MVMString *str,
                                       MVMuint64 *output_size, MVMint64 start, MVMint64 length, MVMString *replacement,
                                       MVMint32 translate_newlines) {

    MVMuint32 startu = (MVMuint32)start;
    MVMStringIndex strgraphs = MVM_string_graphs(tc, str);
    MVMuint32 lengthu = (MVMuint32)(length == -1 ? strgraphs - startu : length);
    MVMuint8 *result = NULL;
    size_t result_alloc;
    MVMuint8 *repl_bytes = NULL;
    MVMuint64 repl_length;

    if (start < 0 || start > strgraphs)
        MVM_exception_throw_adhoc(tc, "start out of range");
    if (length < -1 || start + lengthu > strgraphs)
        MVM_exception_throw_adhoc(tc, "length out of range");

    if (replacement)
        repl_bytes = (MVMuint8 *) MVM_string_gb2312_encode_substr(tc,
                     replacement, &repl_length, 0, -1, NULL, translate_newlines);

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
            if (codepoint <= 0x7F) {
                /* ASCII character */
                result[out_pos++] = codepoint;
            }
            else {
                MVMint32 gb2312_cp;
                gb2312_cp = gb2312_cp_to_index(codepoint);
                if (gb2312_cp == GB2312_NULL) {
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
                    MVM_free(result);
                    MVM_exception_throw_adhoc(tc, "Error encoding gb2312 string: could not encode codepoint 0x%x", codepoint);
                }
                result[out_pos++] = gb2312_cp / 256;
                result[out_pos++] = gb2312_cp % 256;
            }
        }
        result[out_pos] = 0;
        if (output_size)
            *output_size = out_pos;
    }
    MVM_free(repl_bytes);
    return (char *)result;
}
