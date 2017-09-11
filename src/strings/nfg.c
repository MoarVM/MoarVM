#include "moar.h"

/* Number of extra elements we add to the synthetics table each time we need
 * to grow it. */
#define MVM_SYNTHETIC_GROW_ELEMS 32

/* Finds the index of a given codepoint within a trie node. Returns it if
 * there is one, or negative if there is not (note 0 is a valid index). */
static MVMint32 find_child_node_idx(MVMThreadContext *tc, const MVMNFGTrieNode *node, MVMCodepoint cp) {
    if (node) {
        /* TODO: update this to do a binary search later on. */
        MVMint32 i;
        for (i = 0; i < node->num_entries; i++)
            if (node->next_codes[i].code == cp)
                return i;
    }
    return -1;
}

/* Does a lookup in the trie for a synthetic for the specified codepoints. */
MVMNFGTrieNode * find_child_node(MVMThreadContext *tc, const MVMNFGTrieNode *node, MVMCodepoint cp) {
    MVMint32 idx = find_child_node_idx(tc, node, cp);
    return idx >= 0 ? node->next_codes[idx].node : NULL;
}
static MVMGrapheme32 lookup_synthetic(MVMThreadContext *tc, MVMCodepoint *codes, MVMint32 num_codes) {
    MVMNFGTrieNode *cur_node        = tc->instance->nfg->grapheme_lookup;
    MVMCodepoint   *cur_code        = codes;
    MVMint32        codes_remaining = num_codes;
    while (cur_node && codes_remaining) {
        cur_node = find_child_node(tc, cur_node, *cur_code);
        cur_code++;
        codes_remaining--;
    }
    return cur_node ? cur_node->graph : 0;
}

/* Recursive algorithm to add to the trie. Descends existing trie nodes so far
 * as we have them following the code points, then passes on a NULL for the
 * levels of current below that do not exist. Once we bottom out, makes a copy
 * of or creates a node for the synthetic. As we walk back up we create or
 * copy+tweak nodes until we have produced a new trie, re-using what we can of
 * the existing one. */
static MVMNFGTrieNode * twiddle_trie_node(MVMThreadContext *tc, MVMNFGTrieNode *current, MVMCodepoint *cur_code, MVMint32 codes_remaining, MVMGrapheme32 synthetic) {
    /* Make a new empty node, which we'll maybe copy some things from the
     * current node into. */
    MVMNFGTrieNode *new_node = MVM_fixed_size_alloc(tc, tc->instance->fsa, sizeof(MVMNFGTrieNode));

    /* If we've more codes remaining... */
    if (codes_remaining > 0) {
        /* Recurse, to get a new child node. */
        MVMint32 idx = find_child_node_idx(tc, current, *cur_code);
        MVMNFGTrieNode *new_child = twiddle_trie_node(tc,
            idx >= 0 ? current->next_codes[idx].node : NULL,
            cur_code + 1, codes_remaining - 1, synthetic);

        /* If we had an existing child node... */
        if (idx >= 0) {
            /* Make a copy of the next_codes list. */
            size_t the_size = current->num_entries * sizeof(MVMNFGTrieNodeEntry);
            MVMNFGTrieNodeEntry *new_next_codes = MVM_fixed_size_alloc(tc,
                tc->instance->fsa, the_size);
            memcpy(new_next_codes, current->next_codes, the_size);

            /* Update the copy to point to the new child. */
            new_next_codes[idx].node = new_child;

            /* Install the new next_codes list in the new node, and free the
             * existing child list at the next safe point. */
            new_node->num_entries = current->num_entries;
            new_node->next_codes  = new_next_codes;
            MVM_fixed_size_free_at_safepoint(tc, tc->instance->fsa, the_size,
                current->next_codes);
        }

        /* Otherwise, we're going to need to insert the new child into a
         * (possibly existing) child list. */
        else {
            /* Calculate new child node list size and allocate it. */
            MVMint32 orig_entries = current ? current->num_entries : 0;
            MVMint32 new_entries  = orig_entries + 1;
            size_t new_size       = new_entries * sizeof(MVMNFGTrieNodeEntry);
            MVMNFGTrieNodeEntry *new_next_codes = MVM_fixed_size_alloc(tc,
                tc->instance->fsa, new_size);

            /* Go through original entries, copying those that are for a lower
             * code point than the one we're inserting a child for. */
            MVMint32 insert_pos = 0;
            MVMint32 orig_pos   = 0;
            while (orig_pos < orig_entries && current->next_codes[orig_pos].code < *cur_code)
                new_next_codes[insert_pos++] = current->next_codes[orig_pos++];

            /* Insert the new child. */
            new_next_codes[insert_pos].code = *cur_code;
            new_next_codes[insert_pos].node = new_child;
            insert_pos++;

            /* Copy the rest. */
            while (orig_pos < orig_entries)
                new_next_codes[insert_pos++] = current->next_codes[orig_pos++];

            /* Install the new next_codes list in the new node, and free any
             * existing child list at the next safe point. */
            new_node->num_entries = new_entries;
            new_node->next_codes  = new_next_codes;
            if (orig_entries)
                MVM_fixed_size_free_at_safepoint(tc, tc->instance->fsa,
                    orig_entries * sizeof(MVMNFGTrieNodeEntry),
                    current->next_codes);
        }

        /* Always need to copy synthetic set on the existing node also;
         * otherwise make sure to clear it. */
        new_node->graph = current ? current->graph : 0;
    }

    /* Otherwise, we reached the point where we need to install the synthetic.
     * If we already had a node here, we re-use the children of it. */
    else {
        new_node->graph = synthetic;
        if (current) {
            new_node->num_entries = current->num_entries;
            new_node->next_codes  = current->next_codes;
        }
        else {
            new_node->num_entries = 0;
            new_node->next_codes  = NULL;
        }
    }

    /* Free any existing node at next safe point, return the new one. */
    if (current)
        MVM_fixed_size_free_at_safepoint(tc, tc->instance->fsa,
            sizeof(MVMNFGTrieNode), current);
    return new_node;
}
static void add_synthetic_to_trie(MVMThreadContext *tc, MVMCodepoint *codes, MVMint32 num_codes, MVMGrapheme32 synthetic) {
    MVMNFGState    *nfg      = tc->instance->nfg;
    MVMNFGTrieNode *new_trie = twiddle_trie_node(tc, nfg->grapheme_lookup, codes, num_codes, synthetic);
    MVM_barrier();
    nfg->grapheme_lookup = new_trie;
}

/* Assumes that we are holding the lock that serializes updates, and already
 * checked that the synthetic does not exist. Adds it to the lookup trie and
 * synthetics table, making sure to do enough copy/free-at-safe-point work to
 * not upset other threads possibly doing concurrent reads. */
static MVMGrapheme32 add_synthetic(MVMThreadContext *tc, MVMCodepoint *codes, MVMint32 num_codes, MVMint32 utf8_c8) {
    MVMNFGState     *nfg = tc->instance->nfg;
    MVMNFGSynthetic *synth;
    MVMGrapheme32    result;

    /* Grow the synthetics table if needed. */
    if (nfg->num_synthetics % MVM_SYNTHETIC_GROW_ELEMS == 0) {
        size_t orig_size = nfg->num_synthetics * sizeof(MVMNFGSynthetic);
        size_t new_size  = (nfg->num_synthetics + MVM_SYNTHETIC_GROW_ELEMS) * sizeof(MVMNFGSynthetic);
        MVMNFGSynthetic *new_synthetics = MVM_fixed_size_alloc(tc, tc->instance->fsa, new_size);
        if (orig_size) {
            memcpy(new_synthetics, nfg->synthetics, orig_size);
            MVM_fixed_size_free_at_safepoint(tc, tc->instance->fsa, orig_size, nfg->synthetics);
        }
        nfg->synthetics = new_synthetics;
    }

    /* Set up the new synthetic entry. */
    synth            = &(nfg->synthetics[nfg->num_synthetics]);
    synth->num_codes = num_codes;
    /* Find which codepoint is the base codepoint. It is always index 0 unless
     * there are Prepend codepoints */
    if (!utf8_c8) {
        MVMint64 i = 0;
        while (i < num_codes
            && MVM_unicode_codepoint_get_property_int(tc, codes[i], MVM_UNICODE_PROPERTY_GRAPHEME_CLUSTER_BREAK)
            == MVM_UNICODE_PVALUE_GCB_PREPEND)
        {
            i++;
        }
        /* If all the codepoints were prepend then we need to set it to 0 */
        synth->base_index = num_codes == i ? 0 : i;
    }
    else {
        synth->base_index = 0;
    }

    synth->codes     = MVM_fixed_size_alloc(tc, tc->instance->fsa,
        num_codes * sizeof(MVMCodepoint));
    memcpy(synth->codes, codes, (synth->num_codes * sizeof(MVMCodepoint)));
    synth->case_uc    = 0;
    synth->case_lc    = 0;
    synth->case_tc    = 0;
    synth->case_fc    = 0;
    synth->is_utf8_c8 = utf8_c8;

    /* Memory barrier to make sure the synthetic is fully in place before we
     * bump the count. */
    MVM_barrier();
    nfg->num_synthetics++;

    /* Give the synthetic an ID by negating the new number of synthetics. */
    result = -(nfg->num_synthetics);

    /* Make an entry in the lookup trie for the new synthetic, so we can use
     * it in the future when seeing the same codepoint sequence. */
    add_synthetic_to_trie(tc, codes, num_codes, result);

    return result;
}

/* Does a lookup of a synthetic in the trie. If we find one, returns it. If
 * not, acquires the update lock, re-checks that we really are missing the
 * synthetic, and then adds it. */
static MVMGrapheme32 lookup_or_add_synthetic(MVMThreadContext *tc, MVMCodepoint *codes, MVMint32 num_codes, MVMint32 utf8_c8) {
    MVMGrapheme32 result = lookup_synthetic(tc, codes, num_codes);
    if (!result) {
        uv_mutex_lock(&tc->instance->nfg->update_mutex);
        result = lookup_synthetic(tc, codes, num_codes);
        if (!result)
            result = add_synthetic(tc, codes, num_codes, utf8_c8);
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
    else if (num_codes < MVM_GRAPHEME_MAX_CODEPOINTS)
        return lookup_or_add_synthetic(tc, codes, num_codes, 0);
    else
        MVM_exception_throw_adhoc(tc, "Too many codepoints (%d) in grapheme", num_codes);
}

/* Does the same as MVM_nfg_codes_to_grapheme, but flags the added grapheme as
 * being an UTF8-C8 synthetic. */
MVMGrapheme32 MVM_nfg_codes_to_grapheme_utf8_c8(MVMThreadContext *tc, MVMCodepoint *codes, MVMint32 num_codes) {
    if (num_codes == 1)
        return codes[0];
    else
        return lookup_or_add_synthetic(tc, codes, num_codes, 1);
}

/* Gets the \r\n synthetic. */
MVMGrapheme32 MVM_nfg_crlf_grapheme(MVMThreadContext *tc) {
    return tc->instance->nfg->crlf_grapheme;
}

/* Does a lookup of information held about a synthetic. The synth parameter
 * must be a synthetic codepoint (that is, negative). The memory returned is
 * not to be freed by the caller; it also is only valid until the next GC
 * safe point. */
MVMNFGSynthetic * MVM_nfg_get_synthetic_info(MVMThreadContext *tc, MVMGrapheme32 synth) {
    MVMNFGState *nfg       = tc->instance->nfg;
    MVMint32     synth_idx = -synth - 1;
    if (synth >= 0)
        MVM_oops(tc, "MVM_nfg_get_synthetic_info illegally called on a non-synthetic codepoint.\nRequested codepoint %i.", synth);
    if (synth_idx >= nfg->num_synthetics)
        MVM_oops(tc, "MVM_nfg_get_synthetic_info call requested a synthetic codepoint that does not exist.\nRequested synthetic %i when only %i have been created.", -synth, nfg->num_synthetics);
    return &(nfg->synthetics[synth_idx]);
}

/* Gets the cached case change if we already computed it, or computes it if
 * this is the first time we're using it. */
static MVMGrapheme32 CASE_UNCHANGED[1] = {0};
static void compute_case_change(MVMThreadContext *tc, MVMGrapheme32 synth, MVMNFGSynthetic *synth_info, MVMint32 case_) {
    MVMGrapheme32 *result;
    MVMint32 num_result_graphs;

    /* Transform the base character. */
    const MVMCodepoint *result_cps;
    MVMuint32     num_result_cps = MVM_unicode_get_case_change(tc, synth_info->codes[0],
        case_, &result_cps);
    if (num_result_cps == 0 || *result_cps == synth_info->codes[0]) {
        /* Base character does not change, so grapheme stays the same. We
         * install a non-null sentinel for this case, and set the result
         * grapheme count to zero, which indicates no change. */
        result = CASE_UNCHANGED;
        num_result_graphs = 0;
    }
    else {
        /* We can potentially get multiple graphemes back. We may also get
         * into situations where we case change the base and suddenly we
         * can normalize the whole thing to a non-synthetic. So, we take
         * a trip through the normalizer. Note we push the first thing
         * we get back from the case change, then our combiners, and
         * finally anything else the case change produced. This should
         * do about the right thing for both case changes that produce a
         * base and a combiner, and those that produce a base and a base,
         * since the normalizer applies Unicode canonical sorting. */
        MVMNormalizer norm;
        MVMint32 i;
        MVM_unicode_normalizer_init(tc, &norm, MVM_NORMALIZE_NFG);
        MVM_unicode_normalizer_push_codepoints(tc, &norm, result_cps, 1);
        MVM_unicode_normalizer_push_codepoints(tc, &norm, synth_info->codes + 1,
            synth_info->num_codes - 1);
        if (num_result_cps > 1)
            MVM_unicode_normalizer_push_codepoints(tc, &norm, result_cps + 1,
                num_result_cps - 1);
        MVM_unicode_normalizer_eof(tc, &norm);

        num_result_graphs = MVM_unicode_normalizer_available(tc, &norm);
        result = MVM_malloc(num_result_graphs * sizeof(MVMGrapheme32));
        for (i = 0; i < num_result_graphs; i++)
            result[i] = MVM_unicode_normalizer_get_grapheme(tc, &norm);
        MVM_unicode_normalizer_cleanup(tc, &norm);
    }

    switch (case_) {
    case MVM_unicode_case_change_type_upper:
        synth_info->case_uc = result;
        synth_info->case_uc_graphs = num_result_graphs;
        break;
    case MVM_unicode_case_change_type_lower:
        synth_info->case_lc = result;
        synth_info->case_lc_graphs = num_result_graphs;
        break;
    case MVM_unicode_case_change_type_title:
        synth_info->case_tc = result;
        synth_info->case_tc_graphs = num_result_graphs;
        break;
    case MVM_unicode_case_change_type_fold:
        synth_info->case_fc = result;
        synth_info->case_fc_graphs = num_result_graphs;
        break;
    default:
        MVM_panic(1, "NFG: invalid case change %d", case_);
    }
}
MVMuint32 MVM_nfg_get_case_change(MVMThreadContext *tc, MVMGrapheme32 synth, MVMint32 case_, MVMGrapheme32 **result) {
    MVMNFGSynthetic *synth_info = MVM_nfg_get_synthetic_info(tc, synth);
    switch (case_) {
    case MVM_unicode_case_change_type_upper:
        if (!synth_info->case_uc)
            compute_case_change(tc, synth, synth_info, case_);
        *result = synth_info->case_uc;
        return synth_info->case_uc_graphs;
    case MVM_unicode_case_change_type_lower:
        if (!synth_info->case_lc)
            compute_case_change(tc, synth, synth_info, case_);
        *result = synth_info->case_lc;
        return synth_info->case_lc_graphs;
    case MVM_unicode_case_change_type_title:
        if (!synth_info->case_tc)
            compute_case_change(tc, synth, synth_info, case_);
        *result = synth_info->case_tc;
        return synth_info->case_tc_graphs;
    case MVM_unicode_case_change_type_fold:
        if (!synth_info->case_fc)
            compute_case_change(tc, synth, synth_info, case_);
        *result = synth_info->case_fc;
        return synth_info->case_fc_graphs;
    default:
        MVM_panic(1, "NFG: invalid case change %d", case_);
    }
}

MVM_STATIC_INLINE MVMint32 passes_quickcheck_and_zero_ccc(MVMThreadContext *tc, MVMCodepoint cp) {
    return MVM_unicode_codepoint_get_property_int(tc, cp, MVM_UNICODE_PROPERTY_NFG_QC)
    &&     MVM_unicode_codepoint_get_property_int(tc, cp,
               MVM_UNICODE_PROPERTY_CANONICAL_COMBINING_CLASS) <= MVM_UNICODE_PVALUE_CCC_0;
}
/* Returns true for cps with Grapheme_Cluster_Break = Control */
MVM_STATIC_INLINE MVMint32 codepoint_GCB_Control (MVMThreadContext *tc, MVMCodepoint codepoint) {
    return MVM_unicode_codepoint_get_property_int(tc, codepoint,
        MVM_UNICODE_PROPERTY_GRAPHEME_CLUSTER_BREAK)
    ==  MVM_UNICODE_PVALUE_GCB_CONTROL;
}
/* Returns non-zero if the result of concatenating the two strings will freely
 * leave us in NFG without any further effort. */
MVMint32 MVM_nfg_is_concat_stable(MVMThreadContext *tc, MVMString *a, MVMString *b) {
    MVMGrapheme32 last_a;
    MVMGrapheme32 first_b;
    MVMGrapheme32 crlf;

    /* If either string is empty, we're good. */
    if (a->body.num_graphs == 0 || b->body.num_graphs == 0)
        return 1;

    /* Get first and last graphemes of the strings. */
    last_a = MVM_string_get_grapheme_at_nocheck(tc, a, a->body.num_graphs - 1);
    first_b = MVM_string_get_grapheme_at_nocheck(tc, b, 0);
    /* Put the case where we are adding a lf or crlf line ending */
    if (first_b == '\n')
        /* If we see \r + \n we need to renormalize. Otherwise we're good */
        return last_a == '\r' ? 0 : 1;

    crlf = MVM_nfg_crlf_grapheme(tc);
    /* As a control code we are always going to break if we see one of these.
     * Check first_b for speeding up line endings */
    if (first_b == crlf || last_a == crlf)
        return 0;
    /* If either is synthetic other than "\r\n", assume we'll have to re-normalize
     * (this is an over-estimate, most likely). Note if you optimize this that it
     * serves as a guard for what follows.
     * TODO get the last codepoint of last_a and first codepoint of first_b and call
     * MVM_unicode_normalize_should_break */
    if (last_a < 0 || first_b < 0)
        return 0;

    /* If both less than the first significant char for NFC we are good */
    if (last_a < MVM_NORMALIZE_FIRST_SIG_NFC && first_b < MVM_NORMALIZE_FIRST_SIG_NFC) {
        return 1;
    }
    else {
        /* Check if the two codepoints would be joined during normalization.
         * Returns 1 if they would break and thus is safe under concat, or 0 if
         * they would be joined. */
        MVMNormalizer norm;
        int rtrn;
        MVM_unicode_normalizer_init(tc, &norm, MVM_NORMALIZE_NFG);
        rtrn = MVM_unicode_normalize_should_break(tc, last_a, first_b, &norm);
        MVM_unicode_normalizer_cleanup(tc, &norm);
        /* If both CCC are non-zero then it may need to be reordered. For now return 0.
         * This can be optimized. */
        if (MVM_unicode_relative_ccc(tc, last_a) != 0 && MVM_unicode_relative_ccc(tc, first_b) != 0)
            return 0;
        return rtrn;
    }
}

/* Initialize NFG subsystem. */
static void cache_crlf(MVMThreadContext *tc) {
    MVMCodepoint codes[2] = { '\r', '\n' };
    tc->instance->nfg->crlf_grapheme = lookup_or_add_synthetic(tc, codes, 2, 0);
}
void MVM_nfg_init(MVMThreadContext *tc) {
    int init_stat;
    tc->instance->nfg = calloc(1, sizeof(MVMNFGState));
    if ((init_stat = uv_mutex_init(&(tc->instance->nfg->update_mutex))) < 0) {
        fprintf(stderr, "MoarVM: Initialization of NFG update mutex failed\n    %s\n",
            uv_strerror(init_stat));
        exit(1);
    }
    cache_crlf(tc);
}

/* Free all memory allocated to hold synthetic graphemes. These are global
 * to a VM instance. */
void MVM_nfg_destroy(MVMThreadContext *tc) {
    MVMNFGState *nfg = tc->instance->nfg;
    MVMint32 i;

    /* Free all synthetics. */
    if (nfg->synthetics) {
        size_t used_synths_in_block = nfg->num_synthetics % MVM_SYNTHETIC_GROW_ELEMS;
        size_t synths_to_free = used_synths_in_block
            ? nfg->num_synthetics + (MVM_SYNTHETIC_GROW_ELEMS - used_synths_in_block)
            : nfg->num_synthetics;

        for (i = 0; i < nfg->num_synthetics; i++) {
            MVM_fixed_size_free(tc, tc->instance->fsa,
                nfg->synthetics[i].num_codes * sizeof(MVMCodepoint),
                nfg->synthetics[i].codes);
            if (nfg->synthetics[i].case_uc != CASE_UNCHANGED)
                MVM_free(nfg->synthetics[i].case_uc);
            if (nfg->synthetics[i].case_lc != CASE_UNCHANGED)
                    MVM_free(nfg->synthetics[i].case_lc);
            if (nfg->synthetics[i].case_tc != CASE_UNCHANGED)
                MVM_free(nfg->synthetics[i].case_tc);
            if (nfg->synthetics[i].case_fc != CASE_UNCHANGED)
                MVM_free(nfg->synthetics[i].case_fc);
        }

        MVM_fixed_size_free(tc, tc->instance->fsa,
            synths_to_free * sizeof(MVMNFGSynthetic),
            nfg->synthetics);
    }

    MVM_free(nfg);
}
