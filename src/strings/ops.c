#include "platform/memmem.h"
#include "moar.h"
#define MVM_DEBUG_STRANDS 0
#define MVM_string_KMP_max_pattern_length 4096
/* Max value possible for MVMuint32 MVMStringBody.num_graphs */
#define MAX_GRAPHEMES     0xFFFFFFFFLL

#if MVM_DEBUG_STRANDS
static void check_strand_sanity(MVMThreadContext *tc, MVMString *s) {
    MVMGraphemeIter gi;
    MVMuint32       len;
    MVM_string_gi_init(tc, &gi, s);
    len = 0;
    while (MVM_string_gi_has_more(tc, &gi)) {
        MVM_string_gi_get_grapheme(tc, &gi);
        len++;
    }
    if (len != MVM_string_graphs(tc, s))
        MVM_exception_throw_adhoc(tc,
            "Strand sanity check failed (strand length %d != num_graphs %d)",
            len, MVM_string_graphs(tc, s));
}
#define STRAND_CHECK(tc, s) check_strand_sanity(tc, s);
#else
#define STRAND_CHECK(tc, s)
#endif

static MVMString * re_nfg(MVMThreadContext *tc, MVMString *in);
#if MVM_DEBUG_NFG
static char * NFG_check_make_debug_string (MVMThreadContext *tc, MVMGrapheme32 g) {
    char *result = NULL;
    char *picked = NULL;
    if (g == '\r')
        picked = "\\r";
    else if (g == '\n')
        picked = "\\n";
    else if (g == MVM_nfg_crlf_grapheme(tc))
        picked = "\\r\\n";
    else if (0 <= g && !MVM_string_is_control_full(tc, g))
        result = MVM_string_utf8_encode_C_string(tc, MVM_string_chr(tc, g));
    else
        picked = "[Control]";
    if (picked) {
        result = MVM_malloc(sizeof(char) * (strlen(picked) + 1));
        strcpy(result, picked);
    }
    if (!result) {
        result = MVM_malloc(sizeof(char) * 1);
        result[0] = 0;
    }
    return result;
}
static char * NFG_checker (MVMThreadContext *tc, MVMString *orig, char *varname);
void NFG_check (MVMThreadContext *tc, MVMString *orig, char *varname) {
    char *out = NFG_checker(tc, orig, varname);
    char *waste[2] = { out, NULL };
    if (!out)
        return;
    MVM_exception_throw_adhoc_free(tc, waste, "%s", out);
}
static char * NFG_checker (MVMThreadContext *tc, MVMString *orig, char *varname) {
    MVMString *renorm = NULL;
    MVMStringIndex orig_graphs = MVM_string_graphs(tc, orig),
                   renorm_graphs = -1;
    MVMROOT(tc, orig, {
    MVMROOT(tc, renorm, {
        renorm = re_nfg(tc, orig);
        renorm_graphs = MVM_string_graphs(tc, renorm);
    });
    });
    if (MVM_DEBUG_NFG_STRICT || orig_graphs != renorm_graphs) {
        MVMGraphemeIter orig_gi, renorm_gi;
        MVMint64 index = 0;
        MVM_string_gi_init(tc, &orig_gi, orig);
        MVM_string_gi_init(tc, &renorm_gi, renorm);
        while (MVM_string_gi_has_more(tc,  &orig_gi) && MVM_string_gi_has_more(tc,  &renorm_gi)) {
            MVMGrapheme32 orig_g   = MVM_string_gi_get_grapheme(tc, &orig_gi),
                          renorm_g = MVM_string_gi_get_grapheme(tc, &renorm_gi);
            if (orig_g != renorm_g) {
                char *orig_render   = NFG_check_make_debug_string(tc, orig_g),
                     *renorm_render = NFG_check_make_debug_string(tc, renorm_g);
                char *format = "NFG failure. Got different grapheme count of %s. "
                    "Got %i but after re_nfg got %i\n"
                        "Differing grapheme at index %"PRIi64"\n"
                            "orig: %"PRIi32"  (%s)  after re_nfg: %"PRIi32"  (%s)\n";
                char *out = MVM_malloc(sizeof(char) * (
                    strlen(orig_render) + strlen(renorm_render)
                    + strlen(varname) + strlen(format) + (5 * 7)
                ) + 1);
                char *waste[] = {orig_render, renorm_render, NULL};
                char **w = waste;
                sprintf(out,
                    format,
                    varname,
                        orig_graphs, renorm_graphs,
                            index,
                                orig_g, orig_render, renorm_g, renorm_render);
                MVM_free(orig_render);
                MVM_free(renorm_render);
                return out;
            }
            index++;
        }
    }
    return NULL;
}
void NFG_check_concat (MVMThreadContext *tc, MVMString *result, MVMString *a, MVMString *b, char *varname) {
    char *a_out = NFG_checker(tc, a, "string ‘a’");
    char *b_out = NFG_checker(tc, b, "string ‘b’");
    char *out = NFG_checker(tc, result, varname);
    char *strings[] = { a_out, b_out, out };
    char *names[]   = { "\nconcat string ‘a’: ", "\nconcat string ‘b’: ", "\nconcat result: " };
    int i = 0, elems = 4;
    int rtrn = 0;
    char * empty = "";
    if (!a_out && !b_out && !out)
        return;
    else {
        MVMGrapheme32 last_a  =  MVM_string_get_grapheme_at_nocheck(tc, a, a->body.num_graphs - 1),
                      first_b = MVM_string_get_grapheme_at_nocheck(tc, b, 0);
        char   *debug_a = NFG_check_make_debug_string(tc, last_a),
               *debug_b = NFG_check_make_debug_string(tc, first_b),
             *escaped_a = MVM_string_utf8_encode_C_string(tc, MVM_string_escape(tc, a)),
             *escaped_b = MVM_string_utf8_encode_C_string(tc, MVM_string_escape(tc, b)),
        *escaped_result = MVM_string_utf8_encode_C_string(tc, MVM_string_escape(tc, result));
        char *waste[] = { out, debug_a, debug_b, escaped_a, escaped_b, escaped_result, NULL };
        MVM_exception_throw_adhoc_free(tc, waste,
            "In concat: a graphs: %"PRIi32" b graphs: %"PRIi32"\n"
            "last_a: %"PRIi32" (%s)  first_b %"PRIi32"  (%s)\n"
            "a: “%s”\n"
            "b: “%s”\n"
            "result: “%s”\n"
            "%s%s%s%s%s%s",
            MVM_string_graphs(tc, a), MVM_string_graphs(tc, b),
            last_a, debug_a, first_b, debug_b,
            escaped_a,
            escaped_b,
            escaped_result,
            (a_out?names[0]:""), (a_out?a_out:""),
            (b_out?names[1]:""), (b_out?b_out:""),
            (out?names[2]:""), (out?out:""));
        }


}
#endif

MVM_STATIC_INLINE MVMint64 string_equal_at_ignore_case_INTERNAL_loop(MVMThreadContext *tc, MVMString *Haystack, MVMString *needle_fc, MVMint64 H_start, MVMint64 H_graphs, MVMint64 n_fc_graphs, int ignoremark, int ignorecase);
static MVMint64 knuth_morris_pratt_string_index (MVMThreadContext *tc, MVMString *needle, MVMString *Haystack, MVMint64 H_offset);

/* Allocates strand storage. */
static MVMStringStrand * allocate_strands(MVMThreadContext *tc, MVMuint16 num_strands) {
    return MVM_malloc(num_strands * sizeof(MVMStringStrand));
}

/* Copies strands from one strand string to another. */
static void copy_strands(MVMThreadContext *tc, const MVMString *from, MVMuint16 from_offset,
        MVMString *to, MVMuint16 to_offset, MVMuint16 num_strands) {
    assert(from->body.storage_type == MVM_STRING_STRAND);
    assert(to->body.storage_type == MVM_STRING_STRAND);
    memcpy(
        to->body.storage.strands + to_offset,
        from->body.storage.strands + from_offset,
        num_strands * sizeof(MVMStringStrand));
}
/* Move strands inside the same strand string. */
static void move_strands(MVMThreadContext *tc, const MVMString *from, MVMuint16 from_offset,
        MVMString *to, MVMuint16 to_offset, MVMuint16 num_strands) {
    assert(from->body.storage_type == MVM_STRING_STRAND);
    assert(to->body.storage_type == MVM_STRING_STRAND);
    memmove(
        to->body.storage.strands + to_offset,
        from->body.storage.strands + from_offset,
        num_strands * sizeof(MVMStringStrand));
}

MVM_STATIC_INLINE int can_fit_into_8bit (MVMGrapheme32 g) {
    return -128 <= g && g <= 127;
}
MVM_STATIC_INLINE int can_fit_into_ascii (MVMGrapheme32 g) {
    return 0 <= g && g <= 127;
}
/* If a string is currently using 32bit storage, turn it into using
 * 8 bit storage. Doesn't do any checks at all. */
static void turn_32bit_into_8bit_unchecked(MVMThreadContext *tc, MVMString *str) {
    MVMGrapheme32 *old_buf = str->body.storage.blob_32;
    MVMStringIndex i;
    str->body.storage_type = MVM_STRING_GRAPHEME_8;
    str->body.storage.blob_8 = MVM_malloc(str->body.num_graphs * sizeof(MVMGrapheme8));

    for (i = 0; i < str->body.num_graphs; i++) {
        str->body.storage.blob_8[i] = old_buf[i];
    }

    MVM_free(old_buf);
}

/* Accepts an allocated string that should have body.num_graphs set but the blob
 * unallocated. This function will allocate the space for the blob and iterate
 * the supplied grapheme iterator for the length of body.num_graphs */
static void iterate_gi_into_string(MVMThreadContext *tc, MVMGraphemeIter *gi, MVMString *result) {
    MVMuint64 i;
    result->body.storage_type    = MVM_STRING_GRAPHEME_8;
    result->body.storage.blob_8  = MVM_malloc(result->body.num_graphs * sizeof(MVMGrapheme8));
    for (i = 0; i < result->body.num_graphs; i++) {
        MVMGrapheme32 g = MVM_string_gi_get_grapheme(tc, gi);
        result->body.storage.blob_8[i] = g;
        if (!can_fit_into_8bit(g)) {
            /* If we get here, we saw a codepoint lower than -127 or higher than 127
             * so turn it into a 32 bit string instead */
            /* Store the old string pointer and previous value of i */
            MVMGrapheme8 *old_ref = result->body.storage.blob_8;
            MVMuint64 prev_i = i;
            /* Set up the string as 32bit now and allocate space for it */
            result->body.storage_type    = MVM_STRING_GRAPHEME_32;
            result->body.storage.blob_32 = MVM_malloc(result->body.num_graphs * sizeof(MVMGrapheme32));
            /* Copy the data so far copied from the 8bit blob since it's faster than
             * setting up the grapheme iterator again */
            for (i = 0; i < prev_i; i++) {
                result->body.storage.blob_32[i] = old_ref[i];
            }
            MVM_free(old_ref);
            /* Store the grapheme which interupted the sequence. After that we can
             * continue from where we left off using the grapheme iterator */
            result->body.storage.blob_32[prev_i] = g;
            for (i = prev_i + 1; i < result->body.num_graphs; i++) {
                result->body.storage.blob_32[i] = MVM_string_gi_get_grapheme(tc, gi);
            }
        }
    }
}

/* Collapses a bunch of strands into a single blob string. */
static MVMString * collapse_strands(MVMThreadContext *tc, MVMString *orig) {
    MVMString      *result = (MVMString *)MVM_repr_alloc_init(tc, tc->instance->VMString);
    MVMGraphemeIter gi;
    MVMROOT(tc, orig, {
        MVM_string_gi_init(tc, &gi, orig);
        result->body.num_graphs = MVM_string_graphs(tc, orig);
        iterate_gi_into_string(tc, &gi, result);
    });
    return result;
}

/* Takes a string that is no longer in NFG form after some concatenation-style
 * operation, and returns a new string that is in NFG. Note that we could do a
 * much, much, smarter thing in the future that doesn't involve all of this
 * copying and allocation and re-doing the whole string, but cases like this
 * should be fairly rare anyway, so go with simplicity for now. */
static MVMString * re_nfg(MVMThreadContext *tc, MVMString *in) {
    MVMNormalizer norm;
    MVMCodepointIter ci;
    MVMint32 ready;
    MVMString *out = NULL;
    MVMuint32 bufsize = in->body.num_graphs;

    /* Create the output buffer. We used to believe it can't ever be bigger
     * than the initial estimate, but utf8-c8 showed us otherwise. */
    MVMGrapheme32 *out_buffer = MVM_malloc(bufsize * sizeof(MVMGrapheme32));
    MVMint64 out_pos = 0;

    /* Iterate codepoints and normalizer. */
    MVM_unicode_normalizer_init(tc, &norm, MVM_NORMALIZE_NFG);
    MVM_string_ci_init(tc, &ci, in, 0);
    while (MVM_string_ci_has_more(tc, &ci)) {
        MVMGrapheme32 g;
        ready = MVM_unicode_normalizer_process_codepoint_to_grapheme(tc, &norm, MVM_string_ci_get_codepoint(tc, &ci), &g);
        if (ready) {
            if (out_pos + ready > bufsize) {
                /* Doubling up the buffer size seems excessive, so just
                 * add a generous amount of storage */
                bufsize += ready + 32;
                out_buffer = MVM_realloc(out_buffer, bufsize * sizeof(MVMGrapheme32));
            }
            out_buffer[out_pos++] = g;
            while (--ready > 0) {
                out_buffer[out_pos++] = MVM_unicode_normalizer_get_grapheme(tc, &norm);
            }
        }
    }
    MVM_unicode_normalizer_eof(tc, &norm);
    ready = MVM_unicode_normalizer_available(tc, &norm);
    if (out_pos + ready > bufsize) {
        bufsize += ready + 1;
        out_buffer = MVM_realloc(out_buffer, bufsize * sizeof(MVMGrapheme32));
    }
    while (ready--) {
        out_buffer[out_pos++] = MVM_unicode_normalizer_get_grapheme(tc, &norm);
    }
    MVM_unicode_normalizer_cleanup(tc, &norm);

    /* Build result string. */
    out = (MVMString *)MVM_repr_alloc_init(tc, tc->instance->VMString);
    out->body.storage.blob_32 = out_buffer;
    out->body.storage_type    = MVM_STRING_GRAPHEME_32;
    out->body.num_graphs      = out_pos;
    return out;
}

/* Returns nonzero if two substrings are equal, doesn't check bounds */
MVMint64 MVM_string_substrings_equal_nocheck(MVMThreadContext *tc, MVMString *a,
        MVMint64 starta, MVMint64 length, MVMString *b, MVMint64 startb) {
    MVMGraphemeIter gia;
    MVMGraphemeIter gib;
    MVMint64 i;

    /* Fast paths when storage types are identical. */
    switch (a->body.storage_type) {
    case MVM_STRING_GRAPHEME_32:
        if (b->body.storage_type == MVM_STRING_GRAPHEME_32)
            return 0 == memcmp(
                a->body.storage.blob_32 + starta,
                b->body.storage.blob_32 + startb,
                length * sizeof(MVMGrapheme32));
        break;
    case MVM_STRING_GRAPHEME_ASCII:
    case MVM_STRING_GRAPHEME_8:
        if (b->body.storage_type == MVM_STRING_GRAPHEME_ASCII ||
                b->body.storage_type == MVM_STRING_GRAPHEME_8)
            return 0 == memcmp(
                a->body.storage.blob_8 + starta,
                b->body.storage.blob_8 + startb,
                length);
        break;
    }

    /* Normal path, for the rest of the time. */
    MVM_string_gi_init(tc, &gia, a);
    MVM_string_gi_init(tc, &gib, b);
    MVM_string_gi_move_to(tc, &gia, starta);
    MVM_string_gi_move_to(tc, &gib, startb);
    for (i = 0; i < length; i++)
        if (MVM_string_gi_get_grapheme(tc, &gia) != MVM_string_gi_get_grapheme(tc, &gib))
            return 0;
    return 1;
}

/* Returns the codepoint without doing checks, for internal VM use only. */
MVMGrapheme32 MVM_string_get_grapheme_at_nocheck(MVMThreadContext *tc, MVMString *a, MVMint64 index) {
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

/* Returns the location of one string in another or -1  */
MVMint64 MVM_string_index(MVMThreadContext *tc, MVMString *Haystack, MVMString *needle, MVMint64 start) {
    size_t index           = (size_t)start;
    MVMStringIndex H_graphs = MVM_string_graphs(tc, Haystack), n_graphs = MVM_string_graphs(tc, needle);
    MVM_string_check_arg(tc, Haystack, "index search target");
    MVM_string_check_arg(tc, needle, "index search term");

    if (!n_graphs)
        return start <= H_graphs ? start : -1; /* the empty string is in any other string */

    if (!H_graphs)
        return -1;

    if (start < 0 || start >= H_graphs)
        return -1;

    if (n_graphs > H_graphs || n_graphs < 1)
        return -1;

    /* Fast paths when storage types are identical. Uses memmem function, which
     * uses Knuth-Morris-Pratt algorithm on Linux and on others
     * Crochemore+Perrin two-way string matching */
    switch (Haystack->body.storage_type) {
        case MVM_STRING_GRAPHEME_32:
            if (needle->body.storage_type == MVM_STRING_GRAPHEME_32) {
                void *start_ptr = Haystack->body.storage.blob_32 + start;
                void *mm_return_32;
                void *end_ptr = (char*)start_ptr + sizeof(MVMGrapheme32) * (H_graphs - start);
                do {
                    /* Keep as void* to not lose precision */
                    mm_return_32 = MVM_memmem(
                        start_ptr, /* start position */
                        (char*)end_ptr - (char*)start_ptr, /* length of Haystack from start position to end */
                        needle->body.storage.blob_32, /* needle start */
                        n_graphs * sizeof(MVMGrapheme32) /* needle length */
                    );
                    if (mm_return_32 == NULL)
                        return -1;
                } /* If we aren't on a 32 bit boundary then continue from where we left off (unlikely but possible) */
                while ( ( (char*)mm_return_32 - (char*)Haystack->body.storage.blob_32) % sizeof(MVMGrapheme32)
                    && ( start_ptr = mm_return_32 ) /* Set the new start pointer at where we left off */
                    && ( end_ptr > start_ptr ) /* Check we aren't past the end of the string just in case */
                );

                return (MVMGrapheme32*)mm_return_32 - Haystack->body.storage.blob_32;
            }
            break;
        case MVM_STRING_GRAPHEME_8:
            if (needle->body.storage_type == MVM_STRING_GRAPHEME_8) {
                void *mm_return_8 = MVM_memmem(
                    Haystack->body.storage.blob_8 + start, /* start position */
                    (H_graphs - start) * sizeof(MVMGrapheme8), /* length of Haystack from start position to end */
                    needle->body.storage.blob_8, /* needle start */
                    n_graphs * sizeof(MVMGrapheme8) /* needle length */
                );
                if (mm_return_8 == NULL)
                    return -1;
                else
                    return (MVMGrapheme8*)mm_return_8 -  Haystack->body.storage.blob_8;
            }
            break;
    }
    if (n_graphs == 1) {
        MVMGraphemeIter H_gi;
        MVMGrapheme32 n_g = MVM_string_get_grapheme_at_nocheck(tc, needle, 0);
        MVM_string_gi_init(tc, &H_gi, Haystack);
        if (index) MVM_string_gi_move_to(tc, &H_gi, index);
        while (index < H_graphs) {
            if (n_g == MVM_string_gi_get_grapheme(tc, &H_gi))
                return (MVMint64)index;
            index++;
        }
    }
    if (1 < n_graphs && n_graphs <= MVM_string_KMP_max_pattern_length)
        return knuth_morris_pratt_string_index(tc, needle, Haystack, start);
    /* brute force is slightly faster for needles of size 1
     * For needles > MVM_string_KMP_max_pattern_length we must revert to brute force for now.
     * Eventually we can implement brute force after it matches the whole needle OR
     * allocate more space for the pattern on reaching the end of the pattern */
    while (index <= H_graphs - n_graphs) {
        if (string_equal_at_ignore_case_INTERNAL_loop(tc, Haystack, needle, index, H_graphs, n_graphs, 0, 0) != -1) {
            return (MVMint64)index;
        }
        index++;
    }
    return -1;
}

/* Returns the location of one string in another or -1  */
MVMint64 MVM_string_index_from_end(MVMThreadContext *tc, MVMString *Haystack, MVMString *needle, MVMint64 start) {
    MVMint64 result = -1;
    size_t index;
    MVMStringIndex H_graphs, n_graphs;

    MVM_string_check_arg(tc, Haystack, "rindex search target");
    MVM_string_check_arg(tc, needle, "rindex search term");
    H_graphs = MVM_string_graphs_nocheck(tc, Haystack);
    n_graphs = MVM_string_graphs_nocheck(tc, needle);

    if (!n_graphs) {
        if (start >= 0)
            return start <= H_graphs ? start : -1; /* the empty string is in any other string */
        else
            return H_graphs; /* no start, so return end */
    }

    if (!H_graphs)
        return -1;

    if (n_graphs > H_graphs || n_graphs < 1)
        return -1;

    if (start == -1)
        start = H_graphs - n_graphs;

    if (start < 0 || start >= H_graphs)
        /* maybe return -1 instead? */
        MVM_exception_throw_adhoc(tc, "index start offset out of range");

    index = start;

    if (index + n_graphs > H_graphs) {
        index = H_graphs - n_graphs;
    }

    /* brute force for now. horrible, yes. halp. */
    do {
        if (MVM_string_substrings_equal_nocheck(tc, needle, 0, n_graphs, Haystack, index)) {
            result = (MVMint64)index;
            break;
        }
    } while (index-- > 0);
    return result;
}

/* Returns a substring of the given string */
MVMString * MVM_string_substring(MVMThreadContext *tc, MVMString *a, MVMint64 offset, MVMint64 length) {
    MVMString *result;
    MVMint64   start_pos, end_pos;

    MVMint64 agraphs;

    MVM_string_check_arg(tc, a, "substring");
    /* convert to signed to avoid implicit arithmetic conversions */
    agraphs = (MVMint64)MVM_string_graphs_nocheck(tc, a);

    /* -1 signifies go to the end of the string; anything less is a bug */
    if (length < -1)
        MVM_exception_throw_adhoc(tc, "Substring length (%"PRId64") cannot be negative", length);

    /* negative offsets count from the end */
    start_pos = offset < 0 ? offset + agraphs : offset;
    end_pos   = length == -1 ? agraphs : start_pos + length;

    /* return an empty string if start_pos is out of bounds but positive */
    if (start_pos > agraphs) {
        start_pos = 0;
        end_pos   = 0;
    }

    if (end_pos < 0)
        MVM_exception_throw_adhoc(tc, "Substring end (%"PRId64") cannot be less than 0", end_pos);

    /* Ensure we're within bounds. */
    if (start_pos < 0)
        start_pos = 0;
    if (end_pos > agraphs)
        end_pos = agraphs;

    /* Check trivial cases: empty string and whole string. */
    if (start_pos == end_pos)
        return tc->instance->str_consts.empty;
    if (start_pos == 0 && end_pos == agraphs)
        return a;

    /* Construct a result; how we efficiently do so will vary based on the
     * input string. */
    MVMROOT(tc, a, {
        result = (MVMString *)MVM_repr_alloc_init(tc, tc->instance->VMString);
        result->body.num_graphs = end_pos - start_pos;
        if (a->body.storage_type != MVM_STRING_STRAND) {
            /* It's some kind of buffer. Construct a strand view into it. */
            result->body.storage_type    = MVM_STRING_STRAND;
            result->body.storage.strands = allocate_strands(tc, 1);
            result->body.num_strands     = 1;
            result->body.storage.strands[0].blob_string = a;
            result->body.storage.strands[0].start       = start_pos;
            result->body.storage.strands[0].end         = end_pos;
            result->body.storage.strands[0].repetitions = 0;
        }
        else if (a->body.num_strands == 1 && a->body.storage.strands[0].repetitions == 0) {
            /* Single strand string; quite possibly already a substring. We'll
             * just produce an updated view. */
            MVMStringStrand *orig_strand = &(a->body.storage.strands[0]);
            result->body.storage_type    = MVM_STRING_STRAND;
            result->body.storage.strands = allocate_strands(tc, 1);
            result->body.num_strands     = 1;
            result->body.storage.strands[0].blob_string = orig_strand->blob_string;
            result->body.storage.strands[0].start       = orig_strand->start + start_pos;
            result->body.storage.strands[0].end         = orig_strand->start + end_pos;
            result->body.storage.strands[0].repetitions = 0;
        }
        else {
            /* Produce a new blob string, collapsing the strands. */
            MVMGraphemeIter gi;
            MVM_string_gi_init(tc, &gi, a);
            MVM_string_gi_move_to(tc, &gi, start_pos);
            iterate_gi_into_string(tc, &gi, result);
        }
    });

    STRAND_CHECK(tc, result);
    return result;
}

MVMString * MVM_string_replace(MVMThreadContext *tc, MVMString *original, MVMint64 start, MVMint64 count, MVMString *replacement) {
    /* XXX this could probably be done more efficiently directly. */
    MVMString *first_part = NULL;
    MVMString *rest_part  = NULL;
    MVMString *result     = NULL;

    MVM_gc_root_temp_push(tc, (MVMCollectable **)&replacement);
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&original);
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&first_part);
    first_part = MVM_string_substring(tc, original, 0, start);

    rest_part  = MVM_string_substring(tc, original, start + count, -1);
    rest_part  = MVM_string_concatenate(tc, replacement, rest_part);
    result     = MVM_string_concatenate(tc, first_part, rest_part);


    STRAND_CHECK(tc, result);
    NFG_CHECK(tc, result, "MVM_string_replace");
    MVM_gc_root_temp_pop_n(tc, 3);
    return result;
}

/* Append one string to another. */
static MVMint32 final_strand_matches(MVMThreadContext *tc, MVMString *a, MVMString *b) {
    if (a->body.storage_type == MVM_STRING_STRAND) {
        MVMStringStrand *ss = &(a->body.storage.strands[a->body.num_strands - 1]);
        if (ss->end - ss->start == MVM_string_graphs(tc, b))
            if (MVM_string_equal_at(tc, ss->blob_string, b, ss->start))
                return 1;
    }
    return 0;
}
MVMString * MVM_string_concatenate(MVMThreadContext *tc, MVMString *a, MVMString *b) {
    MVMString *result = NULL, *renormalized_section = NULL;
    int renormalized_section_graphs = 0, consumed_a = 0, consumed_b = 0;
    MVMuint32  agraphs, bgraphs;
    MVMuint64  total_graphs;
    int lost_strands          = 0;
    int is_concat_stable      = 0;
    int index_ss_b;
    MVM_string_check_arg(tc, a, "concatenate");
    MVM_string_check_arg(tc, b, "concatenate");

    /* Simple empty-string cases. */
    agraphs = MVM_string_graphs_nocheck(tc, a);
    if (agraphs == 0)
        return b;
    bgraphs = MVM_string_graphs_nocheck(tc, b);
    if (bgraphs == 0)
        return a;

    is_concat_stable = MVM_nfg_is_concat_stable(tc, a, b);

    /* If is_concat_stable equals 0 and a and b are not repetitions. */
    if (is_concat_stable == 0 && !(a->body.storage_type == MVM_STRING_STRAND && a->body.storage.strands[a->body.num_strands - 1].repetitions)
    && !(b->body.storage_type == MVM_STRING_STRAND && b->body.storage.strands[0].repetitions)) {
        MVMCodepoint last_a_first_b[2] = {
            MVM_string_get_grapheme_at_nocheck(tc, a, a->body.num_graphs - 1),
            MVM_string_get_grapheme_at_nocheck(tc, b, 0)
        };
        MVMROOT(tc, a, {
        MVMROOT(tc, b, {
        /* Needing both to be CCC = 0 can probably be relaxed some, but be careful optimizing */
        if (0 <= last_a_first_b[0] && 0 <= last_a_first_b[1]) {
            renormalized_section = MVM_unicode_codepoints_c_array_to_nfg_string(tc, last_a_first_b, 2);
            consumed_a = 1; consumed_b = 1;
        }
        else {
            MVMCodepointIter last_a_ci;
            MVMCodepointIter first_b_ci;
            MVMuint32 a_codes = MVM_string_grapheme_ci_init(tc, &last_a_ci,  last_a_first_b[0]);
            MVMuint32 b_codes = MVM_string_grapheme_ci_init(tc, &first_b_ci, last_a_first_b[1]);
            /* MSVC doesn't allow variable length arrays so use alloca to allocate onto the stack */
            MVMCodepoint *last_a_first_b_codes = alloca((a_codes + b_codes) * sizeof(MVMCodepoint));
            MVMuint32 i = 0;
            for (; MVM_string_grapheme_ci_has_more(tc, &last_a_ci); i++) {
                last_a_first_b_codes[i] = MVM_string_grapheme_ci_get_codepoint(tc, &last_a_ci);
            }
            for (; MVM_string_grapheme_ci_has_more(tc, &first_b_ci); i++) {
                last_a_first_b_codes[i] = MVM_string_grapheme_ci_get_codepoint(tc, &first_b_ci);
            }
            renormalized_section = MVM_unicode_codepoints_c_array_to_nfg_string(tc, last_a_first_b_codes, a_codes + b_codes);
            consumed_a = 1; consumed_b = 1;
        }
        });
        });
        if (renormalized_section) {
            if (agraphs == consumed_a && bgraphs == consumed_b) {
                NFG_CHECK_CONCAT(tc, renormalized_section, a, b, "renormalized_section");
                return renormalized_section;
            }
            renormalized_section_graphs = MVM_string_graphs_nocheck(tc, renormalized_section);
        }
    }
    /* Total size of the resulting string can't be bigger than an MVMString is allowed to be. */
    total_graphs = (MVMuint64)agraphs + (MVMuint64)bgraphs;
    if (total_graphs > MAX_GRAPHEMES)
        MVM_exception_throw_adhoc(tc,
            "Can't concatenate strings, required number of graphemes %"PRIu64" > max allowed of %lld",
             total_graphs, MAX_GRAPHEMES);

    /* Otherwise, we'll assemble a result string. */
    MVMROOT(tc, a, {
    MVMROOT(tc, b, {
    MVMROOT(tc, renormalized_section, {
    MVMROOT(tc, result, {

        /* Allocate it. */
        result = (MVMString *)MVM_repr_alloc_init(tc, tc->instance->VMString);

        /* Total graphemes is trivial; just total up inputs. */
        result->body.num_graphs = (MVMuint32)total_graphs;

        /* Result string will be made of strands. */
        result->body.storage_type = MVM_STRING_STRAND;

        /* Detect the wonderful case where we're repeatedly concating the same
         * string again and again, and thus can just bump a repetition. */
        if (is_concat_stable == 1 && final_strand_matches(tc, a, b)) {
            /* We have it; just copy the strands to a new string and bump the
             * repetitions count of the last one. */
            result->body.storage.strands = allocate_strands(tc, a->body.num_strands);
            copy_strands(tc, a, 0, result, 0, a->body.num_strands);
            result->body.storage.strands[a->body.num_strands - 1].repetitions++;
            result->body.num_strands = a->body.num_strands;
        }

        /* Otherwise, construct a new strand string. */
        else {
            /* See if we have too many strands between the two. If so, we will
             * collapse the biggest side. */
            MVMuint16 strands_a = a->body.storage_type == MVM_STRING_STRAND
                ? a->body.num_strands
                : 1;
            MVMuint16 strands_b = b->body.storage_type == MVM_STRING_STRAND
                ? b->body.num_strands
                : 1;
            MVMString *effective_a = a;
            MVMString *effective_b = b;
            if (strands_a + strands_b > MVM_STRING_MAX_STRANDS) {
                MVMROOT(tc, result, {
                    if (strands_a >= strands_b) {
                        effective_a = collapse_strands(tc, effective_a);
                        strands_a   = 1;
                    }
                    else {
                        effective_b = collapse_strands(tc, effective_b);
                        strands_b   = 1;
                    }
                });
            }
            /* Assemble the result. */
            result->body.num_strands = strands_a + strands_b + (renormalized_section_graphs ? 1 : 0);
            result->body.storage.strands = allocate_strands(tc, result->body.num_strands);
            /* START 1 */
            if (effective_a->body.storage_type == MVM_STRING_STRAND) {
                copy_strands(tc, effective_a, 0, result, 0, strands_a);
            }
            else {
                int index_ss_a = 0;
                MVMStringStrand *ss_a = &(result->body.storage.strands[index_ss_a]);
                ss_a->blob_string = effective_a;
                ss_a->start       = 0;
                ss_a->end         = effective_a->body.num_graphs;
                ss_a->repetitions = 0;
            }
            if (renormalized_section) {
                int index_ss_re;
                int index_ss_a = strands_a - 1;
                /* Tweak the end of the last strand of string a. Since if a is made up of multiple strands, we can't just refer to index 0 and instead erfer to strands_a - 1 */
                MVMStringStrand *ss_a = &(result->body.storage.strands[index_ss_a]);
                MVMStringStrand *ss_re = NULL;
                ss_a->end -= consumed_a;
                /* If the strands ends up to be zero length we need to reduce the number of strand_index and also incease lost_strands so the next operation writes over it */
                if (ss_a->start == ss_a->end)
                    lost_strands++;
            /* END 1 */
            /* START 1.5 (only triggered in some cases) */
                index_ss_re = strands_a - lost_strands;
                ss_re = &(result->body.storage.strands[index_ss_re]);

                /* Add the renormalized section in as a strand */
                ss_re->blob_string = renormalized_section;
                ss_re->start       = 0;
                ss_re->end         = renormalized_section->body.num_graphs;
                ss_re->repetitions = 0;
                if (ss_re->start == ss_re->end) {
                    MVM_exception_throw_adhoc(tc, "Unexpected error in concatenation: renormalized_section is 0 graphemes.\n");
                    /* renormalized_section should always be at least one grapheme
                     * in length so throw if it does not (zero length is an error
                     * we shouldn't lost_strands++ unlike the other strands */
                }
            /* END 1.5 */
            }
            /* START 2 */
            index_ss_b = strands_a - lost_strands + (renormalized_section_graphs ? 1 : 0 );
            if (effective_b->body.storage_type == MVM_STRING_STRAND) {
                copy_strands(tc, effective_b, 0, result, index_ss_b, strands_b);
            }
            else {
                MVMStringStrand *ss_b = &(result->body.storage.strands[index_ss_b]);
                ss_b->blob_string = effective_b;
                ss_b->start       = 0;
                ss_b->end         = effective_b->body.num_graphs;
                ss_b->repetitions = 0;
            }
            if (renormalized_section_graphs) {
                /* Tweak the beginning of the first strand of string b */
                MVMStringStrand *ss_b = &(result->body.storage.strands[index_ss_b]);
                ss_b->start += consumed_b;
                if (ss_b->start == ss_b->end) {
                    lost_strands++;
                    move_strands(tc, result, index_ss_b + 1, result, index_ss_b, strands_b - 1);
                }
            /* END 2 */
            /* Adjust result->num_strands */
                if (lost_strands)
                    result->body.num_strands -= lost_strands;
                /* Adjust result->num_graphs */
                result->body.num_graphs += renormalized_section_graphs - consumed_b - consumed_a;
            }
        }
    STRAND_CHECK(tc, result);
    if (is_concat_stable == 1 || (is_concat_stable == 0 && renormalized_section))
        NFG_CHECK_CONCAT(tc, result, a, b, "'result'");
    });
    });
    });
    });
    if (is_concat_stable == 1 || (is_concat_stable == 0 && renormalized_section))
        return result;
    /* If it's regional indicator (is_concat_stable == 2) */
    return re_nfg(tc, result);
}

MVMString * MVM_string_repeat(MVMThreadContext *tc, MVMString *a, MVMint64 count) {
    MVMString *result = NULL;
    MVMuint32  agraphs;
    MVMuint64  total_graphs;

    MVM_string_check_arg(tc, a, "repeat");

    /* Validate count; handle common cases. */
    if (count == 0)
        return tc->instance->str_consts.empty;
    if (count == 1)
        return a;
    if (count < 0)
        MVM_exception_throw_adhoc(tc, "Repeat count (%"PRId64") cannot be negative", count);
    if (count > MAX_GRAPHEMES)
        MVM_exception_throw_adhoc(tc, "Repeat count (%"PRId64") cannot be greater than max allowed number of graphemes %lld", count, MAX_GRAPHEMES);

    /* If input string is empty, repeating it is empty. */
    agraphs = MVM_string_graphs_nocheck(tc, a);
    if (agraphs == 0)
        return tc->instance->str_consts.empty;

    /* Total size of the resulting string can't be bigger than an MVMString is allowed to be. */
    total_graphs = (MVMuint64)agraphs * (MVMuint64)count;
    if (total_graphs > MAX_GRAPHEMES)
        MVM_exception_throw_adhoc(tc,
            "Can't repeat string, required number of graphemes (%"PRIu32" * %"PRIu64") greater than max allowed of %lld",
             agraphs, count, MAX_GRAPHEMES);

    /* Now build a result string with the repetition set. */
    MVMROOT(tc, a, {
        result = (MVMString *)MVM_repr_alloc_init(tc, tc->instance->VMString);
        result->body.num_graphs      = agraphs * count;
        result->body.storage_type    = MVM_STRING_STRAND;
        result->body.storage.strands = allocate_strands(tc, 1);
        if (a->body.storage_type == MVM_STRING_STRAND) {
            if (a->body.num_strands == 1 && a->body.storage.strands[0].repetitions == 0) {
                copy_strands(tc, a, 0, result, 0, 1);
            }
            else {
                MVMROOT(tc, result, {
                    a = collapse_strands(tc, a);
                });
                result->body.storage.strands[0].blob_string = a;
                result->body.storage.strands[0].start       = 0;
                result->body.storage.strands[0].end         = agraphs;
            }
        }
        else {
            result->body.storage.strands[0].blob_string = a;
            result->body.storage.strands[0].start       = 0;
            result->body.storage.strands[0].end         = agraphs;
        }
        result->body.storage.strands[0].repetitions = count - 1;
        result->body.num_strands = 1;
    });
    /* If string a is not stable under concatenation, we need to create a flat
     * string and ensure it is normalized */
    if (!MVM_nfg_is_concat_stable(tc, a, a))
        result = re_nfg(tc, result);
    STRAND_CHECK(tc, result);
    return result;
}

void MVM_string_say(MVMThreadContext *tc, MVMString *a) {
    MVM_string_check_arg(tc, a, "say");
    MVM_string_print(tc, MVM_string_concatenate(tc, a,
        tc->instance->str_consts.platform_newline));
}

void MVM_string_print(MVMThreadContext *tc, MVMString *a) {
    MVMOSHandle *handle = (MVMOSHandle *)tc->instance->stdout_handle;
    MVMuint64 encoded_size;
    char *encoded;
    MVM_string_check_arg(tc, a, "print");
    encoded = MVM_string_utf8_encode(tc, a, &encoded_size, MVM_TRANSLATE_NEWLINE_OUTPUT);
    MVM_io_write_bytes_c(tc, tc->instance->stdout_handle, encoded, encoded_size);
    MVM_free(encoded);
}
/* Meant to be pased in a MVMNormalizer of type MVM_NORMALIZE_NFD */
static MVMGrapheme32 ord_getbasechar (MVMThreadContext *tc, MVMGrapheme32 g) {
    /* If we get a synthetic, extract the base codepoint and call ord_getbasechar again */
    if (g < 0)
        return ord_getbasechar(tc, MVM_nfg_get_synthetic_info(tc, g)->base);
    else {
        MVMGrapheme32 return_g;
        MVMint32 ready;
        MVMNormalizer norm;
        MVM_unicode_normalizer_init(tc, &norm, MVM_NORMALIZE_NFD);

        ready = MVM_unicode_normalizer_process_codepoint_to_grapheme(tc, &norm, g, &return_g);
        MVM_unicode_normalizer_eof(tc, &norm);
        if (!ready)
            return_g = MVM_unicode_normalizer_get_grapheme(tc, &norm);
        MVM_unicode_normalizer_cleanup(tc, &norm);
        return return_g;
    }
}
/* Tests whether one string a has the other string b as a substring at that index */
MVMint64 MVM_string_equal_at(MVMThreadContext *tc, MVMString *a, MVMString *b, MVMint64 offset) {

    MVMStringIndex agraphs, bgraphs;

    MVM_string_check_arg(tc, a, "equal_at");
    MVM_string_check_arg(tc, b, "equal_at");

    agraphs = MVM_string_graphs_nocheck(tc, a);
    bgraphs = MVM_string_graphs_nocheck(tc, b);

    if (offset < 0) {
        offset += agraphs;
        if (offset < 0)
            offset = 0; /* XXX I think this is the right behavior here */
    }
    if (agraphs - offset < bgraphs)
        return 0;
    return MVM_string_substrings_equal_nocheck(tc, a, offset, bgraphs, b, 0);
}
/* Ensure return value can hold numbers at least 3x higher than MVMStringIndex.
 * Theoretically if the string has all ﬃ ligatures and 1/3 the max size of
 * MVMStringIndex in length, we could have some weird results. */

/* ignoremark is 0 for normal operation and 1 for ignoring diacritics */
MVM_STATIC_INLINE MVMint64 string_equal_at_ignore_case_INTERNAL_loop(MVMThreadContext *tc, MVMString *Haystack, MVMString *needle_fc, MVMint64 H_start, MVMint64 H_graphs, MVMint64 n_fc_graphs, int ignoremark, int ignorecase) {
    MVMuint32 H_fc_cps;
    /* An additional needle offset which is used only when codepoints expand
     * when casefolded. The offset is the number of additional codepoints that
     * have been seen so Haystack and needle stay aligned */
    MVMint64 n_offset = 0;
    MVMint64 i, j;
    MVMGrapheme32 H_g, n_g;
    for (i = 0; i + H_start < H_graphs && i + n_offset < n_fc_graphs; i++) {
        const MVMCodepoint* H_result_cps;
        H_g = MVM_string_get_grapheme_at_nocheck(tc, Haystack, H_start + i);
        if (!ignorecase) {
            H_fc_cps = 0;
        }
        else if (H_g >= 0) {
            /* For codeponits we can get the case change directly */
            H_fc_cps = MVM_unicode_get_case_change(tc, H_g, MVM_unicode_case_change_type_fold, &H_result_cps);
        }
        else {
            /* Synthetics must use this function */
            H_fc_cps = MVM_nfg_get_case_change(tc, H_g, MVM_unicode_case_change_type_fold, (MVMGrapheme32**) &H_result_cps);
        }
        /* If we get 0 for the number that means the cp doesn't change when casefolded */
        if (H_fc_cps == 0) {
            n_g = MVM_string_get_grapheme_at_nocheck(tc, needle_fc, i + n_offset);
            if (ignoremark) {
                H_g = ord_getbasechar(tc, H_g);
                n_g = ord_getbasechar(tc, n_g);
            }
            if (H_g != n_g)
                return -1;
        }
        else if (H_fc_cps >= 1) {
            for (j = 0; j < H_fc_cps; j++) {
                n_g = MVM_string_get_grapheme_at_nocheck(tc, needle_fc, i + n_offset);
                H_g = H_result_cps[j];
                if (ignoremark) {
                    H_g = ord_getbasechar(tc, H_g);
                    n_g = ord_getbasechar(tc, n_g);
                }
                if (H_g != n_g)
                    return -1;
                n_offset++;
            }
            n_offset--;
        }
    }
    return n_offset;
    /* We return -1 if the strings are not equal and 0 or more if they are equal
     * The return values from 0, 1 etc designate how many Haystack graphemes
     * were expanded.
     * This may seem like an odd arangement, but this extra information is needed
     * to determine the length of the Haystack which was traversed, as it can
     * differ from the length of the needle if there are expansions. */
}
/* Checks if needle exists at the offset, but ignores case.
 * Sometimes there is a difference in length of a string before and after foldcase,
 * because of this we must compare this differently than just foldcasing both
 * strings to ensure the offset is correct */
static MVMint64 string_equal_at_ignore_case(MVMThreadContext *tc, MVMString *Haystack, MVMString *needle, MVMint64 H_offset, int ignoremark, int ignorecase) {
    /* Foldcase version of needle */
    MVMString *needle_fc = NULL;
    MVMStringIndex H_graphs = MVM_string_graphs(tc, Haystack);
    MVMStringIndex n_graphs = MVM_string_graphs(tc, needle);
    MVMStringIndex n_fc_graphs;
    /* H_expansion must be able to hold integers 3x larger than MVMStringIndex */
    MVMint64 H_expansion;

    if (H_offset < 0) {
        H_offset += H_graphs;
        if (H_offset < 0)
            H_offset = 0; /* XXX I think this is the right behavior here */
    }
    /* If the offset is greater or equal to the number of Haystack graphemes
     * return 0. Since size of graphemes could change under casefolding, we
     * can't assume too much. If optimizing this be careful */
    if (H_offset >= H_graphs)
        return 0;
    MVMROOT(tc, Haystack, {
        needle_fc = ignorecase ? MVM_string_fc(tc, needle) : needle;
    });
    n_fc_graphs = MVM_string_graphs(tc, needle_fc);
    H_expansion = string_equal_at_ignore_case_INTERNAL_loop(tc, Haystack, needle_fc, H_offset, H_graphs, n_fc_graphs, ignoremark, ignorecase);
    if (H_expansion >= 0)
        return H_graphs + H_expansion - H_offset >= n_fc_graphs  ? 1 : 0;
    return 0;
}
static void knuth_morris_pratt_process_pattern (MVMThreadContext *tc, MVMString *pat, MVMGrapheme32 *next, MVMStringIndex pat_graphs) {
    MVMint64 i = 0;
    MVMint64 j = next[0] = -1;
    while (i < pat_graphs) {
        if (j == -1 || MVM_string_get_grapheme_at_nocheck(tc, pat, i)
                    == MVM_string_get_grapheme_at_nocheck(tc, pat, j)) {
            i++; j++;
            next[i] = (i < pat_graphs
            && MVM_string_get_grapheme_at_nocheck(tc, pat, j)
            == MVM_string_get_grapheme_at_nocheck(tc, pat, i))
                ? next[j]
                : j;
        }
        else j = next[j];
    }
}
static MVMint64 knuth_morris_pratt_string_index (MVMThreadContext *tc, MVMString *needle, MVMString *Haystack, MVMint64 H_offset) {
    MVMint64 needle_offset = 0;
    MVMint64 text_offset   = H_offset;
    MVMStringIndex Haystack_graphs = MVM_string_graphs(tc, Haystack);
    MVMStringIndex needle_graphs   = MVM_string_graphs(tc, needle);
    MVMGrapheme32    *next = NULL;
    MVMString *flat_needle = NULL;
    assert(needle_graphs <= MVM_string_KMP_max_pattern_length);
    /* Empty string is found at start of string */
    if (needle_graphs == 0)
        return 0;
    next = alloca((1 + needle_graphs) * sizeof(MVMGrapheme32));
    /* If the needle is a strand, flatten it, otherwise use the original string */
    if (needle->body.storage_type == MVM_STRING_STRAND) {
        flat_needle = collapse_strands(tc, needle);
    }
    else flat_needle = needle;
    /* Process the needle into a jump table put into variable 'next' */
    knuth_morris_pratt_process_pattern(tc, flat_needle, next, needle_graphs);
    while (text_offset < Haystack_graphs && needle_offset < needle_graphs) {
        if (needle_offset == -1 || MVM_string_get_grapheme_at_nocheck(tc, flat_needle, needle_offset)
                                == MVM_string_get_grapheme_at_nocheck(tc, Haystack, text_offset)) {
            text_offset++; needle_offset++;
            if (needle_offset == needle_graphs) {
                return text_offset - needle_offset;
            }
        }
        else needle_offset = next[needle_offset];
    }
    return -1;
}
static MVMint64 string_index_ignore_case(MVMThreadContext *tc, MVMString *Haystack, MVMString *needle, MVMint64 start, int ignoremark, int ignorecase) {
    /* Foldcase version of needle */
    MVMString *needle_fc = NULL;
    MVMStringIndex n_fc_graphs;

    size_t index           = (size_t)start;
    MVMStringIndex H_graphs, n_graphs;
    /* H_expansion must be able to hold integers 3x larger than MVMStringIndex */
    MVMint64 H_expansion;
    MVMint64 return_val = -1;
    MVM_string_check_arg(tc, Haystack, ignoremark ? "index ignore case ignore mark search target" : "index ignore case search target");
    MVM_string_check_arg(tc, needle,   ignoremark ? "index ignore case ignore mark search term"   : "index ignore case search term");
    H_graphs = MVM_string_graphs_nocheck(tc, Haystack);
    n_graphs = MVM_string_graphs_nocheck(tc, needle);
    if (!n_graphs)
        return start <= H_graphs ? start : -1; /* Empty string is in any other string */
    if (!H_graphs)
        return -1;
    if (start < 0 || start >= H_graphs)
        return -1;
    /* Codepoints can expand into up to THREE codepoints (as of Unicode 9.0). The next check
     * checks if it is at all possible for the needle grapheme number to be higher
     * than the Haystack */
    if (n_graphs > H_graphs * 3)
        return -1;

    if (n_graphs < 1)
        return -1;

    MVMROOT(tc, Haystack, {
        needle_fc = ignorecase ? MVM_string_fc(tc, needle) : needle;
    });
    n_fc_graphs = MVM_string_graphs(tc, needle_fc);
    /* brute force for now. horrible, yes. halp. */
    while (index <= H_graphs) {
        H_expansion = string_equal_at_ignore_case_INTERNAL_loop(tc, Haystack, needle_fc, index, H_graphs, n_fc_graphs, ignoremark, ignorecase);
        if (H_expansion >= 0)
            return H_graphs + H_expansion - index >= n_fc_graphs  ? (MVMint64)index : -1;
        index++;
    }
    return -1;
}

MVMint64 MVM_string_equal_at_ignore_case(MVMThreadContext *tc, MVMString *Haystack, MVMString *needle, MVMint64 H_offset) {
    return string_equal_at_ignore_case(tc, Haystack, needle, H_offset, 0, 1);
}
MVMint64 MVM_string_index_ignore_case(MVMThreadContext *tc, MVMString *Haystack, MVMString *needle, MVMint64 start) {
    return string_index_ignore_case(tc, Haystack, needle, start, 0, 1);
}
MVMint64 MVM_string_equal_at_ignore_mark(MVMThreadContext *tc, MVMString *Haystack, MVMString *needle, MVMint64 H_offset) {
    return string_equal_at_ignore_case(tc, Haystack, needle, H_offset, 1, 0);
}
MVMint64 MVM_string_index_ignore_mark(MVMThreadContext *tc, MVMString *Haystack, MVMString *needle, MVMint64 start) {
    return string_index_ignore_case(tc, Haystack, needle, start, 1, 0);
}
MVMint64 MVM_string_equal_at_ignore_case_ignore_mark(MVMThreadContext *tc, MVMString *Haystack, MVMString *needle, MVMint64 H_offset) {
    return string_equal_at_ignore_case(tc, Haystack, needle, H_offset, 1, 1);
}
MVMint64 MVM_string_index_ignore_case_ignore_mark(MVMThreadContext *tc, MVMString *Haystack, MVMString *needle, MVMint64 start) {
    return string_index_ignore_case(tc, Haystack, needle, start, 1, 1);
}

MVMGrapheme32 MVM_string_ord_at(MVMThreadContext *tc, MVMString *s, MVMint64 offset) {
    MVMStringIndex agraphs;
    MVMGrapheme32 g;

    MVM_string_check_arg(tc, s, "grapheme_at");

    agraphs = MVM_string_graphs(tc, s);
    if (offset < 0 || offset >= agraphs)
        return -1;

    g = MVM_string_get_grapheme_at_nocheck(tc, s, offset);

    return g >= 0 ? g : MVM_nfg_get_synthetic_info(tc, g)->base;
}

/* Gets the base character at a grapheme position, ignoring things like diacritics */
MVMGrapheme32 MVM_string_ord_basechar_at(MVMThreadContext *tc, MVMString *s, MVMint64 offset) {
    MVMStringIndex agraphs;
    MVMint32 ready;

    MVM_string_check_arg(tc, s, "ord_basechar_at");

    agraphs = MVM_string_graphs_nocheck(tc, s);
    if (offset < 0 || offset >= agraphs)
        return -1;  /* fixes RT #126771 */

    return ord_getbasechar(tc, MVM_string_get_grapheme_at_nocheck(tc, s, offset));
}


/* Compares two strings for equality. */
MVMint64 MVM_string_equal(MVMThreadContext *tc, MVMString *a, MVMString *b) {
    MVMStringIndex agraphs, bgraphs;

    MVM_string_check_arg(tc, a, "equal");
    MVM_string_check_arg(tc, b, "equal");

    if (a == b)
        return 1;

    agraphs = MVM_string_graphs_nocheck(tc, a);
    bgraphs = MVM_string_graphs_nocheck(tc, b);

    if (agraphs != bgraphs)
        return 0;

    return MVM_string_substrings_equal_nocheck(tc, a, 0, bgraphs, b, 0);
}

/* more general form of has_at; compares two substrings for equality */
MVMint64 MVM_string_have_at(MVMThreadContext *tc, MVMString *a,
        MVMint64 starta, MVMint64 length, MVMString *b, MVMint64 startb) {

    MVM_string_check_arg(tc, a, "have_at");
    MVM_string_check_arg(tc, b, "have_at");

    if (starta < 0 || startb < 0)
        return 0;
    if (length == 0)
        return 1;
    if (starta + length > MVM_string_graphs_nocheck(tc, a) || startb + length > MVM_string_graphs_nocheck(tc, b))
        return 0;

    return MVM_string_substrings_equal_nocheck(tc, a, starta, length, b, startb);
}

/* Returns the grapheme at a given index of the string */
MVMint64 MVM_string_get_grapheme_at(MVMThreadContext *tc, MVMString *a, MVMint64 index) {
    MVMStringIndex agraphs;

    MVM_string_check_arg(tc, a, "grapheme_at");

    agraphs = MVM_string_graphs_nocheck(tc, a);

    if (index < 0 || index >= agraphs)
        MVM_exception_throw_adhoc(tc, "Invalid string index: max %"PRId32", got %"PRId64,
            agraphs - 1, index);

    return (MVMint64)MVM_string_get_grapheme_at_nocheck(tc, a, index);
}

/* Finds the location of a grapheme in a string.  Useful for small character class lookup */
MVMint64 MVM_string_index_of_grapheme(MVMThreadContext *tc, MVMString *a, MVMGrapheme32 grapheme) {
    size_t index = -1;
    MVMGraphemeIter gi;

    MVM_string_check_arg(tc, a, "string_index_of_grapheme");

    MVM_string_gi_init(tc, &gi, a);
    while (MVM_string_gi_has_more(tc, &gi)) {
        index++;
        if (MVM_string_gi_get_grapheme(tc, &gi) == grapheme)
            return index;
    }
    return -1;
}

/* Case change functions. */
static MVMint64 grapheme_is_cclass(MVMThreadContext *tc, MVMint64 cclass, MVMGrapheme32 g);
static MVMString * do_case_change(MVMThreadContext *tc, MVMString *s, MVMint32 type, char *error) {
    MVMint64 sgraphs;
    MVM_string_check_arg(tc, s, error);
    sgraphs = MVM_string_graphs_nocheck(tc, s);
    if (sgraphs) {
        MVMString *result;
        MVMGraphemeIter gi;
        MVMint64 result_graphs = sgraphs;
        MVMGrapheme32 *result_buf = MVM_malloc(result_graphs * sizeof(MVMGrapheme32));
        MVMint32 changed = 0;
        MVMint64 i = 0;
        MVM_string_gi_init(tc, &gi, s);
        while (MVM_string_gi_has_more(tc, &gi)) {
            MVMGrapheme32 g = MVM_string_gi_get_grapheme(tc, &gi);
          peeked:
            if (g == 0x03A3) {
                /* Greek sigma needs special handling when lowercased. */
                switch (type) {
                    case MVM_unicode_case_change_type_upper:
                    case MVM_unicode_case_change_type_title:
                        result_buf[i++] = g;
                        break;
                    case MVM_unicode_case_change_type_lower:
                        changed = 1;
                        if (i == 0) {
                            /* Start of string, so not final. */
                            result_buf[i++] = 0x03C3;
                        }
                        else if (!grapheme_is_cclass(tc, MVM_CCLASS_ALPHABETIC, result_buf[i - 1])) {
                            /* Previous char is not a letter; not final (as has
                             * to be at end of a word and not only thing in a
                             * word). */
                            result_buf[i++] = 0x03C3;
                        }
                        else if (!MVM_string_gi_has_more(tc, &gi)) {
                            /* End of string. We only reach here if we have a
                             * letter before us, so it must be final. */
                            result_buf[i++] = 0x03C2;
                        }
                        else {
                            /* Letter before us, something ahead of us. Need to
                             * peek ahead to see if it's a letter, to decide if
                             * we have final sigma or not. */
                            g = MVM_string_gi_get_grapheme(tc, &gi);
                            if (grapheme_is_cclass(tc, MVM_CCLASS_ALPHABETIC, g))
                                result_buf[i++] = 0x03C3;
                            else
                                result_buf[i++] = 0x03C2;
                            goto peeked;
                        }
                        break;
                    case MVM_unicode_case_change_type_fold:
                        result_buf[i++] = 0x03C3;
                        changed = 1;
                        break;
                }
            }
            else if (g >= 0) {
                const MVMCodepoint *result_cps;
                MVMuint32 num_result_cps = MVM_unicode_get_case_change(tc,
                    g, type, &result_cps);
                if (num_result_cps == 0) {
                    result_buf[i++] = g;
                }
                else if (num_result_cps == 1) {
                    result_buf[i++] = *result_cps;
                    changed = 1;
                }
                else {
                    /* To maintain NFG, we need to re-normalize when we get an
                     * expansion. */
                    MVMNormalizer norm;
                    MVMint32 num_result_graphs;
                    MVM_unicode_normalizer_init(tc, &norm, MVM_NORMALIZE_NFG);
                    MVM_unicode_normalizer_push_codepoints(tc, &norm, result_cps, num_result_cps);
                    MVM_unicode_normalizer_eof(tc, &norm);
                    num_result_graphs = MVM_unicode_normalizer_available(tc, &norm);

                    /* Make space for any extra graphemes. */
                    if (num_result_graphs > 1) {
                        result_graphs += num_result_graphs - 1;
                        result_buf = MVM_realloc(result_buf,
                            result_graphs * sizeof(MVMGrapheme32));
                    }

                    /* Copy resulting graphemes. */
                    while (num_result_graphs > 0) {
                        result_buf[i++] = MVM_unicode_normalizer_get_grapheme(tc, &norm);
                        num_result_graphs--;
                    }
                    changed = 1;

                    /* Clean up normalizer (we could init one per transform
                     * and keep it around in the future, if we find it's a
                     * worthwhile gain). */
                    MVM_unicode_normalizer_cleanup(tc, &norm);
                }
            }
            else {
                MVMGrapheme32 *transformed;
                MVMint32 num_transformed = MVM_nfg_get_case_change(tc, g, type, &transformed);
                if (num_transformed == 0) {
                    result_buf[i++] = g;
                }
                else if (num_transformed == 1) {
                    result_buf[i++] = *transformed;
                    changed = 1;
                }
                else {
                    MVMuint32 j;
                    result_graphs += num_transformed - 1;
                    result_buf = MVM_realloc(result_buf,
                        result_graphs * sizeof(MVMGrapheme32));
                    for (j = 0; j < num_transformed; j++)
                        result_buf[i++] = transformed[j];
                    changed = 1;
                }
            }
        }
        if (changed) {
            result = (MVMString *)MVM_repr_alloc_init(tc, tc->instance->VMString);
            result->body.num_graphs      = result_graphs;
            result->body.storage_type    = MVM_STRING_GRAPHEME_32;
            result->body.storage.blob_32 = result_buf;
            return result;
        }
        else {
            MVM_free(result_buf);
        }
    }
    STRAND_CHECK(tc, s);
    return s;
}
MVMString * MVM_string_uc(MVMThreadContext *tc, MVMString *s) {
    return do_case_change(tc, s, MVM_unicode_case_change_type_upper, "uc");
}
MVMString * MVM_string_lc(MVMThreadContext *tc, MVMString *s) {
    return do_case_change(tc, s, MVM_unicode_case_change_type_lower, "lc");
}
MVMString * MVM_string_tc(MVMThreadContext *tc, MVMString *s) {
    return do_case_change(tc, s, MVM_unicode_case_change_type_title, "tc");
}
MVMString * MVM_string_fc(MVMThreadContext *tc, MVMString *s) {
    return do_case_change(tc, s, MVM_unicode_case_change_type_fold, "fc");
}

/* Decodes a C buffer to an MVMString, dependent on the encoding type flag. */
MVMString * MVM_string_decode(MVMThreadContext *tc,
        const MVMObject *type_object, char *Cbuf, MVMint64 byte_length, MVMint64 encoding_flag) {
    switch(encoding_flag) {
        case MVM_encoding_type_utf8:
            return MVM_string_utf8_decode_strip_bom(tc, type_object, Cbuf, byte_length);
        case MVM_encoding_type_ascii:
            return MVM_string_ascii_decode(tc, type_object, Cbuf, byte_length);
        case MVM_encoding_type_latin1:
            return MVM_string_latin1_decode(tc, type_object, Cbuf, byte_length);
        case MVM_encoding_type_utf16:
            return MVM_string_utf16_decode(tc, type_object, Cbuf, byte_length);
        case MVM_encoding_type_windows1252:
            return MVM_string_windows1252_decode(tc, type_object, Cbuf, byte_length);
        case MVM_encoding_type_utf8_c8:
            return MVM_string_utf8_c8_decode(tc, type_object, Cbuf, byte_length);
        default:
            MVM_exception_throw_adhoc(tc, "invalid encoding type flag: %"PRId64, encoding_flag);
    }
}

/* Encodes an MVMString to a C buffer, dependent on the encoding type flag */
char * MVM_string_encode(MVMThreadContext *tc, MVMString *s, MVMint64 start,
        MVMint64 length, MVMuint64 *output_size, MVMint64 encoding_flag,
        MVMString *replacement, MVMint32 translate_newlines) {
    switch(encoding_flag) {
        case MVM_encoding_type_utf8:
            return MVM_string_utf8_encode_substr(tc, s, output_size, start, length, replacement, translate_newlines);
        case MVM_encoding_type_ascii:
            return MVM_string_ascii_encode_substr(tc, s, output_size, start, length, replacement, translate_newlines);
        case MVM_encoding_type_latin1:
            return MVM_string_latin1_encode_substr(tc, s, output_size, start, length, replacement, translate_newlines);
        case MVM_encoding_type_utf16:
            return MVM_string_utf16_encode_substr(tc, s, output_size, start, length, replacement, translate_newlines);
        case MVM_encoding_type_windows1252:
            return MVM_string_windows1252_encode_substr(tc, s, output_size, start, length, replacement, translate_newlines);
        case MVM_encoding_type_utf8_c8:
            return MVM_string_utf8_c8_encode_substr(tc, s, output_size, start, length, replacement);
        default:
            MVM_exception_throw_adhoc(tc, "invalid encoding type flag: %"PRId64, encoding_flag);
    }
}

/* Encodes a string, and writes the encoding string into the supplied Buf
 * instance, which should be an integer array with MVMArray REPR. */
MVMObject * MVM_string_encode_to_buf(MVMThreadContext *tc, MVMString *s, MVMString *enc_name,
        MVMObject *buf, MVMString *replacement) {
    MVMuint64 output_size;
    MVMuint8 *encoded;
    MVMArrayREPRData *buf_rd;
    MVMuint8 elem_size = 0;

    /* Ensure the target is in the correct form. */
    MVM_string_check_arg(tc, s, "encode");
    if (!IS_CONCRETE(buf) || REPR(buf)->ID != MVM_REPR_ID_VMArray)
        MVM_exception_throw_adhoc(tc, "encode requires a native array to write into");
    buf_rd = (MVMArrayREPRData *)STABLE(buf)->REPR_data;
    if (buf_rd) {
        switch (buf_rd->slot_type) {
            case MVM_ARRAY_I64: elem_size = 8; break;
            case MVM_ARRAY_I32: elem_size = 4; break;
            case MVM_ARRAY_I16: elem_size = 2; break;
            case MVM_ARRAY_I8:  elem_size = 1; break;
            case MVM_ARRAY_U64: elem_size = 8; break;
            case MVM_ARRAY_U32: elem_size = 4; break;
            case MVM_ARRAY_U16: elem_size = 2; break;
            case MVM_ARRAY_U8:  elem_size = 1; break;
        }
    }
    if (!elem_size)
        MVM_exception_throw_adhoc(tc, "encode requires a native int array");
    if (((MVMArray *)buf)->body.slots.any)
        MVM_exception_throw_adhoc(tc, "encode requires an empty array");

    /* At least find_encoding may allocate on first call, so root just
     * in case. */
    MVMROOT(tc, buf, {
    MVMROOT(tc, s, {
        const MVMuint8 encoding_flag = MVM_string_find_encoding(tc, enc_name);
        encoded = (MVMuint8 *)MVM_string_encode(tc, s, 0, MVM_string_graphs_nocheck(tc, s), &output_size,
            encoding_flag, replacement, 0);
    });
    });

    /* Stash the encoded data in the VMArray. */
    ((MVMArray *)buf)->body.slots.i8 = (MVMint8 *)encoded;
    ((MVMArray *)buf)->body.start    = 0;
    ((MVMArray *)buf)->body.ssize    = output_size / elem_size;
    ((MVMArray *)buf)->body.elems    = output_size / elem_size;
    return buf;
}

/* Decodes a string using the data from the specified Buf. */
MVMString * MVM_string_decode_from_buf(MVMThreadContext *tc, MVMObject *buf, MVMString *enc_name) {
    MVMArrayREPRData *buf_rd;
    MVMuint8 encoding_flag;
    MVMuint8 elem_size = 0;

    /* Ensure the source is in the correct form. */
    if (!IS_CONCRETE(buf) || REPR(buf)->ID != MVM_REPR_ID_VMArray)
        MVM_exception_throw_adhoc(tc, "decode requires a native array to read from");
    buf_rd = (MVMArrayREPRData *)STABLE(buf)->REPR_data;
    if (buf_rd) {
        switch (buf_rd->slot_type) {
            case MVM_ARRAY_I64: elem_size = 8; break;
            case MVM_ARRAY_I32: elem_size = 4; break;
            case MVM_ARRAY_I16: elem_size = 2; break;
            case MVM_ARRAY_I8:  elem_size = 1; break;
            case MVM_ARRAY_U64: elem_size = 8; break;
            case MVM_ARRAY_U32: elem_size = 4; break;
            case MVM_ARRAY_U16: elem_size = 2; break;
            case MVM_ARRAY_U8:  elem_size = 1; break;
        }
    }
    if (!elem_size)
        MVM_exception_throw_adhoc(tc, "encode requires a native int array");

    /* Decode. */
    MVMROOT(tc, buf, {
        encoding_flag = MVM_string_find_encoding(tc, enc_name);
    });
    return MVM_string_decode(tc, tc->instance->VMString,
        (char *)(((MVMArray *)buf)->body.slots.i8 + ((MVMArray *)buf)->body.start),
        ((MVMArray *)buf)->body.elems * elem_size,
        encoding_flag);
}

MVMObject * MVM_string_split(MVMThreadContext *tc, MVMString *separator, MVMString *input) {
    MVMObject *result;
    MVMStringIndex start, end, sep_length;
    MVMHLLConfig *hll = MVM_hll_current(tc);

    MVM_string_check_arg(tc, separator, "split separator");
    MVM_string_check_arg(tc, input, "split input");

    MVMROOT(tc, input, {
    MVMROOT(tc, separator, {
        result = MVM_repr_alloc_init(tc, hll->slurpy_array_type);
        MVMROOT(tc, result, {
            start = 0;
            end = MVM_string_graphs_nocheck(tc, input);
            sep_length = MVM_string_graphs_nocheck(tc, separator);

            while (start < end) {
                MVMString *portion;
                MVMStringIndex index;
                MVMStringIndex length;

                /* XXX make this use the dual-traverse iterator, but such that it
                    can reset the index of what it's comparing... <!> */
                index = MVM_string_index(tc, input, separator, start);
                length = sep_length ? (index == -1 ? end : index) - start : 1;
                if (length > 0 || (sep_length && length == 0)) {
                    portion = MVM_string_substring(tc, input, start, length);
                    MVMROOT(tc, portion, {
                        MVMObject *pobj = MVM_repr_alloc_init(tc, hll->str_box_type);
                        MVM_repr_set_str(tc, pobj, portion);
                        MVM_repr_push_o(tc, result, pobj);
                    });
                }
                start += length + sep_length;
                /* Gather an empty string if the delimiter is found at the end. */
                if (sep_length && start == end) {
                    MVMObject *pobj = MVM_repr_alloc_init(tc, hll->str_box_type);
                    MVM_repr_set_str(tc, pobj, tc->instance->str_consts.empty);
                    MVM_repr_push_o(tc, result, pobj);
                }
            }
        });
    });
    });

    return result;
}

MVMString * MVM_string_join(MVMThreadContext *tc, MVMString *separator, MVMObject *input) {
    MVMString  *result = NULL;
    MVMString **pieces = NULL;
    MVMint64    elems, num_pieces, sgraphs, i, is_str_array, total_graphs;
    MVMuint16   sstrands, total_strands;
    MVMint32    concats_stable = 1;

    MVM_string_check_arg(tc, separator, "join separator");
    if (!IS_CONCRETE(input))
        MVM_exception_throw_adhoc(tc, "join needs a concrete array to join");

    /* See how many things we have to join; if the answer is "none" then we
     * can make a hasty escape. */
    elems = MVM_repr_elems(tc, input);
    if (elems == 0)
        return tc->instance->str_consts.empty;
    is_str_array = REPR(input)->pos_funcs.get_elem_storage_spec(tc,
        STABLE(input)).boxed_primitive == MVM_STORAGE_SPEC_BP_STR;

    /* Allocate result. */
    MVMROOT(tc, separator, {
    MVMROOT(tc, input, {
        result = (MVMString *)MVM_repr_alloc_init(tc, tc->instance->VMString);
    });
    });

    /* Take a first pass through the string, counting up length and the total
     * number of strands we encounter as well as building a flat array of the
     * strings (to we only have to do the indirect calls once). */
    sgraphs  = MVM_string_graphs_nocheck(tc, separator);
    if (sgraphs)
        sstrands = separator->body.storage_type == MVM_STRING_STRAND
            ? separator->body.num_strands
            : 1;
    else
        sstrands = 1;
    pieces        = MVM_malloc(elems * sizeof(MVMString *));
    num_pieces    = 0;
    total_graphs  = 0;
    total_strands = 0;
    for (i = 0; i < elems; i++) {
        /* Get piece of the string. */
        MVMString *piece;
        MVMint64   piece_graphs;
        if (is_str_array) {
            piece = MVM_repr_at_pos_s(tc, input, i);
            if (!piece)
                continue;
        }
        else {
            MVMObject *item = MVM_repr_at_pos_o(tc, input, i);
            if (!item || !IS_CONCRETE(item))
                continue;
            piece = MVM_repr_get_str(tc, item);
        }

        /* If it wasn't the first piece, add separator here. */
        if (num_pieces) {
            total_strands += sstrands;
            total_graphs  += sgraphs;
        }

        /* Add on the piece's strands and graphs. */
        piece_graphs = MVM_string_graphs(tc, piece);
        if (piece_graphs) {
            total_strands += piece->body.storage_type == MVM_STRING_STRAND
                ? piece->body.num_strands
                : 1;
            total_graphs += piece_graphs;
        }

        /* Store piece. */
        pieces[num_pieces++] = piece;
    }

    /* We now know the total eventual number of graphemes. */
    if (total_graphs == 0) {
        free(pieces);
        return tc->instance->str_consts.empty;
    }
    result->body.num_graphs = total_graphs;

    /* If we just collect all the things as strands, are we within bounds, and
     * will be come out ahead? */
    if (total_strands < MVM_STRING_MAX_STRANDS && total_graphs / total_strands >= 16) {
        /* XXX TODO: Implement this, conditionalize branch thing below. */
    }
    /*else {*/
    if (1) {
        /* We'll produce a single, flat string. */
        MVMint64        position = 0;
        MVMGraphemeIter gi;
        result->body.storage_type    = MVM_STRING_GRAPHEME_32;
        result->body.storage.blob_32 = MVM_malloc(total_graphs * sizeof(MVMGrapheme32));
        for (i = 0; i < num_pieces; i++) {
            /* Get piece. */
            MVMString *piece = pieces[i];

            /* Add separator if needed. */
            if (i > 0) {
                /* If there's no separator and one piece is The Empty String we
                 * have to be extra careful about concat stability */
                if (sgraphs == 0 && MVM_string_graphs_nocheck(tc, piece) == 0 && concats_stable
                        && i + 1 < num_pieces
                        && !MVM_nfg_is_concat_stable(tc, pieces[i - 1], pieces[i + 1])) {
                    concats_stable = 0;
                }

                if (sgraphs) {
                    if (!concats_stable)
                        /* Already unstable; no more checks. */;
                    else if (!MVM_nfg_is_concat_stable(tc, pieces[i - 1], separator))
                        concats_stable = 0;
                    else if (!MVM_nfg_is_concat_stable(tc, separator, piece))
                        concats_stable = 0;

                    switch (separator->body.storage_type) {
                    case MVM_STRING_GRAPHEME_32:
                        memcpy(
                            result->body.storage.blob_32 + position,
                            separator->body.storage.blob_32,
                            sgraphs * sizeof(MVMGrapheme32));
                        position += sgraphs;
                        break;
                    /* XXX Can special-case 8-bit NFG and ASCII here too. */
                    default:
                        MVM_string_gi_init(tc, &gi, separator);
                        while (MVM_string_gi_has_more(tc, &gi))
                            result->body.storage.blob_32[position++] =
                                MVM_string_gi_get_grapheme(tc, &gi);
                        break;
                    }
                }
                else {
                    /* Separator has no graphemes, so NFG stability check
                     * should consider pieces. */
                    if (!concats_stable)
                        /* Already stable; no more checks. */;
                    else if (!MVM_nfg_is_concat_stable(tc, pieces[i - 1], piece))
                        concats_stable = 0;
                }
            }

            /* Add piece. */
            switch (piece->body.storage_type) {
            case MVM_STRING_GRAPHEME_32: {
                MVMint64 pgraphs = MVM_string_graphs(tc, piece);
                memcpy(
                    result->body.storage.blob_32 + position,
                    piece->body.storage.blob_32,
                    pgraphs * sizeof(MVMGrapheme32));
                position += pgraphs;
                break;
            }
            /* XXX Can special-case 8-bit NFG and ASCII here too. */
            default:
                MVM_string_gi_init(tc, &gi, piece);
                while (MVM_string_gi_has_more(tc, &gi))
                    result->body.storage.blob_32[position++] =
                        MVM_string_gi_get_grapheme(tc, &gi);
                break;
            }
        }
    }

    MVM_free(pieces);
    STRAND_CHECK(tc, result);
    return concats_stable ? result : re_nfg(tc, result);
}

/* Returning nonzero means it found the char at the position specified in 'a' in 'b'.
 * For character enumerations in regexes. */
MVMint64 MVM_string_char_at_in_string(MVMThreadContext *tc, MVMString *a, MVMint64 offset, MVMString *b) {
    MVMuint32     bgraphs;
    MVMGrapheme32 search;

    MVM_string_check_arg(tc, a, "char_at_in_string");
    MVM_string_check_arg(tc, b, "char_at_in_string");

    /* We return -2 here only to be able to distinguish between "out of bounds" and "not in string". */
    if (offset < 0 || offset >= MVM_string_graphs_nocheck(tc, a))
        return -2;

    search  = MVM_string_get_grapheme_at_nocheck(tc, a, offset);
    bgraphs = MVM_string_graphs_nocheck(tc, b);
    switch (b->body.storage_type) {
    case MVM_STRING_GRAPHEME_32: {
        MVMStringIndex i;
        for (i = 0; i < bgraphs; i++)
            if (b->body.storage.blob_32[i] == search)
                return i;
        break;
    }
    case MVM_STRING_GRAPHEME_ASCII:
        if (can_fit_into_ascii(search)) {
            MVMStringIndex i;
            for (i = 0; i < bgraphs; i++)
                if (b->body.storage.blob_ascii[i] == search)
                    return i;
        }
        break;
    case MVM_STRING_GRAPHEME_8:
        if (can_fit_into_8bit(search)) {
            MVMStringIndex i;
            for (i = 0; i < bgraphs; i++)
                if (b->body.storage.blob_8[i] == search)
                    return i;
        }
        break;
    case MVM_STRING_STRAND: {
        MVMGraphemeIter gi;
        MVMStringIndex  i;
        MVM_string_gi_init(tc, &gi, b);
        for (i = 0; i < bgraphs; i++)
            if (MVM_string_gi_get_grapheme(tc, &gi) == search)
                return i;
    }
    }
    return -1;
}

MVMint64 MVM_string_offset_has_unicode_property_value(MVMThreadContext *tc, MVMString *s, MVMint64 offset, MVMint64 property_code, MVMint64 property_value_code) {
    MVMGrapheme32 g;
    MVMCodepoint  cp;

    MVM_string_check_arg(tc, s, "uniprop");

    if (offset < 0 || offset >= MVM_string_graphs_nocheck(tc, s))
        return 0;

    g = MVM_string_get_grapheme_at_nocheck(tc, s, offset);
    if (g >= 0)
        cp = (MVMCodepoint)g;
    else
        cp = MVM_nfg_get_synthetic_info(tc, g)->base;
    return MVM_unicode_codepoint_has_property_value(tc, cp, property_code, property_value_code);
}

/* If the string is made up of strands, then produces a flattend string
 * representing the exact same graphemes but without strands. Otherwise,
 * returns the input string. Intended for strings that will be indexed
 * into heavily (when evaluating regexes, for example). */
MVMString * MVM_string_indexing_optimized(MVMThreadContext *tc, MVMString *s) {
    MVM_string_check_arg(tc, s, "indexingoptimized");
    if (s->body.storage_type == MVM_STRING_STRAND)
        return collapse_strands(tc, s);
    else
        return s;
}

/* Escapes a string, replacing various chars like \n with \\n. Can no doubt be
 * further optimized. */
MVMString * MVM_string_escape(MVMThreadContext *tc, MVMString *s) {
    MVMString      *res     = NULL;
    MVMStringIndex  spos    = 0;
    MVMStringIndex  bpos    = 0;
    MVMStringIndex  sgraphs, balloc;
    MVMGrapheme32  *buffer  = NULL;
    MVMGrapheme32   crlf;
    MVMint8         string_can_fit_into_8bit = 1;

    MVM_string_check_arg(tc, s, "escape");

    sgraphs = MVM_string_graphs_nocheck(tc, s);
    balloc  = sgraphs;
    buffer  = MVM_malloc(sizeof(MVMGrapheme32) * balloc);

    crlf = MVM_nfg_crlf_grapheme(tc);

    for (; spos < sgraphs; spos++) {
        MVMGrapheme32 graph = MVM_string_get_grapheme_at_nocheck(tc, s, spos);
        MVMGrapheme32 esc   = 0;
        switch (graph) {
            case '\\': esc = '\\'; break;
            case 7:    esc = 'a';  break;
            case '\b': esc = 'b';  break;
            case '\n': esc = 'n';  break;
            case '\r': esc = 'r';  break;
            case '\t': esc = 't';  break;
            case '\f': esc = 'f';  break;
            case '"':  esc = '"';  break;
            case 27:   esc = 'e';  break;
        }
        if (esc) {
            if (bpos + 2 > balloc) {
                balloc += 32;
                buffer = MVM_realloc(buffer, sizeof(MVMGrapheme32) * balloc);
            }
            buffer[bpos++] = '\\';
            buffer[bpos++] = esc;
        }
        else if (graph == crlf) {
            if (bpos + 4 > balloc) {
                balloc += 32;
                buffer = MVM_realloc(buffer, sizeof(MVMGrapheme32) * balloc);
            }
            buffer[bpos++] = '\\';
            buffer[bpos++] = 'r';
            buffer[bpos++] = '\\';
            buffer[bpos++] = 'n';
        }
        else {
            if (bpos + 1 > balloc) {
                balloc += 32;
                buffer = MVM_realloc(buffer, sizeof(MVMGrapheme32) * balloc);
            }
            if (!can_fit_into_8bit(graph))
                string_can_fit_into_8bit = 0;
            buffer[bpos++] = graph;
        }
    }

    res = (MVMString *)MVM_repr_alloc_init(tc, tc->instance->VMString);
    res->body.storage_type    = MVM_STRING_GRAPHEME_32;
    res->body.storage.blob_32 = buffer;
    res->body.num_graphs      = bpos;

    if (string_can_fit_into_8bit)
        turn_32bit_into_8bit_unchecked(tc, res);

    STRAND_CHECK(tc, res);
    return res;
}

/* Takes a string and reverses its characters. */
MVMString * MVM_string_flip(MVMThreadContext *tc, MVMString *s) {
    MVMString      *res     = NULL;
    MVMStringIndex  spos    = 0;
    MVMStringIndex  sgraphs;
    MVMStringIndex  rpos;

    MVM_string_check_arg(tc, s, "flip");
    sgraphs = MVM_string_graphs_nocheck(tc, s);
    rpos    = sgraphs;

    if (s->body.storage_type == MVM_STRING_GRAPHEME_8) {
        MVMGrapheme8   *rbuffer;
        rbuffer = MVM_malloc(sizeof(MVMGrapheme8) * sgraphs);

        for (; spos < sgraphs; spos++)
            rbuffer[--rpos] = s->body.storage.blob_8[spos];

        res = (MVMString *)MVM_repr_alloc_init(tc, tc->instance->VMString);
        res->body.storage_type    = MVM_STRING_GRAPHEME_8;
        res->body.storage.blob_8  = rbuffer;
    } else {
        MVMGrapheme32  *rbuffer;
        rbuffer = MVM_malloc(sizeof(MVMGrapheme32) * sgraphs);

        if (s->body.storage_type == MVM_STRING_GRAPHEME_32)
            for (; spos < sgraphs; spos++)
                rbuffer[--rpos] = s->body.storage.blob_32[spos];
        else
            for (; spos < sgraphs; spos++)
                rbuffer[--rpos] = MVM_string_get_grapheme_at_nocheck(tc, s, spos);

        res = (MVMString *)MVM_repr_alloc_init(tc, tc->instance->VMString);
        res->body.storage_type    = MVM_STRING_GRAPHEME_32;
        res->body.storage.blob_32 = rbuffer;
    }

    res->body.num_graphs      = sgraphs;

    STRAND_CHECK(tc, res);
    return res;
}

/* Compares two strings, returning -1, 0 or 1 to indicate less than,
 * equal or greater than. */
MVMint64 MVM_string_compare(MVMThreadContext *tc, MVMString *a, MVMString *b) {
    MVMStringIndex alen, blen, i, scanlen;

    MVM_string_check_arg(tc, a, "compare");
    MVM_string_check_arg(tc, b, "compare");

    /* Simple cases when one or both are zero length. */
    alen = MVM_string_graphs_nocheck(tc, a);
    blen = MVM_string_graphs_nocheck(tc, b);
    if (alen == 0)
        return blen == 0 ? 0 : -1;
    if (blen == 0)
        return 1;

    /* Otherwise, need to scan them. */
    scanlen = blen < alen ? blen : alen;
    for (i = 0; i < scanlen; i++) {
        MVMGrapheme32 g_a = MVM_string_get_grapheme_at_nocheck(tc, a, i);
        MVMGrapheme32 g_b = MVM_string_get_grapheme_at_nocheck(tc, b, i);
        if (g_a != g_b) {
            MVMint64 rtrn;
            /* If one of the deciding graphemes is a synthetic then we need to
             * iterate the codepoints inside it */
            if (g_a < 0 || g_b < 0) {
                MVMCodepointIter ci_a, ci_b;
                MVM_string_grapheme_ci_init(tc, &ci_a, g_a);
                MVM_string_grapheme_ci_init(tc, &ci_b, g_b);
                while (MVM_string_grapheme_ci_has_more(tc, &ci_a) && MVM_string_grapheme_ci_has_more(tc, &ci_b)) {
                    g_a = MVM_string_grapheme_ci_get_codepoint(tc, &ci_a);
                    g_b = MVM_string_grapheme_ci_get_codepoint(tc, &ci_b);
                    if (g_a != g_b)
                        break;
                }
                rtrn = g_a < g_b ? -1 :
                       g_b < g_a ?  1 :
                                    0 ;
                /* If we get here, all the codepoints in the synthetics have matched
                 * so go based on which has more codepoints left in that grapheme */
                if (!rtrn) {
                    MVMint32 a_has_more = MVM_string_grapheme_ci_has_more(tc, &ci_a),
                             b_has_more = MVM_string_grapheme_ci_has_more(tc, &ci_b);

                    return a_has_more < b_has_more ? -1 :
                           b_has_more < a_has_more ?  1 :
                                                      0 ;
                }
                return rtrn;
            }
            return g_a < g_b ? -1 :
                   g_b < g_a ?  1 :
                                0 ;
        }
    }

    /* All shared chars equal, so go on length. */
    return alen < blen ? -1 :
           blen < alen ?  1 :
                          0 ;
}

/* Takes two strings and AND's their characters. */
MVMString * MVM_string_bitand(MVMThreadContext *tc, MVMString *a, MVMString *b) {
    MVMString      *res    = NULL;
    MVMGrapheme32  *buffer = NULL;
    MVMStringIndex  i, alen, blen, sgraphs;

    MVM_string_check_arg(tc, a, "bitwise and");
    MVM_string_check_arg(tc, b, "bitwise and");

    alen = MVM_string_graphs_nocheck(tc, a);
    blen = MVM_string_graphs_nocheck(tc, b);
    sgraphs = alen < blen ? alen : blen;
    buffer = MVM_malloc(sizeof(MVMGrapheme32) * sgraphs);

    /* Binary-and up to the length of the shortest string. */
    for (i = 0; i < sgraphs; i++)
        buffer[i] = (MVM_string_get_grapheme_at_nocheck(tc, a, i)
                   & MVM_string_get_grapheme_at_nocheck(tc, b, i));

    res = (MVMString *)MVM_repr_alloc_init(tc, tc->instance->VMString);
    res->body.storage_type    = MVM_STRING_GRAPHEME_32;
    res->body.storage.blob_32 = buffer;
    res->body.num_graphs      = sgraphs;

    STRAND_CHECK(tc, res);
    return res;
}

/* Takes two strings and OR's their characters. */
MVMString * MVM_string_bitor(MVMThreadContext *tc, MVMString *a, MVMString *b) {
    MVMString      *res    = NULL;
    MVMGrapheme32  *buffer = NULL;
    MVMStringIndex  alen, blen, sgraphs, i, scanlen;

    MVM_string_check_arg(tc, a, "bitwise or");
    MVM_string_check_arg(tc, b, "bitwise or");

    alen = MVM_string_graphs_nocheck(tc, a);
    blen = MVM_string_graphs_nocheck(tc, b);
    sgraphs = (alen > blen ? alen : blen);
    buffer = MVM_malloc(sizeof(MVMGrapheme32) * sgraphs);

    /* First, binary-or up to the length of the shortest string. */
    scanlen = alen > blen ? blen : alen;
    for (i = 0; i < scanlen; i++)
        buffer[i] = (MVM_string_get_grapheme_at_nocheck(tc, a, i)
                   | MVM_string_get_grapheme_at_nocheck(tc, b, i));

    /* Second pass, fill with characters of the longest string. */
    if (alen > blen)
        for (; i < sgraphs; i++)
            buffer[i] = MVM_string_get_grapheme_at_nocheck(tc, a, i);
    else
        for (; i < sgraphs; i++)
            buffer[i] = MVM_string_get_grapheme_at_nocheck(tc, b, i);

    res = (MVMString *)MVM_repr_alloc_init(tc, tc->instance->VMString);
    res->body.storage_type    = MVM_STRING_GRAPHEME_32;
    res->body.storage.blob_32 = buffer;
    res->body.num_graphs      = sgraphs;

    STRAND_CHECK(tc, res);
    return res;
}

/* Takes two strings and XOR's their characters. */
MVMString * MVM_string_bitxor(MVMThreadContext *tc, MVMString *a, MVMString *b) {
    MVMString      *res    = NULL;
    MVMGrapheme32  *buffer = NULL;
    MVMStringIndex  alen, blen, sgraphs, i, scanlen;

    MVM_string_check_arg(tc, a, "bitwise xor");
    MVM_string_check_arg(tc, b, "bitwise xor");

    alen = MVM_string_graphs_nocheck(tc, a);
    blen = MVM_string_graphs_nocheck(tc, b);
    sgraphs = (alen > blen ? alen : blen);
    buffer = MVM_malloc(sizeof(MVMGrapheme32) * sgraphs);

    /* First, binary-xor up to the length of the shorter string. */
    scanlen = alen > blen ? blen : alen;
    for (i = 0; i < scanlen; i++)
        buffer[i] = (MVM_string_get_grapheme_at_nocheck(tc, a, i)
                   ^ MVM_string_get_grapheme_at_nocheck(tc, b, i));

    /* Second pass, fill with characters of the longest string. */
    if (alen > blen)
        for (; i < sgraphs; i++)
            buffer[i] = MVM_string_get_grapheme_at_nocheck(tc, a, i);
    else
        for (; i < sgraphs; i++)
            buffer[i] = MVM_string_get_grapheme_at_nocheck(tc, b, i);

    res = (MVMString *)MVM_repr_alloc_init(tc, tc->instance->VMString);
    res->body.storage_type    = MVM_STRING_GRAPHEME_32;
    res->body.storage.blob_32 = buffer;
    res->body.num_graphs      = sgraphs;

    STRAND_CHECK(tc, res);
    return res;
}

/* The following statics hold on to various unicode property values we will
 * resolve once so we don't have to do it repeatedly. */
static MVMint64 UPV_Nd = 0;
static MVMint64 UPV_Lu = 0;
static MVMint64 UPV_Ll = 0;
static MVMint64 UPV_Lt = 0;
static MVMint64 UPV_Lm = 0;
static MVMint64 UPV_Lo = 0;
static MVMint64 UPV_Zs = 0;
static MVMint64 UPV_Zl = 0;
static MVMint64 UPV_Pc = 0;
static MVMint64 UPV_Pd = 0;
static MVMint64 UPV_Ps = 0;
static MVMint64 UPV_Pe = 0;
static MVMint64 UPV_Pi = 0;
static MVMint64 UPV_Pf = 0;
static MVMint64 UPV_Po = 0;

/* concatenating with "" ensures that only literal strings are accepted as argument. */
#define STR_WITH_LEN(str)  ("" str ""), (sizeof(str) - 1)

/* Resolves various unicode property values that we'll need. */
void MVM_string_cclass_init(MVMThreadContext *tc) {
    UPV_Nd = MVM_unicode_cname_to_property_value_code(tc,
        MVM_UNICODE_PROPERTY_GENERAL_CATEGORY, STR_WITH_LEN("Nd"));
    UPV_Lu = MVM_unicode_cname_to_property_value_code(tc,
        MVM_UNICODE_PROPERTY_GENERAL_CATEGORY, STR_WITH_LEN("Lu"));
    UPV_Ll = MVM_unicode_cname_to_property_value_code(tc,
        MVM_UNICODE_PROPERTY_GENERAL_CATEGORY, STR_WITH_LEN("Ll"));
    UPV_Lt = MVM_unicode_cname_to_property_value_code(tc,
        MVM_UNICODE_PROPERTY_GENERAL_CATEGORY, STR_WITH_LEN("Lt"));
    UPV_Lm = MVM_unicode_cname_to_property_value_code(tc,
        MVM_UNICODE_PROPERTY_GENERAL_CATEGORY, STR_WITH_LEN("Lm"));
    UPV_Lo = MVM_unicode_cname_to_property_value_code(tc,
        MVM_UNICODE_PROPERTY_GENERAL_CATEGORY, STR_WITH_LEN("Lo"));
    UPV_Zs = MVM_unicode_cname_to_property_value_code(tc,
        MVM_UNICODE_PROPERTY_GENERAL_CATEGORY, STR_WITH_LEN("Zs"));
    UPV_Zl = MVM_unicode_cname_to_property_value_code(tc,
        MVM_UNICODE_PROPERTY_GENERAL_CATEGORY, STR_WITH_LEN("Zl"));
    UPV_Pc = MVM_unicode_cname_to_property_value_code(tc,
        MVM_UNICODE_PROPERTY_GENERAL_CATEGORY, STR_WITH_LEN("Pc"));
    UPV_Pd = MVM_unicode_cname_to_property_value_code(tc,
        MVM_UNICODE_PROPERTY_GENERAL_CATEGORY, STR_WITH_LEN("Pd"));
    UPV_Ps = MVM_unicode_cname_to_property_value_code(tc,
        MVM_UNICODE_PROPERTY_GENERAL_CATEGORY, STR_WITH_LEN("Ps"));
    UPV_Pe = MVM_unicode_cname_to_property_value_code(tc,
        MVM_UNICODE_PROPERTY_GENERAL_CATEGORY, STR_WITH_LEN("Pe"));
    UPV_Pi = MVM_unicode_cname_to_property_value_code(tc,
        MVM_UNICODE_PROPERTY_GENERAL_CATEGORY, STR_WITH_LEN("Pi"));
    UPV_Pf = MVM_unicode_cname_to_property_value_code(tc,
        MVM_UNICODE_PROPERTY_GENERAL_CATEGORY, STR_WITH_LEN("Pf"));
    UPV_Po = MVM_unicode_cname_to_property_value_code(tc,
        MVM_UNICODE_PROPERTY_GENERAL_CATEGORY, STR_WITH_LEN("Po"));
}

/* Checks if the specified grapheme is in the given character class. */
static MVMint64 grapheme_is_cclass(MVMThreadContext *tc, MVMint64 cclass, MVMGrapheme32 g) {
    /* If it's a synthetic, then grab the base codepoint. */
    MVMCodepoint cp;
    if (g >= 0)
        cp = (MVMCodepoint)g;
    else
        cp = MVM_nfg_get_synthetic_info(tc, g)->base;

    switch (cclass) {
        case MVM_CCLASS_ANY:
            return 1;

        case MVM_CCLASS_UPPERCASE:
            return MVM_unicode_codepoint_has_property_value(tc, cp,
                MVM_UNICODE_PROPERTY_GENERAL_CATEGORY, UPV_Lu);

        case MVM_CCLASS_LOWERCASE:
            return MVM_unicode_codepoint_has_property_value(tc, cp,
                MVM_UNICODE_PROPERTY_GENERAL_CATEGORY, UPV_Ll);

        case MVM_CCLASS_WORD:
            if (cp <= 'z') {  /* short circuit common case */
                if (cp >= 'a' || cp == '_' || (cp >= 'A' && cp <= 'Z') || (cp >= '0' && cp <= '9'))
                    return 1;
                else
                    return 0;
            }
            /* Deliberate fall-through; word is _ or digit or alphabetic. */

        case MVM_CCLASS_ALPHANUMERIC:
            if (cp <= '9' && cp >= '0')  /* short circuit common case */
                return 1;
            if (MVM_unicode_codepoint_has_property_value(tc, cp,
                    MVM_UNICODE_PROPERTY_GENERAL_CATEGORY, UPV_Nd))
                return 1;
            /* Deliberate fall-through; alphanumeric is digit or alphabetic. */

        case MVM_CCLASS_ALPHABETIC:
            if (cp <= 'z') {  /* short circuit common case */
                if (cp >= 'a' || (cp >= 'A' && cp <= 'Z'))
                    return 1;
                else
                    return 0;
            }
            return
                MVM_unicode_codepoint_has_property_value(tc, cp,
                    MVM_UNICODE_PROPERTY_GENERAL_CATEGORY, UPV_Lo) /* lots of CJK chars */
             || MVM_unicode_codepoint_has_property_value(tc, cp,
                    MVM_UNICODE_PROPERTY_GENERAL_CATEGORY, UPV_Ll) /* (ascii handled above) */
             || MVM_unicode_codepoint_has_property_value(tc, cp,
                    MVM_UNICODE_PROPERTY_GENERAL_CATEGORY, UPV_Lu)
             || MVM_unicode_codepoint_has_property_value(tc, cp,
                    MVM_UNICODE_PROPERTY_GENERAL_CATEGORY, UPV_Lt)
             || MVM_unicode_codepoint_has_property_value(tc, cp,
                    MVM_UNICODE_PROPERTY_GENERAL_CATEGORY, UPV_Lm);

        case MVM_CCLASS_NUMERIC:
            if (cp <= '9' && cp >= '0')  /* short circuit common case */
                return 1;
            return MVM_unicode_codepoint_has_property_value(tc, cp,
                MVM_UNICODE_PROPERTY_GENERAL_CATEGORY, UPV_Nd);

        case MVM_CCLASS_HEXADECIMAL:
            return MVM_unicode_codepoint_has_property_value(tc, cp,
                MVM_UNICODE_PROPERTY_ASCII_HEX_DIGIT, 1);

        case MVM_CCLASS_WHITESPACE:
            if (cp <= '~') {
                if (cp == ' ' || (cp <= 13 && cp >= 9))
                    return 1;
                else
                    return 0;
            }
            return MVM_unicode_codepoint_has_property_value(tc, cp,
                MVM_UNICODE_PROPERTY_WHITE_SPACE, 1);

        case MVM_CCLASS_BLANK:
            if (cp == '\t')
                return 1;
            return MVM_unicode_codepoint_has_property_value(tc, cp,
                MVM_UNICODE_PROPERTY_GENERAL_CATEGORY, UPV_Zs);

        case MVM_CCLASS_CONTROL: {
            return (cp >= 0 && cp < 32) || (cp >= 127 && cp < 160);
        }

        case MVM_CCLASS_PRINTING: {
            return !((cp >= 0 && cp < 32) || (cp >= 127 && cp < 160));
        }

        case MVM_CCLASS_PUNCTUATION:
            return
                MVM_unicode_codepoint_has_property_value(tc, cp,
                    MVM_UNICODE_PROPERTY_GENERAL_CATEGORY, UPV_Pc)
             || MVM_unicode_codepoint_has_property_value(tc, cp,
                    MVM_UNICODE_PROPERTY_GENERAL_CATEGORY, UPV_Pd)
             || MVM_unicode_codepoint_has_property_value(tc, cp,
                    MVM_UNICODE_PROPERTY_GENERAL_CATEGORY, UPV_Ps)
             || MVM_unicode_codepoint_has_property_value(tc, cp,
                    MVM_UNICODE_PROPERTY_GENERAL_CATEGORY, UPV_Pe)
             || MVM_unicode_codepoint_has_property_value(tc, cp,
                    MVM_UNICODE_PROPERTY_GENERAL_CATEGORY, UPV_Pi)
             || MVM_unicode_codepoint_has_property_value(tc, cp,
                    MVM_UNICODE_PROPERTY_GENERAL_CATEGORY, UPV_Pf)
             || MVM_unicode_codepoint_has_property_value(tc, cp,
                    MVM_UNICODE_PROPERTY_GENERAL_CATEGORY, UPV_Po);

        case MVM_CCLASS_NEWLINE: {
            if (cp == '\n' || cp == 0x0b || cp == 0x0c || cp == '\r' ||
                cp == 0x85 || cp == 0x2028 || cp == 0x2029)
                return 1;
            return MVM_unicode_codepoint_has_property_value(tc, cp,
                MVM_UNICODE_PROPERTY_GENERAL_CATEGORY, UPV_Zl);
        }

        default:
            return 0;
    }
}

/* Checks if the character at the specified offset is a member of the
 * indicated character class. */
MVMint64 MVM_string_is_cclass(MVMThreadContext *tc, MVMint64 cclass, MVMString *s, MVMint64 offset) {
    MVMGrapheme32 g;
    MVM_string_check_arg(tc, s, "is_cclass");
    if (offset < 0 || offset >= MVM_string_graphs_nocheck(tc, s))
        return 0;
    g = MVM_string_get_grapheme_at_nocheck(tc, s, offset);
    return grapheme_is_cclass(tc, cclass, g);
}

/* Searches for the next char that is in the specified character class. */
MVMint64 MVM_string_find_cclass(MVMThreadContext *tc, MVMint64 cclass, MVMString *s, MVMint64 offset, MVMint64 count) {
    MVMGraphemeIter gi;
    MVMint64        length, end, pos;

    MVM_string_check_arg(tc, s, "find_cclass");

    length = MVM_string_graphs_nocheck(tc, s);
    end    = offset + count;
    end = length < end ? length : end;
    if (offset < 0 || offset >= length)
        return end;

    MVM_string_gi_init(tc, &gi, s);
    MVM_string_gi_move_to(tc, &gi, offset);
    for (pos = offset; pos < end; pos++) {
        MVMGrapheme32 g = MVM_string_gi_get_grapheme(tc, &gi);
        if (grapheme_is_cclass(tc, cclass, g) > 0)
            return pos;
    }

    return end;
}

/* Searches for the next char that is not in the specified character class. */
MVMint64 MVM_string_find_not_cclass(MVMThreadContext *tc, MVMint64 cclass, MVMString *s, MVMint64 offset, MVMint64 count) {
    MVMGraphemeIter gi;
    MVMint64        length, end, pos;

    MVM_string_check_arg(tc, s, "find_not_cclass");

    length = MVM_string_graphs_nocheck(tc, s);
    end    = offset + count;
    end = length < end ? length : end;
    if (offset < 0 || offset >= length)
        return end;

    MVM_string_gi_init(tc, &gi, s);
    MVM_string_gi_move_to(tc, &gi, offset);
    for (pos = offset; pos < end; pos++) {
        MVMGrapheme32 g = MVM_string_gi_get_grapheme(tc, &gi);
        if (grapheme_is_cclass(tc, cclass, g) == 0)
            return pos;
    }

    return end;
}

static MVMint16   encoding_name_init         = 0;
static MVMString *encoding_utf8_name         = NULL;
static MVMString *encoding_ascii_name        = NULL;
static MVMString *encoding_latin1_name       = NULL;
static MVMString *encoding_utf16_name        = NULL;
static MVMString *encoding_windows1252_name  = NULL;
static MVMString *encoding_utf8_c8_name      = NULL;
MVMuint8 MVM_string_find_encoding(MVMThreadContext *tc, MVMString *name) {
    MVM_string_check_arg(tc, name, "find encoding");
    if (!encoding_name_init) {
        MVM_gc_allocate_gen2_default_set(tc);
        encoding_utf8_name        = MVM_string_ascii_decode_nt(tc, tc->instance->VMString, "utf8");
        MVM_gc_root_add_permanent_desc(tc, (MVMCollectable **)&encoding_utf8_name, "Encoding name");
        encoding_ascii_name       = MVM_string_ascii_decode_nt(tc, tc->instance->VMString, "ascii");
        MVM_gc_root_add_permanent_desc(tc, (MVMCollectable **)&encoding_ascii_name, "Encoding name");
        encoding_latin1_name      = MVM_string_ascii_decode_nt(tc, tc->instance->VMString, "iso-8859-1");
        MVM_gc_root_add_permanent_desc(tc, (MVMCollectable **)&encoding_latin1_name, "Encoding name");
        encoding_utf16_name       = MVM_string_ascii_decode_nt(tc, tc->instance->VMString, "utf16");
        MVM_gc_root_add_permanent_desc(tc, (MVMCollectable **)&encoding_utf16_name, "Encoding name");
        encoding_windows1252_name = MVM_string_ascii_decode_nt(tc, tc->instance->VMString, "windows-1252");
        MVM_gc_root_add_permanent_desc(tc, (MVMCollectable **)&encoding_windows1252_name, "Encoding name");
        encoding_utf8_c8_name     = MVM_string_ascii_decode_nt(tc, tc->instance->VMString, "utf8-c8");
        MVM_gc_root_add_permanent_desc(tc, (MVMCollectable **)&encoding_utf8_c8_name, "Encoding name");
        encoding_name_init   = 1;
        MVM_gc_allocate_gen2_default_clear(tc);
    }

    if (MVM_string_equal(tc, name, encoding_utf8_name)) {
        return MVM_encoding_type_utf8;
    }
    else if (MVM_string_equal(tc, name, encoding_ascii_name)) {
        return MVM_encoding_type_ascii;
    }
    else if (MVM_string_equal(tc, name, encoding_latin1_name)) {
        return MVM_encoding_type_latin1;
    }
    else if (MVM_string_equal(tc, name, encoding_windows1252_name)) {
        return MVM_encoding_type_windows1252;
    }
    else if (MVM_string_equal(tc, name, encoding_utf16_name)) {
        return MVM_encoding_type_utf16;
    }
    else if (MVM_string_equal(tc, name, encoding_utf8_c8_name)) {
        return MVM_encoding_type_utf8_c8;
    }
    else {
        char *c_name = MVM_string_utf8_encode_C_string(tc, name);
        char *waste[] = { c_name, NULL };
        MVM_exception_throw_adhoc_free(tc, waste, "Unknown string encoding: '%s'",
            c_name);
    }
}

/* Turns a codepoint into a string. If required uses the normalizer to ensure
 * that we get a valid NFG string (NFG is a superset of NFC, and singleton
 * decompositions exist). */
MVMString * MVM_string_chr(MVMThreadContext *tc, MVMint64 cp) {
    MVMString *s = NULL;
    MVMGrapheme32 g;

    if (cp < 0)
        MVM_exception_throw_adhoc(tc, "chr codepoint cannot be negative");
    /* If the codepoint decomposes we may need to normalize it.
     * The first cp that decomposes is U+0340, but to be on the safe side
     * for now we go with the first significant character which at the time
     * of writing (Unicode 9.0) is COMBINING GRAVE ACCENT U+300 */
    if (cp >= MVM_NORMALIZE_FIRST_SIG_NFC
        && MVM_unicode_codepoint_get_property_int(tc, cp, MVM_UNICODE_PROPERTY_DECOMPOSITION_TYPE)
        != MVM_UNICODE_PVALUE_DT_NONE) {

        MVMNormalizer norm;
        MVM_unicode_normalizer_init(tc, &norm, MVM_NORMALIZE_NFG);
        if (!MVM_unicode_normalizer_process_codepoint_to_grapheme(tc, &norm, cp, &g)) {
            MVM_unicode_normalizer_eof(tc, &norm);
            g = MVM_unicode_normalizer_get_grapheme(tc, &norm);
        }
        MVM_unicode_normalizer_cleanup(tc, &norm);
    }
    else {
        g = (MVMGrapheme32) cp;
    }

    s = (MVMString *)REPR(tc->instance->VMString)->allocate(tc, STABLE(tc->instance->VMString));
    if (can_fit_into_8bit(g)) {
        s->body.storage_type       = MVM_STRING_GRAPHEME_8;
        s->body.storage.blob_8     = MVM_malloc(sizeof(MVMGrapheme8));
        s->body.storage.blob_8[0]  = g;
    } else {
        s->body.storage_type       = MVM_STRING_GRAPHEME_32;
        s->body.storage.blob_32    = MVM_malloc(sizeof(MVMGrapheme32));
        s->body.storage.blob_32[0] = g;
    }
    s->body.num_graphs         = 1;
    return s;
}

/* Takes a string and computes a hash code for it, storing it in the hash code
 * cache field of the string. Hashing code is derived from the Jenkins hash
 * implementation in uthash.h. */
typedef union {
    MVMint32 graphs[3];
    unsigned char bytes[12];
} MVMJenHashGraphemeView;
void MVM_string_compute_hash_code(MVMThreadContext *tc, MVMString *s) {
    /* The hash algorithm works in bytes. Since we can represent strings in a
     * number of ways, and we want consistent hashing, then we'll read the
     * strings using the grapheme iterator in groups of 3, using 32-bit ints
     * for the graphemes no matter what the string really holds them as. Then
     * we'll use the bytes view of that in the hashing function. */
    MVMJenHashGraphemeView hash_block;
    MVMGraphemeIter gi;
    MVMuint32 graphs_remaining = MVM_string_graphs(tc, s);

    /* Initialize hash state. */
    MVMuint32 hashv = 0xfeedbeef;
    MVMuint32 _hj_i, _hj_j;
    _hj_i = _hj_j = 0x9e3779b9;

    /* Work through the string 3 graphemes at a time. */
    MVM_string_gi_init(tc, &gi, s);
    while (graphs_remaining >= 3) {
        hash_block.graphs[0] = MVM_string_gi_get_grapheme(tc, &gi);
        hash_block.graphs[1] = MVM_string_gi_get_grapheme(tc, &gi);
        hash_block.graphs[2] = MVM_string_gi_get_grapheme(tc, &gi);
        _hj_i += (hash_block.bytes[0] + ( (unsigned)hash_block.bytes[1] << 8 )
            + ( (unsigned)hash_block.bytes[2] << 16 )
            + ( (unsigned)hash_block.bytes[3] << 24 ) );
        _hj_j +=    (hash_block.bytes[4] + ( (unsigned)hash_block.bytes[5] << 8 )
            + ( (unsigned)hash_block.bytes[6] << 16 )
            + ( (unsigned)hash_block.bytes[7] << 24 ) );
        hashv += (hash_block.bytes[8] + ( (unsigned)hash_block.bytes[9] << 8 )
            + ( (unsigned)hash_block.bytes[10] << 16 )
            + ( (unsigned)hash_block.bytes[11] << 24 ) );

        HASH_JEN_MIX(_hj_i, _hj_j, hashv);

        graphs_remaining -= 3;
    }

    /* Mix in key length (in bytes, not graphemes). */
    hashv += MVM_string_graphs(tc, s) * sizeof(MVMGrapheme32);

    /* Now handle trailing graphemes (must be 2, 1, or 0). */
    switch (graphs_remaining) {
        case 2:
            hash_block.graphs[1] = MVM_string_gi_get_grapheme(tc, &gi);
            _hj_j += ( (unsigned)hash_block.bytes[7] << 24 ) +
                     ( (unsigned)hash_block.bytes[6] << 16 ) +
                     ( (unsigned)hash_block.bytes[5] << 8 ) +
                     hash_block.bytes[4];
        /* Fallthrough */
        case 1:
            hash_block.graphs[0] = MVM_string_gi_get_grapheme(tc, &gi);
            _hj_i += ( (unsigned)hash_block.bytes[3] << 24 ) +
                     ( (unsigned)hash_block.bytes[2] << 16 ) +
                     ( (unsigned)hash_block.bytes[1] << 8 ) +
                     hash_block.bytes[0];
    }
    HASH_JEN_MIX(_hj_i, _hj_j, hashv);

    /* Store computed hash value. */
    s->body.cached_hash_code = hashv;
}
