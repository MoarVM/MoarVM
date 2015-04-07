/* Normalization modes. Numbers picked so that:
 *  - The LSB tells us whether to do canonical or compatibility normalization
 *  - The second bit tells us whether to do canonical normalization
 *  - The third bit tells us to go a step further and create synthetic codes.
 */ 
typedef enum {
    MVM_NORMALIZE_NFD   = 0,
    MVM_NORMALIZE_NFKD  = 1,
    MVM_NORMALIZE_NFC   = 2,
    MVM_NORMALIZE_NFKC  = 3,
    MVM_NORMALIZE_NFG   = 6
} MVMNormalization;

/* First codepoint where we have to actually do a real check and maybe some
 * work when normalizing. */
#define MVM_NORMALIZE_FIRST_SIG_NFD     0x00C0
#define MVM_NORMALIZE_FIRST_SIG_NFC     0x0300
#define MVM_NORMALIZE_FIRST_SIG_NFKD    0x00A0
#define MVM_NORMALIZE_FIRST_SIG_NFKC    0x00A0

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
};

/* Takes a codepoint to process for normalization as the "in" parameter. If we
 * are able to produce one or more normalized codepoints right off, then we
 * put it into the location pointed to by "out", and return the number of
 * codepoints now available including the one we just passed out. If we can't
 * produce a normalized codepoint right now, we return a 0. */
MVM_STATIC_INLINE MVMint32 MVM_unicode_normalizer_process_codepoint(MVMThreadContext *tc, MVMNormalizer *n, MVMCodepoint in, MVMCodepoint *out) {
    /* Fast-path when it's one-in-one-out. */
    if (n->buffer_end - n->buffer_start == 1 && in < n->first_significant) {
        if (n->buffer[n->buffer_start] < n->first_significant) {
            *out = n->buffer[n->buffer_start];
            n->buffer[n->buffer_start] = in;
            return 1;
        }
    }

    /* Fall back to slow path. */
    return MVM_unicode_normalizer_process_codepoint_full(tc, n, in, out);
}

/* TODO: grapheme version of the above. */

/* Get the number of codepoints/graphemes ready to fetch. */
MVM_STATIC_INLINE MVMint32 MVM_unicode_normalizer_available(MVMThreadContext *tc, MVMNormalizer *n) {
    return n->buffer_norm_end - n->buffer_start;
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

/* TODO: grapheme version of the above. */

/* Setup and teardown of the MVMNormalizer struct. */
MVMNormalization MVN_unicode_normalizer_form(MVMThreadContext *tc, MVMint64 form_in);
void MVM_unicode_normalizer_init(MVMThreadContext *tc, MVMNormalizer *n, MVMNormalization norm);
void MVM_unicode_normalizer_cleanup(MVMThreadContext *tc, MVMNormalizer *n);

/* High-level normalize implementation, working from an input array of
 * codepoints and producing an output array of codepoints. */
void MVM_unicode_normalize_codepoints(MVMThreadContext *tc, MVMObject *in, MVMObject *out, MVMNormalization form);

/* Guts-y functions, called by the API level ones above. */
MVMint32 MVM_unicode_normalizer_process_codepoint_full(MVMThreadContext *tc, MVMNormalizer *n, MVMCodepoint in, MVMCodepoint *out);
