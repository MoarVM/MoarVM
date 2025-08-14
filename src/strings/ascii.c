#include "moar.h"

/* Decodes the specified number of bytes of ASCII into an NFG string, creating
 * a result of the specified type. The type must have the MVMString REPR. */
MVMString * MVM_string_ascii_decode(MVMThreadContext *tc, const MVMObject *result_type, const char *ascii, size_t bytes) {
    MVMString *result;
    MVMGrapheme32 *buffer;
    size_t i, result_graphs;

    if (bytes == 0 && tc->instance->str_consts.empty) {
        return tc->instance->str_consts.empty;
    }

    buffer = MVM_malloc(sizeof(MVMGrapheme32) * bytes);

    result_graphs = 0;
    for (i = 0; i < bytes; i++) {
        if (ascii[i] == '\r' && i + 1 < bytes && ascii[i + 1] == '\n') {
            buffer[result_graphs++] = MVM_nfg_crlf_grapheme(tc);
            i++;
        }
        /* `ascii[i] < 0` is always false on platforms where char is unsigned.
         * This expression works whatever plain char is. */
        else if ((ascii[i] & 0x80) == 0) {
            buffer[result_graphs++] = ascii[i];
        }
        else {
            MVM_free(buffer);
            MVM_exception_throw_adhoc(tc,
                "Will not decode invalid ASCII (code point (%"PRId32") < 0 found)", ascii[i]);
        }
    }
    result = (MVMString *)REPR(result_type)->allocate(tc, STABLE(result_type));
    result->body.storage.blob_32 = buffer;
    result->body.storage_type = MVM_STRING_GRAPHEME_32;
    result->body.num_graphs = result_graphs;

    return result;
}

/* Decodes a NULL-terminated ASCII string into an NFG string, creating
 * a result of the specified type. The type must have the MVMString REPR. */
MVMString * MVM_string_ascii_decode_nt(MVMThreadContext *tc, const MVMObject *result_type, const char *ascii) {
    return MVM_string_ascii_decode(tc, result_type, ascii, strlen(ascii));
}

/* Decodes using a decodestream. Decodes as far as it can with the input
 * buffers, or until a stopper is reached. */
MVMuint32 MVM_string_ascii_decodestream(MVMThreadContext *tc, MVMDecodeStream *ds,
                                   const MVMuint32 *stopper_chars,
                                   MVMDecodeStreamSeparators *seps) {
    MVMuint32             count = 0, total = 0;
    MVMuint32             bufsize;
    MVMGrapheme32        *buffer;
    MVMDecodeStreamBytes *cur_bytes;
    MVMDecodeStreamBytes *last_accept_bytes = ds->bytes_head;
    MVMint32 last_accept_pos, last_was_cr;
    MVMuint32 reached_stopper;

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
    while (cur_bytes) {
        /* Process this buffer. */
        MVMint32  pos   = cur_bytes == ds->bytes_head ? ds->bytes_head_pos : 0;
        MVMuint8 *bytes = (MVMuint8*)cur_bytes->bytes;
        while (pos < cur_bytes->length) {
            MVMCodepoint codepoint = bytes[pos++];
            MVMGrapheme32 graph;
            if (codepoint > 127) {
                MVM_free(buffer);
                MVM_exception_throw_adhoc(tc,
                    "Will not decode invalid ASCII (code point (%"PRId32") > 127 found)", codepoint);
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
            else {
                graph = codepoint;
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

/* Encodes the specified substring to ASCII. Anything outside of ASCII range
 * will become replaced with the supplied replacement, or an exception will be
 * thrown if there isn't one. The result string is NULL terminated, but the
 * specified size is the non-null part. */
char * MVM_string_ascii_encode_substr(MVMThreadContext *tc, MVMString *str, MVMuint64 *output_size, MVMint64 start, MVMint64 length, MVMString *replacement, MVMint32 translate_newlines) {
    /* ASCII is a single byte encoding, but \r\n is a 2-byte grapheme, so we
     * may have to resize as we go. */
    MVMStringIndex strgraphs = MVM_string_graphs(tc, str);
    MVMuint32      lengthu   = (MVMuint32)(length == -1 ? strgraphs - (MVMuint32)start : length);
    MVMuint8      *result;
    size_t         result_alloc;
    MVMuint8      *repl_bytes = NULL;
    MVMuint64      repl_length;

    /* must check start first since it's used in the length check */
    if (start < 0 || start > strgraphs)
        MVM_exception_throw_adhoc(tc, "start (%"PRId64") out of range (0..%"PRIu32")", start, strgraphs);
    if (length < -1 || start + lengthu > strgraphs)
        MVM_exception_throw_adhoc(tc, "length (%"PRId64") out of range (-1..%"PRIu32")", length, strgraphs);

    if (replacement)
        repl_bytes = (MVMuint8 *) MVM_string_ascii_encode_substr(tc, replacement,
            &repl_length, 0, -1, NULL, translate_newlines);

    result_alloc = lengthu;
    result = MVM_malloc(result_alloc + 1);
    if (str->body.storage_type == MVM_STRING_GRAPHEME_ASCII) {
        /* No encoding needed; directly copy. */
        memcpy(result, str->body.storage.blob_ascii + start, lengthu);
        result[lengthu] = 0;
        if (output_size)
            *output_size = lengthu;
    }
    else {
        MVMuint32 i = 0;
        MVMCodepointIter ci, gci;
        MVMGraphemeIter gi;
        MVMuint64 codepoints_in_graphemes = 0;
        MVM_string_ci_init(tc, &ci, str, translate_newlines, 0);
        MVM_string_gi_init(tc, &gi, str);
        /* Skip `start` number of graphmes, counting how many codepoints that is. */
        while (MVM_string_gi_has_more(tc, &gi) && start > 0) {
            MVMGrapheme32 g = MVM_string_gi_get_grapheme(tc, &gi);
            codepoints_in_graphemes += MVM_string_grapheme_ci_init(tc, &gci, g, 0);
            start--;
        }
        /* Now skip that many codepoints. */
        while (codepoints_in_graphemes > 0) {
            MVM_string_ci_get_codepoint(tc, &ci);
            codepoints_in_graphemes--;
        }
        /* Read `lengthu` number of graphemes, counting how many codepoints that is. */
        while (MVM_string_gi_has_more(tc, &gi) && lengthu > 0) {
            MVMGrapheme32 g = MVM_string_gi_get_grapheme(tc, &gi);
            codepoints_in_graphemes += MVM_string_grapheme_ci_init(tc, &gci, g, 0);
            lengthu--;
        }
        /* Now encode that many codepoints. */
        while (codepoints_in_graphemes > 0) {
            MVMCodepoint ord = MVM_string_ci_get_codepoint(tc, &ci);
            codepoints_in_graphemes--;
            if (i == result_alloc) {
                result_alloc += 8;
                result = MVM_realloc(result, result_alloc + 1);
            }
            if (0 <= ord && ord <= 127) {
                result[i++] = (MVMuint8)ord;
            }
            else if (replacement) {
                if (repl_length >= result_alloc || i >= result_alloc - repl_length) {
                    result_alloc += repl_length;
                    result = MVM_realloc(result, result_alloc + 1);
                }
                memcpy(result + i, repl_bytes, repl_length);
                i += repl_length;
            }
            else {
                MVM_free(result);
                MVM_free(repl_bytes);
                MVM_exception_throw_adhoc(tc,
                    "Error encoding ASCII string: could not encode codepoint %d",
                    ord);
            }
        }
        result[i] = 0;
        if (output_size)
            *output_size = i;
    }

    if (repl_bytes) MVM_free(repl_bytes);
    return (char *)result;
}

/* Encodes the specified string to ASCII.  */
char * MVM_string_ascii_encode(MVMThreadContext *tc, MVMString *str, MVMuint64 *output_size, MVMint32 translate_newlines) {
    return MVM_string_ascii_encode_substr(tc, str, output_size, 0, -1, NULL, translate_newlines);
}

/* Encodes the specified string to ASCII not returning length.  */
char * MVM_string_ascii_encode_any(MVMThreadContext *tc, MVMString *str) {
    return MVM_string_ascii_encode(tc, str, NULL, 0);
}

/* Encodes the specified string to ASCII using libc malloc.  */
char * MVM_string_ascii_encode_malloc(MVMThreadContext *tc, MVMString *str) {
    /* ASCII is a single byte encoding, but \r\n is a 2-byte grapheme, so we
     * may have to resize as we go. */
    MVMuint32      lengthu   = MVM_string_graphs(tc, str);

    size_t         result_alloc = lengthu;
    MVMuint8      *result = malloc(result_alloc + 1);
    if (str->body.storage_type == MVM_STRING_GRAPHEME_ASCII) {
        /* No encoding needed; directly copy. */
        memcpy(result, str->body.storage.blob_ascii, lengthu);
        result[lengthu] = 0;
    }
    else {
        MVMuint32 i = 0;
        MVMCodepointIter ci;
        MVM_string_ci_init(tc, &ci, str, 0, 0);
        while (MVM_string_ci_has_more(tc, &ci)) {
            MVMCodepoint ord = MVM_string_ci_get_codepoint(tc, &ci);
            if (i == result_alloc) {
                result_alloc += 8;
                result = realloc(result, result_alloc + 1);
            }
            if (0 <= ord && ord <= 127) {
                result[i++] = (MVMuint8)ord;
            }
            else {
                free(result);
                MVM_exception_throw_adhoc(tc,
                    "Error encoding ASCII string: could not encode codepoint %d",
                    ord);
            }
        }
        result[i] = 0;
    }

    return (char *)result;
}
