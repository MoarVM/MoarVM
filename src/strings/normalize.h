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

/* Streaming Unicode normalizer structure. */
struct MVMNormalizer {
    /* What form of normalization are we doing? */
    MVMNormalization form;

    /* TODO: much more here. */
};

/* Takes a codepoint to process for normalization as the "in" parameter. If we
 * are able to produce one or more normalized codepoints right off, then we
 * put it into the location pointed to by "out", and return the number of
 * codepoints now available including the one we just passed out. If we can't
 * produce a normalized codepoint right now, we return a 0. */
MVM_STATIC_INLINE MVMint32 MVM_unicode_normalizer_process_codepoint(MVMThreadContext *tc, MVMNormalizer *n, MVMCodepoint in, MVMCodepoint *out) {
    /* TODO: Implement normalization! */
    *out = in;
    return 1;
}

/* TODO: grapheme version of the above. */

/* Get the number of codepoints/graphemes ready to fetch. */
MVM_STATIC_INLINE MVMint32 MVM_unicode_normalizer_available(MVMThreadContext *tc, MVMNormalizer *n) {
    return 0;
}
    
/* Indicate that we've reached the end of the input stream. Any codepoints
 * left to normalize now can be. */
void MVM_unicode_normalizer_eof(MVMThreadContext *tc, MVMNormalizer *n);

/* Get a normalized codepoint; should only ever be called if there are some
 * known to be available, either because normalize_to_codepoint returned a
 * value greater than 1, or normalize_available returned a non-zero value. */
MVM_STATIC_INLINE MVMCodepoint MVM_unicode_normalizer_get_codepoint(MVMThreadContext *tc, MVMNormalizer *n) {
    MVM_exception_throw_adhoc(tc, "Normalization: illegal call to get codepoint");
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
/* TODO: many of these. */
