#include "moar.h"

/* Number of extra elements we add to the synthetics table each time we need
 * to grow it. */
#define MVM_SYNTHETIC_GROW_ELEMS 32

/* Does a lookup in the trie for a synthetic for the specified codepoints. */
static MVMGrapheme32 lookup_synthetic(MVMThreadContext *tc, MVMCodepoint *codes, MVMint32 num_codes) {
    /* XXX TODO: actually implement the lookup. */
    return 0;
}

/* TODO: describe/implement this algorithm. */
static void add_synthetic_to_trie(MVMThreadContext *tc, MVMCodepoint *codes, MVMint32 num_codes, MVMGrapheme32 synthetic) {
}

/* Assumes that we are holding the lock that serializes updates, and already
 * checked that the synthetic does not exist. Adds it to the lookup trie and
 * synthetics table, making sure to do enough copy/free-at-safe-point work to
 * not upset other threads possibly doing concurrent reads. */
static MVMGrapheme32 add_synthetic(MVMThreadContext *tc, MVMCodepoint *codes, MVMint32 num_codes) {
    MVMNFGState     *nfg = tc->instance->nfg;
    MVMNFGSynthetic *synth;
    MVMGrapheme32    result;
    size_t           comb_size;

    /* Grow the synthetics table if needed. */
    if (nfg->num_synthetics % MVM_SYNTHETIC_GROW_ELEMS == 0) {
        size_t orig_size = nfg->num_synthetics * sizeof(MVMNFGSynthetic);
        size_t new_size  = nfg->num_synthetics * sizeof(MVMNFGSynthetic);
        MVMNFGSynthetic *new_synthetics = MVM_fixed_size_alloc(tc, tc->instance->fsa,
            (nfg->num_synthetics + MVM_SYNTHETIC_GROW_ELEMS) * sizeof(MVMNFGSynthetic));
        if (orig_size) {
            memcpy(new_synthetics, nfg->synthetics, orig_size);
            MVM_fixed_size_free_at_safepoint(tc, tc->instance->fsa, orig_size, nfg->synthetics);
        }
        nfg->synthetics = new_synthetics;
    }

    /* Set up the new synthetic entry. */
    synth            = &(nfg->synthetics[nfg->num_synthetics]);
    synth->base      = *codes;
    synth->num_combs = num_codes - 1;
    comb_size        = synth->num_combs * sizeof(MVMCodepoint);
    synth->combs     = MVM_fixed_size_alloc(tc, tc->instance->fsa, comb_size);
    memcpy(synth->combs, codes + 1, comb_size);
    synth->case_uc   = 0;
    synth->case_lc   = 0;
    synth->case_tc   = 0;
    synth->case_fc   = 0;

    /* Memory barrier to make sure the synthetic is fully in place before we
     * bump the count. */
    MVM_barrier();
    nfg->num_synthetics++;

    /* Give the synthetic an ID by negating the new number of synthetics. */
    result = -nfg->num_synthetics;

    /* Make an entry in the lookup trie for the new synthetic, so we can use
     * it in the future when seeing the same codepoint sequence. */
    add_synthetic_to_trie(tc, codes, num_codes, result);

    return result;
}

/* Does a lookup of a synthetic in the trie. If we find one, returns it. If
 * not, acquires the update lock, re-checks that we really are missing the
 * synthetic, and then adds it. */
static MVMGrapheme32 lookup_or_add_synthetic(MVMThreadContext *tc, MVMCodepoint *codes, MVMint32 num_codes) {
    MVMGrapheme32 result = lookup_synthetic(tc, codes, num_codes);
    if (!result) {
        uv_mutex_lock(&tc->instance->nfg->update_mutex);
        result = lookup_synthetic(tc, codes, num_codes);
        if (!result)
            result = add_synthetic(tc, codes, num_codes);
        uv_mutex_unlock(&tc->instance->nfg->update_mutex);
    }
    return result;
}

/* Takes one or more code points. If only one code point is passed, it is
 * returned as the grapheme. Otherwise, resolves it to a synthetic - either an
 * already existing one if we saw it before, or a new one if not.  Assumes
 * that the code points are already in NFC, and as such canonical ordering has
 * been applied. */
MVMGrapheme32 MVM_nfg_codes_to_grapheme(MVMThreadContext *tc, MVMCodepoint *codes, MVMint32 num_codes) {
    if (num_codes == 1)
        return codes[0];
    else
        return lookup_or_add_synthetic(tc, codes, num_codes);
}
