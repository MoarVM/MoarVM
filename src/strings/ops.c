#include "moar.h"

#define MVM_DEBUG_STRANDS 0

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
            "Strand sanity check failed (stand length %d != num_graphs %d)",
            len, MVM_string_graphs(tc, s));
}
#define STRAND_CHECK(tc, s) check_strand_sanity(tc, s);
#else
#define STRAND_CHECK(tc, s)
#endif

/* Checks a string is not null or non-concrete and throws if so. */
MVM_STATIC_INLINE MVM_string_check_arg(MVMThreadContext *tc, MVMString *s, const char *operation) {
    if (!s || !IS_CONCRETE(s))
        MVM_exception_throw_adhoc(tc, "%s requires a concrete string, but got %s",
            operation, s ? "a type object" : "null");
}

/* Allocates strand storage. */
static MVMStringStrand * allocate_strands(MVMThreadContext *tc, MVMuint16 num_strands) {
    return MVM_malloc(num_strands * sizeof(MVMStringStrand));
}

/* Copies strands from one strand string to another. */
static void copy_strands(MVMThreadContext *tc, MVMString *from, MVMuint16 from_offset,
        MVMString *to, MVMuint16 to_offset, MVMuint16 num_strands) {
    assert(from->body.storage_type == MVM_STRING_STRAND);
    assert(to->body.storage_type == MVM_STRING_STRAND);
    memcpy(
        to->body.storage.strands + to_offset,
        from->body.storage.strands + from_offset,
        num_strands * sizeof(MVMStringStrand));
}

/* Collapses a bunch of strands into a single blob string. */
static MVMString * collapse_strands(MVMThreadContext *tc, MVMString *orig) {
    MVMString       *result;
    MVMStringIndex   i;
    MVMuint32        ographs;
    MVMGraphemeIter  gi;

    MVMROOT(tc, orig, {
        result = (MVMString *)MVM_repr_alloc_init(tc, tc->instance->VMString);
    });
    ographs                      = MVM_string_graphs(tc, orig);
    result->body.num_graphs      = ographs;
    result->body.storage_type    = MVM_STRING_GRAPHEME_32;
    result->body.storage.blob_32 = MVM_malloc(ographs * sizeof(MVMGrapheme32));

    MVM_string_gi_init(tc, &gi, orig);
    for (i = 0; i < ographs; i++)
        result->body.storage.blob_32[i] = MVM_string_gi_get_grapheme(tc, &gi);

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
    MVMString *out;

    /* Create output buffer; it'll never be longer than the initial estimate
     * since the most we'll do is collapse two things into one in places. */
    MVMGrapheme32 *out_buffer = MVM_malloc(in->body.num_graphs * sizeof(MVMGrapheme32));
    MVMint64 out_pos = 0;

    /* Iterate codepoints and normalizer. */
    MVM_unicode_normalizer_init(tc, &norm, MVM_NORMALIZE_NFG);
    MVM_string_ci_init(tc, &ci, in);
    while (MVM_string_ci_has_more(tc, &ci)) {
        MVMGrapheme32 g;
        ready = MVM_unicode_normalizer_process_codepoint_to_grapheme(tc, &norm, MVM_string_ci_get_codepoint(tc, &ci), &g);
        if (ready) {
            out_buffer[out_pos++] = g;
            while (--ready > 0)
                out_buffer[out_pos++] = MVM_unicode_normalizer_get_grapheme(tc, &norm);
        }
    }
    MVM_unicode_normalizer_eof(tc, &norm);
    ready = MVM_unicode_normalizer_available(tc, &norm);
    while (ready--)
        out_buffer[out_pos++] = MVM_unicode_normalizer_get_grapheme(tc, &norm);
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
MVMint64 MVM_string_index(MVMThreadContext *tc, MVMString *haystack, MVMString *needle, MVMint64 start) {
    MVMint64 result        = -1;
    size_t index           = (size_t)start;
    MVMStringIndex hgraphs = MVM_string_graphs(tc, haystack), ngraphs = MVM_string_graphs(tc, needle);

    MVM_string_check_arg(tc, haystack, "index search target");
    MVM_string_check_arg(tc, needle, "index search term");

    if (!ngraphs)
        return start <= hgraphs ? start : -1; /* the empty string is in any other string */

    if (!hgraphs)
        return -1;

    if (start < 0 || start >= hgraphs)
        return -1;

    if (ngraphs > hgraphs || ngraphs < 1)
        return -1;

    /* brute force for now. horrible, yes. halp. */
    while (index <= hgraphs - ngraphs) {
        if (MVM_string_substrings_equal_nocheck(tc, needle, 0, ngraphs, haystack, index)) {
            result = (MVMint64)index;
            break;
        }
        index++;
    }
    return result;
}

/* Returns the location of one string in another or -1  */
MVMint64 MVM_string_index_from_end(MVMThreadContext *tc, MVMString *haystack, MVMString *needle, MVMint64 start) {
    MVMint64 result = -1;
    size_t index;
    MVMStringIndex hgraphs = MVM_string_graphs(tc, haystack), ngraphs = MVM_string_graphs(tc, needle);

    MVM_string_check_arg(tc, haystack, "rindex search target");
    MVM_string_check_arg(tc, needle, "rindex search term");

    if (!ngraphs) {
        if (start >= 0)
            return start <= hgraphs ? start : -1; /* the empty string is in any other string */
        else
            return hgraphs; /* no start, so return end */
    }

    if (!hgraphs)
        return -1;

    if (ngraphs > hgraphs || ngraphs < 1)
        return -1;

    if (start == -1)
        start = hgraphs - ngraphs;

    if (start < 0 || start >= hgraphs)
        /* maybe return -1 instead? */
        MVM_exception_throw_adhoc(tc, "index start offset out of range");

    index = start;

    if (index + ngraphs > hgraphs) {
        index = hgraphs - ngraphs;
    }

    /* brute force for now. horrible, yes. halp. */
    do {
        if (MVM_string_substrings_equal_nocheck(tc, needle, 0, ngraphs, haystack, index)) {
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

    /* convert to signed to avoid implicit arithmetic conversions */
    MVMint64 agraphs = (MVMint64)MVM_string_graphs(tc, a);

    MVM_string_check_arg(tc, a, "substring");

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
            MVMint32 i;
            result->body.storage_type    = MVM_STRING_GRAPHEME_32;
            result->body.storage.blob_32 = MVM_malloc(result->body.num_graphs * sizeof(MVMGrapheme32));
            MVM_string_gi_init(tc, &gi, a);
            MVM_string_gi_move_to(tc, &gi, start_pos);
            for (i = 0; i < result->body.num_graphs; i++)
                result->body.storage.blob_32[i] = MVM_string_gi_get_grapheme(tc, &gi);
        }
    });

    STRAND_CHECK(tc, result);
    return result;
}

MVMString * MVM_string_replace(MVMThreadContext *tc, MVMString *original, MVMint64 start, MVMint64 count, MVMString *replacement) {
    /* XXX this could probably be done more efficiently directly. */
    MVMString *first_part;
    MVMString *rest_part;
    MVMString *result;

    MVM_gc_root_temp_push(tc, (MVMCollectable **)&replacement);
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&original);
    first_part = MVM_string_substring(tc, original, 0, start);
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&first_part);

    rest_part  = MVM_string_substring(tc, original, start + count, -1);
    rest_part  = MVM_string_concatenate(tc, replacement, rest_part);
    result     = MVM_string_concatenate(tc, first_part, rest_part);

    MVM_gc_root_temp_pop_n(tc, 3);

    STRAND_CHECK(tc, result);
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
    MVMString *result;
    MVMuint32  agraphs, bgraphs;

    MVM_string_check_arg(tc, a, "concatenate");
    MVM_string_check_arg(tc, b, "concatenate");

    /* Simple empty-string cases. */
    agraphs = MVM_string_graphs(tc, a);
    if (agraphs == 0)
        return b;
    bgraphs = MVM_string_graphs(tc, b);
    if (bgraphs == 0)
        return a;

    /* Otherwise, we'll assemble a result string. */
    MVMROOT(tc, a, {
    MVMROOT(tc, b, {
        /* Allocate it. */
        result = (MVMString *)MVM_repr_alloc_init(tc, tc->instance->VMString);

        /* Total graphemes is trivial; just total up inputs. */
        result->body.num_graphs = agraphs + bgraphs;

        /* Result string will be made of strands. */
        result->body.storage_type = MVM_STRING_STRAND;

        /* Detect the wonderful case where we're repeatedly concating the same
         * string again and again, and thus can just bump a repetition. */
        if (final_strand_matches(tc, a, b)) {
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
                if (strands_a >= strands_b) {
                    effective_a = collapse_strands(tc, effective_a);
                    strands_a   = 1;
                }
                else {
                    effective_b = collapse_strands(tc, effective_b);
                    strands_b   = 1;
                }
            }

            /* Assemble the result. */
            result->body.num_strands = strands_a + strands_b;
            result->body.storage.strands = allocate_strands(tc, strands_a + strands_b);
            if (effective_a->body.storage_type == MVM_STRING_STRAND) {
                copy_strands(tc, effective_a, 0, result, 0, strands_a);
            }
            else {
                MVMStringStrand *ss = &(result->body.storage.strands[0]);
                ss->blob_string = effective_a;
                ss->start       = 0;
                ss->end         = effective_a->body.num_graphs;
                ss->repetitions = 0;
            }
            if (effective_b->body.storage_type == MVM_STRING_STRAND) {
                copy_strands(tc, effective_b, 0, result, strands_a, strands_b);
            }
            else {
                MVMStringStrand *ss = &(result->body.storage.strands[strands_a]);
                ss->blob_string = effective_b;
                ss->start       = 0;
                ss->end         = effective_b->body.num_graphs;
                ss->repetitions = 0;
            }
        }
    });
    });

    STRAND_CHECK(tc, result);
    return MVM_nfg_is_concat_stable(tc, a, b) ? result : re_nfg(tc, result);
}

MVMString * MVM_string_repeat(MVMThreadContext *tc, MVMString *a, MVMint64 count) {
    MVMString *result;
    MVMuint32  agraphs;

    MVM_string_check_arg(tc, a, "repeat");

    /* Validate count; handle common cases. */
    if (count == 0)
        return tc->instance->str_consts.empty;
    if (count == 1)
        return a;
    if (count < 0)
        MVM_exception_throw_adhoc(tc, "repeat count (%"PRId64") cannot be negative", count);
    if (count > (1 << 30))
        MVM_exception_throw_adhoc(tc, "repeat count > %d arbitrarily unsupported...", (1 << 30));

    /* If input string is empty, repeating it is empty. */
    agraphs = MVM_string_graphs(tc, a);
    if (agraphs == 0)
        return tc->instance->str_consts.empty;

    /* Now build a result string with the repetition set. */
    MVMROOT(tc, a, {
        result = (MVMString *)MVM_repr_alloc_init(tc, tc->instance->VMString);
        result->body.num_graphs      = agraphs * count;
        result->body.storage_type    = MVM_STRING_STRAND;
        result->body.storage.strands = allocate_strands(tc, 1);
        result->body.num_strands     = 1;
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
    });

    STRAND_CHECK(tc, result);
    return result;
}

void MVM_string_say(MVMThreadContext *tc, MVMString *a) {
    MVM_string_check_arg(tc, a, "say");
    MVM_io_write_string(tc, tc->instance->stdout_handle, a, 1);
}

void MVM_string_print(MVMThreadContext *tc, MVMString *a) {
    MVM_string_check_arg(tc, a, "print");
    MVM_io_write_string(tc, tc->instance->stdout_handle, a, 0);
}

/* Tests whether one string a has the other string b as a substring at that index */
MVMint64 MVM_string_equal_at(MVMThreadContext *tc, MVMString *a, MVMString *b, MVMint64 offset) {

    MVMStringIndex agraphs, bgraphs;

    MVM_string_check_arg(tc, a, "equal_at");
    MVM_string_check_arg(tc, b, "equal_at");

    agraphs = MVM_string_graphs(tc, a);
    bgraphs = MVM_string_graphs(tc, b);

    if (offset < 0) {
        offset += agraphs;
        if (offset < 0)
            offset = 0; /* XXX I think this is the right behavior here */
    }
    if (agraphs - offset < bgraphs)
        return 0;
    return MVM_string_substrings_equal_nocheck(tc, a, offset, bgraphs, b, 0);
}

MVMint64 MVM_string_equal_at_ignore_case(MVMThreadContext *tc, MVMString *a, MVMString *b, MVMint64 offset) {
    MVMString *lca;
    MVMString *lcb;
    MVMROOT(tc, b, {
        lca = MVM_string_lc(tc, a);
        MVMROOT(tc, lca, {
            lcb = MVM_string_lc(tc, b);
        });
    });
    return MVM_string_equal_at(tc, lca, lcb, offset);
}

MVMGrapheme32 MVM_string_ord_basechar_at(MVMThreadContext *tc, MVMString *s, MVMint64 offset) {
    MVMGrapheme32 g = MVM_string_get_grapheme_at(tc, s, offset);
    MVMNormalizer norm;
    MVMint32 ready;
    MVM_unicode_normalizer_init(tc, &norm, MVM_NORMALIZE_NFD);
    ready = MVM_unicode_normalizer_process_codepoint_to_grapheme(tc, &norm, g, &g);
    MVM_unicode_normalizer_eof(tc, &norm);
    if (!ready)
        g = MVM_unicode_normalizer_get_grapheme(tc, &norm);
    return g;
}

/* Compares two strings for equality. */
MVMint64 MVM_string_equal(MVMThreadContext *tc, MVMString *a, MVMString *b) {
    MVM_string_check_arg(tc, a, "equal");
    MVM_string_check_arg(tc, b, "equal");
    if (a == b)
        return 1;
    if (MVM_string_graphs(tc, a) != MVM_string_graphs(tc, b))
        return 0;
    return MVM_string_equal_at(tc, a, b, 0);
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
    if (starta + length > MVM_string_graphs(tc, a) || startb + length > MVM_string_graphs(tc, b))
        return 0;

    return MVM_string_substrings_equal_nocheck(tc, a, starta, length, b, startb);
}

/* Returns the grapheme at a given index of the string */
MVMint64 MVM_string_get_grapheme_at(MVMThreadContext *tc, MVMString *a, MVMint64 index) {
    MVMStringIndex agraphs;

    MVM_string_check_arg(tc, a, "grapheme_at");

    agraphs = MVM_string_graphs(tc, a);

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

#define change_case_iterate(member, dest_member, dest_size) \
for (i = string->body.member + start; i < string->body.member + start + length; ) { \
    if (dest->body.graphs == state->size) { \
        if (!state->size) state->size = 16; \
        else state->size *= 2; \
        dest->body.dest_member = MVM_realloc(dest->body.dest_member, \
            state->size * sizeof(dest_size)); \
    } \
    dest->body.dest_member[dest->body.graphs++] = \
        MVM_unicode_get_case_change(tc, (MVMCodepoint32) *i++, state->case_change_type); \
}

/* Case change functions. */
#define case_change_func(funcname, type, error) \
MVMString * funcname(MVMThreadContext *tc, MVMString *s) { \
    MVMint64 sgraphs; \
    MVM_string_check_arg(tc, s, error); \
    sgraphs = MVM_string_graphs(tc, s); \
    if (sgraphs) { \
        MVMString *result; \
        MVMGraphemeIter gi; \
        MVMGrapheme32 *result_buf = MVM_malloc(sgraphs * sizeof(MVMGrapheme32)); \
        MVMint32 changed = 0; \
        MVMint64 i = 0; \
        MVM_string_gi_init(tc, &gi, s); \
        while (i < sgraphs) { \
            MVMGrapheme32 before = MVM_string_gi_get_grapheme(tc, &gi); \
            MVMGrapheme32 after  = before >= 0 \
                ? MVM_unicode_get_case_change(tc, before, type) \
                : MVM_nfg_get_case_change(tc, before, type); \
            result_buf[i++]      = after; \
            if (before != after) \
                changed = 1; \
        } \
        if (changed) { \
            result = (MVMString *)MVM_repr_alloc_init(tc, tc->instance->VMString); \
            result->body.num_graphs      = sgraphs; \
            result->body.storage_type    = MVM_STRING_GRAPHEME_32; \
            result->body.storage.blob_32 = result_buf; \
            return result; \
        } \
        else { \
            MVM_free(result_buf); \
        } \
    } \
    STRAND_CHECK(tc, s); \
    return s; \
}
case_change_func(MVM_string_uc, MVM_unicode_case_change_type_upper, "uc")
case_change_func(MVM_string_lc, MVM_unicode_case_change_type_lower, "lc")
case_change_func(MVM_string_tc, MVM_unicode_case_change_type_title, "tc")

/* Decodes a C buffer to an MVMString, dependent on the encoding type flag. */
MVMString * MVM_string_decode(MVMThreadContext *tc,
        MVMObject *type_object, char *Cbuf, MVMint64 byte_length, MVMint64 encoding_flag) {
    switch(encoding_flag) {
        case MVM_encoding_type_utf8:
            return MVM_string_utf8_decode(tc, type_object, Cbuf, byte_length);
        case MVM_encoding_type_ascii:
            return MVM_string_ascii_decode(tc, type_object, Cbuf, byte_length);
        case MVM_encoding_type_latin1:
            return MVM_string_latin1_decode(tc, type_object, Cbuf, byte_length);
        case MVM_encoding_type_utf16:
            return MVM_string_utf16_decode(tc, type_object, Cbuf, byte_length);
        case MVM_encoding_type_windows1252:
            return MVM_string_windows1252_decode(tc, type_object, Cbuf, byte_length);
        default:
            MVM_exception_throw_adhoc(tc, "invalid encoding type flag: %"PRId64, encoding_flag);
    }
    return NULL;
}

/* Encodes an MVMString to a C buffer, dependent on the encoding type flag */
char * MVM_string_encode(MVMThreadContext *tc, MVMString *s, MVMint64 start, MVMint64 length, MVMuint64 *output_size, MVMint64 encoding_flag) {
    switch(encoding_flag) {
        case MVM_encoding_type_utf8:
            return MVM_string_utf8_encode_substr(tc, s, output_size, start, length);
        case MVM_encoding_type_ascii:
            return MVM_string_ascii_encode_substr(tc, s, output_size, start, length);
        case MVM_encoding_type_latin1:
            return MVM_string_latin1_encode_substr(tc, s, output_size, start, length);
        case MVM_encoding_type_utf16:
            return MVM_string_utf16_encode_substr(tc, s, output_size, start, length);
        case MVM_encoding_type_windows1252:
            return MVM_string_windows1252_encode_substr(tc, s, output_size, start, length);
        default:
            MVM_exception_throw_adhoc(tc, "invalid encoding type flag: %"PRId64, encoding_flag);
    }
    return NULL;
}

/* Encodes a string, and writes the encoding string into the supplied Buf
 * instance, which should be an integer array with MVMArray REPR. */
void MVM_string_encode_to_buf(MVMThreadContext *tc, MVMString *s, MVMString *enc_name, MVMObject *buf) {
    MVMuint64 output_size;
    MVMuint8 *encoded;
    MVMArrayREPRData *buf_rd;
    MVMuint8 elem_size = 0;

    /* Ensure the target is in the correct form. */
    MVM_string_check_arg(tc, s, "encode");
    if (!IS_CONCRETE(buf) || REPR(buf)->ID != MVM_REPR_ID_MVMArray)
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
        encoded = (MVMuint8 *)MVM_string_encode(tc, s, 0, MVM_string_graphs(tc, s), &output_size,
            encoding_flag);
    });
    });

    /* Stash the encoded data in the VMArray. */
    ((MVMArray *)buf)->body.slots.i8 = (MVMint8 *)encoded;
    ((MVMArray *)buf)->body.start    = 0;
    ((MVMArray *)buf)->body.ssize    = output_size / elem_size;
    ((MVMArray *)buf)->body.elems    = output_size / elem_size;
}

/* Decodes a string using the data from the specified Buf. */
MVMString * MVM_string_decode_from_buf(MVMThreadContext *tc, MVMObject *buf, MVMString *enc_name) {
    MVMArrayREPRData *buf_rd;
    MVMuint8 encoding_flag;
    MVMuint8 elem_size = 0;

    /* Ensure the source is in the correct form. */
    if (!IS_CONCRETE(buf) || REPR(buf)->ID != MVM_REPR_ID_MVMArray)
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
            end = MVM_string_graphs(tc, input);
            sep_length = MVM_string_graphs(tc, separator);

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
    MVMString  *result;
    MVMString **pieces;
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
    sgraphs  = MVM_string_graphs(tc, separator);
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
                if (!concats_stable)
                    /* Already stable; no more checks. */;
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
    if (offset < 0 || offset >= MVM_string_graphs(tc, a))
        return -2;

    search  = MVM_string_get_grapheme_at_nocheck(tc, a, offset);
    bgraphs = MVM_string_graphs(tc, b);
    switch (b->body.storage_type) {
    case MVM_STRING_GRAPHEME_32: {
        MVMStringIndex i;
        for (i = 0; i < bgraphs; i++)
            if (b->body.storage.blob_32[i] == search)
                return i;
        break;
    }
    case MVM_STRING_GRAPHEME_ASCII:
        if (search >= 0 && search <= 127) {
            MVMStringIndex i;
            for (i = 0; i < bgraphs; i++)
                if (b->body.storage.blob_ascii[i] == search)
                    return i;
        }
        break;
    case MVM_STRING_GRAPHEME_8:
        if (search >= -128 && search <= 127) {
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

    if (offset < 0 || offset >= MVM_string_graphs(tc, s))
        return 0;

    g = MVM_string_get_grapheme_at_nocheck(tc, s, offset);
    if (g >= 0)
        cp = (MVMCodepoint)g;
    else
        cp = MVM_nfg_get_synthetic_info(tc, g)->base;
    return MVM_unicode_codepoint_has_property_value(tc, cp, property_code, property_value_code);
}

/* Normalizes a string to a flat MVMGrapheme32 buffer, for the benefit of
 * hashing. Would rather not have to do this eventually. */
void MVM_string_flatten(MVMThreadContext *tc, MVMString *s) {
    MVM_string_check_arg(tc, s, "flatten");
    switch (s->body.storage_type) {
    case MVM_STRING_GRAPHEME_32:
        return;
    case MVM_STRING_GRAPHEME_ASCII:
    case MVM_STRING_GRAPHEME_8: {
        MVMuint32      length = MVM_string_graphs(tc, s);
        MVMGrapheme32 *flat   = MVM_malloc(length * sizeof(MVMGrapheme32));
        MVMGrapheme8  *orig   = s->body.storage.blob_8;
        MVMuint32 i;
        for (i = 0; i < length; i++)
            flat[i] = orig[i];
        s->body.storage.blob_32 = flat;
        s->body.storage_type    = MVM_STRING_GRAPHEME_32;
        MVM_free(orig);
        break;
    }
    case MVM_STRING_STRAND: {
        MVMGrapheme32   *flat = MVM_malloc(MVM_string_graphs(tc, s) * sizeof(MVMGrapheme32));
        MVMStringStrand *orig = s->body.storage.strands;
        MVMuint32        i    = 0;
        MVMGraphemeIter gi;
        MVM_string_gi_init(tc, &gi, s);
        while (MVM_string_gi_has_more(tc, &gi))
            flat[i++] = MVM_string_gi_get_grapheme(tc, &gi);
        s->body.storage.blob_32 = flat;
        s->body.storage_type    = MVM_STRING_GRAPHEME_32;
        MVM_free(orig);
        break;
    }
    }
}

/* Escapes a string, replacing various chars like \n with \\n. Can no doubt be
 * further optimized. */
MVMString * MVM_string_escape(MVMThreadContext *tc, MVMString *s) {
    MVMString      *res     = NULL;
    MVMStringIndex  spos    = 0;
    MVMStringIndex  bpos    = 0;
    MVMStringIndex  sgraphs, balloc;
    MVMGrapheme32  *buffer;

    MVM_string_check_arg(tc, s, "escape");

    sgraphs = MVM_string_graphs(tc, s);
    balloc  = sgraphs;
    buffer  = MVM_malloc(sizeof(MVMGrapheme32) * balloc);

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
        else {
            if (bpos + 1 > balloc) {
                balloc += 32;
                buffer = MVM_realloc(buffer, sizeof(MVMGrapheme32) * balloc);
            }
            buffer[bpos++] = graph;
        }
    }

    res = (MVMString *)MVM_repr_alloc_init(tc, tc->instance->VMString);
    res->body.storage_type    = MVM_STRING_GRAPHEME_32;
    res->body.storage.blob_32 = buffer;
    res->body.num_graphs      = bpos;

    STRAND_CHECK(tc, res);
    return res;
}

/* Takes a string and reverses its characters. */
MVMString * MVM_string_flip(MVMThreadContext *tc, MVMString *s) {
    MVMString      *res     = NULL;
    MVMStringIndex  spos    = 0;
    MVMStringIndex  sgraphs;
    MVMGrapheme32  *rbuffer;
    MVMStringIndex  rpos;

    MVM_string_check_arg(tc, s, "flip");

    sgraphs = MVM_string_graphs(tc, s);
    rbuffer = MVM_malloc(sizeof(MVMGrapheme32) * sgraphs);
    rpos    = sgraphs;

    for (; spos < sgraphs; spos++)
        rbuffer[--rpos] = MVM_string_get_grapheme_at_nocheck(tc, s, spos);

    res = (MVMString *)MVM_repr_alloc_init(tc, tc->instance->VMString);
    res->body.storage_type    = MVM_STRING_GRAPHEME_32;
    res->body.storage.blob_32 = rbuffer;
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
    alen = MVM_string_graphs(tc, a);
    blen = MVM_string_graphs(tc, b);
    if (alen == 0)
        return blen == 0 ? 0 : -1;
    if (blen == 0)
        return 1;

    /* Otherwise, need to scan them. */
    scanlen = alen > blen ? blen : alen;
    for (i = 0; i < scanlen; i++) {
        MVMGrapheme32 ai = MVM_string_get_grapheme_at_nocheck(tc, a, i);
        MVMGrapheme32 bi = MVM_string_get_grapheme_at_nocheck(tc, b, i);
        if (ai != bi)
            return ai < bi ? -1 : 1;
    }

    /* All shared chars equal, so go on length. */
    return alen < blen ? -1 :
           alen > blen ?  1 :
                          0 ;
}

/* Takes two strings and AND's their characters. */
MVMString * MVM_string_bitand(MVMThreadContext *tc, MVMString *a, MVMString *b) {
    MVMString      *res;
    MVMGrapheme32  *buffer;
    MVMStringIndex  i, alen, blen, sgraphs;

    MVM_string_check_arg(tc, a, "bitwise and");
    MVM_string_check_arg(tc, b, "bitwise and");

    alen = MVM_string_graphs(tc, a);
    blen = MVM_string_graphs(tc, b);
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
    MVMString      *res;
    MVMGrapheme32  *buffer;
    MVMStringIndex  alen, blen, sgraphs, i, scanlen;

    MVM_string_check_arg(tc, a, "bitwise or");
    MVM_string_check_arg(tc, b, "bitwise or");

    alen = MVM_string_graphs(tc, a);
    blen = MVM_string_graphs(tc, b);
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
    MVMString      *res;
    MVMGrapheme32  *buffer;
    MVMStringIndex  alen, blen, sgraphs, i, scanlen;

    MVM_string_check_arg(tc, a, "bitwise xor");
    MVM_string_check_arg(tc, b, "bitwise xor");

    alen = MVM_string_graphs(tc, a);
    blen = MVM_string_graphs(tc, b);
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

/* Resolves various unicode property values that we'll need. */
void MVM_string_cclass_init(MVMThreadContext *tc) {
    UPV_Nd = MVM_unicode_name_to_property_value_code(tc,
        MVM_UNICODE_PROPERTY_GENERAL_CATEGORY,
        MVM_string_ascii_decode_nt(tc, tc->instance->VMString, "Nd"));
    UPV_Lu = MVM_unicode_name_to_property_value_code(tc,
        MVM_UNICODE_PROPERTY_GENERAL_CATEGORY,
        MVM_string_ascii_decode_nt(tc, tc->instance->VMString, "Lu"));
    UPV_Ll = MVM_unicode_name_to_property_value_code(tc,
        MVM_UNICODE_PROPERTY_GENERAL_CATEGORY,
        MVM_string_ascii_decode_nt(tc, tc->instance->VMString, "Ll"));
    UPV_Lt = MVM_unicode_name_to_property_value_code(tc,
        MVM_UNICODE_PROPERTY_GENERAL_CATEGORY,
        MVM_string_ascii_decode_nt(tc, tc->instance->VMString, "Lt"));
    UPV_Lm = MVM_unicode_name_to_property_value_code(tc,
        MVM_UNICODE_PROPERTY_GENERAL_CATEGORY,
        MVM_string_ascii_decode_nt(tc, tc->instance->VMString, "Lm"));
    UPV_Lo = MVM_unicode_name_to_property_value_code(tc,
        MVM_UNICODE_PROPERTY_GENERAL_CATEGORY,
        MVM_string_ascii_decode_nt(tc, tc->instance->VMString, "Lo"));
    UPV_Zs = MVM_unicode_name_to_property_value_code(tc,
        MVM_UNICODE_PROPERTY_GENERAL_CATEGORY,
        MVM_string_ascii_decode_nt(tc, tc->instance->VMString, "Zs"));
    UPV_Zl = MVM_unicode_name_to_property_value_code(tc,
        MVM_UNICODE_PROPERTY_GENERAL_CATEGORY,
        MVM_string_ascii_decode_nt(tc, tc->instance->VMString, "Zl"));
    UPV_Pc = MVM_unicode_name_to_property_value_code(tc,
        MVM_UNICODE_PROPERTY_GENERAL_CATEGORY,
        MVM_string_ascii_decode_nt(tc, tc->instance->VMString, "Pc"));
    UPV_Pd = MVM_unicode_name_to_property_value_code(tc,
        MVM_UNICODE_PROPERTY_GENERAL_CATEGORY,
        MVM_string_ascii_decode_nt(tc, tc->instance->VMString, "Pd"));
    UPV_Ps = MVM_unicode_name_to_property_value_code(tc,
        MVM_UNICODE_PROPERTY_GENERAL_CATEGORY,
        MVM_string_ascii_decode_nt(tc, tc->instance->VMString, "Ps"));
    UPV_Pe = MVM_unicode_name_to_property_value_code(tc,
        MVM_UNICODE_PROPERTY_GENERAL_CATEGORY,
        MVM_string_ascii_decode_nt(tc, tc->instance->VMString, "Pe"));
    UPV_Pi = MVM_unicode_name_to_property_value_code(tc,
        MVM_UNICODE_PROPERTY_GENERAL_CATEGORY,
        MVM_string_ascii_decode_nt(tc, tc->instance->VMString, "Pi"));
    UPV_Pf = MVM_unicode_name_to_property_value_code(tc,
        MVM_UNICODE_PROPERTY_GENERAL_CATEGORY,
        MVM_string_ascii_decode_nt(tc, tc->instance->VMString, "Pf"));
    UPV_Po = MVM_unicode_name_to_property_value_code(tc,
        MVM_UNICODE_PROPERTY_GENERAL_CATEGORY,
        MVM_string_ascii_decode_nt(tc, tc->instance->VMString, "Po"));
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
            return !(cp >= 0 && cp < 32) || (cp >= 127 && cp < 160);
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
    if (offset < 0 || offset >= MVM_string_graphs(tc, s))
        return 0;
    g = MVM_string_get_grapheme_at_nocheck(tc, s, offset);
    return grapheme_is_cclass(tc, cclass, g);
}

/* Searches for the next char that is in the specified character class. */
MVMint64 MVM_string_find_cclass(MVMThreadContext *tc, MVMint64 cclass, MVMString *s, MVMint64 offset, MVMint64 count) {
    MVMGraphemeIter gi;
    MVMint64        length, end, pos;

    MVM_string_check_arg(tc, s, "find_cclass");

    length = MVM_string_graphs(tc, s);
    end    = offset + count;
    if (offset < 0 || offset >= length)
        return end;
    end = length < end ? length : end;

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

    length = MVM_string_graphs(tc, s);
    end    = offset + count;
    if (offset < 0 || offset >= length)
        return offset;
    end = length < end ? length : end;

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
MVMuint8 MVM_string_find_encoding(MVMThreadContext *tc, MVMString *name) {
    MVM_string_check_arg(tc, name, "find encoding");
    if (!encoding_name_init) {
        MVM_gc_allocate_gen2_default_set(tc);
        encoding_utf8_name        = MVM_string_ascii_decode_nt(tc, tc->instance->VMString, "utf8");
        MVM_gc_root_add_permanent(tc, (MVMCollectable **)&encoding_utf8_name);
        encoding_ascii_name       = MVM_string_ascii_decode_nt(tc, tc->instance->VMString, "ascii");
        MVM_gc_root_add_permanent(tc, (MVMCollectable **)&encoding_ascii_name);
        encoding_latin1_name      = MVM_string_ascii_decode_nt(tc, tc->instance->VMString, "iso-8859-1");
        MVM_gc_root_add_permanent(tc, (MVMCollectable **)&encoding_latin1_name);
        encoding_utf16_name       = MVM_string_ascii_decode_nt(tc, tc->instance->VMString, "utf16");
        MVM_gc_root_add_permanent(tc, (MVMCollectable **)&encoding_utf16_name);
        encoding_windows1252_name = MVM_string_ascii_decode_nt(tc, tc->instance->VMString, "windows-1252");
        MVM_gc_root_add_permanent(tc, (MVMCollectable **)&encoding_windows1252_name);
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
    else {
        MVM_exception_throw_adhoc(tc, "Unknown string encoding: '%s'",
            MVM_string_utf8_encode_C_string(tc, name));
    }
}

/* Turns a codepoint into a string. Uses the normalizer to ensure that we get
 * a valid NFG string (NFG is a superset of NFC, and singleton decompositions
 * exist). */
MVMString * MVM_string_chr(MVMThreadContext *tc, MVMCodepoint cp) {
    MVMString *s;
    MVMGrapheme32 g;
    MVMNormalizer norm;

    if (cp < 0)
        MVM_exception_throw_adhoc(tc, "chr codepoint cannot be negative");

    MVM_unicode_normalizer_init(tc, &norm, MVM_NORMALIZE_NFG);
    if (!MVM_unicode_normalizer_process_codepoint_to_grapheme(tc, &norm, cp, &g)) {
        MVM_unicode_normalizer_eof(tc, &norm);
        g = MVM_unicode_normalizer_get_grapheme(tc, &norm);
    }
    MVM_unicode_normalizer_cleanup(tc, &norm);

    s = (MVMString *)REPR(tc->instance->VMString)->allocate(tc, STABLE(tc->instance->VMString));
    s->body.storage_type       = MVM_STRING_GRAPHEME_32;
    s->body.storage.blob_32    = MVM_malloc(sizeof(MVMGrapheme32));
    s->body.storage.blob_32[0] = g;
    s->body.num_graphs         = 1;
    return s;
}
