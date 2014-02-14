#include "moar.h"

/* A decode stream represents an on-going decoding process, from bytes into
 * characters. Bytes can be contributed to the decode stream, and chars can be
 * obtained. Byte buffers and decoded char buffers are kept in linked lists.
 * Note that characters may start at the end of one byte buffer and finish in
 * the next, which is taken care of by the logic in here and the decoders
 * themselves.
 */

/* Creates a new decoding stream. */
MVMDecodeStream * MVM_string_decodestream_create(MVMThreadContext *tc, MVMint32 encoding, MVMint64 abs_byte_pos) {
    MVMDecodeStream *ds = calloc(1, sizeof(MVMDecodeStream));
    ds->encoding        = encoding;
    ds->abs_byte_pos    = abs_byte_pos;
    return ds;
}

/* Adds another byte buffer into the decoding stream. */
void MVM_string_decodestream_add_bytes(MVMThreadContext *tc, MVMDecodeStream *ds, char *bytes, MVMint32 length) {
    if (length > 0) {
        MVMDecodeStreamBytes *new_bytes = calloc(1, sizeof(MVMDecodeStreamBytes));
        new_bytes->bytes  = bytes;
        new_bytes->length = length;
        if (ds->bytes_tail)
            ds->bytes_tail->next = new_bytes;
        ds->bytes_tail = new_bytes;
        if (!ds->bytes_head)
            ds->bytes_head = new_bytes;
    }
}

/* Adds another char result buffer into the decoding stream. */
void MVM_string_decodestream_add_chars(MVMThreadContext *tc, MVMDecodeStream *ds, MVMCodepoint32 *chars, MVMint32 length) {
    MVMDecodeStreamChars *new_chars = calloc(1, sizeof(MVMDecodeStreamChars));
    new_chars->chars  = chars;
    new_chars->length = length;
    if (ds->chars_tail)
        ds->chars_tail->next = new_chars;
    ds->chars_tail = new_chars;
    if (!ds->chars_head)
        ds->chars_head = new_chars;
}

/* Throws away byte buffers no longer needed. */
void MVM_string_decodestream_discard_to(MVMThreadContext *tc, MVMDecodeStream *ds, MVMDecodeStreamBytes *bytes, MVMint32 pos) {
    while (ds->bytes_head != bytes) {
        MVMDecodeStreamBytes *discard = ds->bytes_head;
        ds->abs_byte_pos += discard->length - ds->bytes_head_pos;
        ds->bytes_head = discard->next;
        ds->bytes_head_pos = 0;
        free(discard->bytes);
        free(discard);
    }
    if (!ds->bytes_head && pos == 0)
        return;
    if (ds->bytes_head->length == pos) {
        /* We ate all of the new head buffer too; also free it. */
        MVMDecodeStreamBytes *discard = ds->bytes_head;
        ds->abs_byte_pos += discard->length - ds->bytes_head_pos;
        ds->bytes_head = discard->next;
        ds->bytes_head_pos = 0;
        free(discard->bytes);
        free(discard);
        if (ds->bytes_head == NULL)
            ds->bytes_tail = NULL;
    }
    else {
        ds->abs_byte_pos += pos - ds->bytes_head_pos;
        ds->bytes_head_pos = pos;
    }
}

/* Does a decode run, selected by encoding. */
static run_decode(MVMThreadContext *tc, MVMDecodeStream *ds, MVMint32 *stopper_chars, MVMint32 *stopper_sep) {
    switch (ds->encoding) {
    case MVM_encoding_type_utf8:
        MVM_string_utf8_decodestream(tc, ds, stopper_chars, stopper_sep);
        break;
    case MVM_encoding_type_ascii:
        MVM_string_ascii_decodestream(tc, ds, stopper_chars, stopper_sep);
        break;
    case MVM_encoding_type_latin1:
        MVM_string_latin1_decodestream(tc, ds, stopper_chars, stopper_sep);
        break;
    default:
        MVM_exception_throw_adhoc(tc, "Streaming decode NYI for encoding %d",
            (int)ds->encoding);
    }
}

/* Gets the specified number of characters. If we are not yet able to decode
 * that many, returns NULL. This may mean more input buffers are needed. */
static MVMint32 missing_chars(MVMThreadContext *tc, MVMDecodeStream *ds, MVMint32 wanted) {
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
static MVMString * take_chars(MVMThreadContext *tc, MVMDecodeStream *ds, MVMint32 chars) {
    MVMint32 found = 0;
    MVMString *result = (MVMString *)MVM_repr_alloc_init(tc, tc->instance->VMString);
    result->body.int32s = malloc(chars * sizeof(MVMCodepoint32));
    result->body.flags  = MVM_STRING_TYPE_INT32;
    result->body.graphs = chars;
    while (found < chars) {
        MVMDecodeStreamChars *cur_chars = ds->chars_head;
        MVMint32 available = cur_chars->length - ds->chars_head_pos;
        if (available <= chars - found) {
            /* We need all that's left in this buffer and likely
             * more. */
            MVMDecodeStreamChars *next_chars = cur_chars->next;
            memcpy(result->body.int32s + found, cur_chars->chars + ds->chars_head_pos,
                available * sizeof(MVMCodepoint32));
            found += available;
            free(cur_chars->chars);
            free(cur_chars);
            ds->chars_head = next_chars;
            ds->chars_head_pos = 0;
            if (ds->chars_head == NULL)
                ds->chars_tail = NULL;
            cur_chars = next_chars;
        }
        else {
            /* There's enough in this buffer to satisfy us, and we'll leave
             * some behind. */
            MVMint32 take = chars - found;
            memcpy(result->body.int32s + found, cur_chars->chars + ds->chars_head_pos,
                take * sizeof(MVMCodepoint32));
            found += take;
            ds->chars_head_pos += take;
        }
    }
    return result;
}
MVMString * MVM_string_decodestream_get_chars(MVMThreadContext *tc, MVMDecodeStream *ds, MVMint32 chars) {
    MVMint32 missing;

    /* If we request nothing, give empty string. */
    if (chars == 0) {
        MVMString *result = (MVMString *)MVM_repr_alloc_init(tc, tc->instance->VMString);
        result->body.int32s = malloc(1);
        result->body.flags  = MVM_STRING_TYPE_INT32;
        result->body.graphs = 0;
        return result;
    }

    /* If we don't already have enough chars, try and decode more. */
    missing = missing_chars(tc, ds, chars);
    if (missing)
        run_decode(tc, ds, &missing, NULL);

    /* If we've got enough, assemble a string. Otherwise, give up. */
    if (missing_chars(tc, ds, chars) == 0)
        return take_chars(tc, ds, chars);
    else
        return NULL;
}

/* Gets characters up until the specified string is encountered. If we do
 * not encounter it, returns NULL. This may mean more input buffers are needed
 * or that we reached the end of the stream. */
MVMint32 find_separator(MVMThreadContext *tc, MVMDecodeStream *ds, MVMCodepoint32 sep) {
    MVMint32 sep_loc = 0;
    MVMDecodeStreamChars *cur_chars = ds->chars_head;
    while (cur_chars) {
        MVMint32 start = cur_chars == ds->chars_head ? ds->chars_head_pos : 0;
        MVMint32 i = 0;
        for (i = start; i < cur_chars->length; i++) {
            sep_loc++;
            if (cur_chars->chars[i] == sep)
                return sep_loc;
        }
        cur_chars = cur_chars->next;
    }
    return 0;
}
MVMString * MVM_string_decodestream_get_until_sep(MVMThreadContext *tc, MVMDecodeStream *ds, MVMCodepoint32 sep) {
    MVMint32 sep_loc;

    /* Look for separator, trying more decoding if it fails. We get the place
     * just beyond the separator, so can use take_chars to get what's need. */
    sep_loc = find_separator(tc, ds, sep);
    if (!sep_loc) {
        run_decode(tc, ds, NULL, &sep);
        sep_loc = find_separator(tc, ds, sep);
    }
    if (sep_loc)
        return take_chars(tc, ds, sep_loc);
    else
        return NULL;
}

/* Decodes all the buffers, producing a string containing all the decoded
 * characters. */
MVMString * MVM_string_decodestream_get_all(MVMThreadContext *tc, MVMDecodeStream *ds) {
    MVMString *result = (MVMString *)MVM_repr_alloc_init(tc, tc->instance->VMString);

    /* Decode all the things. */
    run_decode(tc, ds, NULL, NULL);

    /* If there's no codepoint buffer, then return the empty string. */
    if (!ds->chars_head) {
        result->body.int32s = malloc(1);
        result->body.flags  = MVM_STRING_TYPE_INT32;
        result->body.graphs = 0;
    }
    
    /* If there's exactly one resulting codepoint buffer and we swallowed none
     * of it, just use it. */
    else if (ds->chars_head == ds->chars_tail && ds->chars_head_pos == 0) {
        /* Set up result string. */
        result->body.int32s = ds->chars_head->chars;
        result->body.flags  = MVM_STRING_TYPE_INT32;
        result->body.graphs = ds->chars_head->length;

        /* Don't free the buffer's memory itself, just the holder, as we
         * stole that for the buffer into the string above. */
        free(ds->chars_head);
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
        result->body.int32s = malloc(length * sizeof(MVMCodepoint32));
        result->body.flags  = MVM_STRING_TYPE_INT32;
        result->body.graphs = length;

        /* Copy all the things into the target, freeing as we go. */
        cur_chars = ds->chars_head;
        while (cur_chars) {
            if (cur_chars == ds->chars_head) {
                MVMint32 to_copy = ds->chars_head->length - ds->chars_head_pos;
                memcpy(result->body.int32s + pos, cur_chars->chars + ds->chars_head_pos,
                    cur_chars->length * sizeof(MVMCodepoint32));
                pos += to_copy;
            }
            else {
                memcpy(result->body.int32s + pos, cur_chars->chars,
                    cur_chars->length * sizeof(MVMCodepoint32));
                pos += cur_chars->length;
            }
            cur_chars = cur_chars->next;
        }
        ds->chars_head = ds->chars_tail = NULL;
    }

    return result;
}

/* Gets the absolute byte offset (the amount we started with plus what we've
 * chewed and handed back in decoded characters). */
MVMint64 MVM_string_decodestream_tell_bytes(MVMThreadContext *tc, MVMDecodeStream *ds) {
    return ds->abs_byte_pos;
}

/* Checks if the decode stream is empty. */
MVMint32 MVM_string_decodestream_is_empty(MVMThreadContext *tc, MVMDecodeStream *ds) {
    return !(ds->bytes_head || ds->chars_head);
}

/* Destroys a decoding stream, freeing all associated memory (including the
 * buffers). */
void MVM_string_decodestream_destory(MVMThreadContext *tc, MVMDecodeStream *ds) {
    MVMDecodeStreamBytes *cur_bytes = ds->bytes_head;
    while (cur_bytes) {
        MVMDecodeStreamBytes *next_bytes = cur_bytes->next;
        free(cur_bytes->bytes);
        free(cur_bytes);
        cur_bytes = next_bytes;
    }
    free(ds);
}
