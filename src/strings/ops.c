#include "moarvm.h"

#define GRAPHS_EQUAL(d1, d2, g) (memcmp(d1, d2, g * sizeof(MVMuint32)) == 0)

/* Compares two strings for equality. */
MVMint64 MVM_string_equal(MVMThreadContext *tc, MVMString *a, MVMString *b) {
    if (a->body.graphs != b->body.graphs)
        return 0;
    return (MVMint64)GRAPHS_EQUAL(a->body.data, b->body.data, a->body.graphs);
}

/* Returns the location of one string in another or -1  */
MVMint64 MVM_string_index(MVMThreadContext *tc, MVMString *haystack, MVMString *needle) {
    MVMint64 result = -1;
    size_t index    = 0;
    if (needle->body.graphs == 0 && haystack->body.graphs == 0)
        return 0; /* the empty strings are equal and start at zero */
    if (needle->body.graphs > haystack->body.graphs || needle->body.graphs < 1)
        return -1;
    /* brute force for now. */
    while (index <= haystack->body.graphs - needle->body.graphs) {
        if (GRAPHS_EQUAL(needle->body.data, haystack->body.data + index, needle->body.graphs)) {
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
    
    if (start < 0) {
        start += a->body.graphs;
        if (start < 0)
            start = 0;
    }
    
    if (start >= a->body.graphs)
        MVM_exception_throw_adhoc(tc, "Substring start offset cannot be past end of string");
    
    if (length < -1) /* -1 signifies go to the end of the string */
        MVM_exception_throw_adhoc(tc, "Substring length cannot be negative");
    
    if (length == -1)
        length = a->body.graphs - start;
    
    if (start + length > a->body.graphs)
        MVM_exception_throw_adhoc(tc, "Substring end cannot be past end of string");
    
    result = (MVMString *)REPR(a)->allocate(tc, STABLE(a));
    
    result->body.codes  = length; /* XXX TODO this is wrong */
    result->body.graphs = length;
    
    if (length > 0) {
        result->body.data = malloc(sizeof(MVMuint32) * length);
        memcpy(result->body.data, a->body.data + start, sizeof(MVMuint32) * length);
    }
    
    return result;
}

MVMString * MVM_string_concatenate(MVMThreadContext *tc, MVMString *a, MVMString *b) {
    MVMString *result = (MVMString *)REPR(a)->allocate(tc, STABLE(a));
    
    /* there could be unattached combining chars at the beginning of b,
       so, XXX TODO handle this */
    result->body.codes  = a->body.codes + b->body.codes;
    result->body.graphs = a->body.graphs + b->body.graphs;
    
    if (result->body.graphs) {
        result->body.data = malloc(sizeof(MVMuint32) * result->body.graphs);
        if (a->body.graphs)
            memcpy(result->body.data, a->body.data, sizeof(MVMuint32) * a->body.graphs);
        if (b->body.graphs)
            memcpy(result->body.data + a->body.graphs, b->body.data, sizeof(MVMuint32) * b->body.graphs);
    }
    
    return result;
}

MVMString * MVM_string_repeat(MVMThreadContext *tc, MVMString *a, MVMint64 count) {
    MVMString *result = (MVMString *)REPR(a)->allocate(tc, STABLE(a));
    size_t i = 0;
    
    if (count < 0)
        MVM_exception_throw_adhoc(tc, "repeat count cannot be negative");
    
    result->body.codes  = a->body.codes * count;
    result->body.graphs = a->body.graphs * count;
    
    if (count) {
        result->body.data = malloc(sizeof(MVMuint32) * result->body.graphs);
        while (count--)
            memcpy(result->body.data + (i++ * a->body.graphs), a->body.data, sizeof(MVMuint32) * a->body.graphs);
    }
    
    return result;
}

void MVM_string_say(MVMThreadContext *tc, MVMString *a) {
    MVMuint8 *utf8_encoded;
    MVMuint64 utf8_encoded_length;
    
    utf8_encoded = MVM_string_utf8_encode(tc, a, &utf8_encoded_length);
    
    fwrite(utf8_encoded, 1, utf8_encoded_length, stdout);
    printf("\n");
    
    free(utf8_encoded);
}

/* Test whether one string a starts with another b. */
MVMint64 MVM_string_starts_with(MVMThreadContext *tc, MVMString *a, MVMString *b) {
    return MVM_string_is_at(tc, a, b, 0);
}

/* Test whether one string a ends with another b. */
MVMint64 MVM_string_ends_with(MVMThreadContext *tc, MVMString *a, MVMString *b) {
    return MVM_string_is_at(tc, a, b, a->body.graphs - b->body.graphs);
}

/* Tests whether one string a has the other string b as a substring at that index */
MVMint64 MVM_string_is_at(MVMThreadContext *tc, MVMString *a, MVMString *b, MVMint64 offset) {
    if (a->body.graphs < b->body.graphs)
        return 0;
    if (offset < 0) {
        offset += a->body.graphs;
        if (offset < 0)
            offset = 0; /* XXX I think this is the right behavior here */
    }
    return (MVMint64)GRAPHS_EQUAL(a->body.data + (size_t)offset, b->body.data, b->body.graphs);
}
