#include "moarvm.h"

#define GRAPHS32_EQUAL(d1, d2, g) (memcmp((d1), (d2), (g) * sizeof(MVMint32)) == 0)
#define GRAPHS8_EQUAL(d1, d2, g) (memcmp((d1), (d2), (g) * sizeof(MVMuint8)) == 0)

/* Returns the size of the strands array. Doesn't need to be fast/cached, I think. */
MVMuint32 MVM_string_rope_strands_size(MVMThreadContext *tc, MVMStringBody *body) {
    if ((body->codes & MVM_STRING_TYPE_MASK) == MVM_STRING_TYPE_ROPE) {
        MVMStrand *strands = body->strands;
        MVMuint32 count = 0;
        while(     strands[count].lower_index != 0
                || strands[count].higher_index != 0)
            count++;
        return count + 1;
    }
    return 0;
}

/* returns the codepoint without doing checks, for internal VM use only. */
MVMint32 MVM_string_get_codepoint_at_nocheck(MVMThreadContext *tc, MVMString *a, MVMint64 index) {
    MVMuint64 idx = (MVMuint64)index;
    
    switch(a->body.codes & MVM_STRING_TYPE_MASK) {
        case MVM_STRING_TYPE_INT32:
            return a->body.data.int32s[index];
        case MVM_STRING_TYPE_UINT8:
            return (MVMint32)a->body.data.uint8s[index];
        case MVM_STRING_TYPE_ROPE: {
            MVMStrand *strands = a->body.strands;
            MVMuint32 table_index = 0;
            MVMuint32 lower_visited = 4294967295ULL;
            MVMuint32 upper_visited = 4294967295ULL;
            MVMStrand *strand;
            /* see MVMString.h.  Starting with the first entry,
                binary search through the strands in the string
                searching for the strand containing that offset. */
                /* XXX I suspect upper_visited is unnecessary */
            for(;;) {
                strand = &strands[table_index];
                if (strand->lower_index == strand->higher_index
                        || table_index == lower_visited
                        || table_index == upper_visited)
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
                    upper_visited = table_index;
                    table_index = strand->lower_index;
                }
            }
            return MVM_string_get_codepoint_at_nocheck(tc,
                strand->string, idx - strand->compare_offset);
        }
    }
    MVM_exception_throw_adhoc(tc, "internal string corruption");
    return 0;
}

MVMint64 MVM_string_substrings_equal_nocheck(MVMThreadContext *tc, MVMString *a,
        MVMint64 starta, MVMint64 length, MVMString *b, MVMint64 startb) {
    MVMint64 index;
    switch(a->body.codes & MVM_STRING_TYPE_MASK) {
        case MVM_STRING_TYPE_INT32:
            if ((b->body.codes & MVM_STRING_TYPE_MASK) == MVM_STRING_TYPE_INT32)
                return (MVMint64)GRAPHS32_EQUAL(a->body.data.int32s + (size_t)starta,
                    b->body.data.int32s + (size_t)startb, (size_t)length);
            break;
        case MVM_STRING_TYPE_UINT8:
            if ((b->body.codes & MVM_STRING_TYPE_MASK) == MVM_STRING_TYPE_UINT8)
                return (MVMint64)GRAPHS8_EQUAL(a->body.data.uint8s + (size_t)starta,
                    b->body.data.uint8s + (size_t)startb, (size_t)length);
            break;
        case MVM_STRING_TYPE_ROPE:
            break;
    }
    /* XXX This can be made far more efficient for all cases by inlining
        and merging the 2 copies of get_codepoint_at_nocheck */
    for (index = 0; index < length; index++) {
        if (       MVM_string_get_codepoint_at_nocheck(tc, a, starta + index)
                != MVM_string_get_codepoint_at_nocheck(tc, b, startb + index))
            return 0;
    }
    return 1;
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
/* XXX If the input string a is a rope with only 1 strand, inline its strand */
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
    strands[0].string_offset = (MVMuint64)start;
    strands[0].string = a;
    strands[1].compare_offset = length;
    
    /* result->body.codes  = 0; /* Populate this lazily. */
    result->body.codes = MVM_STRING_TYPE_ROPE;
    result->body.graphs = length;
    
    return result;
}

/* Recursively populate the binary search table for strands.  Runs in O(log2(top-bottom+1)).
    Will *not* overflow the C stack. :) */
static MVMuint32 MVM_string_generate_strand_binary_search_table(MVMThreadContext *tc, MVMStrand *strands, MVMuint32 bottom, MVMuint32 top) {
    MVMuint32 mid;
    if (top == bottom) {
        strands[top].lower_index = top;
        strands[top].higher_index = top;
        return top;
    }
    mid = (top - bottom + 1)/2;
    strands[mid].lower_index =
        MVM_string_generate_strand_binary_search_table(tc, strands, bottom, mid - 1);
    strands[mid].higher_index =
        MVM_string_generate_strand_binary_search_table(tc, strands, mid, top);
    
    return mid;
}

/* Append one string to another. */
/* XXX inline parent's strands if it's a rope too */
MVMString * MVM_string_concatenate(MVMThreadContext *tc, MVMString *a, MVMString *b) {
    MVMString *result;
    MVMuint32 strand_count = 1;
    MVMStrand *strands;
    MVMuint64 index = 0;
    
    if (!IS_CONCRETE((MVMObject *)a) || !IS_CONCRETE((MVMObject *)b)) {
        MVM_exception_throw_adhoc(tc, "Concatenate needs concrete strings");
    }
    
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&a);
    result = (MVMString *)REPR(a)->allocate(tc, STABLE(a));
    MVM_gc_root_temp_pop(tc);
    
    /* there could be unattached combining chars at the beginning of b,
       so, XXX TODO handle this */
    result->body.codes = MVM_STRING_TYPE_ROPE;
    result->body.graphs = a->body.graphs + b->body.graphs;
    
    if (a->body.graphs)
        strand_count = 2;
    if (b->body.graphs)
        ++strand_count;
    strands = result->body.strands = calloc(sizeof(MVMStrand), strand_count);
    strand_count = 0;
    if (a->body.graphs) {
        strands[0].string = a;
        strands[0].string_offset = 0;
        strands[0].compare_offset = index;
        index = a->body.graphs;
        strand_count = 1;
    }
    if (b->body.graphs) {
        strands[strand_count].string = b;
        strands[strand_count].string_offset = 0;
        strands[strand_count].compare_offset = index;
        index += b->body.graphs;
        ++strand_count;
    }
    strands[strand_count].compare_offset = index;
    strands[0].higher_index =
        MVM_string_generate_strand_binary_search_table(tc, strands, 0, strand_count - 1);
    
    return result;
}

MVMString * MVM_string_repeat(MVMThreadContext *tc, MVMString *a, MVMint64 count) {
    MVMString *result;
    MVMint64 bkup_count = count;
    
    if (!IS_CONCRETE((MVMObject *)a)) {
        MVM_exception_throw_adhoc(tc, "repeat needs a concrete string");
    }
    
    if (count < 0)
        MVM_exception_throw_adhoc(tc, "repeat count (%lld) cannot be negative", count);
    
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&a);
    result = (MVMString *)REPR(a)->allocate(tc, STABLE(a));
    MVM_gc_root_temp_pop(tc);
    
    result->body.graphs = a->body.graphs * count;
    
    if (result->body.graphs) {
        MVMStrand *strands = result->body.strands = calloc(sizeof(MVMStrand), count + 1);
        result->body.codes = MVM_STRING_TYPE_ROPE;
        strands[count].compare_offset = result->body.graphs;
        
        while (count--) {
            strands[count].compare_offset = result->body.graphs - (count * a->body.graphs);
            strands[count].string = a;
        }
        strands[0].higher_index =
            MVM_string_generate_strand_binary_search_table(tc, strands, 0, bkup_count - 1);
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
        if (MVM_string_get_codepoint_at_nocheck(tc, a, index) == codepoint)
            return index;
    return -1;
}

/* Uppercases a string. */
MVMString * MVM_string_uc(MVMThreadContext *tc, MVMString *s) {
    MVMString *result;
    MVMuint64 i;
    
    if (!IS_CONCRETE((MVMObject *)s)) {
        MVM_exception_throw_adhoc(tc, "uc needs a concrete string");
    }
    
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&s);
    result = (MVMString *)REPR(s)->allocate(tc, STABLE(s));
    MVM_gc_root_temp_pop(tc);
    
    /* XXX Need to handle cases where character count varies. */
    result->body.codes  = s->body.codes;
    result->body.graphs = s->body.graphs;
    result->body.data.int32s = malloc(sizeof(MVMint32) * result->body.graphs);
    for (i = 0; i < s->body.graphs; i++) {
        result->body.data.int32s[i] = MVM_unicode_get_case_change(tc,
            MVM_string_get_codepoint_at_nocheck(tc, s, i), 0);
    }

    return result;
}

/* Lowercases a string. */
MVMString * MVM_string_lc(MVMThreadContext *tc, MVMString *s) {
    MVMString *result;
    MVMuint64 i;
    
    if (!IS_CONCRETE((MVMObject *)s)) {
        MVM_exception_throw_adhoc(tc, "lc needs a concrete string");
    }
    
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&s);
    result = (MVMString *)REPR(s)->allocate(tc, STABLE(s));
    MVM_gc_root_temp_pop(tc);
    
    /* XXX Need to handle cases where character count varies. */
    result->body.codes  = s->body.codes;
    result->body.graphs = s->body.graphs;
    result->body.data.int32s = malloc(sizeof(MVMint32) * result->body.graphs);
    for (i = 0; i < s->body.graphs; i++) {
        result->body.data.int32s[i] = MVM_unicode_get_case_change(tc,
            MVM_string_get_codepoint_at_nocheck(tc, s, i), 1);
    }

    return result;
}

/* Titlecases a string. */
MVMString * MVM_string_tc(MVMThreadContext *tc, MVMString *s) {
    MVMString *result;
    MVMuint64 i;
    
    if (!IS_CONCRETE((MVMObject *)s)) {
        MVM_exception_throw_adhoc(tc, "tc needs a concrete string");
    }
    
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&s);
    result = (MVMString *)REPR(s)->allocate(tc, STABLE(s));
    MVM_gc_root_temp_pop(tc);
    
    /* XXX Need to handle cases where character count varies. */
    result->body.codes  = s->body.codes;
    result->body.graphs = s->body.graphs;
    result->body.data.int32s = malloc(sizeof(MVMint32) * result->body.graphs);
    for (i = 0; i < s->body.graphs; i++) {
        result->body.data.int32s[i] = MVM_unicode_get_case_change(tc,
            MVM_string_get_codepoint_at_nocheck(tc, s, i), 2);
    }

    return result;
}

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
        portion = MVM_string_substring(tc, input, start, length);
        start += length + sep_length;
        
        if (portion->body.graphs) {
            REPR(result)->pos_funcs->push_boxed(tc, STABLE(result),
                result, OBJECT_BODY(result), (MVMObject *)portion);
        }
    }
    
    MVM_gc_root_temp_pop_n(tc, 4);
    
    return result;
}

MVMString * MVM_string_join(MVMThreadContext *tc, MVMObject *input, MVMString *separator) {
    MVMint64 elems, length = 0, index = -1, position;
    MVMString *portion, *result;
    MVMuint32 codes = 0, portion_index = 0;
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
        MVMObject *item = REPR(input)->pos_funcs->at_pos_boxed(tc, STABLE(input),
            input, OBJECT_BODY(input), index);
        
        /* allow null items in the array, I guess.. */
        if (!item)
            continue;
        if (REPR(item)->ID != MVM_REPR_ID_MVMString || !IS_CONCRETE(item))
            MVM_exception_throw_adhoc(tc, "join needs concrete strings only");
        
        portion = (MVMString *)item;
        if (portion->body.graphs)
            ++portion_index;
        length += portion->body.graphs + (index ? separator->body.graphs : 0);
        /* XXX codes += portion->body.codes + (index ? separator->body.codes : 0); */
    }
    
    result->body.graphs = length;
    /* XXX consider whether to coalesce combining characters
    if they cause new combining sequences to appear */
    /* XXX result->body.codes = codes; */
    
    index = -1;
    position = 0;
    strands = result->body.strands = calloc(sizeof(MVMStrand),
        separator->body.graphs ? 2 * portion_index : portion_index + 1);
    
    portion_index = 0;
    while (++index < elems) {
        MVMObject *item = REPR(input)->pos_funcs->at_pos_boxed(tc, STABLE(input),
            input, OBJECT_BODY(input), index);
        
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
    strands[portion_index].compare_offset = position;
    strands[0].higher_index =
        MVM_string_generate_strand_binary_search_table(tc, strands, 0, portion_index - 1);
    result->body.codes |= MVM_STRING_TYPE_ROPE;
    
    MVM_gc_root_temp_pop_n(tc, 3);
    
    if (result->body.graphs != position)
        MVM_exception_throw_adhoc(tc, "join had an internal error");
    
    return result;
}

/* returning nonzero means it found the char at the position specified in 'a' in 'b'.
    For character enumerations in regexes.  */
MVMint64 MVM_string_char_at_in_string(MVMThreadContext *tc, MVMString *a, MVMint64 offset, MVMString *b) {
    MVMuint32 index;
    MVMint32 codepoint;
    
    if (offset < 0 || offset >= a->body.graphs)
        return 0;
    
    codepoint = MVM_string_get_codepoint_at_nocheck(tc, a, offset);
    
    for (index = 0; index < b->body.graphs; index++)
        if (MVM_string_get_codepoint_at_nocheck(tc, b, index) == codepoint)
            return 1;
    return 0;
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
