#include "moar.h"

/* Maps outside-world normalization form codes to our internal set, validating
 * that we got something valid. */
MVMNormalization MVN_unicode_normalizer_form(MVMThreadContext *tc, MVMint64 form_in) {
    switch (form_in) {
    case 1: return MVM_NORMALIZE_NFC;
    case 2: return MVM_NORMALIZE_NFD;
    case 3: return MVM_NORMALIZE_NFKC;
    case 4: return MVM_NORMALIZE_NFKD;
    default: MVM_exception_throw_adhoc(tc, "Invalid normalization form %d", (int)form_in);
    }
}

/* Takes two objects, which must be of VMArray representation and holding
 * 32-bit integers. Performs normalization to the specified form. */
static void assert_codepoint_array(MVMThreadContext *tc, MVMObject *arr, char *error) {
    if (IS_CONCRETE(arr) && REPR(arr)->ID == MVM_REPR_ID_MVMArray) {
        MVMuint8 slot_type = ((MVMArrayREPRData *)STABLE(arr)->REPR_data)->slot_type;
        if (slot_type == MVM_ARRAY_I32 || slot_type == MVM_ARRAY_U32)
            return;
    }
    MVM_exception_throw_adhoc(tc, "%s", error);
}
MVM_STATIC_INLINE void maybe_grow_result(MVMCodepoint **result, MVMint64 *result_alloc, MVMint64 needed) {
    if (needed >= *result_alloc) {
        *result_alloc += 32;
        *result = MVM_realloc(*result, *result_alloc * sizeof(MVMCodepoint));
    }
}
void MVM_unicode_normalize_codepoints(MVMThreadContext *tc, MVMObject *in, MVMObject *out, MVMNormalization form) {
    MVMNormalizer  norm;
    MVMCodepoint  *input;
    MVMCodepoint  *result;
    MVMint64       input_pos, input_codes, result_pos, result_alloc;
    MVMint32       ready;

    /* Validate input/output array. */
    assert_codepoint_array(tc, in, "Normalization input must be native array of 32-bit integers");
    assert_codepoint_array(tc, out, "Normalization output must be native array of 32-bit integers");

    /* Get input array; if it's empty, we're done already. */
    input       = (MVMCodepoint *)((MVMArray *)in)->body.slots.u32 + ((MVMArray *)in)->body.start;
    input_codes = ((MVMArray *)in)->body.elems;
    if (input_codes == 0)
        return;

    /* Guess output size based on input size. */
    result_alloc = input_codes;
    result       = MVM_malloc(result_alloc * sizeof(MVMCodepoint));

    /* Perform normalization. */
    MVM_unicode_normalizer_init(tc, &norm, form);
    input_pos  = 0;
    result_pos = 0;
    while (input_pos < input_codes) {
        MVMCodepoint cp;
        ready = MVM_unicode_normalizer_process_codepoint(tc, &norm, input[input_pos], &cp);
        if (ready) {
            maybe_grow_result(&result, &result_alloc, result_pos + ready);
            result[result_pos++] = cp;
            while (--ready > 0)
                result[result_pos++] = MVM_unicode_normalizer_get_codepoint(tc, &norm);
        }
        input_pos++;
    }
    MVM_unicode_normalizer_eof(tc, &norm);
    ready = MVM_unicode_normalizer_available(tc, &norm);
    maybe_grow_result(&result, &result_alloc, result_pos + ready);
    while (ready--)
        result[result_pos++] = MVM_unicode_normalizer_get_codepoint(tc, &norm);
    MVM_unicode_normalizer_cleanup(tc, &norm);

    /* Put result into array body. */
    ((MVMArray *)out)->body.slots.u32 = result;
    ((MVMArray *)out)->body.start     = 0;
    ((MVMArray *)out)->body.elems     = result_pos;
}

/* Takes an object, which must be of VMArray representation and holding
 * 32-bit integers. Treats them as Unicode codepoints, normalizes them at
 * Grapheme level, and returns the resulting NFG string. */
MVMString * MVM_unicode_codepoints_to_nfg_string(MVMThreadContext *tc, MVMObject *codes) {
    MVMNormalizer  norm;
    MVMCodepoint  *input;
    MVMGrapheme32 *result;
    MVMint64       input_pos, input_codes, result_pos, result_alloc;
    MVMint32       ready;
    MVMString     *str;

    /* Get input array; if it's empty, we're done already. */
    assert_codepoint_array(tc, codes, "Code points to string input must be native array of 32-bit integers");
    input       = (MVMCodepoint *)((MVMArray *)codes)->body.slots.u32 + ((MVMArray *)codes)->body.start;
    input_codes = ((MVMArray *)codes)->body.elems;
    if (input_codes == 0)
        return tc->instance->str_consts.empty;

    /* Guess output size based on input size. */
    result_alloc = input_codes;
    result       = MVM_malloc(result_alloc * sizeof(MVMCodepoint));

    /* Perform normalization at grapheme level. */
    MVM_unicode_normalizer_init(tc, &norm, MVM_NORMALIZE_NFG);
    input_pos  = 0;
    result_pos = 0;
    while (input_pos < input_codes) {
        MVMGrapheme32 g;
        ready = MVM_unicode_normalizer_process_codepoint_to_grapheme(tc, &norm, input[input_pos], &g);
        if (ready) {
            maybe_grow_result(&result, &result_alloc, result_pos + ready);
            result[result_pos++] = g;
            while (--ready > 0)
                result[result_pos++] = MVM_unicode_normalizer_get_grapheme(tc, &norm);
        }
        input_pos++;
    }
    MVM_unicode_normalizer_eof(tc, &norm);
    ready = MVM_unicode_normalizer_available(tc, &norm);
    maybe_grow_result(&result, &result_alloc, result_pos + ready);
    while (ready--)
        result[result_pos++] = MVM_unicode_normalizer_get_grapheme(tc, &norm);
    MVM_unicode_normalizer_cleanup(tc, &norm);

    /* Produce an MVMString of the result. */
    str = (MVMString *)MVM_repr_alloc_init(tc, tc->instance->VMString);
    str->body.storage.blob_32 = result;
    str->body.storage_type    = MVM_STRING_GRAPHEME_32;
    str->body.num_graphs      = result_pos;
    return str;
}

/* Takes an NFG string and populates the array out, which must be a 32-bit
 * integer array, with codepoints normalized according to the specified
 * normalization form. */
void MVM_unicode_string_to_codepoints(MVMThreadContext *tc, MVMString *s, MVMNormalization form, MVMObject *out) {
    MVMCodepoint     *result;
    MVMint64          result_pos, result_alloc;
    MVMCodepointIter  ci;

    /* Validate output array and set up result storage. */
    assert_codepoint_array(tc, out, "Normalization output must be native array of 32-bit integers");
    result_alloc = s->body.num_graphs;
    result       = MVM_malloc(result_alloc * sizeof(MVMCodepoint));
    result_pos   = 0;

    /* Create codepoint iterator. */
    MVM_string_ci_init(tc, &ci, s);

    /* If we want NFC, just iterate, since NFG is constructed out of NFC. */
    if (form == MVM_NORMALIZE_NFC) {
        while (MVM_string_ci_has_more(tc, &ci)) {
            maybe_grow_result(&result, &result_alloc, result_pos + 1);
            result[result_pos++] = MVM_string_ci_get_codepoint(tc, &ci);
        }
    }

    /* Otherwise, need to feed it through a normalizer. */
    else {
        MVMNormalizer norm;
        MVMint32      ready;
        MVM_unicode_normalizer_init(tc, &norm, form);
        while (MVM_string_ci_has_more(tc, &ci)) {
            MVMCodepoint cp;
            ready = MVM_unicode_normalizer_process_codepoint(tc, &norm, MVM_string_ci_get_codepoint(tc, &ci), &cp);
            if (ready) {
                maybe_grow_result(&result, &result_alloc, result_pos + ready);
                result[result_pos++] = cp;
                while (--ready > 0)
                    result[result_pos++] = MVM_unicode_normalizer_get_codepoint(tc, &norm);
            }
        }
        MVM_unicode_normalizer_eof(tc, &norm);
        ready = MVM_unicode_normalizer_available(tc, &norm);
        maybe_grow_result(&result, &result_alloc, result_pos + ready);
        while (ready--)
            result[result_pos++] = MVM_unicode_normalizer_get_codepoint(tc, &norm);
        MVM_unicode_normalizer_cleanup(tc, &norm);
    }

    /* Put result into array body. */
    ((MVMArray *)out)->body.slots.u32 = result;
    ((MVMArray *)out)->body.start     = 0;
    ((MVMArray *)out)->body.elems     = result_pos;
}

/* Initialize the MVMNormalizer pointed to to perform the specified kind of
 * normalization. */
void MVM_unicode_normalizer_init(MVMThreadContext *tc, MVMNormalizer *n, MVMNormalization form) {
    n->form            = form;
    n->buffer_size     = 32;
    n->buffer          = MVM_malloc(n->buffer_size * sizeof(MVMCodepoint));
    n->buffer_start    = 0;
    n->buffer_end      = 0;
    n->buffer_norm_end = 0;
    switch (n->form) {
        case MVM_NORMALIZE_NFD:
            n->first_significant    = MVM_NORMALIZE_FIRST_SIG_NFD;
            n->quick_check_property = MVM_UNICODE_PROPERTY_NFD_QC;
            break;
        case MVM_NORMALIZE_NFKD:
            n->first_significant = MVM_NORMALIZE_FIRST_SIG_NFKD;
            n->quick_check_property = MVM_UNICODE_PROPERTY_NFKD_QC;
            break;
        case MVM_NORMALIZE_NFC:
            n->first_significant = MVM_NORMALIZE_FIRST_SIG_NFC;
            n->quick_check_property = MVM_UNICODE_PROPERTY_NFC_QC;
            break;
        case MVM_NORMALIZE_NFKC:
            n->first_significant = MVM_NORMALIZE_FIRST_SIG_NFKC;
            n->quick_check_property = MVM_UNICODE_PROPERTY_NFKC_QC;
            break;
        case MVM_NORMALIZE_NFG:
            n->quick_check_property = MVM_UNICODE_PROPERTY_NFC_QC;
            n->first_significant = MVM_NORMALIZE_FIRST_SIG_NFC;
            break;
        default:
            abort();
    }
}

/* Cleanup an MVMNormalization once we're done normalizing. */
void MVM_unicode_normalizer_cleanup(MVMThreadContext *tc, MVMNormalizer *n) {
    free(n->buffer);
}

/* Adds a codepoint into the buffer, making sure there's space. */
static void add_codepoint_to_buffer(MVMThreadContext *tc, MVMNormalizer *n, MVMCodepoint cp) {
    if (n->buffer_end == n->buffer_size) {
        if (n->buffer_start != 0) {
            MVMint32 shuffle = n->buffer_start;
            MVMint32 to_move = n->buffer_end - n->buffer_start;
            memmove(n->buffer, n->buffer + n->buffer_start, to_move * sizeof(MVMCodepoint));
            n->buffer_start = 0;
            n->buffer_end -= shuffle;
            n->buffer_norm_end -= shuffle;
        }
        else {
            n->buffer_size *= 2;
            n->buffer = MVM_realloc(n->buffer, n->buffer_size * sizeof(MVMCodepoint));
        }
    }
    n->buffer[n->buffer_end++] = cp;
}

/* Hangul-related constants from Unicode spec 3.12, following naming
 * convention from spec. */
static int
    SBase = 0xAC00,
    LBase = 0x1100, VBase = 0x1161, TBase = 0x11A7,
    LCount = 19, VCount = 21, TCount = 28,
    NCount = 588, /* VCount * TCount */
    SCount = 11172; /* LCount * NCount */

/* Decomposes a Hangul codepoint and add it into the buffer. */
static void decomp_hangul_to_buffer(MVMThreadContext *tc, MVMNormalizer *n, MVMCodepoint s) {
    /* Algorithm from Unicode spec 3.12, following naming convention from spec. */
    int SIndex = s - SBase;
    if (SIndex < 0 || SIndex >= SCount) {
        add_codepoint_to_buffer(tc, n, s);
    }
    else {
        int L = LBase + SIndex / NCount;
        int V = VBase + (SIndex % NCount) / TCount;
        int T = TBase + SIndex % TCount;
        add_codepoint_to_buffer(tc, n, (MVMCodepoint)L);
        add_codepoint_to_buffer(tc, n, (MVMCodepoint)V);
        if (T != TBase)
            add_codepoint_to_buffer(tc, n, (MVMCodepoint)T);
    }
}

/* Decompose the codepoint and add it into the buffer. */
static void decomp_codepoint_to_buffer(MVMThreadContext *tc, MVMNormalizer *n, MVMCodepoint cp) {
    /* See if we actually need to decompose (can skip if the decomposition
     * type is None, or we're only doing Canonical decomposition and it is
     * anything except Canonical). */
    const char *type = MVM_unicode_codepoint_get_property_cstr(tc, cp, MVM_UNICODE_PROPERTY_DECOMPOSITION_TYPE);
    MVMint64 decompose = 1;
    if (!type)
        decompose = 0;
    else if (strcmp(type, "None") == 0)
        decompose = 0;
    else if (!MVM_NORMALIZE_COMPAT_DECOMP(n->form) && strcmp(type, "Canonical") != 0)
        decompose = 0;
    if (decompose) {
        /* We need to decompose. Get the decomp spec and go over the things in
         * it; things without a decomp spec are presumably Hangul and need the
         * algorithmic treatment. */
        char *spec = (char *)MVM_unicode_codepoint_get_property_cstr(tc, cp, MVM_UNICODE_PROPERTY_DECOMP_SPEC);
        if (spec && spec[0]) {
            char *end = spec + strlen(spec);
            while (spec < end) {
                /* Parse hex character code, and then recurse to do any further
                * decomposition on it; this recursion terminates when we find a
                * non-decomposable thing and add it to the buffer. */
                MVMCodepoint decomp_char = (MVMCodepoint)strtol(spec, &spec, 16);
                decomp_codepoint_to_buffer(tc, n, decomp_char);
            }
        }
        else {
            decomp_hangul_to_buffer(tc, n, cp);
        }
    }
    else {
        /* Don't need to decompose; add it right into the buffer. */
        add_codepoint_to_buffer(tc, n, cp);
    }
}

/* Checks if the specified character answers "yes" on the appropriate quick check. */
static MVMint64 passes_quickcheck(MVMThreadContext *tc, MVMNormalizer *n, MVMCodepoint cp) {
    const char *pval = MVM_unicode_codepoint_get_property_cstr(tc, cp, n->quick_check_property);
    return pval && pval[0] == 'Y';
}

/* Gets the canonical combining class for a codepoint. */
static MVMint64 ccc(MVMThreadContext *tc, MVMCodepoint cp) {
    const char *ccc_str = MVM_unicode_codepoint_get_property_cstr(tc, cp, MVM_UNICODE_PROPERTY_CANONICAL_COMBINING_CLASS);
    return !ccc_str || strlen(ccc_str) > 3 ? 0 : atoi(ccc_str);
}

/* Implements the Unicode Canonical Ordering algorithm (3.11, D109). */
static void canonical_sort(MVMThreadContext *tc, MVMNormalizer *n, MVMint32 from, MVMint32 to) {
    /* Yes, this is the simplest possible thing. Key thing if you decide to
     * replace it with something more optimal: it must not re-order code
     * points with equal CCC. */
    MVMint32 reordered = 1;
    while (reordered) {
        MVMint32 i = from;
        reordered = 0;
        while (i < to - 1) {
            MVMint64 cccA = ccc(tc, n->buffer[i]);
            MVMint64 cccB = ccc(tc, n->buffer[i + 1]);
            if (cccA > cccB && cccB > 0) {
                MVMCodepoint tmp = n->buffer[i];
                n->buffer[i] = n->buffer[i + 1];
                n->buffer[i + 1] = tmp;
                reordered = 1;
            }
            i++;
        }
    }
}

/* Implements the Unicode Canonical Composition algorithm (3.11, D117). */
static void canonical_composition(MVMThreadContext *tc, MVMNormalizer *n, MVMint32 from, MVMint32 to) {
    MVMint32 c_idx = from + 1;
    while (c_idx < to) {
        /* Search for the last non-blocked starter. */
        MVMint32 ss_idx = c_idx - 1;
        MVMint32 c_ccc  = ccc(tc, n->buffer[c_idx]);
        while (ss_idx >= from) {
            /* Make sure we don't go past a code point that blocks a starter
             * from the current character we're considering. */
            MVMint32 ss_ccc = ccc(tc, n->buffer[ss_idx]);
            if (ss_ccc >= c_ccc)
                break;

            /* Have we found a starter? */
            if (ss_ccc == 0) {
                /* See if there's a primary composite for the starter and the
                 * current code point under consideration. */
                MVMCodepoint pc = MVM_unicode_find_primary_composite(tc, n->buffer[ss_idx], n->buffer[c_idx]);
                if (pc > 0) {
                    /* Replace the starter with the primary composite. */
                    n->buffer[ss_idx] = pc;

                    /* Move the rest of the buffer back one position. */
                    memmove(n->buffer + c_idx, n->buffer + c_idx + 1,
                        (n->buffer_end - (c_idx + 1)) * sizeof(MVMCodepoint));
                    n->buffer_end--;
                    n->buffer_norm_end--;

                    /* Sync cc_idx and to with the change. */
                    c_idx--;
                    to--;
                }

                /* Don't look back beyond this starter; covers the ccc(B) = 0
                 * case of D105. */
                break;
            }
            ss_idx--;
        }

        /* Move on to the next character. */
        c_idx++;
    }

    /* Make another pass for the Hangul special case. (A future optimization
     * may be to incorporate this into the above loop.) */
    c_idx = from;
    while (c_idx < to - 1) {
        /* Do we have a potential LPart? */
        MVMCodepoint LPart = n->buffer[c_idx];
        if (LPart >= LBase && LPart <= (LBase + LCount)) {
            /* Yes, now see if it's followed by a VPart (always safe to look
             * due to "to - 1" in loop condition above). */
            MVMCodepoint LIndex = LPart - LBase;
            MVMCodepoint VPart  = n->buffer[c_idx + 1];
            if (VPart >= VBase && VPart <= (VBase + VCount)) {
                /* Certainly something to compose; compute that. */
                MVMCodepoint VIndex = VPart - VBase;
                MVMCodepoint LVIndex = LIndex * NCount + VIndex * TCount;
                MVMCodepoint s = SBase + LVIndex;
                MVMint32 composed = 1;

                /* Is there a TPart too? */
                if (c_idx < to - 2) {
                    MVMCodepoint TPart  = n->buffer[c_idx + 2];
                    if (TPart >= TBase && TPart <= (TBase + TCount)) {
                        /* We need to compose 3 things. */
                        MVMCodepoint TIndex = TPart - TBase;
                        s += TIndex;
                        composed = 2;
                    }
                }

                /* Put composed codepoint into the buffer. */
                n->buffer[c_idx] = s;

                /* Shuffle codepoints after this in the buffer back. */
                memmove(n->buffer + c_idx + 1, n->buffer + c_idx + 1 + composed,
                        (n->buffer_end - (c_idx + 1 + composed)) * sizeof(MVMCodepoint));
                n->buffer_end -= composed;
                n->buffer_norm_end -= composed;

                /* Sync to with updated buffer size. */
                to -= composed;
            }
        }
        c_idx++;
    }
}

/* Performs grapheme composition (to get Normal Form Grapheme) on the range of
 * codepoints provided. This algorithm is as follows (laid out here because
 * this is not one of the Unicode standard ones):
 * 1) If we only have one code point in the range to normalize, then NFC was
 *    already sufficient here. Return.
 * 2) Take the from position as the location of the current "starterish".
 * 3) Walk codepoints until we reach "to" or the next codepoint is a starter.
 * 4) Take the codepoints from and including the last starterish up to the
 *    current one, and get a synthetic for them. Replace them with the
 *    synthetic.
 * 5) If we didn't reach "to", take the next codepoint as our next starterish
 *    and goto step 3.
 * Note that this is specified to handle strings starting with non-starter
 * code point sequences.
 */
static void grapheme_composition(MVMThreadContext *tc, MVMNormalizer *n, MVMint32 from, MVMint32 to) {
    if (to - from >= 2) {
        MVMint32 starterish = from;
        MVMint32 insert_pos = from;
        MVMint32 pos        = from;
        while (pos < to) {
            MVMint32 next_pos = pos + 1;
            if (next_pos == to || ccc(tc, n->buffer[next_pos]) == 0) {
                /* Last in buffer or next code point is a non-starter; turn
                 * sequence into a synthetic. */
                MVMGrapheme32 g = MVM_nfg_codes_to_grapheme(tc, n->buffer + starterish, next_pos - starterish);
                n->buffer[insert_pos++] = g;

                /* The next code point is our new starterish (harmless if we
                 * are already at the end of the buffer). */
                starterish = next_pos;
            }
            pos++;
        }
        memmove(n->buffer + insert_pos, n->buffer + to, (n->buffer_end - to) * sizeof(MVMCodepoint));
        n->buffer_end -= to - insert_pos;
    }
}

/* Called when the very fast case of normalization fails (that is, when we get
 * any two codepoints in a row where at least one is greater than the first
 * significant codepoint identified by a quick check for the target form). We
 * may find the quick check itself is enough; if not, we have to do real work
 * compute the normalization. */
MVMint32 MVM_unicode_normalizer_process_codepoint_full(MVMThreadContext *tc, MVMNormalizer *n, MVMCodepoint in, MVMCodepoint *out) {
    /* Do a quickcheck on the codepoint we got in and get its CCC. */
    MVMint64 qc_in  = passes_quickcheck(tc, n, in);
    MVMint64 ccc_in = ccc(tc, in);

    /* Fast cases when we pass quick check and what we got in has CCC = 0. */
    if (qc_in && ccc_in == 0) {
        if (MVM_NORMALIZE_COMPOSE(n->form)) {
            /* We're composing. If we have exactly one thing in the buffer and
             * it also passes the quick check, and both it and the thing in the
             * buffer have a CCC of zero, we can hand back the first of the
             * two - effectively replacing what's in the buffer with the new
             * codepoint coming in. */
            if (n->buffer_end - n->buffer_start == 1) {
                MVMCodepoint maybe_result = n->buffer[n->buffer_start];
                if (passes_quickcheck(tc, n, maybe_result) && ccc(tc, maybe_result) == 0) {
                    *out = n->buffer[n->buffer_start];
                    n->buffer[n->buffer_start] = in;
                    return 1;
                }
            }
        }
        else {
            /* We're only decomposing. There should probably be nothing in the
             * buffer in this case; if so we can simply return the codepoint. */
            if (n->buffer_start == n->buffer_end) {
                *out = in;
                return 1;
            }
        }
    }

    /* If we didn't pass quick check... */
    if (!qc_in) {
        /* If we're composing, then decompose the last thing placed in the
         * buffer, if any. We need to do this since it may have passed
         * quickcheck, but having seen some character that does pass then we
         * must make sure we decomposed the prior passing one too. */
        if (MVM_NORMALIZE_COMPOSE(n->form) && n->buffer_end != n->buffer_start) {
            MVMCodepoint decomp = n->buffer[n->buffer_end - 1];
            n->buffer_end--;
            decomp_codepoint_to_buffer(tc, n, decomp);
        }

        /* Decompose this new character into the buffer. We'll need to see
         * more before we can go any further. */
        decomp_codepoint_to_buffer(tc, n, in);
        return 0;
    }

    /* Since anything we have at this point does pass quick check, add it to
     * the buffer directly. */
    add_codepoint_to_buffer(tc, n, in);

    /* If the codepoint has a CCC that is non-zero, it's not a starter so we
     * should see more before normalizing. */
    if (ccc_in > 0)
        return 0;

    /* If we don't have at least one codepoint in the buffer, it's too early
     * to hand anything back. */
    if (n->buffer_end - n->buffer_start <= 1)
        return 0;

    /* Perform canonical sorting on everything from the start of the buffer
     * up to but excluding the quick-check-passing thing we just added. */
    canonical_sort(tc, n, n->buffer_start, n->buffer_end - 1);

    /* Perform canonical composition and grapheme composition if needed. */
    if (MVM_NORMALIZE_COMPOSE(n->form)) {
        canonical_composition(tc, n, n->buffer_start, n->buffer_end - 1);
        if (MVM_NORMALIZE_GRAPHEME(n->form))
            grapheme_composition(tc, n, n->buffer_start, n->buffer_end - 1);
    }

    /* We've now normalized all except the latest, quick-check-passing
     * codepoint. */
    n->buffer_norm_end = n->buffer_end - 1;

    /* Hand back a codepoint, and flag how many more are available. */
    *out = n->buffer[n->buffer_start];
    return n->buffer_norm_end - n->buffer_start++;
}

/* Processes a codepoint that we regard as a "normalization terminator". These
 * never have a decomposition, and for all practical purposes will not have a
 * combiner on them. We treat them specially so we don't, during I/O, block on
 * seeing a codepoint after them, which for things like REPLs that need to see
 * input right after a \n makes for problems. */
MVMint32 MVM_unicode_normalizer_process_codepoint_norm_terminator(MVMThreadContext *tc, MVMNormalizer *n, MVMCodepoint in, MVMCodepoint *out) {
    /* Add the codepoint into the buffer. */
    add_codepoint_to_buffer(tc, n, in);

    /* Treat this as an "eof", which really means "normalize what ya got". */
    MVM_unicode_normalizer_eof(tc, n);

    /* Hand back a normalized codepoint, and the number available (have to
     * compensate for the one we steal for *out). */
    *out = MVM_unicode_normalizer_get_codepoint(tc, n);
    return 1 + MVM_unicode_normalizer_available(tc, n);
}

/* Called when we are expecting no more codepoints. */
void MVM_unicode_normalizer_eof(MVMThreadContext *tc, MVMNormalizer *n) {
    /* Perform canonical ordering and, if needed, canonical composition on
     * what remains. */
    canonical_sort(tc, n, n->buffer_start, n->buffer_end);
    if (MVM_NORMALIZE_COMPOSE(n->form)) {
        canonical_composition(tc, n, n->buffer_start, n->buffer_end);
        if (MVM_NORMALIZE_GRAPHEME(n->form))
            grapheme_composition(tc, n, n->buffer_start, n->buffer_end);
    }

    /* We've now normalized all that remains. */
    n->buffer_norm_end = n->buffer_end;
}
