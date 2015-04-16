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
        gi->pos                  = strands[0].start;
        gi->end                  = strands[0].end;
        gi->start                = strands[0].start;
        gi->repetitions          = strands[0].repetitions;
        gi->next_strand          = strands + 1;
    }
    else {
        gi->active_blob.any   = s->body.storage.any;
        gi->blob_type         = s->body.storage_type;
        gi->strands_remaining = 0;
        gi->pos               = 0;
        gi->end               = s->body.num_graphs;
        gi->repetitions       = 0;
    }
};

/* Sets the position of the iterator. (Can be optimized in many ways in the
 * repetitions and strands branches.) */
MVM_STATIC_INLINE void MVM_string_gi_move_to(MVMThreadContext *tc, MVMGraphemeIter *gi, MVMuint32 pos) {
    MVMuint32 remaining = pos;
    MVMuint32 strand_graphs;

    /* Find the appropriate strand. */
    while (remaining > (strand_graphs = (gi->end - gi->pos) * (gi->repetitions + 1))) {
        MVMStringStrand *next = gi->next_strand;
        if (!gi->strands_remaining)
            MVM_exception_throw_adhoc(tc, "Iteration past end of grapheme iterator");
        gi->active_blob.any = next->blob_string->body.storage.any;
        gi->blob_type       = next->blob_string->body.storage_type;
        gi->pos             = next->start;
        gi->end             = next->end;
        gi->start           = next->start;
        gi->repetitions     = next->repetitions;
        gi->strands_remaining--;
        gi->next_strand++;
        remaining -= strand_graphs;
    }

    /* Now look within the strand. */
    while (1) {
        if (remaining == 0) {
            return;
        }
        if (gi->pos < gi->end) {
            if (gi->pos + remaining <= gi->end) {
                gi->pos += remaining;
                return;
            }
            remaining -= gi->end - gi->pos;
            gi->pos = gi->end;
        }
        else if (gi->repetitions) {
            MVMuint32 rep_graphs     = gi->end - gi->start;
            MVMuint32 remaining_reps = remaining / rep_graphs;
            if (remaining_reps > gi->repetitions)
                remaining_reps = gi->repetitions;
            gi->repetitions -= remaining_reps;
            remaining       -= remaining_reps * rep_graphs;
            if (gi->repetitions)
                gi->pos = gi->start;
        }
        else {
            MVM_exception_throw_adhoc(tc, "Iteration past end of grapheme iterator");
        }
    }
}

/* Checks if there is more to read from a grapheme iterator. */
MVM_STATIC_INLINE MVMint32 MVM_string_gi_has_more(MVMThreadContext *tc, MVMGraphemeIter *gi) {
    return gi->pos < gi->end || gi->repetitions || gi->strands_remaining;
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

/* Code point iterator. Uses the grapheme iterator, and adds some extra bits
 * in order to iterate the code points in synthetics. */
struct MVMCodepointIter {
    /* The grapheme iterator. */
    MVMGraphemeIter gi;

    /* TODO: more fields here. */
};

/* Initializes a code point iterator. */
MVM_STATIC_INLINE void MVM_string_ci_init(MVMThreadContext *tc, MVMCodepointIter *ci, MVMString *s) {
    /* Initialize our underlying grapheme iterator. */
    MVM_string_gi_init(tc, &(ci->gi), s);

    /* TODO: setup codepoint iteration related fields. */
};

/* Checks if there is more to read from a code point iterator. */
MVM_STATIC_INLINE MVMint32 MVM_string_ci_has_more(MVMThreadContext *tc, MVMCodepointIter *ci) {
    /* TODO: check if anything more from a current synthetic. */
    return MVM_string_gi_has_more(tc, &(ci->gi));
}

/* Gets the next code point. */
MVM_STATIC_INLINE MVMCodepoint MVM_string_ci_get_codepoint(MVMThreadContext *tc, MVMCodepointIter *ci) {
    MVMGrapheme32 g = MVM_string_gi_get_grapheme(tc, &(ci->gi));
    if (g >= 0) {
        /* It's not a synthetic, so just return it. */
        return (MVMCodepoint)g;
    }
    else {
        /* It's a synthetic. TODO: handle synthetics. */
        MVM_panic(1, "MVM_string_ci_get_codepoint synthetic handling NYI");
    }
}
