/* Represents a bytes => chars decoding stream. */
struct MVMDecodeStream {
    /* Head and tail of the input byte buffers. */
    MVMDecodeStreamBytes *bytes_head;
    MVMDecodeStreamBytes *bytes_tail;

    /* Head and tail of the output char buffers. */
    MVMDecodeStreamChars *chars_head;
    MVMDecodeStreamChars *chars_tail;

    /* Often, when reading lines or chunks, we'll fill up one char buffer
     * and then immediately take it. That results in a lot of allocating
     * and freeing of MVMDecodeStreamChars structures. Keeping a free one
     * avoids this. (There's not really a common steady state where we
     * have multiple free ones, so a free isn't worth the extra work.) */
    MVMDecodeStreamChars *chars_reuse;

    /* The byte position (for tell). */
    MVMint64 abs_byte_pos;

    /* How far we've eaten into the current head bytes buffer. */
    MVMint32 bytes_head_pos;

    /* How far we've eaten into the current head char buffer. */
    MVMint32 chars_head_pos;

    /* The encoding we're using. */
    MVMint32 encoding;

    /* Suggestion for decoders of how many bytes to guess at when allocating
     * decoded result buffers. */
    MVMint32 result_size_guess;

    /* Normalizer. */
    MVMNormalizer norm;

    /* Optional place for the decoder to keep any extra state it needs between
     * decode calls. Will be freed when the decode stream is destroyed. */
    void *decoder_state;

    /* Stores a replacement which is used upon encountering undecodable characters.
     * Set to NULL if a replacement is not desired. */
    MVMString *replacement;

    /* Currently stores only whether or not the decoder should decode strictly or
     * permissively. Set to 1 for permissive decoding, default is strict */
    MVMuint32 config;
};

/* A single bunch of bytes added to a decode stream, with a link to the next
 * one, if any. */
struct MVMDecodeStreamBytes {
    MVMuint8             *bytes;
    MVMint32              length;
    MVMDecodeStreamBytes *next;
};

/* A bunch of characters already decoded, with a link to the next bunch. */
struct MVMDecodeStreamChars {
    MVMGrapheme32        *chars;
    MVMint32              length;
    MVMDecodeStreamChars *next;
};

/* For situations where we need to decode up to some separators, this data
 * structure holds the information about them. */
struct MVMDecodeStreamSeparators {
    /* The lengths of the separators, in graphemes. */
    MVMint32 *sep_lengths;

    /* The grapehemes themselves, in a single array (use sep_lengths to find
     * out how many there are in each separator). */
    MVMGrapheme32 *sep_graphemes;

    /* The number of separators we have. */
    MVMint32 num_seps;

    /* Cached maximum separator length, to save regular recalculation. */
    MVMint32 max_sep_length;

    /* Cached final graphemes, for quick lookups in stream_maybe_sep. */
    MVMGrapheme32 *final_graphemes;

    /* Since separators are most often control chars, we can quickly filter
     * out many graphemes without a separator search by keeping around the
     * maximum codepoint/synthetic index of any final grapheme and doing a
     * quick comparison. */
    MVMGrapheme32 max_final_grapheme;
};

/* Checks if we may have encountered one of the separators. This just looks to
 * see if we hit the final grapheme of any of the separators, which is all we
 * demand the actual encodings themselves work out (multi-grapheme separators
 * are handled in the decode stream logic itself). */
MVM_STATIC_INLINE MVMint32 MVM_string_decode_stream_maybe_sep(MVMThreadContext *tc, MVMDecodeStreamSeparators *sep_spec, MVMGrapheme32 g) {
    if (sep_spec && g <= sep_spec->max_final_grapheme) {
        MVMint32 i;
        for (i = 0; i < sep_spec->num_seps; i++)
            if (sep_spec->final_graphemes[i] == g)
                return 1;
    }
    return 0;
}

MVMDecodeStream * MVM_string_decodestream_create(MVMThreadContext *tc, MVMint32 encoding, MVMint64 abs_byte_pos, MVMint32 translate_newlines);
void MVM_string_decodestream_add_bytes(MVMThreadContext *tc, MVMDecodeStream *ds, MVMuint8 *bytes, MVMint32 length);
void MVM_string_decodestream_add_chars(MVMThreadContext *tc, MVMDecodeStream *ds, MVMGrapheme32 *chars, MVMint32 length);
void MVM_string_decodestream_discard_to(MVMThreadContext *tc, MVMDecodeStream *ds, const MVMDecodeStreamBytes *bytes, MVMint32 pos);
MVMString * MVM_string_decodestream_get_chars(MVMThreadContext *tc, MVMDecodeStream *ds, MVMint32 chars, MVMint64 eof);
MVMString * MVM_string_decodestream_get_until_sep(MVMThreadContext *tc, MVMDecodeStream *ds, MVMDecodeStreamSeparators *seps, MVMint32 chomp);
MVMString * MVM_string_decodestream_get_until_sep_eof(MVMThreadContext *tc, MVMDecodeStream *ds, MVMDecodeStreamSeparators *sep_spec, MVMint32 chomp);
MVMString * MVM_string_decodestream_get_all(MVMThreadContext *tc, MVMDecodeStream *ds);
MVMString * MVM_string_decodestream_get_available(MVMThreadContext *tc, MVMDecodeStream *ds);
MVMint64 MVM_string_decodestream_have_bytes(MVMThreadContext *tc, const MVMDecodeStream *ds, MVMint32 bytes);
MVMint64 MVM_string_decodestream_bytes_available(MVMThreadContext *tc, const MVMDecodeStream *ds);
MVMint64 MVM_string_decodestream_bytes_to_buf(MVMThreadContext *tc, MVMDecodeStream *ds, MVMuint8 **buf, MVMint32 bytes);
MVMint64 MVM_string_decodestream_tell_bytes(MVMThreadContext *tc, const MVMDecodeStream *ds);
MVMint32 MVM_string_decodestream_is_empty(MVMThreadContext *tc, MVMDecodeStream *ds);
void MVM_string_decodestream_destroy(MVMThreadContext *tc, MVMDecodeStream *ds);
void MVM_string_decode_stream_sep_default(MVMThreadContext *tc, MVMDecodeStreamSeparators *sep_spec);
void MVM_string_decode_stream_sep_from_strings(MVMThreadContext *tc, MVMDecodeStreamSeparators *sep_spec, MVMString **seps, MVMint32 num_seps);
void MVM_string_decode_stream_sep_destroy(MVMThreadContext *tc, MVMDecodeStreamSeparators *sep_spec);
