/* State kept around for implementing Normal Form Grapheme. The design is such
 * that we can always do lookups without needing to acquire a lock. When we
 * do additions of new synthetics, we must acquire the lock before doing so,
 * and be sure to validate nothing changed. We also must do sufficient copying
 * to ensure that we never break another thread doing a read. Memory to be
 * freed is thus done at a global safe point, which means we never have one
 * thread reading memory freed by another. */
struct MVMNFGState {
    /* Table of information about synthetic graphemes. Given some (negative)
     * synthetic S, we look up in this table with (-S - 1). */
    MVMNFGSynthetic *synthetics;

    /* Trie used to do lookups by codepoints (already in NFC) to an (NFG)
     * grapheme. */
    MVMNFGTrieNode *grapheme_lookup;

    /* Mutex used when we wish to do updates to the grapheme table. */
    uv_mutex_t update_mutex;

    /* Number of synthetics we have. */
    MVMuint32 num_synthetics;

    /* Cached CRLF grapheme index, since we need it so often. */
    MVMGrapheme32 crlf_grapheme;
};

/* State held about a synthetic. */
struct MVMNFGSynthetic {
    /* The base (non-combining) grapheme. */
    /* The index of the base (non-combining) grapheme
     * set to -1 if it does not exist */
    MVMint32 base_index;

    /* The number of codepoints we have. */
    MVMint32 num_codes;

    /* Array of codepoints. */
    MVMCodepoint *codes;

    /* Cached case transforms, NULL if not calculated. */
    MVMGrapheme32 *case_uc;
    MVMGrapheme32 *case_lc;
    MVMGrapheme32 *case_tc;
    MVMGrapheme32 *case_fc;

    /* Grapheme counts of cached case transforms. */
    MVMint32 case_uc_graphs;
    MVMint32 case_lc_graphs;
    MVMint32 case_tc_graphs;
    MVMint32 case_fc_graphs;

    /* Is this a UTF-8 C-8 synthetic? */
    MVMint32 is_utf8_c8;
};

/* A node in the NFG trie. */
struct MVMNFGTrieNode {
    /* Set of entries for further traversal, sorted ascending on codepoint
     * so we can find an entry using binary search. */
    MVMNFGTrieNodeEntry *next_codes;

    /* Number of entries in next_cps. */
    MVMint32 num_entries;

    /* Non-zero if we reach a result at this node (and will always be negative
     * since it's an NFG synthetic). */
    MVMGrapheme32 graph;
};

/* An entry in the list of next possible codepoints in the NFG trie. */
struct MVMNFGTrieNodeEntry {
    /* The codepoint. */
    MVMCodepoint code;

    /* Trie node to traverse to if we find this node. */
    MVMNFGTrieNode *node;
};

/* The maximum number of codepoints we will allow in a synthetic grapheme.
 * This is a good bit higher than any real-world use case is going to run
 * in to. */
#define MVM_GRAPHEME_MAX_CODEPOINTS 1024

/* Functions related to grapheme handling. */
MVMGrapheme32 MVM_nfg_codes_to_grapheme(MVMThreadContext *tc, MVMCodepoint *codes, MVMint32 num_codes);
MVMGrapheme32 MVM_nfg_codes_to_grapheme_utf8_c8(MVMThreadContext *tc, MVMCodepoint *codes, MVMint32 num_codes);
MVMGrapheme32 MVM_nfg_crlf_grapheme(MVMThreadContext *tc);
MVMNFGSynthetic * MVM_nfg_get_synthetic_info(MVMThreadContext *tc, MVMGrapheme32 synth);
MVMuint32 MVM_nfg_get_case_change(MVMThreadContext *tc, MVMGrapheme32 codepoint, MVMint32 case_, MVMGrapheme32 **result);
MVMint32 MVM_nfg_is_concat_stable(MVMThreadContext *tc, MVMString *a, MVMString *b);

/* NFG subsystem initialization and cleanup. */
void MVM_nfg_init(MVMThreadContext *tc);
void MVM_nfg_destroy(MVMThreadContext *tc);
