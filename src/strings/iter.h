/* Grapheme iterator structure; iterates through graphemes in a string. */
struct MVMGraphemeIter {
    /* The blob we're currently iterating over. */
    union {
        MVMGrapheme32    *blob_32;
        MVMGraphemeASCII *blob_ascii;
        MVMGrapheme8     *blob_8;
        void             *any;
    } active_blob;

    /* The type of blob we have. */
    MVMuint16 blob_type;

    /* The number of strands remaining, if any. */
    MVMuint16 strands_remaining;

    /* The current position, and the end position. */
    MVMStringIndex pos;
    MVMStringIndex end;

    /* Repetition count, and the start index in the blob (only needed if we're
     * doing an iteration over a repetition). */
    MVMStringIndex start;
    MVMuint32      repetitions;

    /* The next strand, if we're doing a strand-based iteration. */
    MVMStringStrand *next_strand;
};

/* Initializes a grapheme iterator. */
MVM_STATIC_INLINE void MVM_string_gi_init(MVMThreadContext *tc, MVMGraphemeIter *gi, MVMString *s) {
    if (s->body.storage_type == MVM_STRING_STRAND) {
        MVMStringStrand *strands = s->body.storage.strands;
        MVMString       *first   = strands[0].blob_string;
        gi->active_blob.any      = first->body.storage.any;
        gi->blob_type            = first->body.storage_type;
        gi->strands_remaining    = s->body.num_strands - 1;
        gi->pos = gi->start      = strands[0].start;
        gi->end                  = strands[0].end;
        gi->repetitions          = strands[0].repetitions;
        gi->next_strand          = strands + 1;
    }
    else {
        gi->active_blob.any   = s->body.storage.any;
        gi->blob_type         = s->body.storage_type;
        gi->end               = s->body.num_graphs;
        gi->strands_remaining = gi->start = gi->pos = gi->repetitions = 0;
        gi->next_strand       = NULL;
    }
};
/* Gets the number of graphemes remaining in the current strand of the grapheme
 * iterator, including repetitions */
#define MVM_string_gi_graphs_left_in_strand_rep(gi) \
    (gi->end - gi->pos + gi->repetitions * (gi->end - gi->start))
/* graphs left in strand + graphs left in repetitions of current strand */

/* Moves to the next strand, or repetition if there is one. */
static void MVM_string_gi_next_strand_rep(MVMThreadContext *tc, MVMGraphemeIter *gi) {
    MVMStringStrand *next = NULL;
    if (gi->repetitions) {
        gi->pos = gi->start;
        gi->repetitions--;
        return;
    }
    if (gi->strands_remaining <= 0)
        MVM_exception_throw_adhoc(tc, "Iteration past end of grapheme iterator\n");

    next = (gi->next_strand)++;
    gi->pos = gi->start = next->start;
    gi->end             = next->end;
    gi->repetitions     = next->repetitions;
    gi->blob_type       = next->blob_string->body.storage_type;
    gi->active_blob.any = next->blob_string->body.storage.any;
    gi->strands_remaining--;
}
/* Sets the position of the iterator. (Can be optimized in many ways in the
 * repetitions and strands branches.) */
MVM_STATIC_INLINE void MVM_string_gi_move_to(MVMThreadContext *tc, MVMGraphemeIter *gi, MVMuint32 pos) {
    MVMuint32 remaining = pos;
    MVMuint32 strand_graphs;
    MVMStringStrand *next = NULL;

    /* Find the appropriate strand. */
    /* Set strand_graphs to the number of graphemes */
    while (remaining > (strand_graphs = MVM_string_gi_graphs_left_in_strand_rep(gi))) {
        remaining -= strand_graphs;
        if (!(gi->strands_remaining--))
            MVM_exception_throw_adhoc(tc, "Iteration past end of grapheme iterator");
        next = (gi->next_strand)++;
        gi->pos = gi->start = next->start;
        gi->end             = next->end;
        gi->repetitions     = next->repetitions;
    }
    if (next) {
        gi->blob_type       = next->blob_string->body.storage_type;
        gi->active_blob.any = next->blob_string->body.storage.any;
    }

    /* Now look within the strand. */
    if (!remaining)
        return;
    /* Most common case where we move within the strand */
    if (gi->pos + remaining <= gi->end) {
        gi->pos += remaining;
        return;
    }
    /* If we are here we are encountering a repetition */
    if (gi->repetitions) {
        MVMuint32 rep_graphs = gi->end - gi->start;
        MVMuint32 remaining_reps;
        /* If we aren't at the end of the repetition, move to the end */
        if (gi->pos < gi->end) {
            remaining -= gi->end - gi->pos;
            gi->pos    = gi->end;
        }
        remaining_reps = remaining / rep_graphs;
        if (gi->repetitions < remaining_reps)
            MVM_exception_throw_adhoc(tc, "Iteration past end of grapheme iterator:"
                                          " no more repetitions remaining\n");
        gi->repetitions -= remaining_reps;
        /* Since we're still at the end, if there's repetitions left over
         * we are going to have to seek forward */
        if (remaining -= remaining_reps * rep_graphs) {
            gi->repetitions--; /* Move to the next repetition. */
            gi->pos = gi->start + remaining;
            /* remaining = 0 now for all purposes now, but since we return, no
             * need to set it */
        }
        return;
    }
    MVM_exception_throw_adhoc(tc, "Iteration past end of grapheme iterator");
}

/* Checks if there is more to read from a grapheme iterator. */
MVM_STATIC_INLINE MVMint32 MVM_string_gi_has_more(MVMThreadContext *tc, MVMGraphemeIter *gi) {
    return gi->pos < gi->end || gi->repetitions || gi->strands_remaining;
}
/* Returns number of graphs left in the strand, ignoring repetitions */
MVM_STATIC_INLINE MVMStringIndex MVM_string_gi_graphs_left_in_strand(MVMThreadContext *tc, MVMGraphemeIter *gi) {
    return gi->end - gi->pos;
}
MVM_STATIC_INLINE MVMGrapheme8 * MVM_string_gi_active_blob_8_pos(MVMThreadContext *tc, MVMGraphemeIter *gi) {
    return gi->active_blob.blob_8 + gi->pos;
}
MVM_STATIC_INLINE MVMGrapheme32 * MVM_string_gi_active_blob_32_pos(MVMThreadContext *tc, MVMGraphemeIter *gi) {
    return gi->active_blob.blob_32 + gi->pos;
}
MVM_STATIC_INLINE MVMuint16 MVM_string_gi_blob_type(MVMThreadContext *tc, MVMGraphemeIter *gi) {
    return gi->blob_type;
}
/* Returns if there are more strands left in the gi, including repetitions */
MVM_STATIC_INLINE int MVM_string_gi_has_more_strands_rep(MVMThreadContext *tc, MVMGraphemeIter *gi) {
    return !!(gi->strands_remaining || gi->repetitions);
}
/* Gets the next grapheme. */
MVM_STATIC_INLINE MVMGrapheme32 MVM_string_gi_get_grapheme(MVMThreadContext *tc, MVMGraphemeIter *gi) {
    while (1) {
        if (gi->pos < gi->end) {
            switch (gi->blob_type) {
                case MVM_STRING_GRAPHEME_32:
                    return gi->active_blob.blob_32[gi->pos++];
                case MVM_STRING_GRAPHEME_ASCII:
                    return gi->active_blob.blob_ascii[gi->pos++];
                case MVM_STRING_GRAPHEME_8:
                    return gi->active_blob.blob_8[gi->pos++];
                }
        }
        else if (gi->repetitions) {
            gi->pos = gi->start;
            gi->repetitions--;
        }
        else if (gi->strands_remaining) {
            MVMStringStrand *next = gi->next_strand;
            gi->active_blob.any = next->blob_string->body.storage.any;
            gi->blob_type       = next->blob_string->body.storage_type;
            gi->pos             = next->start;
            gi->end             = next->end;
            gi->start           = next->start;
            gi->repetitions     = next->repetitions;
            gi->strands_remaining--;
            gi->next_strand++;
        }
        else {
            MVM_exception_throw_adhoc(tc, "Iteration past end of grapheme iterator");
        }
    }
}


/* Returns the codepoint without doing checks, for internal VM use only. */
MVM_STATIC_INLINE MVMGrapheme32 MVM_string_get_grapheme_at_nocheck(MVMThreadContext *tc, MVMString *a, MVMint64 index) {
    switch (a->body.storage_type) {
        case MVM_STRING_GRAPHEME_32:
            return a->body.storage.blob_32[index];
        case MVM_STRING_GRAPHEME_ASCII:
            return a->body.storage.blob_ascii[index];
        case MVM_STRING_GRAPHEME_8:
            return a->body.storage.blob_8[index];
        case MVM_STRING_STRAND: {
            MVMGraphemeIter gi;
            MVM_string_gi_init(tc, &gi, a);
            MVM_string_gi_move_to(tc, &gi, index);
            return MVM_string_gi_get_grapheme(tc, &gi);
        }
        default:
            MVM_exception_throw_adhoc(tc, "String corruption detected: bad storage type");
    }
}

/* Code point iterator. Uses the grapheme iterator, and adds some extra bits
 * in order to iterate the code points in synthetics. */
struct MVMCodepointIter {
    /* The grapheme iterator. */
    MVMGraphemeIter gi;

    /* The codes of the current synthetic we're walking through, if any, with
     * the number of combiners we returned so far, and the total number of
     * combiners there are. */
    MVMCodepoint  *synth_codes;
    MVMint32       visited_synth_codes;
    MVMint32       total_synth_codes;
    /* first_code is only used for string_grapheme_ci functions */
    MVMCodepoint   first_code;
    /* If we should translate newline \n into \r\n. */
    MVMint32       translate_newlines;
    /* Used to pass through utf8-c8 synthetics, but not any others so we can
     * renomalize text without getting rid of utf8-c8 synthetics */
    MVMint32       pass_utfc8_synthetics;

};

/* Initializes a code point iterator. */
MVM_STATIC_INLINE void MVM_string_ci_init(MVMThreadContext *tc, MVMCodepointIter *ci, MVMString *s,
        MVMint32 translate_newlines, MVMint32 pass_utfc8_synthetics) {
    /* Initialize our underlying grapheme iterator. */
    MVM_string_gi_init(tc, &(ci->gi), s);

    /* We've no currently active synthetic codepoint (and other fields are
     * unused until we do, so leave them alone for now). */
    ci->synth_codes           = NULL;
    ci->visited_synth_codes   = -1;
    ci->total_synth_codes     = 0;
    ci->translate_newlines    = translate_newlines;
    ci->pass_utfc8_synthetics = pass_utfc8_synthetics;
};
/* Iterates on a grapheme. Returns the number of codepoints in the grapheme */
MVM_STATIC_INLINE MVMGrapheme32 MVM_string_grapheme_ci_init(MVMThreadContext *tc, MVMCodepointIter *ci, MVMGrapheme32 g, MVMint32 pass_utfc8_synthetics) {
    MVMNFGSynthetic *synth = NULL;
    if (g < 0) {
        /* Get the synthetics info. */
        synth = MVM_nfg_get_synthetic_info(tc, g);
    }
    /* If we got a synth, but *not* if we are supposed to pass utf8-c8 synthetics
     * and it is a utf8-c8 synthetic */
    if (synth && !(pass_utfc8_synthetics && synth->is_utf8_c8)) {
        /* Set up the iterator so in the next iteration we will start to
        * hand back codepoints after the initial.
        * TODO: This may be able to be optimized
        * to remove first_code. */
        ci->synth_codes         =  synth->codes + 1;
        ci->visited_synth_codes = -1;
        ci->total_synth_codes   =  synth->num_codes - 1;
        ci->first_code          =  synth->codes[0];
    }
    else {
        ci->synth_codes         =  NULL;
        ci->visited_synth_codes = -1;
        ci->total_synth_codes   =  0;
        ci->first_code          =  g;
    }
    return ci->total_synth_codes + 1;
}
/* Only for string_grapheme_ci ops */
MVM_STATIC_INLINE MVMCodepoint MVM_string_grapheme_ci_get_codepoint(MVMThreadContext *tc, MVMCodepointIter *ci) {
    MVMCodepoint result = ci->visited_synth_codes < 0
        ? ci->first_code
        : ci->synth_codes[ci->visited_synth_codes];
    ci->visited_synth_codes++;
    return result;
}

/* Checks if there is more to read from a code point iterator; this is the
 * case if we're still walking through a synthetic or we have more things
 * available from the underlying grapheme iterator. */
MVM_STATIC_INLINE MVMint32 MVM_string_ci_has_more(MVMThreadContext *tc, MVMCodepointIter *ci) {
    return ci->synth_codes || MVM_string_gi_has_more(tc, &(ci->gi));
}
/* Only for use with string_grapheme_ci ops */
MVM_STATIC_INLINE MVMint32 MVM_string_grapheme_ci_has_more(MVMThreadContext *tc, MVMCodepointIter *ci) {
    return ci->visited_synth_codes < ci->total_synth_codes;
}

/* Gets the next code point. */
MVM_STATIC_INLINE MVMCodepoint MVM_string_ci_get_codepoint(MVMThreadContext *tc, MVMCodepointIter *ci) {
    MVMCodepoint result;

    /* Do we have combiners from a synthetic to return? */
    if (ci->synth_codes) {
        /* Take the current combiner as the result. */
        result = ci->synth_codes[ci->visited_synth_codes];

        /* If we've seen all of the synthetics, clear up so we'll take another
         * grapheme next time around. */
        ci->visited_synth_codes++;
        if (ci->visited_synth_codes == ci->total_synth_codes)
            ci->synth_codes = NULL;
    }

    /* Otherwise, proceed to the next grapheme. */
    else {
        MVMGrapheme32 g = MVM_string_gi_get_grapheme(tc, &(ci->gi));
#ifdef _WIN32
        if (ci->translate_newlines && g == '\n')
            g = MVM_nfg_crlf_grapheme(tc);
#endif
        if (g >= 0) {
            /* It's not a synthetic, so we're done. */
            result = (MVMCodepoint)g;
        }
        else {
            /* It's a synthetic. Look it up. */
            MVMNFGSynthetic *synth = MVM_nfg_get_synthetic_info(tc, g);
            /* If we have pass_utfc8_synthetics set and it's a utf_c8 codepoint
             * pass it back unchanged */
            if (ci->pass_utfc8_synthetics && synth->is_utf8_c8) {
                result = g;
            }
            else {
                /* Set up the iterator so in the next iteration we will start to
                * hand back codepoints. */
                ci->synth_codes         = synth->codes + 1;
                ci->visited_synth_codes = 0;
                /* Emulate num_combs and subtract one from num_codes */
                ci->total_synth_codes   = synth->num_codes - 1;

                /* Result is the first codepoint of the `codes` array */
                result = synth->codes[0];
            }
        }
    }

    return result;
}
/* The MVMGraphemeIter_cached is used for the Knuth-Morris-Pratt algorithm
 * because often it will request the same grapheme again, and our grapheme
 * iterators only return the next grapheme */
struct MVMGraphemeIter_cached {
    MVMGraphemeIter gi;
    MVMGrapheme32   last_g;
    MVMStringIndex  last_location;
    MVMString      *string;
};
typedef struct MVMGraphemeIter_cached MVMGraphemeIter_cached;
MVM_STATIC_INLINE void MVM_string_gi_cached_init (MVMThreadContext *tc, MVMGraphemeIter_cached *gic, MVMString *s, MVMint64 index) {
    MVM_string_gi_init(tc, &(gic->gi), s);
    if (index) MVM_string_gi_move_to(tc, &(gic->gi), index);
    gic->last_location = index;
    gic->last_g = MVM_string_gi_get_grapheme(tc, &(gic->gi));
    gic->string = s;
}
MVM_STATIC_INLINE MVMGrapheme32 MVM_string_gi_cached_get_grapheme(MVMThreadContext *tc, MVMGraphemeIter_cached *gic, MVMint64 index) {
    /* Most likely case is we are getting the next grapheme. When that happens
     * we will go directly to the end. */
    if (index == gic->last_location + 1) {
    }
    /* Second most likely is getting the cached grapheme */
    else if (index == gic->last_location) {
        return gic->last_g;
    }
    /* If we have to move forward */
    else if (gic->last_location < index) {
        MVM_string_gi_move_to(tc, &(gic->gi), index - gic->last_location - 1);
    }
    /* If we have to backtrack we need to reinitialize the grapheme iterator */
    else {
        MVM_string_gi_cached_init(tc, gic, gic->string, index);
        return gic->last_g;
    }
    gic->last_location = index;
    return (gic->last_g = MVM_string_gi_get_grapheme(tc, &(gic->gi)));
}
