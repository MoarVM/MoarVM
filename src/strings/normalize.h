/* Normalization modes. Numbers picked so that:
 *  - The LSB tells us whether to do canonical or compatibility normalization
 *  - The second bit tells us whether to do canonical normalization
 *  - The third bit tells us to go a step further and create synthetic codes
 *    for graphemes.
 */
typedef enum {
    MVM_NORMALIZE_NFD   = 0,
    MVM_NORMALIZE_NFKD  = 1,
    MVM_NORMALIZE_NFC   = 2,
    MVM_NORMALIZE_NFKC  = 3,
    MVM_NORMALIZE_NFG   = 6
} MVMNormalization;

/* Ways of checking various properties of the normalization form. */
#define MVM_NORMALIZE_COMPAT_DECOMP(form) (form & 1)
#define MVM_NORMALIZE_COMPOSE(form)       (form & 2)
#define MVM_NORMALIZE_GRAPHEME(form)      (form & 4)

/* First codepoint where we have to actually do a real check and maybe some
 * work when normalizing. */
#define MVM_NORMALIZE_FIRST_SIG_NFD     0x00C0
#define MVM_NORMALIZE_FIRST_SIG_NFC     0x0300
#define MVM_NORMALIZE_FIRST_SIG_NFKD    0x00A0
#define MVM_NORMALIZE_FIRST_SIG_NFKC    0x00A0

/* First codepoint with a non-zero canonical combining class. */
#define MVM_NORMALIZE_FIRST_NONZERO_CCC 0x300

/* Streaming Unicode normalizer structure. */
struct MVMNormalizer {
    /* What form of normalization are we doing? */
    MVMNormalization form;

    /* Current buffer of codepoints we're working to normalize. */
    MVMCodepoint *buffer;

    /* Size of the normalization buffer. */
    MVMint32 buffer_size;

    /* Start offset in the buffer where we're still processing. */
    MVMint32 buffer_start;

    /* End offset in the buffer, and where to add the next thing to process. */
    MVMint32 buffer_end;

    /* End offset in the buffer for things we've normalized and so can return. */
    MVMint32 buffer_norm_end;

    /* The first significant codepoint in this normalization form that we may
     * have to do something with. If we see two things beneath the limit in a
     * row then we know the first one below it is good to spit out. */
    MVMCodepoint first_significant;

    /* The quickcheck property for the normalization form in question. */
    MVMint32 quick_check_property;

    /* If we should translate the \r\n grapheme to \n (only applicable when
     * normalizing to NFG). */
    MVMint32 translate_newlines;

    MVMint32 prepend_buffer;

    MVMint32 regional_indicator;

};

/* Guts-y functions, called by the API level ones below. */
MVMint32 MVM_unicode_normalizer_process_codepoint_full(MVMThreadContext *tc, MVMNormalizer *n, MVMCodepoint in, MVMCodepoint *out);
MVMint32 MVM_unicode_normalizer_process_codepoint_norm_terminator(MVMThreadContext *tc, MVMNormalizer *n, MVMCodepoint in, MVMCodepoint *out);

/* Takes a codepoint to process for normalization as the "in" parameter. If we
 * are able to produce one or more normalized codepoints right off, then we
 * put it into the location pointed to by "out", and return the number of
 * codepoints now available including the one we just passed out. If we can't
 * produce a normalized codepoint right now, we return a 0. */
MVM_STATIC_INLINE MVMint32 MVM_unicode_normalizer_process_codepoint(MVMThreadContext *tc, MVMNormalizer *n, MVMCodepoint in, MVMCodepoint *out) {
    /* Control characters in the Latin-1 range are normalization terminators -
     * that is, we know we can spit out whatever codepoints we have seen so
     * far in normalized form without having to consider them into the
     * normalization process. The exception is if we're computing NFG, and
     * we got \r, which can form a grapheme in the case of \r\n. */
    if (in < 0x20 || (0x7F <= in && in <= 0x9F) || in == 0xAD) {
        /* For utf8-c8 synthetic graphemes. May be able to be removed after
         * changing and further testing of the TODO marked below. */
        if (MVM_UNLIKELY(in < 0)) {
            if (MVM_LIKELY(MVM_nfg_get_synthetic_info(tc, in)->is_utf8_c8))
                return MVM_unicode_normalizer_process_codepoint_norm_terminator(tc, n, in, out);
            MVM_exception_throw_adhoc(tc, "Internal error: encountered non-utf8-c8 synthetic (%"PRId32") during normalization", in);
        }
        /* If in isn't \r */
        if (in != 0x0D || !MVM_NORMALIZE_GRAPHEME(n->form))
            return MVM_unicode_normalizer_process_codepoint_norm_terminator(tc, n, in, out);
    }

    /* Fast-paths apply when the codepoint to consider is too low to have any
     * interesting properties in the target normalization form AND
     * it doesn't follow a prepend character */
    if (in < n->first_significant && !n->prepend_buffer) {
        if (MVM_LIKELY(MVM_NORMALIZE_COMPOSE(n->form))) {
            /* For the composition fast path we always have to know that we've
            * seen two codepoints in a row that are below those needing a full
            * check. Then we can spit out the first one. Exception: we are
            * normalizing to graphemes and see \r. */
            if (MVM_LIKELY(in != 0x0D || !MVM_NORMALIZE_GRAPHEME(n->form))) {
                if (n->buffer_end - n->buffer_start == 1) {
                    if (n->buffer[n->buffer_start] < n->first_significant) {
                        *out = n->buffer[n->buffer_start];
                        n->buffer[n->buffer_start] = in;
                        return 1;
                    }
                }
            }
        }
        else {
            /* For decomposition fast-path, the buffer should be empty. In
             * that case, we just hand back what we got. */
            if (n->buffer_start == n->buffer_end) {
                *out = in;
                return 1;
            }
        }
    }
    /* Fall back to slow path. */
    return MVM_unicode_normalizer_process_codepoint_full(tc, n, in, out);
}

/* Grapheme version of the above. Note that this exists mostly for API clarity
 * rather than adding any semantics; the normalizer must be configured to
 * produce NFG to get synthetics out. */
MVM_STATIC_INLINE MVMint32 MVM_unicode_normalizer_process_codepoint_to_grapheme(MVMThreadContext *tc, MVMNormalizer *n, MVMCodepoint in, MVMGrapheme32 *out) {
    assert(sizeof(MVMCodepoint) == sizeof(MVMGrapheme32));
    return MVM_unicode_normalizer_process_codepoint(tc, n, in, (MVMGrapheme32 *)out);
}

/* Push a number of codepoints into the "to normalize" buffer. */
void MVM_unicode_normalizer_push_codepoints(MVMThreadContext *tc, MVMNormalizer *n, const MVMCodepoint *in, MVMint32 num_codepoints);

/* Get the number of codepoints/graphemes ready to fetch. */
MVM_STATIC_INLINE MVMint32 MVM_unicode_normalizer_available(MVMThreadContext *tc, MVMNormalizer *n) {
    return n->buffer_norm_end - n->buffer_start;
}

/* Get the number of codepoints/graphemes ready to fetch. */
MVM_STATIC_INLINE MVMint32 MVM_unicode_normalizer_empty(MVMThreadContext *tc, MVMNormalizer *n) {
    return n->buffer_end == n->buffer_start;
}

/* Indicate that we've reached the end of the input stream. Any codepoints
 * left to normalize now can be. */
void MVM_unicode_normalizer_eof(MVMThreadContext *tc, MVMNormalizer *n);

/* Get a normalized codepoint; should only ever be called if there are some
 * known to be available, either because normalize_to_codepoint returned a
 * value greater than 1, or normalize_available returned a non-zero value. */
MVM_STATIC_INLINE MVMCodepoint MVM_unicode_normalizer_get_codepoint(MVMThreadContext *tc, MVMNormalizer *n) {
    if (n->buffer_norm_end == n->buffer_start)
        MVM_exception_throw_adhoc(tc, "Normalization: illegal call to get codepoint");
    return n->buffer[n->buffer_start++];
}

/* Grapheme version of the above. Note that this exists mostly for API clarity
 * rather than adding any semantics; the normalizer must be configured to
 * produce NFG to get synthetics out. */
MVM_STATIC_INLINE MVMGrapheme32 MVM_unicode_normalizer_get_grapheme(MVMThreadContext *tc, MVMNormalizer *n) {
    assert(sizeof(MVMCodepoint) == sizeof(MVMGrapheme32));
    if (n->buffer_norm_end == n->buffer_start)
        MVM_exception_throw_adhoc(tc, "Normalization: illegal call to get grapheme");
    return (MVMGrapheme32)n->buffer[n->buffer_start++];
}

/* Setup and teardown of the MVMNormalizer struct. */
MVMNormalization MVM_unicode_normalizer_form(MVMThreadContext *tc, MVMint64 form_in);
void MVM_unicode_normalizer_init(MVMThreadContext *tc, MVMNormalizer *n, MVMNormalization norm);
void MVM_unicode_normalizer_translate_newlines(MVMThreadContext *tc, MVMNormalizer *n);
void MVM_unicode_normalizer_cleanup(MVMThreadContext *tc, MVMNormalizer *n);

/* High-level normalize implementation, working from an input array of
 * codepoints and producing an output array of codepoints. */
void MVM_unicode_normalize_codepoints(MVMThreadContext *tc, const MVMObject *in, MVMObject *out, MVMNormalization form);

/* High-level function to produces an NFG string from an input array of
 * codepoints. */
MVMString * MVM_unicode_codepoints_to_nfg_string(MVMThreadContext *tc,const MVMObject *codes);
MVMString * MVM_unicode_codepoints_c_array_to_nfg_string(MVMThreadContext *tc, MVMCodepoint * cp_v, MVMint64 cp_count);

/* High-level function to produce an array of codepoints from a string. */
void MVM_unicode_string_to_codepoints(MVMThreadContext *tc, MVMString *s, MVMNormalization form, MVMObject *out);

/* faster atoi function */
MVM_STATIC_INLINE MVMint32 fast_atoi( const char * dec_str ) {
    MVMint32 value = 0;
    while( *dec_str ) {
        value = value*10 + (*dec_str++ - '0');
    }
    return value;
}
MVMint64 MVM_unicode_relative_ccc(MVMThreadContext *tc, MVMCodepoint cp);
MVMint32 MVM_unicode_normalize_should_break(MVMThreadContext *tc, MVMCodepoint a, MVMCodepoint b, MVMNormalizer *norm);
MVMint64 MVM_unicode_relative_ccc(MVMThreadContext *tc, MVMCodepoint cp);
MVMint32 MVM_string_is_control_full(MVMThreadContext *tc, MVMCodepoint in);
/* Function for choosing the appropriate line-ending grapheme depending on if
 * newline translation is enabled. */
MVM_STATIC_INLINE MVMGrapheme32 MVM_unicode_normalizer_translated_crlf(MVMThreadContext *tc, MVMNormalizer *n) {
    return n->translate_newlines
        ? '\n'
        : MVM_nfg_crlf_grapheme(tc);
}
