#include "moarvm.h"

#define GRAPHS_EQUAL(d1, d2, g, type) (memcmp((d1), (d2), (g) * sizeof(type)) == 0)

/*  TODO:
- add a tunable global determining at what depth it should
    start flattening.  Write a rope-tree-flattening function
    (not like string_flatten) that puts all the sub strands
    in one MVMStrand array. (see below on > 256)
    (optimization)
- add a tunable global determining under which size result
    string should it just do the old copying behavior, for
    join, concat, split, and repeat.
    This might be related to MVMString object size?
    (optimization)
- enable repeat and join to handle > 256 by adding a
    recursive divvy function that takes arrays of MVMStrands
    and allocates ropes in a tree until each has equal to or
    fewer than N, a tunable global <= 256.
    (necessary feature)
- make the uc, lc, tc functions intelligently
    create ropes from the originals when deemed advantageous.
    (optimization)
*/

/* Returns the size of the strands array. Doesn't need to be fast/cached, I think. */
MVMStrandIndex MVM_string_rope_strands_size(MVMThreadContext *tc, MVMStringBody *body) {
    if ((body->flags & MVM_STRING_TYPE_MASK) == MVM_STRING_TYPE_ROPE && body->graphs) {
        return body->strands->lower_index;
    }
    return 0;
}

/* for physical strings, applies the consumer function directly. For
 * "ropes", performs the binary search as directed by the strands table
 * to find the first starting strand index of the desired substring. 
 * Then recursively traverses more strands until the desired number of
 * characters are passed to the consumer function from the strand tree.
 * Both this and the consumer function return a boolean saying whether
 * to abort the traversal early. */
MVMuint8 MVM_string_traverse_substring(MVMThreadContext *tc, MVMString *a, MVMStringIndex start, MVMStringIndex length, MVMStringIndex top_index, MVMSubstringConsumer consumer, void *data) {
    
    switch(STR_FLAGS(a)) {
        case MVM_STRING_TYPE_INT32:
        case MVM_STRING_TYPE_UINT8:
            return consumer(tc, a, start, length, top_index, data);
        case MVM_STRING_TYPE_ROPE: {
            MVMStrand *strands = a->body.strands;
            MVMStrandIndex strand_index = 0;
            MVMStrandIndex lower = 255;
            MVMStrand *strand;
            MVMStringIndex index = start;
            for(;;) {
                strand = &strands[strand_index];
                if (strand->lower_index == strand->higher_index
                        || strand_index == lower)
                    break;
                if (index >= strand->compare_offset) {
                    lower = strand_index;
                    strand_index = strand->higher_index;
                }
                else {
                    strand_index = strand->lower_index;
                }
            }
            for(;;) {
                MVMuint8 return_val;
                /* get the number of available codepoints from
                 * this strand. */
                MVMStringIndex substring_length =
                    strands[strand_index + 1].compare_offset - index;
                /* determine how many codepoints to actually consume */
                if (length < substring_length)
                    substring_length = length;
                /* call ourself on the sub-strand */
                return_val = MVM_string_traverse_substring(tc, strand->string,
                    index - strand->compare_offset + strand->string_offset, 
                    substring_length, top_index + index, consumer, data);
                /* if we've been instructed to, abort early */
                if (return_val)
                    return return_val;
                /* reduce the number of codepoints requested by the number
                 * consumed. */
                length -= substring_length;
                /* if we've consumed all the codepoints, return without 
                 * indicating abort early. */
                if (!length)
                    return 0;
                /* advance the index by the number of codepoints consumed. */
                index += substring_length;
                /* advance in the strand table. The caller must guarantee
                 * this won't overflow by checking lengths. */
                strand = &strands[++strand_index];
            }
            break; // should be unreachable
        }
        default:
            MVM_exception_throw_adhoc(tc, "internal string corruption");
    }
    return 0; // should be unreachable
}

/* stack record created with each invocation of the string descent. */
typedef struct _MVMCompareCursor {
    MVMString *string; /* string into which descended */
    struct _MVMCompareCursor *parent; /* parent cursor */
    MVMint16 *gaps; /* number of gaps can ascend */
    MVMStringIndex string_idx; /* in string, not strand */
    MVMStringIndex end_idx; /* index past last codepoint needed */
    MVMStrandIndex strand_idx; /* current strand index */
    MVMuint8 owns_buffer; /* whether it owns the last buffer.
            for ropes, whether it's uninitialized. */
    MVMuint8 isa; /* whether it's from string tree a */
} MVMCompareCursor;

typedef struct _MVMCompareDescentState {
    MVMCompareCursor *cursora, *cursorb; /* cursors */
    MVMCodepoint32 *int32s; /* last physical string's buffer */
    MVMCodepoint8 *uint8s; /* last physical string's buffer */
    MVMStringIndex available; /* number of codepoints in the buffer */
    MVMStringIndex needed; /* number of codepoints still needed */
} MVMCompareDescentState;

/* uses the computed binary search table to find the strand containing the index */
static MVMStrandIndex find_strand_index(MVMString *s, MVMStringIndex index) {
    MVMStrand *strands = s->body.strands;
    MVMStrandIndex strand_index = 0;
    MVMStrandIndex lower = 255;
    MVMStrand *strand;
    while(1) {
        strand = &strands[strand_index];
        if (strand->lower_index == strand->higher_index
                || strand_index == lower)
            return strand_index;
        if (index >= strand->compare_offset) {
            lower = strand_index;
            strand_index = strand->higher_index;
        }
        else {
            strand_index = strand->lower_index;
        }
    }
}

#define descend_sized_chew_success(member, other_member) \
st->needed -= tocompare; \
if (!st->needed) { \
    *result = 2; \
    return; \
} \
else if (wehave > tocompare) { \
    st->member = c->string->body.member + c->string_idx + tocompare; \
    st->available = wehave - tocompare; \
    c->owns_buffer = 1; \
    st->other_member = NULL; \
} \
else { \
    st->other_member += tocompare; \
    st->available -= tocompare;\
} \

#define descend_sized(member, other_member, size, cp, other_cp) \
MVMStringIndex wehave = c->end_idx - c->string_idx; \
if (st->available) { \
    MVMStringIndex tocompare = st->available < wehave ? st->available : wehave; \
    if (st->member) { \
        if (memcmp(st->member, c->string->body.member + c->string_idx, \
               wehave * sizeof(size))) { \
            *result = 1; \
            return; \
        } \
        descend_sized_chew_success(member, other_member) \
    } \
    else { \
        size \
            *cp = c->string->body.member + c->string_idx, \
            *final = cp + tocompare; \
        MVMCodepoint8 *other_cp = st->other_member; \
        for (; cp < final; ) { \
            if (*cp32++ != (MVMCodepoint32) *cp8++) { \
                *result = 1; \
                return; \
            } \
        } \
        descend_sized_chew_success(other_member, member) \
    } \
    if (c->isa) { st->cursora = c->parent; } \
    else { st->cursorb = c->parent; } \
    return; \
} \
st->member = c->string->body.member + c->string_idx; \
st->other_member = NULL; \
st->available = wehave; \
c->owns_buffer = 1; \
continue;

/* If a is composed of strands, create a descent record, initialize it, and
 * descend into the substring. If result is set when it returns, return. 
 * If a is a real string, grab some codepoints from a string, compare
 * with codepoints from the other buffer. If different, set result nonzero and
 * it will fast return. Otherwise, reset buffer indexes
 * to zero. Loop if `needed` for the current string is nonzero. */
static void compare_descend(MVMThreadContext *tc, MVMCompareDescentState *st,
        MVMuint8 *result, MVMCompareCursor *orig) {
    /* while we still need to compare some things */
    while (st->needed) {
        /* pick a tree from which to get more characters. */
        MVMCompareCursor *c = st->cursora->owns_buffer ? st->cursorb : st->cursora;
        
        if (*c->gaps) {
            (*c->gaps)--;
            if (c->isa) { st->cursora = c->parent; }
            else { st->cursorb = c->parent; }
            return;
        }
        
        /* get some characters, comparing as they're found */
        switch(STR_FLAGS(c->string)) {
            case MVM_STRING_TYPE_INT32: {
                descend_sized(int32s, uint8s, MVMCodepoint32, cp32, cp8)
            }
            case MVM_STRING_TYPE_UINT8: {
                descend_sized(uint8s, int32s, MVMCodepoint8, cp8, cp32)
            }
            case MVM_STRING_TYPE_ROPE: {
                MVMStrand *strand;
                MVMStringIndex next_compare_offset, child_length, child_idx, child_end_idx;
                if (c->owns_buffer) { /* first time we've seen this rope */
                    c->strand_idx = find_strand_index(c->string, c->string_idx);
                    c->owns_buffer = 0;
                }
                if (c->string_idx == c->end_idx) {
                    if (c->isa) { st->cursora = c->parent; }
                    else { st->cursorb = c->parent; }
                    if (orig == c)
                        return;
                    (*c->gaps)++;
                    continue;
                }
                strand = c->string->body.strands + c->strand_idx;
                child_idx = c->string_idx - strand->compare_offset;
                next_compare_offset = (strand + 1)->compare_offset;
                child_length = next_compare_offset - strand->compare_offset;
                child_length = child_length > c->end_idx - c->string_idx
                    ? c->end_idx - c->string_idx : child_length;
                child_end_idx = child_idx + child_length;
                c->string_idx += child_length;
                c->strand_idx++;
                {
                    MVMCompareCursor child = { strand->string, c, c->gaps, child_idx,
                        child_end_idx, 0, IS_ROPE(strand->string)?1:0, c->isa?1:0 };
                    if (c->isa) { st->cursora = &child; } else { st->cursorb = &child; }
                    compare_descend(tc, st, result, &child);
                }
                if (*result) return;
                continue;
            }
            default:
                MVM_exception_throw_adhoc(tc, "internal string corruption");
        }
    }
    *result = 2;
}

/* returns nonzero if two substrings are equal, doesn't check bounds */
MVMint64 MVM_string_substrings_equal_nocheck(MVMThreadContext *tc, MVMString *a,
        MVMint64 starta, MVMint64 length, MVMString *b, MVMint64 startb) {
    MVMint16 gapsa = 0, gapsb = 0;
    MVMuint8 result = 0;
    MVMCompareCursor
        cursora = { a, NULL, &gapsa, starta, starta + length, 0,
            IS_ROPE(a)?1:0, 1 },
        cursorb = { b, NULL, &gapsb, startb, startb + length, 0,
            IS_ROPE(b)?1:0, 0 };
    MVMCompareDescentState st = { &cursora, &cursorb, NULL, NULL, 0, length };
    
    compare_descend(tc, &st, &result, &cursora);
    return result ? result - 1 : 1;
}

/* returns the codepoint without doing checks, for internal VM use only. */
MVMCodepoint32 MVM_string_get_codepoint_at_nocheck(MVMThreadContext *tc, MVMString *a, MVMint64 index) {
    MVMStringIndex idx = (MVMStringIndex)index;
    
    switch(STR_FLAGS(a)) {
        case MVM_STRING_TYPE_INT32:
            return a->body.int32s[idx];
        case MVM_STRING_TYPE_UINT8:
            return (MVMCodepoint32)a->body.uint8s[idx];
        case MVM_STRING_TYPE_ROPE: {
            MVMStrand *strands = a->body.strands;
            MVMStrandIndex table_index = 0;
            MVMStrandIndex lower_visited = 255;
            /*MVMStrandIndex upper_visited = 255;*/
            MVMStrand *strand;
            /* see MVMString.h.  Starting with the first entry,
                binary search through the strands in the string
                searching for the strand containing that offset. */
            for(;;) {
                strand = &strands[table_index];
                if (strand->lower_index == strand->higher_index
                        || table_index == lower_visited
                        /*|| table_index == upper_visited*/)
                    break;
                if (idx >= strand->compare_offset) {
                    /* mark that we've visited this node on the
                        lower side so we can halt if we return to it */
                    lower_visited = table_index;
                    table_index = strand->higher_index;
                }
                else {
                    /* mark that we've visited this node on the
                        upper side so we can halt if we return to it */
                    /*upper_visited = table_index;*/
                    table_index = strand->lower_index;
                }
            }
            return MVM_string_get_codepoint_at_nocheck(tc,
                strand->string, idx - strand->compare_offset + strand->string_offset);
        }
    }
    MVM_exception_throw_adhoc(tc, "internal string corruption");
    return 0;
}

/* Returns the location of one string in another or -1  */
MVMint64 MVM_string_index(MVMThreadContext *tc, MVMString *haystack, MVMString *needle, MVMint64 start) {
    MVMint64 result = -1;
    size_t index    = (size_t)start;
    
    if (!IS_CONCRETE((MVMObject *)haystack)) {
        MVM_exception_throw_adhoc(tc, "index needs a concrete search target");
    }
    
    if (!IS_CONCRETE((MVMObject *)needle)) {
        MVM_exception_throw_adhoc(tc, "index needs a concrete search term");
    }
    
    if (!needle->body.graphs && !haystack->body.graphs)
        return 0; /* the empty strings are equal and start at zero */
    
    if (!haystack->body.graphs)
        return -1;
    
    if (start < 0 || start >= haystack->body.graphs)
        /* maybe return -1 instead? */
        MVM_exception_throw_adhoc(tc, "index start offset out of range");
    
    if (needle->body.graphs > haystack->body.graphs || needle->body.graphs < 1)
        return -1;
    /* brute force for now. */
    while (index <= haystack->body.graphs - needle->body.graphs) {
        if (MVM_string_substrings_equal_nocheck(tc, needle, 0, needle->body.graphs, haystack, index)) {
            result = (MVMint64)index;
            break;
        }
        index++;
    }
    return result;
}

/* Returns a substring of the given string */
MVMString * MVM_string_substring(MVMThreadContext *tc, MVMString *a, MVMint64 start, MVMint64 length) {
    MVMString *result;
    MVMStrand *strands;
    
    if (start < 0) {
        start += a->body.graphs;
        if (start < 0)
            start = 0;
    }
    
    if (!IS_CONCRETE((MVMObject *)a)) {
        MVM_exception_throw_adhoc(tc, "Substring needs a concrete string");
    }
    
    if (start >= a->body.graphs)
        MVM_exception_throw_adhoc(tc, "Substring start offset (%lld) cannot be past end of string (%lld)",
            start, a->body.graphs);
    
    if (length < -1) /* -1 signifies go to the end of the string */
        MVM_exception_throw_adhoc(tc, "Substring length (%lld) cannot be negative", length);
    
    if (length == -1)
        length = a->body.graphs - start;
    
    if (start + length > a->body.graphs)
        MVM_exception_throw_adhoc(tc, "Substring end (%lld) cannot be past end of string (%lld)",
            start + length, a->body.graphs);
    
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&a);
    result = (MVMString *)REPR(a)->allocate(tc, STABLE(a));
    MVM_gc_root_temp_pop(tc);
    
    strands = result->body.strands = calloc(sizeof(MVMStrand), 2);
    /* if we're substringing a substring, substring the same one */
    if (IS_SUBSTRING(a)) {
        strands->string_offset = (MVMStringIndex)start + a->body.strands->string_offset;
        strands->string = a->body.strands->string;
    }
    else {
        strands->string_offset = (MVMStringIndex)start;
        strands->string = a;
    }
    _STRAND_DEPTH(result) = STRAND_DEPTH(strands->string) + 1;
    strands[1].compare_offset = length;
    
    /* result->body.codes  = 0; /* Populate this lazily. */
    result->body.flags = MVM_STRING_TYPE_ROPE;
    result->body.graphs = length;
    result->body.strands->lower_index = 1;
    
    return result;
}

/* Recursively populate the binary search table for strands.
    Will *not* overflow the C stack. :) */
static MVMStrandIndex MVM_string_generate_strand_binary_search_table(MVMThreadContext *tc, MVMStrand *strands, MVMStrandIndex bottom, MVMStrandIndex top) {
    MVMStrandIndex mid, lower_result;
    if (top == bottom) {
        strands[bottom].lower_index = bottom;
        strands[bottom].higher_index = bottom;
        return bottom;
    }
    mid = (top - bottom + 1)/2 + bottom;
    lower_result =
        MVM_string_generate_strand_binary_search_table(tc, strands, bottom, mid - 1);
    strands[mid].higher_index =
        MVM_string_generate_strand_binary_search_table(tc, strands, mid, top);
    strands[mid].lower_index = lower_result;
    
    return mid;
}

/* Append one string to another. */
/* XXX inline parent's strands if it's a rope too? */
MVMString * MVM_string_concatenate(MVMThreadContext *tc, MVMString *a, MVMString *b) {
    MVMString *result;
    MVMStrandIndex strand_count = 0;
    MVMStrand *strands;
    MVMStringIndex index = 0;
    MVMuint8 max_strand_depth = 0;
    
    if (!IS_CONCRETE((MVMObject *)a) || !IS_CONCRETE((MVMObject *)b)) {
        MVM_exception_throw_adhoc(tc, "Concatenate needs concrete strings");
    }
    
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&a);
    result = (MVMString *)REPR(a)->allocate(tc, STABLE(a));
    MVM_gc_root_temp_pop(tc);
    
    /* there could be unattached combining chars at the beginning of b,
       so, XXX TODO handle this */
    result->body.flags = MVM_STRING_TYPE_ROPE;
    result->body.graphs = a->body.graphs + b->body.graphs;
    
    if (a->body.graphs)
        strand_count = 1;
    if (b->body.graphs)
        ++strand_count;
    strands = result->body.strands = calloc(sizeof(MVMStrand), strand_count + 1);
    strand_count = 0;
    if (a->body.graphs) {
        strands->string = a;
        strands->string_offset = 0;
        strands->compare_offset = index;
        index = a->body.graphs;
        strand_count = 1;
        max_strand_depth = STRAND_DEPTH(a);
    }
    if (b->body.graphs) {
        strands[strand_count].string = b;
        strands[strand_count].string_offset = 0;
        strands[strand_count].compare_offset = index;
        index += b->body.graphs;
        ++strand_count;
        if (STRAND_DEPTH(b) > max_strand_depth)
            max_strand_depth = STRAND_DEPTH(b);
    }
    if (strand_count)
        strands->higher_index =
            MVM_string_generate_strand_binary_search_table(tc, strands, 0, strand_count - 1);
    strands[strand_count].compare_offset = index;
    strands->lower_index = strand_count;
    _STRAND_DEPTH(result) = max_strand_depth + 1;
    
    return result;
}

MVMString * MVM_string_repeat(MVMThreadContext *tc, MVMString *a, MVMint64 count) {
    MVMString *result;
    MVMint64 bkup_count = count;
    MVMStringIndex string_offset = 0, graphs = 0;
    
    if (!IS_CONCRETE((MVMObject *)a)) {
        MVM_exception_throw_adhoc(tc, "repeat needs a concrete string");
    }
    
    if (count < 0)
        MVM_exception_throw_adhoc(tc, "repeat count (%lld) cannot be negative", count);
    
    if (count > 256)
        MVM_exception_throw_adhoc(tc, "repeat count > 256 NYI...");
    
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&a);
    result = (MVMString *)REPR(a)->allocate(tc, STABLE(a));
    MVM_gc_root_temp_pop(tc);
    
    if (IS_SUBSTRING(a)) {
        string_offset = a->body.strands->string_offset;
        graphs = a->body.strands[1].compare_offset;
        a = a->body.strands->string;
    }
    else {
        graphs = a->body.graphs;
    }
    
    result->body.graphs = graphs * count;
    
    if (result->body.graphs) {
        MVMStrand *strands = result->body.strands = calloc(sizeof(MVMStrand), count + 1);
        result->body.flags = MVM_STRING_TYPE_ROPE;
        
        while (count--) {
            strands[count].compare_offset = count * graphs;
            strands[count].string = a;
            strands[count].string_offset = string_offset;
        }
        strands->higher_index =
            MVM_string_generate_strand_binary_search_table(tc, strands, 0, bkup_count - 1);
        strands[bkup_count].compare_offset = result->body.graphs;
        result->body.strands->lower_index = bkup_count;
        _STRAND_DEPTH(result) = bkup_count;
    }
    
    return result;
}

void MVM_string_say(MVMThreadContext *tc, MVMString *a) {
    MVMuint8 *utf8_encoded;
    MVMuint64 utf8_encoded_length;
    
    if (!IS_CONCRETE((MVMObject *)a)) {
        MVM_exception_throw_adhoc(tc, "say needs a concrete string");
    }
    
    /* XXX send a buffer of substrings of size 100 or something? */
    utf8_encoded = MVM_string_utf8_encode(tc, a, &utf8_encoded_length);
    
    fwrite(utf8_encoded, 1, utf8_encoded_length, stdout);
    printf("\n");
    
    free(utf8_encoded);
}

/* Tests whether one string a has the other string b as a substring at that index */
MVMint64 MVM_string_equal_at(MVMThreadContext *tc, MVMString *a, MVMString *b, MVMint64 offset) {
    
    if (!IS_CONCRETE((MVMObject *)a) || !IS_CONCRETE((MVMObject *)b)) {
        MVM_exception_throw_adhoc(tc, "equal_at needs concrete strings");
    }
    
    if (offset < 0) {
        offset += a->body.graphs;
        if (offset < 0)
            offset = 0; /* XXX I think this is the right behavior here */
    }
    if (a->body.graphs - offset < b->body.graphs)
        return 0;
    return MVM_string_substrings_equal_nocheck(tc, a, offset, b->body.graphs, b, 0);
}

/* Compares two strings for equality. */
MVMint64 MVM_string_equal(MVMThreadContext *tc, MVMString *a, MVMString *b) {
    if (a->body.graphs != b->body.graphs)
        return 0;
    return MVM_string_equal_at(tc, a, b, 0);
}

/* more general form of has_at; compares two substrings for equality */
MVMint64 MVM_string_have_at(MVMThreadContext *tc, MVMString *a,
        MVMint64 starta, MVMint64 length, MVMString *b, MVMint64 startb) {
    
    if (!IS_CONCRETE((MVMObject *)a) || !IS_CONCRETE((MVMObject *)b)) {
        MVM_exception_throw_adhoc(tc, "have_at needs concrete strings");
    }
    
    if (starta < 0 || startb < 0)
        return 0;
    if (length == 0)
        return 1;
    if (starta + length > a->body.graphs || startb + length > b->body.graphs)
        return 0;
    return MVM_string_substrings_equal_nocheck(tc, a, starta, length, b, startb);
}

/* returns the codepoint (could be a negative synthetic) at a given index of the string */
MVMint64 MVM_string_get_codepoint_at(MVMThreadContext *tc, MVMString *a, MVMint64 index) {
    
    if (!IS_CONCRETE((MVMObject *)a)) {
        MVM_exception_throw_adhoc(tc, "codepoint_at needs a concrete string");
    }
    
    if (index < 0 || index >= a->body.graphs)
        MVM_exception_throw_adhoc(tc, "Invalid string index: max %lld, got %lld",
            index >= a->body.graphs - 1, index);
    return (MVMint64)MVM_string_get_codepoint_at_nocheck(tc, a, index);
}

/* finds the location of a codepoint in a string.  Useful for small character class lookup */
MVMint64 MVM_string_index_of_codepoint(MVMThreadContext *tc, MVMString *a, MVMint64 codepoint) {
    size_t index = -1;
    while (++index < a->body.graphs)
        /* XXX make this traverse the rope tree once recursively */
        if (MVM_string_get_codepoint_at_nocheck(tc, a, index) == codepoint)
            return index;
    return -1;
}

typedef struct _MVMCaseChangeState {
    MVMString *dest;
    MVMStringIndex size;
    MVMint32 case_change_type;
} MVMCaseChangeState;

/* XXX make this handle case changes that change the number of characters.
    Will require some form of buffering/lookahead, or rollback of a placed
    char if a following char forms a composite sequence that case changes
    together. */
MVM_SUBSTRING_CONSUMER(MVM_string_case_change_consumer) {
    MVMCaseChangeState *state = (MVMCaseChangeState *)data;
    MVMString *dest = state->dest;
    switch (STR_FLAGS(string)) {
        case MVM_STRING_TYPE_INT32: {
            MVMCodepoint32 *i;
            if (!IS_WIDE(dest)) {
                MVM_string_flatten(tc, dest);
            }
            for (i = string->body.int32s + start; i < string->body.int32s + start + length; ) {
                if (dest->body.graphs == state->size) {
                    if (!state->size) state->size = 16;
                    else state->size *= 2;
                    dest->body.int32s = realloc(dest->body.int32s,
                        state->size * sizeof(MVMCodepoint32));
                }
                dest->body.int32s[dest->body.graphs++] = 
                    MVM_unicode_get_case_change(tc, *i++, state->case_change_type);
            }
            break;
        }
        case MVM_STRING_TYPE_UINT8: {
            MVMCodepoint8 *i;
            if (IS_WIDE(dest)) {
                for (i = string->body.uint8s + start; i < string->body.uint8s + start + length; ) {
                    if (dest->body.graphs == state->size) {
                        if (!state->size) state->size = 16;
                        else state->size *= 2;
                        dest->body.int32s = realloc(dest->body.int32s,
                            state->size * sizeof(MVMCodepoint32));
                    }
                    dest->body.int32s[dest->body.graphs++] = 
                        MVM_unicode_get_case_change(tc, (MVMCodepoint32) *i++,
                            state->case_change_type);
                }
            }
            else { /* hopefully most common/fast case of ascii->ascii */
                for (i = string->body.uint8s + start; i < string->body.uint8s + start + length; ) {
                    if (dest->body.graphs == state->size) {
                        if (!state->size) state->size = 16;
                        else state->size *= 2;
                        dest->body.uint8s = realloc(dest->body.uint8s,
                            state->size * sizeof(MVMCodepoint8));
                    }
                    dest->body.uint8s[dest->body.graphs++] = 
                        MVM_unicode_get_case_change(tc, (MVMCodepoint32) *i++,
                            state->case_change_type);
                }
            }
            break;
        }
        default:
            MVM_exception_throw_adhoc(tc, "internal string corruption");
    }
    return 0;
}

/* Uppercases a string. */
#define case_change_func(funcname, type, error) \
MVMString * funcname(MVMThreadContext *tc, MVMString *s) { \
    MVMString *result; \
    MVMStringIndex i; \
    MVMCaseChangeState state = { NULL, 0, type }; \
    \
    if (!IS_CONCRETE((MVMObject *)s)) { \
        MVM_exception_throw_adhoc(tc, error); \
    } \
    \
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&s); \
    state.dest = result = (MVMString *)REPR(s)->allocate(tc, STABLE(s)); \
    MVM_gc_root_temp_pop(tc); \
     \
    MVM_string_traverse_substring(tc, s, 0, s->body.graphs, 0, \
        MVM_string_case_change_consumer, &state); \
     \
    return result; \
}

case_change_func(MVM_string_uc, MVM_unicode_case_change_type_upper, "uc needs a concrete string")
case_change_func(MVM_string_lc, MVM_unicode_case_change_type_lower, "lc needs a concrete string")
case_change_func(MVM_string_tc, MVM_unicode_case_change_type_title, "tc needs a concrete string")

/* decodes a C buffer to an MVMString, dependent on the encoding type flag */
MVMString * MVM_decode_C_buffer_to_string(MVMThreadContext *tc,
        MVMObject *type_object, char *Cbuf, MVMint64 byte_length, MVMint64 encoding_flag) {
        
    /* someday make 0 mean "try really hard to detect the encoding */
    
    switch(encoding_flag) {
        case MVM_encoding_type_utf8:
            return MVM_string_utf8_decode(tc, type_object, Cbuf, byte_length);
        case MVM_encoding_type_ascii:
            return MVM_string_ascii_decode(tc, type_object, Cbuf, byte_length);
        case MVM_encoding_type_latin1:
            return MVM_string_latin1_decode(tc, type_object, Cbuf, byte_length);
        default:
            MVM_exception_throw_adhoc(tc, "invalid encoding type flag: %d", encoding_flag);
    }
    return NULL;
}

/* encodes an MVMString to a C buffer, dependent on the encoding type flag */
char * MVM_encode_string_to_C_buffer(MVMThreadContext *tc, MVMString *s, MVMint64 start, MVMint64 length, MVMuint64 *output_size, MVMint64 encoding_flag) {
    switch(encoding_flag) {
        case MVM_encoding_type_utf8:
            return MVM_string_utf8_encode_substr(tc, s, output_size, start, length);
        case MVM_encoding_type_ascii:
            return MVM_string_ascii_encode_substr(tc, s, output_size, start, length);
        case MVM_encoding_type_latin1:
            return MVM_string_latin1_encode_substr(tc, s, output_size, start, length);
        default:
            MVM_exception_throw_adhoc(tc, "invalid encoding type flag: %d", encoding_flag);
    }
    return NULL;
}

MVMObject * MVM_string_split(MVMThreadContext *tc, MVMString *input, MVMObject *type_object, MVMString *separator) {
    MVMObject *result;
    MVMint64 start, end, sep_length;
    
    if (!IS_CONCRETE((MVMObject *)separator)) {
        MVM_exception_throw_adhoc(tc, "split needs a concrete string separator");
    }
    
    if (REPR(type_object)->ID != MVM_REPR_ID_MVMArray || IS_CONCRETE(type_object)) {
        MVM_exception_throw_adhoc(tc, "split needs a type object with MVMArray REPR");
    }
    
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&type_object);
    result = REPR(type_object)->allocate(tc, STABLE(type_object));
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&input);
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&separator);
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&result);
    
    start = 0;
    end = input->body.graphs;
    sep_length = separator->body.graphs;
    
    while (start < end) {
        MVMString *portion;
        MVMint64 index, length;
        
        index = MVM_string_index(tc, input, separator, start);
        length = sep_length ? (index == -1 ? end : index) - start : 1;
        if (length) {
            portion = MVM_string_substring(tc, input, start, length);
             MVM_repr_push_o(tc, result, (MVMObject *)portion);
        }
        start += length + sep_length;
    }
    
    MVM_gc_root_temp_pop_n(tc, 4);
    
    return result;
}

MVMString * MVM_string_join(MVMThreadContext *tc, MVMObject *input, MVMString *separator) {
    MVMint64 elems, length = 0, index = -1, position = 0;
    MVMString *portion, *result;
    MVMuint32 codes = 0;
    MVMStrandIndex portion_index = 0;
    MVMStrand *strands;
    
    if (REPR(input)->ID != MVM_REPR_ID_MVMArray || !IS_CONCRETE(input)) {
        MVM_exception_throw_adhoc(tc, "join needs a concrete object with MVMArray REPR");
    }
    
    if (!IS_CONCRETE((MVMObject *)separator)) {
        MVM_exception_throw_adhoc(tc, "join needs a concrete separator");
    }
    
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&separator);
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&input);
    result = (MVMString *)(REPR(separator)->allocate(tc, STABLE(separator)));
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&result);
    
    elems = REPR(input)->pos_funcs->elems(tc, STABLE(input),
        input, OBJECT_BODY(input));
    
    while (++index < elems) {
        MVMObject *item = MVM_repr_at_pos_o(tc, input, index);
        
        /* allow null items in the array, I guess.. */
        if (!item)
            continue;
        if (REPR(item)->ID != MVM_REPR_ID_MVMString || !IS_CONCRETE(item))
            MVM_exception_throw_adhoc(tc, "join needs concrete strings only");
        
        portion = (MVMString *)item;
        if (portion->body.graphs) 
            ++portion_index;
        if (index && separator->body.graphs)
            ++portion_index;
        length += portion->body.graphs + (index ? separator->body.graphs : 0);
        /* XXX codes += portion->body.codes + (index ? separator->body.codes : 0); */
    }
    
    result->body.graphs = length;
    /* XXX consider whether to coalesce combining characters
    if they cause new combining sequences to appear */
    /* XXX result->body.codes = codes; */
    
    if (portion_index > 256)
        MVM_exception_throw_adhoc(tc, "join array items > 256 NYI...");
    
    if (portion_index) {
        index = -1;
        position = 0;
        strands = result->body.strands = calloc(sizeof(MVMStrand), portion_index + 1);
        
        portion_index = 0;
        while (++index < elems) {
            MVMObject *item = MVM_repr_at_pos_o(tc, input, index);
            
            if (!item)
                continue;
            
            /* Note: this allows the separator to precede the empty string. */
            if (index && separator->body.graphs) {
                strands[portion_index].compare_offset = position;
                strands[portion_index].string = separator;
                position += separator->body.graphs;
                ++portion_index;
            }
            
            portion = (MVMString *)item;
            length = portion->body.graphs;
            if (length) {
                strands[portion_index].compare_offset = position;
                strands[portion_index].string = portion;
                position += length;
                ++portion_index;
            }
        }
        strands->higher_index =
            MVM_string_generate_strand_binary_search_table(tc, strands, 0, portion_index - 1);
        strands[portion_index].compare_offset = position;
        strands->lower_index = portion_index;
    }
    result->body.flags = MVM_STRING_TYPE_ROPE;
    
    MVM_gc_root_temp_pop_n(tc, 3);
    
    if (result->body.graphs != position)
        MVM_exception_throw_adhoc(tc, "join had an internal error");
    
    return result;
}

typedef struct _MVMCharAtState {
    MVMCodepoint32 search;
    MVMStringIndex result;
} MVMCharAtState;

MVM_SUBSTRING_CONSUMER(MVM_string_char_at_consumer) {
    MVMCharAtState *state = (MVMCharAtState *)data;
    switch (STR_FLAGS(string)) {
        case MVM_STRING_TYPE_INT32: {
            MVMCodepoint32 *i;
            for (i = string->body.int32s + start; i < string->body.int32s + start + length; ) {
                if (*i++ == state->search) {
                    state->result = top_index + (MVMStringIndex)(i - (string->body.int32s + start) - 1);
                    return 1;
                }
            }
            break;
        }
        case MVM_STRING_TYPE_UINT8: {
            MVMCodepoint8 *i;
            for (i = string->body.uint8s + start; i < string->body.uint8s + start + length; i++) {
                if (*i++ == state->search) {
                    state->result = top_index + (MVMStringIndex)(i - (string->body.uint8s + start) - 1);
                    return 1;
                }
            }
            break;
        }
        default:
            MVM_exception_throw_adhoc(tc, "internal string corruption");
    }
    return 0;
}

/* returning nonzero means it found the char at the position specified in 'a' in 'b'.
    For character enumerations in regexes.  */
MVMint64 MVM_string_char_at_in_string(MVMThreadContext *tc, MVMString *a, MVMint64 offset, MVMString *b) {
    MVMuint32 index;
    MVMCharAtState state;
    
    if (offset < 0 || offset >= a->body.graphs)
        return 0;
    
    state.search = MVM_string_get_codepoint_at_nocheck(tc, a, offset);
    state.result = -1;
    
    MVM_string_traverse_substring(tc, b, 0, b->body.graphs, 0,
        MVM_string_char_at_consumer, &state);
    return state.result;
}

MVMint64 MVM_string_offset_has_unicode_property_value(MVMThreadContext *tc, MVMString *s, MVMint64 offset, MVMint64 property_code, MVMint64 property_value_code) {
    
    if (!IS_CONCRETE((MVMObject *)s)) {
        MVM_exception_throw_adhoc(tc, "uniprop lookup needs a concrete string");
    }
    
    if (offset < 0 || offset >= s->body.graphs)
        return 0;
    
    return MVM_unicode_codepoint_has_property_value(tc,
        MVM_string_get_codepoint_at_nocheck(tc, s, offset), property_code, property_value_code);
}

/* internal function so hashes can easily compute hashes of hash keys */
void MVM_string_flatten(MVMThreadContext *tc, MVMString *s) {
    /* XXX This is temporary until we can get the hashing mechanism
        to compute the hash (and test for equivalence!) using the
        codepoint iterator interface.  It's not thread-safe. */
    MVMStringIndex position = 0;
    MVMCodepoint32 *buffer;
    if (IS_WIDE(s))
        return;
    buffer = malloc(sizeof(MVMCodepoint32) * s->body.graphs);
    for (; position < s->body.graphs; position++) {
            /* XXX this needs to traverse the rope tree */
        buffer[position] = MVM_string_get_codepoint_at_nocheck(tc, s, position);
    }
    s->body.int32s = buffer;
    s->body.flags = MVM_STRING_TYPE_INT32;
}
