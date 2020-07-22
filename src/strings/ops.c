#include "platform/memmem.h"
#include "platform/memmem32.h"
#include "moar.h"
#define MVM_DEBUG_STRANDS 0
#define MVM_string_KMP_max_pattern_length 8192
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
    else if (g < 0) {
        MVMNFGSynthetic *synth = MVM_nfg_get_synthetic_info(tc, g);
        char *format_str = " with num_codes = ";
        char *format_str2 = " first, second cp = ";
        char *synthtype_str = synth->is_utf8_c8 ?  "utf8-8 Synthetic" :  "Normal Synthetic";
        int this_len = strlen(format_str) + strlen(synthtype_str) + 6 + strlen(format_str2) + 11 + 1 + 11 + 1;
        result = MVM_malloc(this_len);
        if (2 <= synth->num_codes)
            sprintf(result, "%s%s%5i%s%.10"PRIi32",%.10"PRIi32"", synthtype_str, format_str, synth->num_codes, format_str2, synth->codes[0], synth->codes[1]);
        else
            sprintf(result, "WARNING synth has less than 2 codes");
        fprintf(stderr, "synth numcodes %i %"PRIi32"\n",
            MVM_nfg_get_synthetic_info(tc, synth->codes[1])->num_codes, MVM_nfg_get_synthetic_info(tc, synth->codes[1])->codes[0]
        );
    }
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
    MVMROOT2(tc, orig, renorm, {
        renorm = re_nfg(tc, orig);
        renorm_graphs = MVM_string_graphs(tc, renorm);
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
                int out_size = strlen(orig_render) + strlen(renorm_render)
                    + strlen(varname) + strlen(format) + (5 * 7) + 1;
                char *out = MVM_malloc(sizeof(char) * out_size);
                char *waste[] = {orig_render, renorm_render, NULL};
                char **w = waste;
                snprintf(out, out_size,
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

MVM_STATIC_INLINE MVMint64 string_equal_at_ignore_case_INTERNAL_loop(MVMThreadContext *tc, void *Hs_or_gic, MVMString *needle_fc, MVMint64 H_start, MVMint64 H_graphs, MVMint64 n_fc_graphs, int ignoremark, int ignorecase, int is_gic);
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

#define can_fit_into_8bit(g) ((-128 <= (g) && (g) <= 127))

MVM_STATIC_INLINE int can_fit_into_ascii (MVMGrapheme32 g) {
    return 0 <= g && g <= 127;
}
/* If a string is currently using 32bit storage, turn it into using
 * 8 bit storage. Doesn't do any checks at all. */
static void turn_32bit_into_8bit_unchecked(MVMThreadContext *tc, MVMString *str) {
    MVMGrapheme32 *old_buf = str->body.storage.blob_32;
    MVMStringIndex i;
    MVMGrapheme8 *dest_buf = NULL;
    MVMStringIndex num_graphs = MVM_string_graphs_nocheck(tc, str);
    str->body.storage_type   = MVM_STRING_GRAPHEME_8;
    dest_buf = str->body.storage.blob_8 = MVM_malloc(str->body.num_graphs * sizeof(MVMGrapheme8));
    MVM_VECTORIZE_LOOP
    for (i = 0; i < num_graphs; i++) {
        dest_buf[i] = old_buf[i];
    }

    MVM_free(old_buf);
}
/* Checks if the next num_graphs graphemes in the iterator can fit into 8 bits.
 * This was written to take advantage of SIMD vectorization, so we use a multiple
 * bitwise operations to check, and biwise OR it with val. Care must be taken
 * to not use any variables altered by the loop outside of the loop and to not
 * have any branching or funcion calls. `i` is not used outside the loop
 * `val` is allowed as biwise OR works with the vectorization well.
 * NOTE: GraphemeIter is not modified by this function. */
static int string_can_be_8bit(MVMThreadContext *tc, MVMGraphemeIter *gi_orig, MVMStringIndex num_graphs) {
    MVMStringIndex pos = 0;
    MVMGraphemeIter gi;
    memcpy(&gi, gi_orig, sizeof(MVMGraphemeIter));
    while (1) {
        MVMStringIndex strand_len = MVM_string_gi_graphs_left_in_strand(tc, &gi);
        MVMStringIndex togo = num_graphs - pos < strand_len
            ? num_graphs - pos
            : strand_len;
        if (MVM_string_gi_blob_type(tc, &gi) == MVM_STRING_GRAPHEME_32) {
            if (!MVM_string_buf32_can_fit_into_8bit(MVM_string_gi_active_blob_32_pos(tc, &gi), togo))
                return 0;
        }
        pos += togo;
        if (num_graphs == pos || !MVM_string_gi_has_more_strands_rep(tc, &gi)) {
            break;
        }
        MVM_string_gi_next_strand_rep(tc, &gi);
    }
    return 1;

}
/* Accepts an allocated string that should have body.num_graphs set but the blob
 * unallocated. This function will allocate the space for the blob and iterate
 * the supplied grapheme iterator for the length of body.num_graphs. Very fast
 * since compilers will convert them to SIMD vector operations. */
static void iterate_gi_into_string(MVMThreadContext *tc, MVMGraphemeIter *gi, MVMString *result, MVMString *orig, MVMStringIndex num) {
    MVMGrapheme8   *result8   = NULL;
    MVMGrapheme32 *result32   = NULL;
    MVMStringIndex result_graphs = MVM_string_graphs_nocheck(tc, result);
    if (!result_graphs)
        return;

    if (string_can_be_8bit(tc, gi, result_graphs)) {
        MVMStringIndex result_pos = 0;
        result->body.storage_type = MVM_STRING_GRAPHEME_8;
        result8 = result->body.storage.blob_8 =
            MVM_malloc(result_graphs * sizeof(MVMGrapheme8));
        while (1) {
            MVMStringIndex strand_len =
                MVM_string_gi_graphs_left_in_strand(tc, gi);
            MVMStringIndex to_copy = result_graphs - result_pos < strand_len
                ? result_graphs - result_pos
                : strand_len;
            MVMGrapheme8  *result_blob8 = result8 + result_pos;
            switch (MVM_string_gi_blob_type(tc, gi)) {
            case MVM_STRING_GRAPHEME_32: {
                MVMStringIndex i;
                MVMGrapheme32 *active_blob =
                    MVM_string_gi_active_blob_32_pos(tc, gi);
                MVM_VECTORIZE_LOOP
                for (i = 0; i < to_copy; i++) {
                    result_blob8[i] = active_blob[i];
                }
                break;
            }
            case MVM_STRING_GRAPHEME_8:
            case MVM_STRING_GRAPHEME_ASCII: {
                memcpy(
                    result_blob8,
                    MVM_string_gi_active_blob_8_pos(tc, gi),
                    to_copy * sizeof(MVMGrapheme8)
                );
                break;
            }
            default: {
                MVM_free(result->body.storage.blob_8);
                MVM_exception_throw_adhoc(tc,
                    "Internal error, string corruption in iterate_gi_into_string\n");
            }
            }
            result_pos += to_copy;
            if (result_graphs <= result_pos || !MVM_string_gi_has_more_strands_rep(tc, gi)) {
                break;
            }
            MVM_string_gi_next_strand_rep(tc, gi);
        }
    }
    else {
        MVMStringIndex result_pos = 0;
        result->body.storage_type            = MVM_STRING_GRAPHEME_32;
        result32 = result->body.storage.blob_32 =
            result_graphs ? MVM_malloc(result_graphs * sizeof(MVMGrapheme32)) : NULL;
        while (1) {
            MVMStringIndex strand_len = MVM_string_gi_graphs_left_in_strand(tc, gi);
            MVMStringIndex to_copy = result_graphs - result_pos < strand_len
                ? result_graphs - result_pos
                : strand_len;
            switch (MVM_string_gi_blob_type(tc, gi)) {
                case MVM_STRING_GRAPHEME_8:
                case MVM_STRING_GRAPHEME_ASCII: {
                    MVMGrapheme8  *active_blob =
                        MVM_string_gi_active_blob_8_pos(tc, gi);
                    MVMGrapheme32 *result_blob32 = result32 + result_pos;
                    MVMStringIndex i;
                    MVM_VECTORIZE_LOOP
                    for (i = 0; i < to_copy; i++) {
                        result_blob32[i] = active_blob[i];
                    }
                    break;
                }
                case MVM_STRING_GRAPHEME_32: {
                    memcpy(
                        result32 + result_pos,
                        MVM_string_gi_active_blob_32_pos(tc, gi),
                        to_copy * sizeof(MVMGrapheme32)
                    );
                    break;
                }
                default: {
                    MVM_free(result->body.storage.blob_32);
                    MVM_exception_throw_adhoc(tc,
                        "Internal error, string corruption in iterate_gi_into_string\n");
                }
            }
            result_pos += to_copy;
            if (result_graphs <= result_pos || !MVM_string_gi_has_more_strands_rep(tc, gi)) {
                break;
            }
            MVM_string_gi_next_strand_rep(tc, gi);
        }
    }
}
#define copy_strands_memcpy(BLOB_TYPE, SIZEOF_TYPE, STORAGE_TYPE) { \
    result->body.storage.BLOB_TYPE = MVM_malloc(sizeof(SIZEOF_TYPE) * MVM_string_graphs_nocheck(tc, orig)); \
    for (i = 0; i < orig->body.num_strands; i++) { \
        size_t graphs_this_strand =  orig->body.storage.strands[i].end - orig->body.storage.strands[i].start; \
        /* If it's 8bit format and there's only one grapheme */ \
        if ((STORAGE_TYPE == MVM_STRING_GRAPHEME_ASCII || STORAGE_TYPE == MVM_STRING_GRAPHEME_8) && graphs_this_strand == 1) { \
            /* If there are not repetitions we can directly set the grapheme */ \
            if (!orig->body.storage.strands[i].repetitions) \
                result->body.storage.BLOB_TYPE[graphs_so_far] = orig->body.storage.strands[i].blob_string->body.storage.BLOB_TYPE[orig->body.storage.strands[i].start]; \
            /* Otherwise, use memset for the correct number of repetitions */ \
            else { \
                graphs_this_strand += orig->body.storage.strands[i].repetitions; \
                memset(graphs_so_far + result->body.storage.BLOB_TYPE, \
                    orig->body.storage.strands[i].blob_string->body.storage.BLOB_TYPE[orig->body.storage.strands[i].start], \
                    graphs_this_strand \
                ); \
            } \
            graphs_so_far += graphs_this_strand; \
        } \
        else { \
            MVMuint32 j = 0; \
            for (; j <= orig->body.storage.strands[i].repetitions; j++) { \
                memcpy(graphs_so_far + result->body.storage.BLOB_TYPE, \
                    orig->body.storage.strands[i].blob_string->body.storage.BLOB_TYPE + orig->body.storage.strands[i].start, \
                    sizeof(SIZEOF_TYPE) * graphs_this_strand \
                ); \
                graphs_so_far += graphs_this_strand; \
            } \
        } \
    } \
}
/* Collapses a bunch of strands into a single blob string. */
static MVMString * collapse_strands(MVMThreadContext *tc, MVMString *orig) {
    MVMString      *result = NULL;
    size_t graphs_so_far = 0;

    /* If it's not a strand, just return it */
    if (orig->body.storage_type != MVM_STRING_STRAND)
        return orig;
    /* If the original string is a STRAND and all the composite strands are
     * of the same type, then we will collapse it using memcpy instead of
     * using a grapheme iterator. */
    else {
        size_t i;
        MVMint32 common_storage_type = orig->body.storage.strands[0].blob_string->body.storage_type;
        MVMROOT(tc, orig, {
            result = (MVMString *)MVM_repr_alloc_init(tc, tc->instance->VMString);
            result->body.num_graphs = MVM_string_graphs(tc, orig);
            for (i = 1; i < orig->body.num_strands; i++) {
                if (common_storage_type != orig->body.storage.strands[i].blob_string->body.storage_type) {
                    common_storage_type = -1;
                    break;
                }
            }
            result->body.storage_type = common_storage_type;
            switch (common_storage_type) {
                case MVM_STRING_GRAPHEME_32:
                    copy_strands_memcpy(blob_32, MVMGrapheme32, MVM_STRING_GRAPHEME_32);
                    break;
                case MVM_STRING_GRAPHEME_ASCII:
                case MVM_STRING_GRAPHEME_8:
                    copy_strands_memcpy(blob_8, MVMGrapheme8, MVM_STRING_GRAPHEME_8);
                    break;
                default: {
                    MVMGraphemeIter gi;
                    MVM_string_gi_init(tc, &gi, orig);
                    iterate_gi_into_string(tc, &gi, result, orig, 0);
                }
            }
        });
    }
#if (MVM_DEBUG_STRANDS || MVM_DEBUG_NFG)
    if (!MVM_string_equal(tc, result, orig))
        MVM_exception_throw_adhoc(tc, "result and original were not eq in collapse_strands");
#endif
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
    /* Codepoint iterator that passes back utf8-c8 graphemes unchanged */
    MVM_string_ci_init(tc, &ci, in, 0, 1);
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

    /* If both are flat, use MVM_string_get_grapheme_at_nocheck on both for speed */
    if (a->body.storage_type != MVM_STRING_STRAND && b->body.storage_type != MVM_STRING_STRAND) {
        for (i = 0; i < length; i++)
            if (MVM_string_get_grapheme_at_nocheck(tc, a, starta + i)
             != MVM_string_get_grapheme_at_nocheck(tc, b, startb + i))
                return 0;
        return 1;
    }
    else if (a->body.storage_type == MVM_STRING_STRAND && b->body.storage_type == MVM_STRING_STRAND) {
        MVMGraphemeIter gia, gib;
        /* Normal path, for the rest of the time. */
        MVM_string_gi_init(tc, &gia, a);
        MVM_string_gi_init(tc, &gib, b);
        /* Move the grapheme iterator if start is not 0 */
        if (starta) MVM_string_gi_move_to(tc, &gia, starta);
        if (startb) MVM_string_gi_move_to(tc, &gib, startb);
        for (i = 0; i < length; i++)
            if (MVM_string_gi_get_grapheme(tc, &gia) != MVM_string_gi_get_grapheme(tc, &gib))
                return 0;
        return 1;
    }
    else {
        MVMGraphemeIter gi_y;
        MVMString *y = NULL, *z = NULL;
        MVMint64 starty, startz;
        if (a->body.storage_type == MVM_STRING_STRAND) {
                 y = a;           z = b;
            starty = starta; startz = startb;
        }
        else {
                 y = b;           z = a;
            starty = startb; startz = starta;
        }
        MVM_string_gi_init(tc, &gi_y, y);
        if (starty) MVM_string_gi_move_to(tc, &gi_y, starty);
        for (i = 0; i < length; i++)
            if (MVM_string_gi_get_grapheme(tc, &gi_y) != MVM_string_get_grapheme_at_nocheck(tc, z, startz + i))
                return 0;
        return 1;
    }
}
static MVMint64 MVM_string_memmem_grapheme32 (MVMThreadContext *tc, MVMGrapheme32 *H_blob32, MVMGrapheme32 *n_blob32, MVMint64 H_start, MVMStringIndex H_graphs, MVMStringIndex n_graphs) {
    MVMGrapheme32 * rtrn = memmem_uint32(H_blob32 + H_start, H_graphs - H_start, n_blob32, n_graphs);
    return rtrn == NULL ? -1 : rtrn - H_blob32;
}
static MVMint64 MVM_string_memmem_grapheme32str (MVMThreadContext *tc, MVMString *Haystack, MVMString *needle, MVMint64 H_start, MVMStringIndex H_graphs, MVMStringIndex n_graphs) {
    MVMGrapheme32 *needle_buf = NULL;
    MVMint64 rtrn;
    if (needle->body.storage_type != MVM_STRING_GRAPHEME_32) {
        MVMStringIndex i;
        MVMGraphemeIter n_gi;
        needle_buf = MVM_malloc(needle->body.num_graphs * sizeof(MVMGrapheme32));
        if (needle->body.storage_type != MVM_STRING_GRAPHEME_8) MVM_string_gi_init(tc, &n_gi, needle);
        for (i = 0; i < needle->body.num_graphs; i++) {
            needle_buf[i] = needle->body.storage_type == MVM_STRING_GRAPHEME_8 ? needle->body.storage.blob_8[i] : MVM_string_gi_get_grapheme(tc, &n_gi);
        }
    }
    rtrn = MVM_string_memmem_grapheme32(tc, Haystack->body.storage.blob_32, needle_buf ? needle_buf : needle->body.storage.blob_32, H_start, H_graphs, n_graphs);
    if (needle_buf) MVM_free(needle_buf);
    return rtrn;
}
/* Returns the location of one string in another or -1  */
MVMint64 MVM_string_index(MVMThreadContext *tc, MVMString *Haystack, MVMString *needle, MVMint64 start) {
    size_t index           = (size_t)start;
    MVMStringIndex H_graphs, n_graphs;
    MVM_string_check_arg(tc, Haystack, "index search target");
    MVM_string_check_arg(tc,   needle, "index search term");
    H_graphs = MVM_string_graphs_nocheck(tc, Haystack);
    n_graphs = MVM_string_graphs_nocheck(tc, needle);

    if (!n_graphs)
        return start <= H_graphs ? start : -1; /* the empty string is in any other string */

    if (!H_graphs)
        return -1;

    if (start < 0 || H_graphs <= start)
        return -1;

    if (H_graphs < n_graphs || n_graphs < 1)
        return -1;

    /* Fast paths when storage types are identical. Uses memmem function, which
     * uses Knuth-Morris-Pratt algorithm on Linux and on others
     * Crochemore+Perrin two-way string matching */
    switch (Haystack->body.storage_type) {
        case MVM_STRING_GRAPHEME_32:
            if (needle->body.storage_type == MVM_STRING_GRAPHEME_32 || needle->body.num_graphs < 100) {
                return MVM_string_memmem_grapheme32str(tc, Haystack, needle, start, H_graphs, n_graphs);
            }
            break;
        case MVM_STRING_GRAPHEME_8:
            if (needle->body.storage_type == MVM_STRING_GRAPHEME_8 || needle->body.num_graphs < 100) {
                void         *mm_return_8 = NULL;
                MVMGrapheme8 *needle_buf  = NULL;
                if (needle->body.storage_type != MVM_STRING_GRAPHEME_8) {
                    MVMStringIndex i;
                    MVMGraphemeIter n_gi;
                    needle_buf = MVM_malloc(needle->body.num_graphs * sizeof(MVMGrapheme8));
                    if (needle->body.storage_type != MVM_STRING_GRAPHEME_32) MVM_string_gi_init(tc, &n_gi, needle);
                    for (i = 0; i < needle->body.num_graphs; i++) {
                        MVMGrapheme32 g = needle->body.storage_type == MVM_STRING_GRAPHEME_32
                            ? needle->body.storage.blob_32[i]
                            : MVM_string_gi_get_grapheme(tc, &n_gi);
                        /* Haystack is 8 bit, needle is 32 bit. if we encounter a non8bit grapheme
                         * it's impossible to match */
                        if (!can_fit_into_8bit(g)) {
                            MVM_free(needle_buf);
                            return -1;
                        }
                        needle_buf[i] = g;
                    }
                }
                mm_return_8 = MVM_memmem(
                    Haystack->body.storage.blob_8 + start, /* start position */
                    (H_graphs - start) * sizeof(MVMGrapheme8), /* length of Haystack from start position to end */
                    needle_buf ? needle_buf : needle->body.storage.blob_8, /* needle start */
                    n_graphs * sizeof(MVMGrapheme8) /* needle length */
                );
                if (needle_buf) MVM_free(needle_buf);
                if (mm_return_8 == NULL)
                    return -1;
                else
                    return (MVMGrapheme8*)mm_return_8 -  Haystack->body.storage.blob_8;
            }
            break;
    }
    /* Minimal code version for needles of size 1 */
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
    else if (n_graphs <= MVM_string_KMP_max_pattern_length)
        return knuth_morris_pratt_string_index(tc, needle, Haystack, start);
    else {
        int is_gic = Haystack->body.storage_type == MVM_STRING_STRAND ? 1 : 0;
        void *Hs_or_gic = Haystack;
        /* If Haystack is a strand allocate space for a MVMGraphemeIter_cached
         * and initialize it */
        if (is_gic) {
            Hs_or_gic = alloca(sizeof(MVMGraphemeIter_cached));
            MVM_string_gi_cached_init(tc, Hs_or_gic, Haystack, start);
        }
        /* For needles > MVM_string_KMP_max_pattern_length we must revert to brute force for now.
         * Eventually we can implement brute force after it matches the whole needle OR
         * allocate more space for the pattern on reaching the end of the pattern */
        while (index <= H_graphs - n_graphs) {
            if (string_equal_at_ignore_case_INTERNAL_loop(tc, Hs_or_gic, needle, index, H_graphs, n_graphs, 0, 0, is_gic) != -1)
                return (MVMint64)index;
            index++;
        }
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
        if (0 <= start)
            return start <= H_graphs ? start : -1; /* the empty string is in any other string */
        else
            return H_graphs; /* no start, so return end */
    }

    if (!H_graphs)
        return -1;

    if (H_graphs < n_graphs || n_graphs < 1)
        return -1;

    if (start == -1)
        start = H_graphs - n_graphs;

    if (start < 0 || H_graphs <= start)
        /* maybe return -1 instead? */
        MVM_exception_throw_adhoc(tc, "index start offset (%"PRId64") out of range (0..%"PRIu32")", start, H_graphs);

    index = start;

    if (H_graphs < index + n_graphs) {
        index = H_graphs - n_graphs;
    }

    /* brute force for now. horrible, yes. halp. */
    do {
        if (MVM_string_substrings_equal_nocheck(tc, needle, 0, n_graphs, Haystack, index)) {
            result = (MVMint64)index;
            break;
        }
    } while (0 < index--);
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
    if (agraphs < start_pos) {
        start_pos = 0;
        end_pos   = 0;
    }

    if (end_pos < 0)
        MVM_exception_throw_adhoc(tc, "Substring end (%"PRId64") cannot be less than 0", end_pos);

    /* Ensure we're within bounds. */
    if (start_pos < 0)
        start_pos = 0;
    if (agraphs < end_pos)
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
            MVM_gc_write_barrier(tc, (MVMCollectable *)result, (MVMCollectable *)a);
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
            MVM_gc_write_barrier(tc, (MVMCollectable *)result, (MVMCollectable *)orig_strand->blob_string);
            result->body.storage.strands[0].start       = orig_strand->start + start_pos;
            result->body.storage.strands[0].end         = orig_strand->start + end_pos;
            result->body.storage.strands[0].repetitions = 0;
        }
        else {
            /* Produce a new blob string, collapsing the strands. */
            MVMGraphemeIter gi;
            MVM_string_gi_init(tc, &gi, a);
            MVM_string_gi_move_to(tc, &gi, start_pos);
            iterate_gi_into_string(tc, &gi, result, a, start_pos);
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

static MVMString * string_from_strand_at_index(MVMThreadContext *tc, MVMString *a, MVMuint16 index) {
    MVMStringStrand *ss = &(a->body.storage.strands[index]);
    return MVM_string_substring(tc, ss->blob_string, ss->start, ss->end - ss->start);
}

static MVMuint32 final_strand_match_with_repetition_count(MVMThreadContext *tc, MVMString *a, MVMString *b) {
    if (a->body.storage_type == MVM_STRING_STRAND) {
        MVMStringStrand *sa = &(a->body.storage.strands[a->body.num_strands - 1]);
        /* If the final strand of a eq b, we'll just increment the final strand of a's repetitions. */
        if (sa->end - sa->start == MVM_string_graphs_nocheck(tc, b)) {
            if (MVM_string_equal_at(tc, sa->blob_string, b, sa->start))
                return 1;
        }
        /* If the final strand of a eq the first (and only) strand of b, we'll just add b's repetitions
	 * (plus 1 for the strand itself) to the final strand of a's repetitions. */
        else if (b->body.storage_type == MVM_STRING_STRAND && b->body.num_strands == 1) {
            MVMStringStrand *sb = &(b->body.storage.strands[0]);
            if (sa->end - sa->start == sb->end - sb->start) {
                MVMString *a_strand, *b_strand;
                MVMROOT(tc, b, {
                    a_strand = string_from_strand_at_index(tc, a, a->body.num_strands - 1);
                });
                MVMROOT(tc, a_strand, {
                    b_strand = string_from_strand_at_index(tc, b, 0);
                });
                if (MVM_string_equal(tc, a_strand, b_strand))
                    return b->body.storage.strands[0].repetitions + 1;
            }
	}
    }
    return 0;
}

/* Append one string to another. */
MVMString * MVM_string_concatenate(MVMThreadContext *tc, MVMString *a, MVMString *b) {
    MVMString *result = NULL, *renormalized_section = NULL;
    MVMuint32 renormalized_section_graphs = 0, consumed_a = 0, consumed_b = 0;
    MVMuint32  agraphs, bgraphs;
    MVMuint64  total_graphs;
    int lost_strands          = 0;
    int is_concat_stable      = 0;
    int index_ss_b;
    MVMuint32 matching_repetition_count;
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
        MVMROOT2(tc, a, b, {
        /* If both are not synthetics, we can can pass those values unchanged
         * instead of iterating by codepoint */
        if (0 <= last_a_first_b[0] && 0 <= last_a_first_b[1]) {
            renormalized_section = MVM_unicode_codepoints_c_array_to_nfg_string(tc, last_a_first_b, 2);
            consumed_a = 1; consumed_b = 1;
        }
        else {
            MVMCodepointIter last_a_ci;
            MVMCodepointIter first_b_ci;
            MVMuint32 a_codes = MVM_string_grapheme_ci_init(tc, &last_a_ci,  last_a_first_b[0], 1);
            MVMuint32 b_codes = MVM_string_grapheme_ci_init(tc, &first_b_ci, last_a_first_b[1], 1);
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
    if (MAX_GRAPHEMES < total_graphs)
        MVM_exception_throw_adhoc(tc,
            "Can't concatenate strings, required number of graphemes %"PRIu64" > max allowed of %lld",
             total_graphs, MAX_GRAPHEMES);

    /* Otherwise, we'll assemble a result string. */
    MVMROOT4(tc, a, b, renormalized_section, result, {

        /* Allocate it. */
        result = (MVMString *)MVM_repr_alloc_init(tc, tc->instance->VMString);

        /* Total graphemes is trivial; just total up inputs. */
        result->body.num_graphs = (MVMuint32)total_graphs;

        /* Result string will be made of strands. */
        result->body.storage_type = MVM_STRING_STRAND;

        /* Detect the wonderful case where we're repeatedly concating the same
         * string again and again, and thus can just bump a repetition. */
        if (is_concat_stable == 1 && (matching_repetition_count = final_strand_match_with_repetition_count(tc, a, b))) {
            /* We have it; just copy the strands to a new string and bump the
             * repetitions count of the last one. */
            result->body.storage.strands = allocate_strands(tc, a->body.num_strands);
            copy_strands(tc, a, 0, result, 0, a->body.num_strands);
            result->body.storage.strands[a->body.num_strands - 1].repetitions += matching_repetition_count;
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
            if (MVM_STRING_MAX_STRANDS < strands_a + strands_b) {
                MVMROOT(tc, result, {
                    if (strands_b <= strands_a) {
                        MVMROOT(tc, effective_b, {
                            effective_a = collapse_strands(tc, effective_a);
                        });
                        strands_a   = 1;
                    }
                    else {
                        MVMROOT(tc, effective_a, {
                            effective_b = collapse_strands(tc, effective_b);
                        });
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
                MVM_gc_write_barrier(tc, (MVMCollectable *)result, (MVMCollectable *)effective_a);
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
                MVM_gc_write_barrier(tc, (MVMCollectable *)result, (MVMCollectable *)renormalized_section);
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
                MVM_gc_write_barrier(tc, (MVMCollectable *)result, (MVMCollectable *)effective_b);
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
    if (is_concat_stable == 1 || (is_concat_stable == 0 && renormalized_section)) {
        NFG_CHECK_CONCAT(tc, result, a, b, "'result'");
    }
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
    if (MAX_GRAPHEMES < count)
        MVM_exception_throw_adhoc(tc, "Repeat count (%"PRId64") cannot be greater than max allowed number of graphemes %lld", count, MAX_GRAPHEMES);

    /* If input string is empty, repeating it is empty. */
    agraphs = MVM_string_graphs_nocheck(tc, a);
    if (agraphs == 0)
        return tc->instance->str_consts.empty;

    /* Total size of the resulting string can't be bigger than an MVMString is allowed to be. */
    total_graphs = (MVMuint64)agraphs * (MVMuint64)count;
    if (MAX_GRAPHEMES < total_graphs)
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
                MVM_gc_write_barrier(tc, (MVMCollectable *)result, (MVMCollectable *)a);
                result->body.storage.strands[0].start       = 0;
                result->body.storage.strands[0].end         = agraphs;
            }
        }
        else {
            result->body.storage.strands[0].blob_string = a;
            MVM_gc_write_barrier(tc, (MVMCollectable *)result, (MVMCollectable *)a);
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
    if (g < 0) {
        MVMNFGSynthetic *synth = MVM_nfg_get_synthetic_info(tc, g);
        return ord_getbasechar(tc, synth->codes[synth->base_index]);
    }
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
MVM_STATIC_INLINE MVMint64 string_equal_at_ignore_case_INTERNAL_loop(MVMThreadContext *tc, void *Hs_or_gic, MVMString *needle_fc, MVMint64 H_start, MVMint64 H_graphs, MVMint64 n_fc_graphs, int ignoremark, int ignorecase, int is_gic) {
    MVMuint32 H_fc_cps;
    /* An additional needle offset which is used only when codepoints expand
     * when casefolded. The offset is the number of additional codepoints that
     * have been seen so Haystack and needle stay aligned */
    MVMint64 n_offset = 0;
    MVMint64 i, j;
    MVMGrapheme32 H_g, n_g;
    for (i = 0; i + H_start < H_graphs && i + n_offset < n_fc_graphs; i++) {
        const MVMCodepoint* H_result_cps;
        H_g = is_gic ? MVM_string_gi_cached_get_grapheme(tc, Hs_or_gic, H_start + i) : MVM_string_get_grapheme_at_nocheck(tc, Hs_or_gic, H_start + i);
        if (!ignorecase) {
            H_fc_cps = 0;
        }
        else if (0 <= H_g) {
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
        else if (1 <= H_fc_cps) {
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
    MVMStringIndex n_fc_graphs;
    /* H_expansion must be able to hold integers 3x larger than MVMStringIndex */
    MVMint64 H_expansion;

    if (H_offset < 0) {
        H_offset += H_graphs;
        if (H_offset < 0)
            H_offset = 0; /* XXX I think this is the right behavior here */
    }
    /* If the offset is greater to the number of Haystack graphemes return 0.
     * Since size of graphemes could change under casefolding, we
     * can't assume too much. If optimizing this be careful */
    if (H_graphs < H_offset)
        return 0;
    MVMROOT(tc, Haystack, {
        needle_fc = ignorecase ? MVM_string_fc(tc, needle) : needle;
    });
    n_fc_graphs = MVM_string_graphs(tc, needle_fc);
    if (Haystack->body.storage_type == MVM_STRING_STRAND) {
        MVMGraphemeIter_cached H_gic;
        MVM_string_gi_cached_init(tc, &H_gic, Haystack, H_offset);
        H_expansion = string_equal_at_ignore_case_INTERNAL_loop(tc, &H_gic, needle_fc, H_offset, H_graphs, n_fc_graphs, ignoremark, ignorecase, 1);
    }
    else {
        H_expansion = string_equal_at_ignore_case_INTERNAL_loop(tc, Haystack, needle_fc, H_offset, H_graphs, n_fc_graphs, ignoremark, ignorecase, 0);
    }
    if (0 <= H_expansion)
        return n_fc_graphs <= H_graphs + H_expansion - H_offset ? 1 : 0;
    return 0;
}
/* Processes the pattern. The pattern must be able to store negative and positive
 * numbers. It must be able to store at least 1/2 the length of the needle,
 * though possibly more (though I am not sure it's possible for it to be more than
 * 1/2). */
static void knuth_morris_pratt_process_pattern (MVMThreadContext *tc, MVMString *pat, MVMint16 *next, MVMStringIndex pat_graphs) {
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
    MVMStringIndex Haystack_graphs = MVM_string_graphs_nocheck(tc, Haystack);
    MVMStringIndex needle_graphs   = MVM_string_graphs_nocheck(tc, needle);
    MVMint16         *next = NULL;
    MVMString *flat_needle = NULL;
    size_t next_size = (1 + needle_graphs) * sizeof(MVMint16);
    int    next_is_malloced = 0;
    assert(needle_graphs <= MVM_string_KMP_max_pattern_length);
    /* Empty string is found at start of string */
    if (needle_graphs == 0)
        return 0;
    /* Allocate max 8K onto the stack, otherwise malloc */
    if (next_size < 3000)
        next = alloca(next_size);
    else {
        next = MVM_malloc(next_size);
        next_is_malloced = 1;
    }
    /* If the needle is a strand, flatten it, otherwise use the original string */
    if (needle->body.storage_type == MVM_STRING_STRAND) {
        MVMROOT(tc, Haystack, {
            flat_needle = collapse_strands(tc, needle);
        });
    }
    else {
        flat_needle = needle;
    }
    /* Process the needle into a jump table put into variable 'next' */
    knuth_morris_pratt_process_pattern(tc, flat_needle, next, needle_graphs);
    /* If the Haystack is a strand, use MVM_string_gi_cached_get_grapheme
     * since it retains its grapheme iterator over invocations unlike
     * MVM_string_get_grapheme_at_nocheck and caches the previous grapheme. It
     * is slower for flat Haystacks though. */
    #define MVM_kmp_loop(Haystack_function) {\
        while (text_offset < Haystack_graphs && needle_offset < needle_graphs) {\
            if (needle_offset == -1 || MVM_string_get_grapheme_at_nocheck(tc, flat_needle, needle_offset)\
                                    == (Haystack_function)) {\
                text_offset++; needle_offset++;\
                if (needle_offset == needle_graphs) {\
                    if (next_is_malloced) MVM_free(next);\
                    return text_offset - needle_offset;\
                }\
            }\
            else needle_offset = next[needle_offset];\
        }\
    }
    if (Haystack->body.storage_type == MVM_STRING_STRAND) {
        MVMGraphemeIter_cached H_gic;
        MVM_string_gi_cached_init(tc, &H_gic, Haystack, H_offset);
        MVM_kmp_loop(MVM_string_gi_cached_get_grapheme(tc, &H_gic, text_offset));
    }
    else {
        MVM_kmp_loop(MVM_string_get_grapheme_at_nocheck(tc, Haystack, text_offset));
    }
    if (next_is_malloced) MVM_free(next);
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
    int is_gic = Haystack->body.storage_type == MVM_STRING_STRAND ? 1 : 0;
    void *Hs_or_gic;
    MVM_string_check_arg(tc, Haystack, ignoremark ? "index ignore case ignore mark search target" : "index ignore case search target");
    MVM_string_check_arg(tc, needle,   ignoremark ? "index ignore case ignore mark search term"   : "index ignore case search term");
    H_graphs = MVM_string_graphs_nocheck(tc, Haystack);
    n_graphs = MVM_string_graphs_nocheck(tc, needle);
    if (!n_graphs)
        return start <= H_graphs ? start : -1; /* Empty string is in any other string */
    if (!H_graphs)
        return -1;
    if (start < 0 || H_graphs <= start)
        return -1;
    /* Codepoints can expand into up to THREE codepoints (as of Unicode 9.0). The next check
     * checks if it is at all possible for the needle grapheme number to be higher
     * than the Haystack */
    if (H_graphs * 3 < n_graphs)
        return -1;

    if (n_graphs < 1)
        return -1;

    MVMROOT(tc, Haystack, {
        needle_fc = ignorecase ? MVM_string_fc(tc, needle) : needle;
    });
    n_fc_graphs = MVM_string_graphs(tc, needle_fc);
    /* brute force for now. horrible, yes. halp. */
    if (is_gic) {
        Hs_or_gic = alloca(sizeof(MVMGraphemeIter_cached));
        MVM_string_gi_cached_init(tc, Hs_or_gic, Haystack, start);
    }
    else {
        Hs_or_gic = Haystack;
    }
    while (index <= H_graphs) {
        H_expansion = string_equal_at_ignore_case_INTERNAL_loop(tc, Hs_or_gic, needle_fc, index, H_graphs, n_fc_graphs, ignoremark, ignorecase, is_gic);
        if (0 <= H_expansion)
            return n_fc_graphs <= H_graphs + H_expansion - index ? (MVMint64)index : -1;
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
    if (offset < 0 || agraphs <= offset)
        return -1;

    g = MVM_string_get_grapheme_at_nocheck(tc, s, offset);

    return 0 <= g ? g : MVM_nfg_get_synthetic_info(tc, g)->codes[0];
}

/* Gets the base character at a grapheme position, ignoring things like diacritics */
MVMGrapheme32 MVM_string_ord_basechar_at(MVMThreadContext *tc, MVMString *s, MVMint64 offset) {
    MVMStringIndex agraphs;

    MVM_string_check_arg(tc, s, "ord_basechar_at");

    agraphs = MVM_string_graphs_nocheck(tc, s);
    if (offset < 0 || agraphs <= offset)
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
    /* If we have cached hash codes from both a and b we can compare if they are identical.
     * If they don't match then we already know the two strings are not equal. */
    if (a->body.cached_hash_code && b->body.cached_hash_code && a->body.cached_hash_code != b->body.cached_hash_code)
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
    if (MVM_string_graphs_nocheck(tc, a) < starta + length || MVM_string_graphs_nocheck(tc, b) < startb + length)
        return 0;

    return MVM_string_substrings_equal_nocheck(tc, a, starta, length, b, startb);
}

/* Returns the grapheme at a given index of the string */
MVMint64 MVM_string_get_grapheme_at(MVMThreadContext *tc, MVMString *a, MVMint64 index) {
    MVMStringIndex agraphs;

    MVM_string_check_arg(tc, a, "grapheme_at");

    agraphs = MVM_string_graphs_nocheck(tc, a);

    if (index < 0 || agraphs <= index)
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
MVMint64 MVM_string_grapheme_is_cclass(MVMThreadContext *tc, MVMint64 cclass, MVMGrapheme32 g);
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
                        else if (!MVM_string_grapheme_is_cclass(tc, MVM_CCLASS_ALPHABETIC, result_buf[i - 1])) {
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
                            if (MVM_string_grapheme_is_cclass(tc, MVM_CCLASS_ALPHABETIC, g))
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
            else if (0 <= g) {
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
                    if (1 < num_result_graphs) {
                        result_graphs += num_result_graphs - 1;
                        result_buf = MVM_realloc(result_buf,
                            result_graphs * sizeof(MVMGrapheme32));
                    }

                    /* Copy resulting graphemes. */
                    while (0 < num_result_graphs) {
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
                MVMuint32 num_transformed = MVM_nfg_get_case_change(tc, g, type, &transformed);
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
                    MVM_VECTORIZE_LOOP
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
char * MVM_string_encoding_cname(MVMThreadContext *tc, MVMint64 code);
/* "Strict"ly (if possible) decodes a C buffer to an MVMString, dependent on the
 * encoding type flag. Unlike MVM_string_decode, it will not pass through
 * codepoints which have no official mapping. `config` can be set to 1 to indicate
 * that you want to decode non-strict ("permissive"), which will try and decode
 * as long as it's possible (For example codepoint 129 in windows-1252 is invalid,
 * but is technically possible to use Unicode codepoint 129 instead (though it's
 * most likely this means the input is actually *not* windows-1252).
 * For now windows-1252 and windows-1251 are the only ones this makes a difference
 * on. And it is mostly irrelevant for utf8/utf8-c8 encodings since they can
 * already represent all codepoints below 0x10FFFF */
MVMString * MVM_string_decode_config(MVMThreadContext *tc,
        const MVMObject *type_object, char *Cbuf, MVMint64 byte_length,
        MVMint64 encoding_flag, MVMString *replacement, MVMint64 config) {
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
            return MVM_string_windows1252_decode_config(tc, type_object, Cbuf, byte_length, replacement, config);
        case MVM_encoding_type_windows1251:
            return MVM_string_windows1251_decode_config(tc, type_object, Cbuf, byte_length, replacement, config);
        case MVM_encoding_type_shiftjis:
            return MVM_string_shiftjis_decode(tc, type_object, Cbuf, byte_length, replacement, config);
        case MVM_encoding_type_utf8_c8:
            return MVM_string_utf8_c8_decode(tc, type_object, Cbuf, byte_length);
        case MVM_encoding_type_utf16le:
            return MVM_string_utf16le_decode(tc, type_object, Cbuf, byte_length);
        case MVM_encoding_type_utf16be:
            return MVM_string_utf16be_decode(tc, type_object, Cbuf, byte_length);
        case MVM_encoding_type_gb2312:
            return MVM_string_gb2312_decode(tc, type_object, Cbuf, byte_length);
        case MVM_encoding_type_gb18030:
            return MVM_string_gb18030_decode(tc, type_object, Cbuf, byte_length);
        default:
            if (encoding_flag < MVM_encoding_type_MIN || MVM_encoding_type_MAX < encoding_flag)
                MVM_exception_throw_adhoc(tc, "invalid encoding type flag: %"PRId64, encoding_flag);
            else
                MVM_exception_throw_adhoc(tc, "unable to handle %s encoding in MVM_string_decode_config", MVM_string_encoding_cname(tc, encoding_flag));
    }
}
/* Strictly decodes a C buffer to an MVMString, dependent on the encoding type flag.
 * See the comments above MVM_string_decode_config() above for more details. */
MVMString * MVM_string_decode(MVMThreadContext *tc,
        const MVMObject *type_object, char *Cbuf, MVMint64 byte_length, MVMint64 encoding_flag) {
    return MVM_string_decode_config(tc, type_object, Cbuf, byte_length, encoding_flag, NULL, MVM_ENCODING_PERMISSIVE);
}

/* Strictly encodes an MVMString to a C buffer, dependent on the encoding type flag.
 * See comments for MVM_string_decode_config() above for more details. */
char * MVM_string_encode_config(MVMThreadContext *tc, MVMString *s, MVMint64 start,
        MVMint64 length, MVMuint64 *output_size, MVMint64 encoding_flag,
        MVMString *replacement, MVMint32 translate_newlines, MVMuint8 config) {
    switch(encoding_flag) {
        case MVM_encoding_type_utf8:
            return MVM_string_utf8_encode_substr(tc, s, output_size, start, length, replacement, translate_newlines);
        case MVM_encoding_type_ascii:
            return MVM_string_ascii_encode_substr(tc, s, output_size, start, length, replacement, translate_newlines);
        case MVM_encoding_type_latin1:
            return MVM_string_latin1_encode_substr(tc, s, output_size, start, length, replacement, translate_newlines);
        case MVM_encoding_type_utf16:
            return MVM_string_utf16_encode_substr(tc, s, output_size, start, length, replacement, translate_newlines);
        case MVM_encoding_type_utf16le:
            return MVM_string_utf16le_encode_substr(tc, s, output_size, start, length, replacement, translate_newlines);
        case MVM_encoding_type_utf16be:
            return MVM_string_utf16be_encode_substr(tc, s, output_size, start, length, replacement, translate_newlines);
        case MVM_encoding_type_windows1252:
            return MVM_string_windows1252_encode_substr_config(tc, s, output_size, start, length, replacement, translate_newlines, config);
        case MVM_encoding_type_windows1251:
            return MVM_string_windows1251_encode_substr_config(tc, s, output_size, start, length, replacement, translate_newlines, config);
        case MVM_encoding_type_shiftjis:
            return MVM_string_shiftjis_encode_substr(tc, s, output_size, start, length, replacement, translate_newlines, config);
        case MVM_encoding_type_utf8_c8:
            return MVM_string_utf8_c8_encode_substr(tc, s, output_size, start, length, replacement);
        case MVM_encoding_type_gb2312:
            return MVM_string_gb2312_encode_substr(tc, s, output_size, start, length, replacement, translate_newlines);
        case MVM_encoding_type_gb18030:
            return MVM_string_gb18030_encode_substr(tc, s, output_size, start, length, replacement, translate_newlines);
        default:
            if (encoding_flag < MVM_encoding_type_MIN || MVM_encoding_type_MAX < encoding_flag)
                MVM_exception_throw_adhoc(tc, "invalid encoding type flag: %"PRId64, encoding_flag);
            else
                MVM_exception_throw_adhoc(tc, "unable to handle %s encoding in MVM_string_encode_config", MVM_string_encoding_cname(tc, encoding_flag));
    }
}
char * MVM_string_encode(MVMThreadContext *tc, MVMString *s, MVMint64 start,
        MVMint64 length, MVMuint64 *output_size, MVMint64 encoding_flag,
        MVMString *replacement, MVMint32 translate_newlines) {
    return MVM_string_encode_config(tc, s, start, length, output_size, encoding_flag, replacement, translate_newlines, MVM_ENCODING_PERMISSIVE);
}

/* Strictly encodes a string, and writes the encoding string into the supplied Buf
 * instance, which should be an integer array with MVMArray REPR. */
MVMObject * MVM_string_encode_to_buf_config(MVMThreadContext *tc, MVMString *s, MVMString *enc_name,
        MVMObject *buf, MVMString *replacement, MVMint64 config) {
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

    /* At least find_encoding may allocate on first call, so root just
     * in case. */
    MVMROOT2(tc, buf, s, {
        const MVMuint8 encoding_flag = MVM_string_find_encoding(tc, enc_name);
        encoded = (MVMuint8 *)MVM_string_encode_config(tc, s, 0, MVM_string_graphs_nocheck(tc, s), &output_size,
            encoding_flag, replacement, 0, config);
    });

    /* Stash the encoded data in the VMArray. */
    if (((MVMArray *)buf)->body.slots.any) {
        MVMuint32 prev_elems = ((MVMArray *)buf)->body.elems;
        MVM_repr_pos_set_elems(tc, buf, ((MVMArray *)buf)->body.elems + output_size / elem_size);
        memmove(((MVMArray *)buf)->body.slots.i8 + prev_elems, (MVMint8 *)encoded, output_size);
        MVM_free(encoded);
    }
    else {
        ((MVMArray *)buf)->body.slots.i8 = (MVMint8 *)encoded;
        ((MVMArray *)buf)->body.start    = 0;
        ((MVMArray *)buf)->body.ssize    = output_size / elem_size;
        ((MVMArray *)buf)->body.elems    = output_size / elem_size;
    }

    return buf;
}
MVMObject * MVM_string_encode_to_buf(MVMThreadContext *tc, MVMString *s, MVMString *enc_name,
        MVMObject *buf, MVMString *replacement) {
    return MVM_string_encode_to_buf_config(tc, s, enc_name, buf, replacement, MVM_ENCODING_PERMISSIVE);
}
/* Decodes a string using the data from the specified Buf. Decodes "strict" by
 * default, but optionally can be "permissive". */
MVMString * MVM_string_decode_from_buf_config(MVMThreadContext *tc, MVMObject *buf,
        MVMString *enc_name, MVMString *replacement, MVMint64 config) {
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
    return MVM_string_decode_config(tc, tc->instance->VMString,
        (char *)(((MVMArray *)buf)->body.slots.i8 + ((MVMArray *)buf)->body.start),
        ((MVMArray *)buf)->body.elems * elem_size,
        encoding_flag, replacement, config);
}
MVMString * MVM_string_decode_from_buf(MVMThreadContext *tc, MVMObject *buf, MVMString *enc_name) {
    return MVM_string_decode_from_buf_config(tc, buf, enc_name, NULL, MVM_ENCODING_PERMISSIVE);
}

MVMObject * MVM_string_split(MVMThreadContext *tc, MVMString *separator, MVMString *input) {
    MVMObject *result = NULL;
    MVMStringIndex start, end, sep_length;
    MVMHLLConfig *hll = MVM_hll_current(tc);

    MVM_string_check_arg(tc, separator, "split separator");
    MVM_string_check_arg(tc, input, "split input");

    MVMROOT3(tc, input, separator, result, {
        result = MVM_repr_alloc_init(tc, hll->slurpy_array_type);
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
            length = sep_length ? (index == (MVMStringIndex)-1 ? end : index) - start : 1;
            if (0 < length || (sep_length && length == 0)) {
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

    return result;
}
/* Used in the MVM_string_join function. Moved here to simplify the code */
void copy_to_32bit (MVMThreadContext *tc, MVMString *source,
    MVMString *dest, MVMint64 *position, MVMGraphemeIter *gi) {
    /* Add source. */
    switch (source->body.storage_type) {
        case MVM_STRING_GRAPHEME_32: {
            memcpy(
                dest->body.storage.blob_32 + *position,
                source->body.storage.blob_32,
                source->body.num_graphs * sizeof(MVMGrapheme32));
            *position += source->body.num_graphs;
            break;
        }
        case MVM_STRING_GRAPHEME_ASCII:
        case MVM_STRING_GRAPHEME_8: {
            MVMStringIndex sindex = 0;
            while (sindex < source->body.num_graphs)
                dest->body.storage.blob_32[(*position)++] =
                    source->body.storage.blob_8[sindex++];
            break;
        }
        default:
            MVM_string_gi_init(tc, gi, source);
            while (MVM_string_gi_has_more(tc, gi))
                dest->body.storage.blob_32[(*position)++] =
                    MVM_string_gi_get_grapheme(tc, gi);
            break;
    }
}
/* For join, we don't do anything fancy such as renormalizing only two codepoints to be
 * combined in a concatenation: here we only care about two states, whether we need to
 * run re_nfg afterward or not. Essentially we treat `2` (force renormalization) the
 * same as 0 (codepoints should not break between them). */
MVM_STATIC_INLINE int nfg_is_join_stable(MVMThreadContext *tc, MVMString *a, MVMString *b) {
    int temp = MVM_nfg_is_concat_stable(tc, a, b);
    return temp == 2 ? 0 : temp;
}
/* Used in MVM_string_join to check stability of adding the next piece */
MVM_STATIC_INLINE void join_check_stability(MVMThreadContext *tc, MVMString *piece,
    MVMString *separator, MVMString **pieces, MVMint32 *concats_stable, MVMint64 num_pieces, MVMint64 sgraphs, MVMint64 piece_index) {
    if (!sgraphs) {
        /* If there's no separator and one piece is The Empty String we
         * have to be extra careful about concat stability */
        if (!MVM_string_graphs_nocheck(tc, piece)
                && piece_index + 1 < num_pieces
                && !nfg_is_join_stable(tc, pieces[piece_index - 1], pieces[piece_index + 1])) {
            *concats_stable = 0;
        }
        /* Separator has no graphemes, so NFG stability check
         * should consider pieces. */
        else if (!nfg_is_join_stable(tc, pieces[piece_index - 1], piece))
            *concats_stable = 0;
    }
    /* If we have a separator, check concat stability */
    else {
        if (!nfg_is_join_stable(tc, pieces[piece_index - 1], separator) /* Before */
         || !nfg_is_join_stable(tc, separator, piece)) { /* And after separator */
            *concats_stable = 0;
        }
    }
}
MVM_STATIC_INLINE MVMString * join_get_str_from_pos(MVMThreadContext *tc, MVMObject *array, MVMint64 index, MVMint64 is_str_array) {
    if (is_str_array) {
        MVMString *piece = MVM_repr_at_pos_s(tc, array, index);
        if (piece)
            return piece;
    }
    else {
        MVMObject *item = MVM_repr_at_pos_o(tc, array, index);
        if (item && IS_CONCRETE(item))
            return MVM_repr_get_str(tc, item);
    }
    return (MVMString*)NULL;
}
MVMString * MVM_string_ascii_from_buf_nocheck(MVMThreadContext *tc, MVMGrapheme8 *buf, MVMStringIndex len) {
    MVMString *result = (MVMString *)MVM_repr_alloc_init(tc, tc->instance->VMString);
    result->body.num_graphs     = len;
    result->body.storage_type   = MVM_STRING_GRAPHEME_ASCII;
    result->body.storage.blob_8 = buf;
    return result;
}
MVMString * MVM_string_join(MVMThreadContext *tc, MVMString *separator, MVMObject *input) {
    MVMString  *result = NULL;
    MVMString **pieces = NULL;
    MVMint64    elems, num_pieces, sgraphs, i, is_str_array, total_graphs;
    MVMuint16   sstrands, total_strands;
    MVMint32    concats_stable = 1, all_strands;
    size_t      bytes;

    MVM_string_check_arg(tc, separator, "join separator");
    if (!IS_CONCRETE(input))
        MVM_exception_throw_adhoc(tc, "join needs a concrete array to join");

    /* See how many things we have to join; if the answer is "none" then we
     * can make a hasty escape. */
    elems = MVM_repr_elems(tc, input);
    if (elems == 0)
        return tc->instance->str_consts.empty;
    bytes = elems * sizeof(MVMString *);
    is_str_array = REPR(input)->pos_funcs.get_elem_storage_spec(tc,
        STABLE(input)).boxed_primitive == MVM_STORAGE_SPEC_BP_STR;

    /* If there's only one element to join, just return it. */
    if (elems == 1) {
        MVMString *piece = join_get_str_from_pos(tc, input, 0, is_str_array);
        if (piece)
            return piece;
    }

    /* Allocate result. */
    MVMROOT2(tc, separator, input, {
        result = (MVMString *)MVM_repr_alloc_init(tc, tc->instance->VMString);
    });

    /* Take a first pass through the string, counting up length and the total
     * number of strands we encounter as well as building a flat array of the
     * strings (so we only have to do the indirect calls once). */
    sgraphs  = MVM_string_graphs_nocheck(tc, separator);
    if (sgraphs)
        sstrands = separator->body.storage_type == MVM_STRING_STRAND
            ? separator->body.num_strands
            : 1;
    else
        sstrands = 1;
    pieces        = MVM_fixed_size_alloc(tc, tc->instance->fsa, bytes);
    num_pieces    = 0;
    total_graphs  = 0;
    total_strands = 0;
    /* Is the separator a strand? */
    all_strands = separator->body.storage_type == MVM_STRING_STRAND;
    for (i = 0; i < elems; i++) {
        /* Get piece of the string. */
        MVMString *piece = join_get_str_from_pos(tc, input, i, is_str_array);
        MVMint64   piece_graphs;
        if (!piece)
            continue;

        /* Check that all the pieces are strands. */
        if (all_strands)
            all_strands = piece->body.storage_type == MVM_STRING_STRAND;

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
    /* This guards the joining by method of multiple concats, and will be faster
     * if we only end up with one piece after going through each element of the array */
    if (num_pieces == 1)
        return pieces[0];
    /* We now know the total eventual number of graphemes. */
    if (total_graphs == 0) {
        MVM_fixed_size_free(tc, tc->instance->fsa, bytes, pieces);
        return tc->instance->str_consts.empty;
    }
    result->body.num_graphs = total_graphs;

    MVMROOT2(tc, result, separator, {
    /* If the separator and pieces are all strands, and there are
     * on average at least 16 graphemes in each of the strands. */
    if (all_strands && total_strands <  MVM_STRING_MAX_STRANDS
              &&  total_strands * 16 <= total_graphs) {
        MVMuint16 offset = 0;
        result->body.storage_type    = MVM_STRING_STRAND;
        result->body.storage.strands = allocate_strands(tc, total_strands);
        result->body.num_strands     = total_strands;
        for (i = 0; i < num_pieces; i++) {
            MVMString *piece = pieces[i];
            if (0 < i) {
                /* No more checks unless still stable */
                if (concats_stable)
                    join_check_stability(tc, piece, separator, pieces,
                        &concats_stable, num_pieces, sgraphs, i);
                copy_strands(tc, separator, 0, result, offset, separator->body.num_strands);
                offset += separator->body.num_strands;
            }
            copy_strands(tc, piece, 0, result, offset, piece->body.num_strands);
            offset += piece->body.num_strands;
        }
    }
    /* Doing multiple concats is only faster if we have about 300 graphemes per
       piece or if we have less than for pieces and more than 150 graphemes per piece */
    else if (total_strands <  MVM_STRING_MAX_STRANDS && (300 < num_pieces/total_graphs || (num_pieces < 4 && 150 < num_pieces/total_graphs))) {
        MVMString *result = NULL;
        MVMROOT(tc, result, {
            if (sgraphs) {
                i = 0;
                result = MVM_string_concatenate(tc, pieces[i++], separator);
                result = MVM_string_concatenate(tc, result, pieces[i++]);
                for (; i < num_pieces;) {
                    result = MVM_string_concatenate(tc, result, separator);
                    result = MVM_string_concatenate(tc, result, pieces[i++]);
                }

            }
            else {
                result = MVM_string_concatenate(tc, pieces[0], pieces[1]);
                i = 2;
                for (; i < num_pieces;) {
                    result = MVM_string_concatenate(tc, result, pieces[i++]);
                }
            }
        });
        return result;
    }
    else {
        /* We'll produce a single, flat string. */
        MVMint64        position = 0;
        MVMGraphemeIter gi;
        result->body.storage_type    = MVM_STRING_GRAPHEME_32;
        result->body.storage.blob_32 = MVM_malloc(total_graphs * sizeof(MVMGrapheme32));
        for (i = 0; i < num_pieces; i++) {
            /* Get piece. */
            MVMString *piece = pieces[i];

            /* Add separator if needed. */
            if (0 < i) {
                /* No more checks unless still stable */
                if (concats_stable)
                    join_check_stability(tc, piece, separator, pieces,
                        &concats_stable, num_pieces, sgraphs, i);
                /* Add separator */
                if (sgraphs)
                    copy_to_32bit(tc, separator, result, &position, &gi);
            }
            /* Add piece */
            copy_to_32bit(tc, piece, result, &position, &gi);
        }
    }

    MVM_fixed_size_free(tc, tc->instance->fsa, bytes, pieces);
    STRAND_CHECK(tc, result);
    /* if concat is stable and NFG_CHECK on, run a NFG_CHECK on it since it
     * should be properly constructed now */
    if (concats_stable) {
        NFG_CHECK(tc, result, "MVM_string_join");
    }
    });
    return concats_stable ? result : re_nfg(tc, result);
}

/* Returning nonzero means it found the char at the position specified in 'a' in 'Haystack'.
 * For character enumerations in regexes. */
MVMint64 MVM_string_char_at_in_string(MVMThreadContext *tc, MVMString *a, MVMint64 offset, MVMString *Haystack) {
    MVMuint32     H_graphs;
    MVMGrapheme32 search;

    MVM_string_check_arg(tc, a, "char_at_in_string");
    MVM_string_check_arg(tc, Haystack, "char_at_in_string");

    /* We return -2 here only to be able to distinguish between "out of bounds" and "not in string". */
    if (offset < 0 || MVM_string_graphs_nocheck(tc, a) <= offset)
        return -2;

    search  = MVM_string_get_grapheme_at_nocheck(tc, a, offset);
    H_graphs = MVM_string_graphs_nocheck(tc, Haystack);
    switch (Haystack->body.storage_type) {
    case MVM_STRING_GRAPHEME_32:
        return MVM_string_memmem_grapheme32(tc, Haystack->body.storage.blob_32, &search, 0, H_graphs, 1);

    case MVM_STRING_GRAPHEME_ASCII:
        if (can_fit_into_ascii(search)) {
            MVMStringIndex i;
            for (i = 0; i < H_graphs; i++)
                if (Haystack->body.storage.blob_ascii[i] == search)
                    return i;
        }
        break;
    case MVM_STRING_GRAPHEME_8:
        if (can_fit_into_8bit(search)) {
            MVMStringIndex i;
            for (i = 0; i < H_graphs; i++)
                if (Haystack->body.storage.blob_8[i] == search)
                    return i;
        }
        break;
    case MVM_STRING_STRAND: {
        MVMGraphemeIter gi;
        MVMStringIndex  i;
        MVM_string_gi_init(tc, &gi, Haystack);
        for (i = 0; i < H_graphs; i++)
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
        cp = MVM_nfg_get_synthetic_info(tc, g)->codes[0];
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

    switch (s->body.storage_type) {
    case MVM_STRING_GRAPHEME_ASCII:
    case MVM_STRING_GRAPHEME_8: {
        MVMGrapheme8   *rbuffer;
        /* Copy the variables, so we can use them just for the loop. This way
         * the loop will vectorize since we won't refer to their values except
         * in the loop. Use size_t to coerce vectorization.  */
        size_t spos_l = spos, rpos_l = rpos;
        rbuffer = MVM_malloc(sizeof(MVMGrapheme8) * sgraphs);
        MVM_VECTORIZE_LOOP
        while (spos_l < s->body.num_graphs)
            rbuffer[--rpos_l] = s->body.storage.blob_8[spos_l++];

        spos += sgraphs - spos;
        rpos -= sgraphs - spos;

        MVMROOT(tc, s, {
            res = (MVMString *)MVM_repr_alloc_init(tc, tc->instance->VMString);
        });
        res->body.storage_type    = s->body.storage_type;
        res->body.storage.blob_8  = rbuffer;
        break;
    }
    default: {
        MVMGrapheme32  *rbuffer;
        rbuffer = MVM_malloc(sizeof(MVMGrapheme32) * sgraphs);

        if (s->body.storage_type == MVM_STRING_GRAPHEME_32) {
            size_t spos_l = spos, rpos_l = rpos;
            MVM_VECTORIZE_LOOP
            while (spos_l < s->body.num_graphs)
                rbuffer[--rpos_l] = s->body.storage.blob_32[spos_l++];

            spos += sgraphs - spos;
            rpos -= sgraphs - spos;
        }
        else
            for (; spos < sgraphs; spos++)
                rbuffer[--rpos] = MVM_string_get_grapheme_at_nocheck(tc, s, spos);

        res = (MVMString *)MVM_repr_alloc_init(tc, tc->instance->VMString);
        res->body.storage_type    = MVM_STRING_GRAPHEME_32;
        res->body.storage.blob_32 = rbuffer;
    }}

    res->body.num_graphs      = sgraphs;

    STRAND_CHECK(tc, res);
    return res;
}

/* Compares two strings, returning -1, 0 or 1 to indicate less than,
 * equal or greater than. */
MVMint64 MVM_string_compare(MVMThreadContext *tc, MVMString *a, MVMString *b) {
    MVMStringIndex alen, blen, i = 0, scanlen;
    MVMGraphemeIter gi_a, gi_b;

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

    /* Short circuit a case where the other conditionals won't speed it up */
    if (a->body.storage_type == MVM_STRING_STRAND || b->body.storage_type == MVM_STRING_STRAND) {

    }
    else if ((a->body.storage_type == MVM_STRING_GRAPHEME_8 || a->body.storage_type == MVM_STRING_GRAPHEME_ASCII)
          && (b->body.storage_type == MVM_STRING_GRAPHEME_8 || b->body.storage_type == MVM_STRING_GRAPHEME_ASCII)) {
        MVMGrapheme8  *a_blob8 = a->body.storage.blob_8;
        MVMGrapheme8  *b_blob8 = b->body.storage.blob_8;
        while (i < scanlen && a_blob8[i] == b_blob8[i]) {
            i++;
        }
    }
    else if (a->body.storage_type == MVM_STRING_GRAPHEME_32 && b->body.storage_type == MVM_STRING_GRAPHEME_32) {
        MVMGrapheme32  *a_blob32 = a->body.storage.blob_32;
        MVMGrapheme32  *b_blob32 = b->body.storage.blob_32;
        while (i < scanlen && a_blob32[i] == b_blob32[i]) {
            i++;
        }
    }
    else {
        MVMGrapheme32 *blob32 = NULL;
        MVMGrapheme8  *blob8  = NULL;
        switch (a->body.storage_type) {
            case MVM_STRING_GRAPHEME_8:
            case MVM_STRING_GRAPHEME_ASCII:
                blob8 = a->body.storage.blob_8;
                break;
            case MVM_STRING_GRAPHEME_32:
                blob32 = a->body.storage.blob_32;
                break;
            default:
                MVM_exception_throw_adhoc(tc,
                    "String corruption in string compare. Unknown string type.");
        }
        switch (b->body.storage_type) {
            case MVM_STRING_GRAPHEME_8:
            case MVM_STRING_GRAPHEME_ASCII:
                blob8 = b->body.storage.blob_8;
                break;
            case MVM_STRING_GRAPHEME_32:
                blob32 = b->body.storage.blob_32;
                break;
            default:
                MVM_exception_throw_adhoc(tc,
                    "String corruption in string compare. Unknown string type.");
        }
        while (i < scanlen && blob32[i] == blob8[i]) {
            i++;
        }
    }
    /* If one of the strings was a strand or we encountered a differing character
     * while scanning in the loops above. */
    if (i < scanlen) {
        MVM_string_gi_init(tc, &gi_a, a);
        MVM_string_gi_init(tc, &gi_b, b);
        if (i) {
            MVM_string_gi_move_to(tc, &gi_a, i);
            MVM_string_gi_move_to(tc, &gi_b, i);
        }
    }
    for (; i < scanlen; i++) {
        MVMGrapheme32 g_a = MVM_string_gi_get_grapheme(tc, &gi_a);
        MVMGrapheme32 g_b = MVM_string_gi_get_grapheme(tc, &gi_b);
        if (g_a != g_b) {
            MVMint64 rtrn;
            /* If one of the deciding graphemes is a synthetic then we need to
             * iterate the codepoints inside it */
            if (g_a < 0 || g_b < 0) {
                MVMCodepointIter ci_a, ci_b;
                MVM_string_grapheme_ci_init(tc, &ci_a, g_a, 0);
                MVM_string_grapheme_ci_init(tc, &ci_b, g_b, 0);
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
#define nfg_ok(cp) ((cp) < MVM_NORMALIZE_FIRST_SIG_NFC)
/* BITOP is the operator that is done to the two strings. GO_FULL_LEN determines
 * if we do the operation for the longest strong. If GO_FULL_LEN == 0 then
 * it only goes to the end of the shortest string (we do this for the string AND
 * op, but the other ones go for the length of the longest string. */
#define MVM_STRING_BITOP(BITOP, GO_FULL_LEN, OP_DESC) \
    MVMString      *res    = NULL;\
    MVMGrapheme32  *buffer = NULL;\
    MVMStringIndex  alen, blen, sgraphs = 0;\
    size_t buf_size;\
    MVMCodepointIter ci_a, ci_b;\
    int nfg_is_safe = 1;\
    MVM_string_check_arg(tc, a, (OP_DESC));\
    MVM_string_check_arg(tc, b, (OP_DESC));\
\
    alen = MVM_string_graphs_nocheck(tc, a);\
    blen = MVM_string_graphs_nocheck(tc, b);\
    buf_size = blen < alen ? alen : blen;\
    buffer = MVM_malloc(sizeof(MVMGrapheme32) * buf_size);\
    MVM_string_ci_init(tc, &ci_a, a, 0, 0);\
    MVM_string_ci_init(tc, &ci_b, b, 0, 0);\
\
    /* First, binary-or up to the length of the shortest string. */\
    while (MVM_string_ci_has_more(tc, &ci_a) && MVM_string_ci_has_more(tc, &ci_b)) {\
        const MVMGrapheme32 g_a = MVM_string_ci_get_codepoint(tc, &ci_a);\
        const MVMGrapheme32 g_b = MVM_string_ci_get_codepoint(tc, &ci_b);\
        buffer[sgraphs++] = g_a BITOP g_b;\
        if (nfg_is_safe && (!nfg_ok(g_a) || !nfg_ok(g_b)))\
            nfg_is_safe = 0;\
        if (sgraphs == buf_size) {\
            buf_size += 16;\
            buffer = MVM_realloc(buffer, buf_size * sizeof(MVMGrapheme32));\
        }\
    }\
    if (GO_FULL_LEN) {\
        while (MVM_string_ci_has_more(tc, &ci_a)) {\
            const MVMGrapheme32 g_a = MVM_string_ci_get_codepoint(tc, &ci_a);\
            buffer[sgraphs++] = g_a;\
            if (nfg_is_safe && !nfg_ok(g_a))\
                nfg_is_safe = 0;\
            if (sgraphs == buf_size) {\
                buf_size += 16;\
                buffer = MVM_realloc(buffer, buf_size * sizeof(MVMGrapheme32));\
            }\
        }\
        while (MVM_string_ci_has_more(tc, &ci_b)) {\
            const MVMGrapheme32 g_b = MVM_string_ci_get_codepoint(tc, &ci_b);\
            buffer[sgraphs++] = g_b;\
            if (nfg_is_safe && !nfg_ok(g_b))\
                nfg_is_safe = 0;\
            if (sgraphs == buf_size) {\
                buf_size += 16;\
                buffer = MVM_realloc(buffer, buf_size * sizeof(MVMGrapheme32));\
            }\
        }\
    }\
\
    res = (MVMString *)MVM_repr_alloc_init(tc, tc->instance->VMString);\
    res->body.storage_type    = MVM_STRING_GRAPHEME_32;\
    res->body.storage.blob_32 = buffer;\
    res->body.num_graphs      = sgraphs;\
\
    res = nfg_is_safe ? res : re_nfg(tc, res);\
    STRAND_CHECK(tc, res);\
    return res;\

/* Takes two strings and AND's their characters. */
MVMString * MVM_string_bitand(MVMThreadContext *tc, MVMString *a, MVMString *b) {
    MVM_STRING_BITOP( & , 0, "bitwise and")
}

/* Takes two strings and OR's their characters. */
MVMString * MVM_string_bitor(MVMThreadContext *tc, MVMString *a, MVMString *b) {
    MVM_STRING_BITOP( | , 1, "bitwise or")
}
/* Takes two strings and XOR's their characters. */
MVMString * MVM_string_bitxor(MVMThreadContext *tc, MVMString *a, MVMString *b) {
    MVM_STRING_BITOP( ^ , 1, "bitwise xor");
}

/* Shortcuts for some unicode general category pvalues */
#define UPV_Nd MVM_UNICODE_PVALUE_GC_ND
#define UPV_Lu MVM_UNICODE_PVALUE_GC_LU
#define UPV_Ll MVM_UNICODE_PVALUE_GC_LL
#define UPV_Lt MVM_UNICODE_PVALUE_GC_LT
#define UPV_Lm MVM_UNICODE_PVALUE_GC_LM
#define UPV_Lo MVM_UNICODE_PVALUE_GC_LO
#define UPV_Zs MVM_UNICODE_PVALUE_GC_ZS
#define UPV_Zl MVM_UNICODE_PVALUE_GC_ZL
#define UPV_Pc MVM_UNICODE_PVALUE_GC_PC
#define UPV_Pd MVM_UNICODE_PVALUE_GC_PD
#define UPV_Ps MVM_UNICODE_PVALUE_GC_PS
#define UPV_Pe MVM_UNICODE_PVALUE_GC_PE
#define UPV_Pi MVM_UNICODE_PVALUE_GC_PI
#define UPV_Pf MVM_UNICODE_PVALUE_GC_PF
#define UPV_Po MVM_UNICODE_PVALUE_GC_PO

/* concatenating with "" ensures that only literal strings are accepted as argument. */
#define STR_WITH_LEN(str)  ("" str ""), (sizeof(str) - 1)

#include "strings/unicode_prop_macros.h"
/* Checks if the specified grapheme is in the given character class. */
MVMint64 MVM_string_grapheme_is_cclass(MVMThreadContext *tc, MVMint64 cclass, MVMGrapheme32 g) {
    /* If it's a synthetic, then grab the base codepoint. */
    MVMCodepoint cp;
    if (0 <= g)
        cp = (MVMCodepoint)g;
    else
        cp = MVM_nfg_get_synthetic_info(tc, g)->codes[0];

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
            MVM_FALLTHROUGH

        case MVM_CCLASS_ALPHANUMERIC:
            if (cp <= '9' && cp >= '0')  /* short circuit common case */
                return 1;
            if (MVM_unicode_codepoint_has_property_value(tc, cp,
                    MVM_UNICODE_PROPERTY_GENERAL_CATEGORY, UPV_Nd))
                return 1;
            /* Deliberate fall-through; alphanumeric is digit or alphabetic. */
            MVM_FALLTHROUGH

        case MVM_CCLASS_ALPHABETIC:
            if (cp <= 'z') {  /* short circuit common case */
                if (cp >= 'a' || (cp >= 'A' && cp <= 'Z'))
                    return 1;
                else
                    return 0;
            }
            /* Property L covers Lo, Ll, Lu, Lt, Lm */
            return !!MVM_unicode_codepoint_get_property_int(tc, cp,
                MVM_UNICODE_PROPERTY_L);
            /* TODO: Maybe we want MVM_UNICODE_PROPERTY_ALPHABETIC instead? */

        case MVM_CCLASS_NUMERIC:
            if (cp <= '9' && cp >= '0')  /* short circuit common case */
                return 1;
            return MVM_unicode_codepoint_has_property_value(tc, cp,
                MVM_UNICODE_PROPERTY_GENERAL_CATEGORY, UPV_Nd);

        case MVM_CCLASS_HEXADECIMAL:
            return MVM_unicode_codepoint_has_property_value(tc, cp,
                MVM_UNICODE_PROPERTY_ASCII_HEX_DIGIT, 1);

        case MVM_CCLASS_WHITESPACE:
            return MVM_CP_is_White_Space(cp);

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
            return !!MVM_unicode_codepoint_get_property_int(tc, cp,
                MVM_UNICODE_PROPERTY_P);

        case MVM_CCLASS_NEWLINE: {
            return (cp == '\n' || cp == 0x0b || cp == 0x0c || cp == '\r' ||
                cp == 0x85 || MVM_CP_is_gencat_name_Zl(cp) || MVM_CP_is_gencat_name_Zp(cp))
                ? 1 : 0;
        }

        default:
            return 0;
    }
}

/* Checks if the character at the specified offset is a member of the
 * indicated character class. */
MVMint64 MVM_string_is_cclass(MVMThreadContext *tc, MVMint64 cclass, MVMString *s, MVMint64 offset) {
    MVM_string_check_arg(tc, s, "is_cclass");
    if (MVM_UNLIKELY(offset < 0 || MVM_string_graphs_nocheck(tc, s) <= offset))
        return 0;
    return MVM_string_grapheme_is_cclass(tc, cclass, MVM_string_get_grapheme_at_nocheck(tc, s, offset));
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
    switch (cclass) {
        case MVM_CCLASS_WHITESPACE:
            for (pos = offset; pos < end; pos++) {
                MVMGrapheme32 g = MVM_string_gi_get_grapheme(tc, &gi);
                MVMCodepoint cp = 0 <= g ? g : MVM_nfg_get_synthetic_info(tc, g)->codes[0];
                if (MVM_CP_is_White_Space(cp))
                    return pos;
            }
            break;
        case MVM_CCLASS_NEWLINE:
            for (pos = offset; pos < end; pos++) {
                MVMGrapheme32 g = MVM_string_gi_get_grapheme(tc, &gi);
                MVMCodepoint cp = 0 <= g ? g : MVM_nfg_get_synthetic_info(tc, g)->codes[0];
                if (cp == '\n' || cp == 0x0b || cp == 0x0c || cp == '\r' ||
                    cp == 0x85 || MVM_CP_is_gencat_name_Zl(cp) || MVM_CP_is_gencat_name_Zp(cp))
                    return pos;
            }
            break;
        default:
            for (pos = offset; pos < end; pos++) {
                MVMGrapheme32 g = MVM_string_gi_get_grapheme(tc, &gi);
                if (MVM_string_grapheme_is_cclass(tc, cclass, g) > 0)
                    return pos;
            }
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
    switch (cclass) {
        case MVM_CCLASS_WHITESPACE:
            for (pos = offset; pos < end; pos++) {
                MVMGrapheme32 g = MVM_string_gi_get_grapheme(tc, &gi);
                MVMCodepoint cp = 0 <= g ? g : MVM_nfg_get_synthetic_info(tc, g)->codes[0];
                if (!MVM_CP_is_White_Space(cp))
                    return pos;
            }
            break;
        case MVM_CCLASS_NEWLINE:
            for (pos = offset; pos < end; pos++) {
                MVMGrapheme32 g = MVM_string_gi_get_grapheme(tc, &gi);
                MVMCodepoint cp = 0 <= g ? g : MVM_nfg_get_synthetic_info(tc, g)->codes[0];
                if (!(cp == '\n' || cp == 0x0b || cp == 0x0c || cp == '\r' ||
                    cp == 0x85 || MVM_CP_is_gencat_name_Zl(cp) || MVM_CP_is_gencat_name_Zp(cp)))
                    return pos;
            }
            break;
        default:
            for (pos = offset; pos < end; pos++) {
                MVMGrapheme32 g = MVM_string_gi_get_grapheme(tc, &gi);
                if (!MVM_string_grapheme_is_cclass(tc, cclass, g))
                    return pos;
            }
    }

    return end;
}

static MVMint16   encoding_name_init         = 0;

struct encoding_name_struct {
    MVMString *encoding_name;
    char *encoding_cname;
    MVMint64 code;
};
static struct encoding_name_struct encoding_names[MVM_encoding_type_MAX] = {
    { NULL, "utf8", MVM_encoding_type_utf8  },               /* 1 */
    { NULL, "ascii", MVM_encoding_type_ascii },              /* 2 */
    { NULL, "iso-8859-1", MVM_encoding_type_latin1 },        /* 3 */
    { NULL, "utf16", MVM_encoding_type_utf16 },              /* 4 */
    { NULL, "windows-1252", MVM_encoding_type_windows1252 }, /* 5 */
    { NULL, "utf8-c8", MVM_encoding_type_utf8_c8 },          /* 6 */
    { NULL, "windows-1251", MVM_encoding_type_windows1251 }, /* 7 */
    { NULL, "windows-932", MVM_encoding_type_shiftjis },     /* 8 */
    { NULL, "utf16le", MVM_encoding_type_utf16le },          /* 9 */
    { NULL, "utf16be", MVM_encoding_type_utf16be },          /* 10 */
    { NULL, "gb2312", MVM_encoding_type_gb2312 },            /* 11 */
    { NULL, "gb18030", MVM_encoding_type_gb18030 }           /* 12 */
};

char * MVM_string_encoding_cname(MVMThreadContext *tc, MVMint64 encoding) {
    if (encoding < MVM_encoding_type_MIN || MVM_encoding_type_MAX < encoding)
        return "Unknown";
    return encoding_names[encoding-1].encoding_cname;
}

MVMuint8 MVM_string_find_encoding(MVMThreadContext *tc, MVMString *name) {
    int i;
    MVM_string_check_arg(tc, name, "find encoding");
    if (MVM_UNLIKELY(!encoding_name_init)) {
        MVM_gc_allocate_gen2_default_set(tc);
        for (i = 0; i < MVM_encoding_type_MAX; i++) {
            /* Catch it in case the array is not in the proper order. */
            if (i + 1 != encoding_names[i].code)
                MVM_oops(tc, "Encoding %s does not have the correct define during initialization.", encoding_names[i].encoding_cname);

            encoding_names[i].encoding_name = MVM_string_ascii_decode_nt(tc, tc->instance->VMString, encoding_names[i].encoding_cname);
            MVM_gc_root_add_permanent_desc(tc, (MVMCollectable **)&encoding_names[i].encoding_name, "Encoding name");
        }
        encoding_name_init   = 1;
        MVM_gc_allocate_gen2_default_clear(tc);
    }
    for (i = 0; i < MVM_encoding_type_MAX; i++) {
        if (MVM_string_equal(tc, name, encoding_names[i].encoding_name))
            return i + 1;
    }
    {
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
        MVM_exception_throw_adhoc(tc, "chr codepoint %"PRId64" cannot be negative", cp);
    if (cp > 0x10FFFF)
        MVM_exception_throw_adhoc(tc, "chr codepoint %"PRId64" (0x%"PRIX64") is out of bounds", cp, cp);
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
    MVMuint32 graphs[2];
    MVMuint64 u64;
} MVMJenHashGraphemeView;

/* To force little endian representation on big endian machines, set
 * MVM_HASH_FORCE_LITTLE_ENDIAN in strings/siphash/csiphash.h
 * If this isn't set, MVM_MAYBE_TO_LITTLE_ENDIAN_32 does nothing (the default).
 * This would mainly be useful for debugging or if there were some other reason
 * someone cared that hashes were identical on different endian platforms */
void MVM_string_compute_hash_code(MVMThreadContext *tc, MVMString *s) {
#if defined(MVM_HASH_FORCE_LITTLE_ENDIAN)
    const MVMuint64 key[2] = {
        MVM_MAYBE_TO_LITTLE_ENDIAN_64(tc->instance->hashSecrets[0]),
        MVM_MAYBE_TO_LITTLE_ENDIAN_64(tc->instance->hashSecrets[1])
    };
#else
    const MVMuint64 *key = tc->instance->hashSecrets;
#endif
    MVMuint64 hash = 0;
    MVMStringIndex s_len = MVM_string_graphs_nocheck(tc, s);
    switch (s->body.storage_type) {
        case MVM_STRING_GRAPHEME_8:
        case MVM_STRING_GRAPHEME_ASCII: {
            size_t i;
            MVMJenHashGraphemeView gv;
            siphash sh;
            siphashinit(&sh, s_len * sizeof(MVMGrapheme32), key);
            for (i = 0; i + 1 < s_len;) {
                gv.graphs[0] = MVM_MAYBE_TO_LITTLE_ENDIAN_32(s->body.storage.blob_8[i++]);
                gv.graphs[1] = MVM_MAYBE_TO_LITTLE_ENDIAN_32(s->body.storage.blob_8[i++]);
                siphashadd64bits(&sh, gv.u64);
            }
            /* If there is a final 32 bit grapheme pass it through, otherwise
             * pass through 0. */
            hash = siphashfinish_32bits(&sh,
                i < s_len
                    ? MVM_MAYBE_TO_LITTLE_ENDIAN_32(s->body.storage.blob_8[i]) : 0);
            break;
        }
#if !defined(MVM_HASH_FORCE_LITTLE_ENDIAN)
        case MVM_STRING_GRAPHEME_32: {
            hash = siphash24(
                (MVMuint8*)s->body.storage.blob_32,
                s_len * sizeof(MVMGrapheme32),
                key);
            break;
        }
#endif
        default: {
            siphash sh;
            MVMGraphemeIter gi;
            MVMJenHashGraphemeView gv;
            size_t i;
            siphashinit(&sh, s_len * sizeof(MVMGrapheme32), key);
            MVM_string_gi_init(tc, &gi, s);
            for (i = 0; i + 1 < s_len; i += 2) {
                gv.graphs[0] = MVM_MAYBE_TO_LITTLE_ENDIAN_32(MVM_string_gi_get_grapheme(tc, &gi));
                gv.graphs[1] = MVM_MAYBE_TO_LITTLE_ENDIAN_32(MVM_string_gi_get_grapheme(tc, &gi));
                siphashadd64bits(&sh, gv.u64);
            }
            hash = siphashfinish_32bits(&sh,
                i < s_len
                    ? MVM_MAYBE_TO_LITTLE_ENDIAN_32(MVM_string_gi_get_grapheme(tc, &gi))
                    : 0);
            break;
        }
    }
    s->body.cached_hash_code = hash;
}
