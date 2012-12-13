#define MVM_encoding_type_utf8 1
#define MVM_encoding_type_ascii 2
#define MVM_encoding_type_latin1 3
/* whether the encoding is valid. XXX make take tc as parameter */
#define ENCODING_VALID(enc) (((enc) >= MVM_encoding_type_utf8 && (enc) <= MVM_encoding_type_latin1) \
                            || (MVM_exception_throw_adhoc(tc, "invalid encoding type flag: %d", (enc)),1))

/* substring consumer functions accept a state object in *data and
    consume a substring portion. Utilized by many of the string ops
    so traversal state can be maintained while applying a function to
    each subsection of a string. Accepts physical strings only. (non-
    ropes). Each function should branch for the ascii and wide modes
    so there doesn't have to be a function call on every codepoint.
    Returns nonzero if the traverser is supposed to stop traversal
    early. See how it's used in ops.c */
#define MVM_SUBSTRING_CONSUMER(name) MVMuint8 name(MVMThreadContext *tc, \
    MVMString *string, MVMStringIndex start, MVMStringIndex length, void *data)
typedef MVM_SUBSTRING_CONSUMER((*MVMSubstringConsumer));

/* gets the code that defines the type of string. More things could be
    stored in flags later. */
#define STR_FLAGS(str) (((MVMString *)(str))->body.flags & MVM_STRING_TYPE_MASK)
/* whether it's a string of full-blown 4-byte (positive and/or negative)
    codepoints. */
#define IS_WIDE(str) (STR_FLAGS((str)) == MVM_STRING_TYPE_INT32)
/* whether it's a string of only codepoints that fit in 8 bits, so
    are stored compactly in a byte array. */
#define IS_ASCII(str) (STR_FLAGS((str)) == MVM_STRING_TYPE_UINT8)
/* whether it's a composite of strand segments */
#define IS_ROPE(str) (STR_FLAGS((str)) == MVM_STRING_TYPE_ROPE)
/* potentially lvalue version of the below */
#define _STRAND_DEPTH(str) ((str)->body.strands[(str)->body.strands->lower_index].lower_index)
/* the max number of levels deep the rope tree goes */
#define STRAND_DEPTH(str) ((IS_ROPE(str) && (str)->body.graphs) ? _STRAND_DEPTH(str) : 0)
/* whether the rope is composed of only one segment of another string */
#define IS_SUBSTRING(str) (IS_ROPE(str) && (str)->body.strands[1].compare_offset == (str)->body.graphs)

typedef struct _MVMConcatState {
    MVMuint32 some_state;
} MVMConcatState;

MVMCodepoint32 MVM_string_get_codepoint_at_nocheck(MVMThreadContext *tc, MVMString *a, MVMint64 index);
MVMStrandIndex MVM_string_rope_strands_size(MVMThreadContext *tc, MVMStringBody *body);
MVMint64 MVM_string_equal(MVMThreadContext *tc, MVMString *a, MVMString *b);
MVMint64 MVM_string_index(MVMThreadContext *tc, MVMString *a, MVMString *b, MVMint64 start);
MVMString * MVM_string_concatenate(MVMThreadContext *tc, MVMString *a, MVMString *b);
MVMString * MVM_string_repeat(MVMThreadContext *tc, MVMString *a, MVMint64 count);
MVMString * MVM_string_substring(MVMThreadContext *tc, MVMString *a, MVMint64 start, MVMint64 length);
void MVM_string_say(MVMThreadContext *tc, MVMString *a);
MVMint64 MVM_string_equal_at(MVMThreadContext *tc, MVMString *a, MVMString *b, MVMint64 offset);
MVMint64 MVM_string_have_at(MVMThreadContext *tc, MVMString *a, MVMint64 starta, MVMint64 length, MVMString *b, MVMint64 startb);
MVMint64 MVM_string_get_codepoint_at(MVMThreadContext *tc, MVMString *a, MVMint64 index);
MVMint64 MVM_string_index_of_codepoint(MVMThreadContext *tc, MVMString *a, MVMint64 codepoint);
MVMString * MVM_string_uc(MVMThreadContext *tc, MVMString *s);
MVMString * MVM_string_lc(MVMThreadContext *tc, MVMString *s);
MVMString * MVM_string_tc(MVMThreadContext *tc, MVMString *s);
MVMString * MVM_decode_C_buffer_to_string(MVMThreadContext *tc, MVMObject *type_object, char *Cbuf, MVMint64 byte_length, MVMint64 encoding_flag);
char * MVM_encode_string_to_C_buffer(MVMThreadContext *tc, MVMString *s, MVMint64 start, MVMint64 length, MVMuint64 *output_size, MVMint64 encoding_flag);
MVMObject * MVM_string_split(MVMThreadContext *tc, MVMString *input, MVMObject *type_object, MVMString *separator);
MVMString * MVM_string_join(MVMThreadContext *tc, MVMObject *input, MVMString *separator);
MVMint64 MVM_string_char_at_in_string(MVMThreadContext *tc, MVMString *a, MVMint64 offset, MVMString *b);
void MVM_string_flatten(MVMThreadContext *tc, MVMString *s);
