#include "moar.h"
#include "gb18030_codeindex.h"

/*  Information about GB18030: http://www.gb18030.com/
    GB18030 to Unicode Mapping used (included in GNU LIBICONV Package):
	ftp://ftp.gnu.org/pub/gnu/libiconv/libiconv-1.7.tar.gz
*/

const MVMint32 gb18030_two_byte_lower_bound[126] = {
64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,
64,64,64,64,64,64,161,161,161,161,161,161,161,64,64,64,64,64,64,64,64,64,64,64,
64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,
64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,
64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64};

const MVMint32 gb18030_two_byte_upper_bound[126] = {
254,254,254,254,254,254,254,254,254,254,254,254,254,254,254,254,254,254,254,254,
254,254,254,254,254,254,254,254,254,254,254,254,254,252,254,243,246,245,241,233,
239,160,160,160,160,160,160,254,254,254,254,254,254,254,254,254,254,254,254,254,
254,254,254,254,254,254,254,254,254,254,254,254,254,254,254,254,254,254,254,254,
254,254,254,254,254,254,249,254,254,254,254,254,254,254,254,254,254,254,254,254,
254,254,254,254,254,254,254,254,254,254,254,254,254,254,254,254,254,254,254,160,
160,160,160,160,160,159};

MVMint32 gb18030_valid_check_len2(MVMint32 c_1, MVMint32 c_2) {
    /* This function serves like a 'first stage check' of c_1 and c_2.
       It eliminates most of the invalid combinations of c_1 and c_2, 
       but for code simplicity and to avoid lots of if-else here,
       ther remaining invalid combinations will be processed 
       in the conversion table. */
    if (c_1 < 0x81 || c_1 > 0xfe) return 0;
    c_1 -= 0x81;
    return gb18030_two_byte_lower_bound[c_1] <= c_2 && c_2 <= gb18030_two_byte_upper_bound[c_1];
}

MVMint32 gb18030_valid_check_len4(MVMint32 c_1, MVMint32 c_2, MVMint32 c_3, MVMint32 c_4) {
    if ((0x81 <= c_1 && c_1 <= 0x83) || (c_1 == 0x84 && c_2 == 0x30)) {
        return (0x30 <= c_2 && c_2 <= 0x39) && (0x81 <= c_3 && c_3 <= 0xfe) && (0x30 <= c_4 && c_4 <= 0x39);
    } else if (c_1 == 0x84 && c_2 == 0x31) {
        return (0x81 <= c_3 && c_3 <= 0xa4) && (0x30 <= c_4 && c_4 <= 0x39);
    }
    return 0;
}

MVMint32 gb18030_valid_check_len4_first2(MVMint32 c_1, MVMint32 c_2) {
    return (((0x81 <= c_1 && c_1 <= 0x83) || (c_1 == 0x84 && c_2 == 0x30)) && (0x30 <= c_2 && c_2 <= 0x39))	|| (c_1 == 0x84 && c_2 == 0x31);
}

MVMString * MVM_string_gb18030_decode(MVMThreadContext *tc, const MVMObject *result_type, const char *gb18030, size_t bytes) {
    size_t i, result_graphs;

    MVMString *result = (MVMString *)REPR(result_type)->allocate(tc, STABLE(result_type));

    result->body.storage_type = MVM_STRING_GRAPHEME_32;
    result->body.storage.blob_32 = MVM_malloc(sizeof(MVMGrapheme32) * bytes);

    result_graphs = 0;

    for (i = 0; i < bytes; i++) {
        if (0 <= gb18030[i] && gb18030[i] <= 127) {
            if (gb18030[i] == '\r' && i + 1 < bytes && gb18030[i + 1] == '\n') {
                result->body.storage.blob_32[result_graphs++] = MVM_nfg_crlf_grapheme(tc);
                i++;
            }
            else {
                result->body.storage.blob_32[result_graphs++] = gb18030[i];
            }
        }
        else {
            if (i + 1 < bytes) {
                /* GB18030 codepoint of length 2 */
                MVMuint8 byte1 = gb18030[i];
                MVMuint8 byte2 = gb18030[i + 1];
                if (gb18030_valid_check_len2(byte1, byte2)) {
                    MVMGrapheme32 index = gb18030_index_to_cp_len2(byte1, byte2);
                    if (index != GB18030_NULL) {
                        result->body.storage.blob_32[result_graphs++] = index;
                        i++;
                        continue;
                    }
                }
            }
            if (i + 3 < bytes) {
                /* GB18030 codepoint of length 4 */
                MVMuint8 byte1 = gb18030[i];
                MVMuint8 byte2 = gb18030[i + 1];
                MVMuint8 byte3 = gb18030[i + 2];
                MVMuint8 byte4 = gb18030[i + 3];
                if (gb18030_valid_check_len4(byte1, byte2, byte3, byte4)) {
                    MVMGrapheme32 index = gb18030_index_to_cp_len4(byte1, byte2, byte3, byte4);
                    if (index != GB18030_NULL) {
                        result->body.storage.blob_32[result_graphs++] = index;
                        i += 3;
                        continue;
                    }
                }
            }
            
            MVM_exception_throw_adhoc(tc, 
            "Error decoding gb18030 string: invalid gb18030 format. Last byte seen was 0x%hhX\n", 
            (MVMuint8)gb18030[i]);
        }
    }

    result->body.num_graphs = result_graphs;

    return result;
}

MVMuint32 MVM_string_gb18030_decodestream(MVMThreadContext *tc, MVMDecodeStream *ds,
                                         const MVMint32 *stopper_chars, MVMDecodeStreamSeparators *seps) {
    MVMint32 count = 0, total = 0;
    MVMint32 bufsize;
    MVMGrapheme32 *buffer = NULL;
    MVMDecodeStreamBytes *cur_bytes = NULL;
    MVMDecodeStreamBytes *last_accept_bytes = ds->bytes_head;
    MVMint32 last_accept_pos, last_was_cr;
    MVMuint32 reached_stopper;

    MVMint32 last_was_first_byte, is_len4;
    MVMint32 last_codepoint;
    MVMint32 len4_cnt,len4_byte1, len4_byte2, len4_byte3, len4_byte4;
    
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
    
    is_len4 = 0;
    len4_cnt = 0;
    len4_byte1 = 0;
    len4_byte2 = 0;
    len4_byte3 = 0;
    len4_byte4 = 0;

    while (cur_bytes) {
        /* Process this buffer. */
        MVMint32 pos = cur_bytes == ds->bytes_head ? ds->bytes_head_pos : 0;
        MVMuint8 *bytes = (MVMuint8 *)cur_bytes->bytes;

        while (pos < cur_bytes->length) {
            MVMGrapheme32 graph;
            MVMint32 codepoint = (MVMint32) bytes[pos++];
            
            if (is_len4) {
                if (len4_cnt == 2) {
                    len4_cnt++;
                    len4_byte3 = codepoint;
                    continue;
                }
                if (len4_cnt == 3) {
                    len4_byte4 = codepoint;
                    if (gb18030_valid_check_len4(len4_byte1, len4_byte2, len4_byte3, len4_byte4)) {
                        graph = gb18030_index_to_cp_len4(len4_byte1, len4_byte2, len4_byte3, len4_byte4);
                        is_len4 = 0;
                    } else {
                        MVM_exception_throw_adhoc(tc, 
                         "Error decoding gb18030 string: invalid gb18030 format. Last four bytes seen was 0x%x, 0x%x, 0x%x, 0x%x\n", 
                         len4_byte1, len4_byte2, len4_byte3, len4_byte4);
                    }
                }
            }
            else if (codepoint <= 127 && !last_was_first_byte) {
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
                else {
                    graph = codepoint;
                }
            }
            else {
                if (last_was_first_byte) {
                    if (gb18030_valid_check_len4_first2(last_codepoint, codepoint)) {
                        is_len4 = 1;
                        len4_byte1 = last_codepoint;
                        len4_byte2 = codepoint;
                        len4_cnt = 2;
                        last_was_first_byte = 0;
                        continue;
                    }
                    graph = gb18030_index_to_cp_len2(last_codepoint, codepoint);
                    if (graph == GB18030_NULL) {
                        MVM_exception_throw_adhoc(tc, 
                        "Error decoding gb18030 string: invalid gb18030 format. Last two bytes seen was 0x%x, 0x%x\n", 
                        last_codepoint, codepoint);
                    }
                    last_was_first_byte = 0;
                }
                else {
                    last_was_first_byte = 1;
                    last_codepoint = codepoint;
                    continue;
                }
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

char * MVM_string_gb18030_encode_substr(MVMThreadContext *tc, MVMString *str,
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
        repl_bytes = (MVMuint8 *) MVM_string_gb18030_encode_substr(tc,
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
                result = MVM_realloc(result, result_alloc + 5);
            }
            if (codepoint <= 0x7F) {
                result[out_pos++] = codepoint;
            }
            else {
                MVMint64 gb18030_cp;
                gb18030_cp = gb18030_cp_to_index(codepoint);
                if (gb18030_cp == GB18030_NULL) {
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
                        MVM_exception_throw_adhoc(tc, "Error encoding gb18030 string: could not encode codepoint 0x%x", codepoint);
                    }
                }
                if (gb18030_cp <= 0xffff) {
                    /* Length = 2 */
                    result[out_pos++] = gb18030_cp / 256;
                    result[out_pos++] = gb18030_cp % 256;
                }
                else {
                    /* Length = 4 */
                    result[out_pos++] = (gb18030_cp / 16777216) % 256;
                    result[out_pos++] = (gb18030_cp / 65536) % 256;
                    result[out_pos++] = (gb18030_cp / 256) % 256;
                    result[out_pos++] = gb18030_cp % 256;
                }
            }
        }
        result[out_pos] = 0;
        if (output_size)
            *output_size = out_pos;
    }
    if (repl_bytes) MVM_free(repl_bytes);
    return (char *)result;
}
