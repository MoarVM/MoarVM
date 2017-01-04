#include "moar.h"

/* A decode stream represents an on-going decoding process, from bytes into
 * characters. Bytes can be contributed to the decode stream, and chars can be
 * obtained. Byte buffers and decoded char buffers are kept in linked lists.
 * Note that characters may start at the end of one byte buffer and finish in
 * the next, which is taken care of by the logic in here and the decoders
 * themselves. Additionally, normalization may be applied using the normalizer
 * in the decode stream, at the discretion of the encoding in question (some,
 * such as ASCII and Latin-1, are normalized by definition).
 */

#define DECODE_NOT_EOF  0
#define DECODE_EOF      1

/* Creates a new decoding stream. */
MVMDecodeStream * MVM_string_decodestream_create(MVMThreadContext *tc, MVMint32 encoding,
        MVMint64 abs_byte_pos, MVMint32 translate_newlines) {
    MVMDecodeStream *ds = MVM_calloc(1, sizeof(MVMDecodeStream));
    ds->encoding        = encoding;
    ds->abs_byte_pos    = abs_byte_pos;
    MVM_unicode_normalizer_init(tc, &(ds->norm), MVM_NORMALIZE_NFG);
    if (translate_newlines)
        MVM_unicode_normalizer_translate_newlines(tc, &(ds->norm));
    return ds;
}

/* Adds another byte buffer into the decoding stream. */
void MVM_string_decodestream_add_bytes(MVMThreadContext *tc, MVMDecodeStream *ds, char *bytes, MVMint32 length) {
    if (length > 0) {
        MVMDecodeStreamBytes *new_bytes = MVM_calloc(1, sizeof(MVMDecodeStreamBytes));
        new_bytes->bytes  = bytes;
        new_bytes->length = length;
        if (ds->bytes_tail)
            ds->bytes_tail->next = new_bytes;
        ds->bytes_tail = new_bytes;
        if (!ds->bytes_head)
            ds->bytes_head = new_bytes;
    }
    else {
        /* It's empty, so free the buffer right away and don't add. */
        MVM_free(bytes);
    }
}

/* Adds another char result buffer into the decoding stream. */
void MVM_string_decodestream_add_chars(MVMThreadContext *tc, MVMDecodeStream *ds, MVMGrapheme32 *chars, MVMint32 length) {
    MVMDecodeStreamChars *new_chars = MVM_calloc(1, sizeof(MVMDecodeStreamChars));
    new_chars->chars  = chars;
    new_chars->length = length;
    if (ds->chars_tail)
        ds->chars_tail->next = new_chars;
    ds->chars_tail = new_chars;
    if (!ds->chars_head)
        ds->chars_head = new_chars;
}

/* Throws away byte buffers no longer needed. */
void MVM_string_decodestream_discard_to(MVMThreadContext *tc, MVMDecodeStream *ds, const MVMDecodeStreamBytes *bytes, MVMint32 pos) {
    while (ds->bytes_head != bytes) {
        MVMDecodeStreamBytes *discard = ds->bytes_head;
        ds->abs_byte_pos += discard->length - ds->bytes_head_pos;
        ds->bytes_head = discard->next;
        ds->bytes_head_pos = 0;
        MVM_free(discard->bytes);
        MVM_free(discard);
    }
    if (!ds->bytes_head && pos == 0)
        return;
    if (ds->bytes_head->length == pos) {
        /* We ate all of the new head buffer too; also free it. */
        MVMDecodeStreamBytes *discard = ds->bytes_head;
        ds->abs_byte_pos += discard->length - ds->bytes_head_pos;
        ds->bytes_head = discard->next;
        ds->bytes_head_pos = 0;
        MVM_free(discard->bytes);
        MVM_free(discard);
        if (ds->bytes_head == NULL)
            ds->bytes_tail = NULL;
    }
    else {
        ds->abs_byte_pos += pos - ds->bytes_head_pos;
        ds->bytes_head_pos = pos;
    }
}

/* Does a decode run, selected by encoding. Returns non-zero if we actually
 * decoded more chars. */
#define RUN_DECODE_NOTHING_DECODED          0
#define RUN_DECODE_STOPPER_NOT_REACHED      1
#define RUN_DECODE_STOPPER_REACHED          2
static MVMuint32 run_decode(MVMThreadContext *tc, MVMDecodeStream *ds, const MVMint32 *stopper_chars, MVMDecodeStreamSeparators *sep_spec, MVMint32 eof) {
    MVMDecodeStreamChars *prev_chars_tail = ds->chars_tail;
    MVMuint32 reached_stopper;
    switch (ds->encoding) {
    case MVM_encoding_type_utf8:
        reached_stopper = MVM_string_utf8_decodestream(tc, ds, stopper_chars, sep_spec);
        break;
    case MVM_encoding_type_ascii:
        reached_stopper = MVM_string_ascii_decodestream(tc, ds, stopper_chars, sep_spec);
        break;
    case MVM_encoding_type_latin1:
        reached_stopper = MVM_string_latin1_decodestream(tc, ds, stopper_chars, sep_spec);
        break;
    case MVM_encoding_type_windows1252:
        reached_stopper = MVM_string_windows1252_decodestream(tc, ds, stopper_chars, sep_spec);
        break;
    case MVM_encoding_type_utf8_c8:
        reached_stopper = MVM_string_utf8_c8_decodestream(tc, ds, stopper_chars, sep_spec, eof);
        break;
    default:
        MVM_exception_throw_adhoc(tc, "Streaming decode NYI for encoding %d",
            (int)ds->encoding);
    }
    if (ds->chars_tail == prev_chars_tail)
        return RUN_DECODE_NOTHING_DECODED;
    else if (reached_stopper)
        return RUN_DECODE_STOPPER_REACHED;
    else
        return RUN_DECODE_STOPPER_NOT_REACHED;
}

/* Gets the specified number of characters. If we are not yet able to decode
 * that many, returns NULL. This may mean more input buffers are needed. The
 * exclude parameter specifies a number of chars that should be taken from the
 * input buffer, but not included in the result string (for chomping a line
 * separator). */
static MVMint32 missing_chars(MVMThreadContext *tc, const MVMDecodeStream *ds, MVMint32 wanted) {
    MVMint32 got = 0;
    MVMDecodeStreamChars *cur_chars = ds->chars_head;
    while (cur_chars && got < wanted) {
        if (cur_chars == ds->chars_head)
            got += cur_chars->length - ds->chars_head_pos;
        else
            got += cur_chars->length;
        cur_chars = cur_chars->next;
    }
    return got >= wanted ? 0 : wanted - got;
}
static MVMString * take_chars(MVMThreadContext *tc, MVMDecodeStream *ds, MVMint32 chars, MVMint32 exclude) {
    MVMString *result;
    MVMint32   found = 0;
    MVMint32   result_found = 0;

    MVMint32   result_chars = chars - exclude;
    if (result_chars < 0)
        MVM_exception_throw_adhoc(tc, "DecodeStream take_chars: chars - exclude < 0 should never happen");

    result                       = (MVMString *)MVM_repr_alloc_init(tc, tc->instance->VMString);
    result->body.storage.blob_32 = MVM_malloc(result_chars * sizeof(MVMGrapheme32));
    result->body.storage_type    = MVM_STRING_GRAPHEME_32;
    result->body.num_graphs      = result_chars;
    while (found < chars) {
        MVMDecodeStreamChars *cur_chars = ds->chars_head;
        MVMint32 available = cur_chars->length - ds->chars_head_pos;
        if (available <= chars - found) {
            /* We need all that's left in this buffer and likely
             * more. */
            MVMDecodeStreamChars *next_chars = cur_chars->next;
            if (available <= result_chars - result_found) {
                memcpy(result->body.storage.blob_32 + result_found,
                    cur_chars->chars + ds->chars_head_pos,
                    available * sizeof(MVMGrapheme32));
                result_found += available;
            }
            else {
                MVMint32 to_copy = result_chars - result_found;
                memcpy(result->body.storage.blob_32 + result_found,
                    cur_chars->chars + ds->chars_head_pos,
                    to_copy * sizeof(MVMGrapheme32));
                result_found += to_copy;
            }
            found += available;
            MVM_free(cur_chars->chars);
            MVM_free(cur_chars);
            ds->chars_head = next_chars;
            ds->chars_head_pos = 0;
            if (ds->chars_head == NULL)
                ds->chars_tail = NULL;
        }
        else {
            /* There's enough in this buffer to satisfy us, and we'll leave
             * some behind. */
            MVMint32 take = chars - found;
            MVMint32 to_copy = result_chars - result_found;
            memcpy(result->body.storage.blob_32 + result_found,
                cur_chars->chars + ds->chars_head_pos,
                to_copy * sizeof(MVMGrapheme32));
            result_found += to_copy;
            found += take;
            ds->chars_head_pos += take;
        }
    }
    return result;
}
MVMString * MVM_string_decodestream_get_chars(MVMThreadContext *tc, MVMDecodeStream *ds, MVMint32 chars) {
    MVMint32 missing;

    /* If we request nothing, give empty string. */
    if (chars == 0)
        return tc->instance->str_consts.empty;

    /* If we don't already have enough chars, try and decode more. */
    missing = missing_chars(tc, ds, chars);
    if (missing)
        run_decode(tc, ds, &missing, NULL, DECODE_NOT_EOF);

    /* If we've got enough, assemble a string. Otherwise, give up. */
    if (missing_chars(tc, ds, chars) == 0)
        return take_chars(tc, ds, chars, 0);
    else
        return NULL;
}

/* Gets characters up until one of the specified separators is encountered. If
 * we do not encounter it, returns 0. This may mean more input buffers are needed
 * or that we reached the end of the stream. Note that it assumes the separator
 * will exist near the end of the buffer, if it occurs at all, due to decode
 * streams looking for stoppers. */
static MVMint32 have_separator(MVMThreadContext *tc, MVMDecodeStreamChars *start_chars, MVMint32 start_pos,
                               MVMDecodeStreamSeparators *sep_spec, MVMint32 sep_idx, MVMint32 sep_graph_pos) {
    MVMint32 sep_pos = 1;
    MVMint32 sep_length = sep_spec->sep_lengths[sep_idx];
    MVMDecodeStreamChars *cur_chars = start_chars;
    while (cur_chars) {
        MVMint32 start = cur_chars == start_chars ? start_pos : 0;
        MVMint32 i;
        for (i = start; i < cur_chars->length; i++) {
            if (cur_chars->chars[i] != sep_spec->sep_graphemes[sep_graph_pos])
                return 0;
            sep_pos++;
            if (sep_pos == sep_length)
                return 1;
            sep_graph_pos++;
        }
        cur_chars = cur_chars->next;
    }
    return 0;
}
static MVMint32 find_separator(MVMThreadContext *tc, const MVMDecodeStream *ds,
                               MVMDecodeStreamSeparators *sep_spec, MVMint32 *sep_length) {
    MVMint32 sep_loc = 0;
    MVMDecodeStreamChars *cur_chars = ds->chars_head;

    /* First, skip over any buffers we need not consider. */
    MVMint32 max_sep_chars = MVM_string_decode_stream_sep_max_chars(tc, sep_spec);
    while (cur_chars && cur_chars->next) {
        if (cur_chars->next->length < max_sep_chars)
            break;
        sep_loc += cur_chars->length;
        cur_chars = cur_chars->next;
    }

    /* Now scan for the separator. */
    while (cur_chars) {
        MVMint32 start = cur_chars == ds->chars_head ? ds->chars_head_pos : 0;
        MVMint32 i, j;
        for (i = start; i < cur_chars->length; i++) {
            MVMint32 sep_graph_pos = 0;
            MVMGrapheme32 cur_char = cur_chars->chars[i];
            sep_loc++;
            for (j = 0; j < sep_spec->num_seps; j++) {
                if (sep_spec->sep_graphemes[sep_graph_pos] == cur_char) {
                    if (sep_spec->sep_lengths[j] == 1) {
                        *sep_length = 1;
                        return sep_loc;
                    }
                    else if (have_separator(tc, cur_chars, i + 1, sep_spec, j, sep_graph_pos + 1)) {
                        *sep_length = sep_spec->sep_lengths[j];
                        sep_loc += sep_spec->sep_lengths[j] - 1;
                        return sep_loc;
                    }
                }
                sep_graph_pos += sep_spec->sep_lengths[j];
            }
        }
        cur_chars = cur_chars->next;
    }
    return 0;
}
MVMString * MVM_string_decodestream_get_until_sep(MVMThreadContext *tc, MVMDecodeStream *ds,
                                                  MVMDecodeStreamSeparators *sep_spec, MVMint32 chomp) {
    MVMint32 sep_loc, sep_length;

    /* Look for separator, trying more decoding if it fails. We get the place
     * just beyond the separator, so can use take_chars to get what's need.
     * Note that decoders are only responsible for finding the final char of
     * the separator, so we may need to loop a few times around this. */
    sep_loc = find_separator(tc, ds, sep_spec, &sep_length);
    while (!sep_loc) {
        MVMuint32 decode_outcome = run_decode(tc, ds, NULL, sep_spec, DECODE_NOT_EOF);
        if (decode_outcome == RUN_DECODE_NOTHING_DECODED)
            break;
        if (decode_outcome == RUN_DECODE_STOPPER_REACHED)
            sep_loc = find_separator(tc, ds, sep_spec, &sep_length);
    }
    if (sep_loc)
        return take_chars(tc, ds, sep_loc, chomp ? sep_length : 0);
    else
        return NULL;
}

/* In situations where we have hit EOF, we need to decode what's left and flush
 * the normalization buffer also. */
static void reached_eof(MVMThreadContext *tc, MVMDecodeStream *ds) {
    /* Decode all the things. */
    if (ds->bytes_head)
        run_decode(tc, ds, NULL, NULL, DECODE_EOF);

    /* If there's some things left in the normalization buffer, take them. */
    MVM_unicode_normalizer_eof(tc, &(ds->norm));
    if (MVM_unicode_normalizer_available(tc, &(ds->norm))) {
        MVMint32 ready = MVM_unicode_normalizer_available(tc, &(ds->norm));
        MVMGrapheme32 *buffer = MVM_malloc(ready * sizeof(MVMGrapheme32));
        MVMint32 count = 0;
        while (ready--)
            buffer[count++] = MVM_unicode_normalizer_get_grapheme(tc, &(ds->norm));
        MVM_string_decodestream_add_chars(tc, ds, buffer, count);
    }
}

/* Variant of MVM_string_decodestream_get_until_sep that is called when we
 * reach EOF. Trims the final separator if there is one, or returns the last
 * line without the EOF marker. */
MVMString * MVM_string_decodestream_get_until_sep_eof(MVMThreadContext *tc, MVMDecodeStream *ds,
                                                      MVMDecodeStreamSeparators *sep_spec, MVMint32 chomp) {
    MVMint32 sep_loc, sep_length;

    /* Decode anything remaining and flush normalization buffer. */
    reached_eof(tc, ds);

    /* Look for separator, which should by now be at the end, and chomp it
     * off if needed. */
    sep_loc = find_separator(tc, ds, sep_spec, &sep_length);
    if (sep_loc)
        return take_chars(tc, ds, sep_loc, chomp ? sep_length : 0);

    /* Otherwise, take all remaining chars. */
    return MVM_string_decodestream_get_all(tc, ds);
}

/* Produces a string consisting of the characters available now in all decdoed
 * buffers. */
static MVMString * get_all_in_buffer(MVMThreadContext *tc, MVMDecodeStream *ds) {
    MVMString *result = (MVMString *)MVM_repr_alloc_init(tc, tc->instance->VMString);
    result->body.storage_type = MVM_STRING_GRAPHEME_32;

    /* If there's no codepoint buffer, then return the empty string. */
    if (!ds->chars_head) {
        result->body.storage.blob_32 = NULL;
        result->body.num_graphs      = 0;
    }

    /* If there's exactly one resulting codepoint buffer and we swallowed none
     * of it, just use it. */
    else if (ds->chars_head == ds->chars_tail && ds->chars_head_pos == 0) {
        /* Set up result string. */
        result->body.storage.blob_32 = ds->chars_head->chars;
        result->body.num_graphs      = ds->chars_head->length;

        /* Don't free the buffer's memory itself, just the holder, as we
         * stole that for the buffer into the string above. */
        MVM_free(ds->chars_head);
        ds->chars_head = ds->chars_tail = NULL;
    }

    /* Otherwise, need to assemble all the things. */
    else {
        /* Calculate length. */
        MVMint32 length = 0, pos = 0;
        MVMDecodeStreamChars *cur_chars = ds->chars_head;
        while (cur_chars) {
            if (cur_chars == ds->chars_head)
                length += cur_chars->length - ds->chars_head_pos;
            else
                length += cur_chars->length;
            cur_chars = cur_chars->next;
        }

        /* Allocate a result buffer of the right size. */
        result->body.storage.blob_32 = MVM_malloc(length * sizeof(MVMGrapheme32));
        result->body.num_graphs      = length;

        /* Copy all the things into the target, freeing as we go. */
        cur_chars = ds->chars_head;
        while (cur_chars) {
            MVMDecodeStreamChars *next_chars = cur_chars->next;
            if (cur_chars == ds->chars_head) {
                MVMint32 to_copy = ds->chars_head->length - ds->chars_head_pos;
                memcpy(result->body.storage.blob_32 + pos, cur_chars->chars + ds->chars_head_pos,
                    cur_chars->length * sizeof(MVMGrapheme32));
                pos += to_copy;
            }
            else {
                memcpy(result->body.storage.blob_32 + pos, cur_chars->chars,
                    cur_chars->length * sizeof(MVMGrapheme32));
                pos += cur_chars->length;
            }
            MVM_free(cur_chars->chars);
            MVM_free(cur_chars);
            cur_chars = next_chars;
        }
        ds->chars_head = ds->chars_tail = NULL;
    }

    return result;
}

/* Decodes all the buffers, signals EOF to flush any normalization buffers, and
 * returns a string of all decoded chars. */
MVMString * MVM_string_decodestream_get_all(MVMThreadContext *tc, MVMDecodeStream *ds) {
    reached_eof(tc, ds);
    return get_all_in_buffer(tc, ds);
}

/* Decodes all the buffers we have, and returns a string of all decoded chars.
 * There may still be more to read after this, due to incomplete multi-byte
 * or multi-codepoint sequences that are not yet completely processed. */
MVMString * MVM_string_decodestream_get_available(MVMThreadContext *tc, MVMDecodeStream *ds) {
    if (ds->bytes_head)
        run_decode(tc, ds, NULL, NULL, DECODE_NOT_EOF);
    return get_all_in_buffer(tc, ds);
}

/* Checks if we have the number of bytes requested. */
MVMint64 MVM_string_decodestream_have_bytes(MVMThreadContext *tc, const MVMDecodeStream *ds, MVMint32 bytes) {
    MVMDecodeStreamBytes *cur_bytes = ds->bytes_head;
    MVMint32 found = 0;
    while (cur_bytes) {
        found += cur_bytes == ds->bytes_head
            ? cur_bytes->length - ds->bytes_head_pos
            : cur_bytes->length;
        if (found >= bytes)
            return 1;
        cur_bytes = cur_bytes->next;
    }
    return 0;
}

/* Gets the number of bytes available. */
MVMint64 MVM_string_decodestream_bytes_available(MVMThreadContext *tc, const MVMDecodeStream *ds) {
    MVMDecodeStreamBytes *cur_bytes = ds->bytes_head;
    MVMint32 available = 0;
    while (cur_bytes) {
        available += cur_bytes == ds->bytes_head
            ? cur_bytes->length - ds->bytes_head_pos
            : cur_bytes->length;
        cur_bytes = cur_bytes->next;
    }
    return available;
}

/* Copies up to the requested number of bytes into the supplied buffer, and
 * returns the number of bytes we actually copied. Takes from from the start
 * of the stream. */
MVMint64 MVM_string_decodestream_bytes_to_buf(MVMThreadContext *tc, MVMDecodeStream *ds, char **buf, MVMint32 bytes) {
    MVMint32 taken = 0;
    *buf = NULL;
    while (taken < bytes && ds->bytes_head) {
        /* Take what we can. */
        MVMDecodeStreamBytes *cur_bytes = ds->bytes_head;
        MVMint32 required  = bytes - taken;
        MVMint32 available = cur_bytes->length - ds->bytes_head_pos;
        if (available <= required) {
            /* Take everything in this buffer and remove it. */
            if (!*buf)
                *buf = MVM_malloc(cur_bytes->next ? bytes : available);
            memcpy(*buf + taken, cur_bytes->bytes + ds->bytes_head_pos, available);
            taken += available;
            ds->bytes_head = cur_bytes->next;
            ds->bytes_head_pos = 0;
            MVM_free(cur_bytes->bytes);
            MVM_free(cur_bytes);
        }
        else {
            /* Just take what we need. */
            if (!*buf)
                *buf = MVM_malloc(required);
            memcpy(*buf + taken, cur_bytes->bytes + ds->bytes_head_pos, required);
            taken += required;
            ds->bytes_head_pos += required;
        }
    }
    if (ds->bytes_head == NULL)
        ds->bytes_tail = NULL;
    ds->abs_byte_pos += taken;
    return taken;
}

/* Gets the absolute byte offset (the amount we started with plus what we've
 * chewed and handed back in decoded characters). */
MVMint64 MVM_string_decodestream_tell_bytes(MVMThreadContext *tc, const MVMDecodeStream *ds) {
    return ds->abs_byte_pos;
}

/* Checks if the decode stream is empty. */
MVMint32 MVM_string_decodestream_is_empty(MVMThreadContext *tc, MVMDecodeStream *ds) {
    return !ds->bytes_head && !ds->chars_head && MVM_unicode_normalizer_empty(tc, &(ds->norm));
}

/* Destroys a decoding stream, freeing all associated memory (including the
 * buffers). */
void MVM_string_decodestream_destory(MVMThreadContext *tc, MVMDecodeStream *ds) {
    MVMDecodeStreamBytes *cur_bytes = ds->bytes_head;
    while (cur_bytes) {
        MVMDecodeStreamBytes *next_bytes = cur_bytes->next;
        MVM_free(cur_bytes->bytes);
        MVM_free(cur_bytes);
        cur_bytes = next_bytes;
    }
    MVM_unicode_normalizer_cleanup(tc, &(ds->norm));
    MVM_free(ds->decoder_state);
    MVM_free(ds);
}

/* Sets a decode stream separator to its default value. */
void MVM_string_decode_stream_sep_default(MVMThreadContext *tc, MVMDecodeStreamSeparators *sep_spec) {
    sep_spec->num_seps = 2;
    sep_spec->sep_lengths = MVM_malloc(sep_spec->num_seps * sizeof(MVMint32));
    sep_spec->sep_graphemes = MVM_malloc(sep_spec->num_seps * sizeof(MVMGrapheme32));

    sep_spec->sep_lengths[0] = 1;
    sep_spec->sep_graphemes[0] = '\n';

    sep_spec->sep_lengths[1] = 1;
    sep_spec->sep_graphemes[1] = MVM_nfg_crlf_grapheme(tc);
}

/* Takes a string and sets it up as a decode stream separator. */
void MVM_string_decode_stream_sep_from_strings(MVMThreadContext *tc, MVMDecodeStreamSeparators *sep_spec,
                                                     MVMString **seps, MVMint32 num_seps) {
    MVMGraphemeIter gi;
    MVMint32 i, graph_length, graph_pos;

    if (num_seps > 0xFFF)
        MVM_exception_throw_adhoc(tc, "Too many line separators");

    MVM_free(sep_spec->sep_lengths);
    MVM_free(sep_spec->sep_graphemes);

    sep_spec->num_seps = num_seps;
    sep_spec->sep_lengths = MVM_malloc(num_seps * sizeof(MVMint32));
    graph_length = 0;
    for (i = 0; i < num_seps; i++) {
        MVMuint32 num_graphs = MVM_string_graphs(tc, seps[i]);
        if (num_graphs > 0xFFFF)
            MVM_exception_throw_adhoc(tc, "Line separator too long");
        sep_spec->sep_lengths[i] = num_graphs;
        graph_length += num_graphs;
    }

    sep_spec->sep_graphemes = MVM_malloc(graph_length * sizeof(MVMGrapheme32));
    graph_pos = 0;
    for (i = 0; i < num_seps; i++) {
        MVM_string_gi_init(tc, &gi, seps[i]);
        while (MVM_string_gi_has_more(tc, &gi))
            sep_spec->sep_graphemes[graph_pos++] = MVM_string_gi_get_grapheme(tc, &gi);
    }
}

/* Returns the maximum length of any separator, in graphemes. */
MVMint32 MVM_string_decode_stream_sep_max_chars(MVMThreadContext *tc, MVMDecodeStreamSeparators *sep_spec) {
    MVMint32 i;
    MVMint32 max_length = 1;
    for (i = 0; i < sep_spec->num_seps; i++)
        if (sep_spec->sep_lengths[i] > max_length)
            max_length = sep_spec->sep_lengths[i];
    return max_length;
}

/* Cleans up memory associated with a stream separator set. */
void MVM_string_decode_stream_sep_destroy(MVMThreadContext *tc, MVMDecodeStreamSeparators *sep_spec) {
    MVM_free(sep_spec->sep_lengths);
    MVM_free(sep_spec->sep_graphemes);
}
