#include "moar.h"

/* Decodes the specified number of bytes of ASCII into an NFG string, creating
 * a result of the specified type. The type must have the MVMString REPR. */
MVMString * MVM_string_ascii_decode(MVMThreadContext *tc, MVMObject *result_type, const char *ascii, size_t bytes) {
    MVMString *result = (MVMString *)REPR(result_type)->allocate(tc, STABLE(result_type));
    size_t i;

    /* There's no combining chars and such stuff in ASCII, so the grapheme count
     * is trivially the same as the buffer length. */
    result->body.num_graphs = bytes;

    /* Allocate grapheme buffer and decode the ASCII string. */
    result->body.storage_type       = MVM_STRING_GRAPHEME_ASCII;
    result->body.storage.blob_ascii = MVM_malloc(bytes);
    for (i = 0; i < bytes; i++)
        if (ascii[i] >= 0)
            result->body.storage.blob_ascii[i] = ascii[i];
        else
            MVM_exception_throw_adhoc(tc,
                "Will not decode invalid ASCII (code point < 0 found)");

    return result;
}

/* Decodes a NULL-terminated ASCII string into an NFG string, creating
 * a result of the specified type. The type must have the MVMString REPR. */
MVMString * MVM_string_ascii_decode_nt(MVMThreadContext *tc, MVMObject *result_type, const char *ascii) {
    return MVM_string_ascii_decode(tc, result_type, ascii, strlen(ascii));
}

/* Decodes using a decodestream. Decodes as far as it can with the input
 * buffers, or until a stopper is reached. */
void MVM_string_ascii_decodestream(MVMThreadContext *tc, MVMDecodeStream *ds,
                                   MVMint32 *stopper_chars, MVMint32 *stopper_sep) {
    MVMint32              count = 0, total = 0;
    MVMint32              bufsize;
    MVMGrapheme32        *buffer;
    MVMDecodeStreamBytes *cur_bytes;
    MVMDecodeStreamBytes *last_accept_bytes = ds->bytes_head;
    MVMint32 last_accept_pos;

    /* If there's no buffers, we're done. */
    if (!ds->bytes_head)
        return;
    last_accept_pos = ds->bytes_head_pos;

    /* If we're asked for zero chars, also done. */
    if (stopper_chars && *stopper_chars == 0)
        return;

    /* Take length of head buffer as initial guess. */
    bufsize = ds->bytes_head->length;
    buffer = MVM_malloc(bufsize * sizeof(MVMGrapheme32));

    /* Decode each of the buffers. */
    cur_bytes = ds->bytes_head;
    while (cur_bytes) {
        /* Process this buffer. */
        MVMint32  pos   = cur_bytes == ds->bytes_head ? ds->bytes_head_pos : 0;
        char     *bytes = cur_bytes->bytes;
        while (pos < cur_bytes->length) {
            MVMGrapheme32 codepoint = bytes[pos++];
            if (codepoint > 127)
                MVM_exception_throw_adhoc(tc,
                    "Will not decode invalid ASCII (code point > 127 found)");
            if (count == bufsize) {
                /* We filled the buffer. Attach this one to the buffers
                 * linked list, and continue with a new one. */
                MVM_string_decodestream_add_chars(tc, ds, buffer, bufsize);
                buffer = MVM_malloc(bufsize * sizeof(MVMGrapheme32));
                count = 0;
            }
            buffer[count++] = codepoint;
            last_accept_bytes = cur_bytes;
            last_accept_pos = pos;
            total++;
            if (stopper_chars && *stopper_chars == total)
                goto done;
            if (stopper_sep && *stopper_sep == codepoint)
                goto done;
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
}

/* Encodes the specified substring to ASCII. Anything outside of ASCII range
 * will become a ?. The result string is NULL terminated, but the specified
 * size is the non-null part. */
char * MVM_string_ascii_encode_substr(MVMThreadContext *tc, MVMString *str, MVMuint64 *output_size, MVMint64 start, MVMint64 length) {
    /* ASCII is a single byte encoding, so each grapheme will just become
     * a single byte. */
    MVMuint32      startu    = (MVMuint32)start;
    MVMStringIndex strgraphs = MVM_string_graphs(tc, str);
    MVMuint32      lengthu   = (MVMuint32)(length == -1 ? strgraphs - startu : length);
    MVMuint8      *result;

    /* must check start first since it's used in the length check */
    if (start < 0 || start > strgraphs)
        MVM_exception_throw_adhoc(tc, "start out of range");
    if (length < -1 || start + lengthu > strgraphs)
        MVM_exception_throw_adhoc(tc, "length out of range");

    result = MVM_malloc(lengthu + 1);
    if (str->body.storage_type == MVM_STRING_GRAPHEME_ASCII) {
        /* No encoding needed; directly copy. */
        memcpy(result, str->body.storage.blob_ascii, lengthu);
        result[lengthu] = 0;
    }
    else {
        MVMuint32 i = 0;
        MVMCodepointIter ci;
        MVM_string_ci_init(tc, &ci, str);
        while (MVM_string_ci_has_more(tc, &ci)) {
            MVMCodepoint ord = MVM_string_ci_get_codepoint(tc, &ci);
            if (ord >= 0 && ord <= 127)
                result[i] = (MVMuint8)ord;
            else
                result[i] = '?';
            i++;
        }
        result[i] = 0;
    }

    if (output_size)
        *output_size = lengthu;

    return (char *)result;
}

/* Encodes the specified string to ASCII.  */
char * MVM_string_ascii_encode(MVMThreadContext *tc, MVMString *str, MVMuint64 *output_size) {
    return MVM_string_ascii_encode_substr(tc, str, output_size, 0,
        MVM_string_graphs(tc, str));
}

/* Encodes the specified string to ASCII not returning length.  */
char * MVM_string_ascii_encode_any(MVMThreadContext *tc, MVMString *str) {
    MVMuint64 output_size;
    return MVM_string_ascii_encode(tc, str, &output_size);
}
