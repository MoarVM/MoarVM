#define MVM_encoding_type_utf8 1
#define MVM_encoding_type_ascii 2
#define MVM_encoding_type_latin1 3
/* whether the encoding is valid. XXX make take tc as parameter */
#define ENCODING_VALID(enc) (((enc) >= MVM_encoding_type_utf8 && (enc) <= MVM_encoding_type_latin1) \
                            || (MVM_exception_throw_adhoc(tc, "invalid encoding type flag: %d", (enc)),1))

#define MVM_open_mode_read 1
#define MVM_open_mode_write 2
#define MVM_open_mode_append 3
#define MVM_open_mode_readpipe 4
#define MVM_open_mode_writepipe 5
#define MVM_get_apr_perms(p) (apr_fileperms_t)((p & 7) + (((p & 56) >> 3) * 16) + (((p & 448) >> 6) << 8))

#define MVM_stat_exists              0
#define MVM_stat_filesize            1
#define MVM_stat_isdir               2
#define MVM_stat_isreg               3
#define MVM_stat_isdev               4
#define MVM_stat_createtime          5
#define MVM_stat_accesstime          6
#define MVM_stat_modifytime          7
#define MVM_stat_changetime          8
#define MVM_stat_backuptime          9
#define MVM_stat_uid                10
#define MVM_stat_gid                11
#define MVM_stat_islnk              12
#define MVM_stat_platform_dev       -1
#define MVM_stat_platform_inode     -2
#define MVM_stat_platform_mode      -3
#define MVM_stat_platform_nlinks    -4
#define MVM_stat_platform_devtype   -5
#define MVM_stat_platform_blocksize -6
#define MVM_stat_platform_blocks    -7

/* substring consumer functions accept a state object in *data and
    consume a substring portion. Utilized by many of the string ops
    so traversal state can be maintained while applying a function to
    each subsection of a string. Accepts physical strings only. (non-
    ropes). Each function should branch for the ascii and wide modes
    so there doesn't have to be a function call on every codepoint.
    Returns nonzero if the traverser is supposed to stop traversal
    early. See how it's used in ops.c */
#define MVM_SUBSTRING_CONSUMER(name) MVMuint8 name(MVMThreadContext *tc, \
    MVMString *string, MVMStringIndex start, MVMStringIndex length, MVMStringIndex top_index, void *data)
typedef MVM_SUBSTRING_CONSUMER((*MVMSubstringConsumer));

/* number of grahemes in the string */
#define NUM_ROPE_GRAPHS(str) ((str)->body.num_strands ? (str)->body.strands[(str)->body.num_strands].graphs : 0)
#define NUM_GRAPHS(str) (IS_ROPE((str)) ? NUM_ROPE_GRAPHS((str)) : (str)->body.graphs)
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
#define _STRAND_DEPTH(str) ((str)->body.strands[(str)->body.num_strands].strand_depth)
/* the max number of levels deep the rope tree goes */
#define STRAND_DEPTH(str) ((IS_ROPE((str)) && NUM_ROPE_GRAPHS(str)) ? _STRAND_DEPTH((str)) : 0)
/* whether the rope is composed of only one segment of another string */
#define IS_ONE_STRING_ROPE(str) (IS_ROPE((str)) && (str)->body.num_strands == 1)

typedef struct _MVMConcatState {
    MVMuint32 some_state;
} MVMConcatState;

/* Character class constants (map to nqp::const::CCLASS_* values). */
#define MVM_CCLASS_ANY          65535
#define MVM_CCLASS_UPPERCASE    1
#define MVM_CCLASS_LOWERCASE    2
#define MVM_CCLASS_ALPHABETIC   4
#define MVM_CCLASS_NUMERIC      8
#define MVM_CCLASS_HEXADECIMAL  16
#define MVM_CCLASS_WHITESPACE   32
#define MVM_CCLASS_BLANK        256
#define MVM_CCLASS_CONTROL      512
#define MVM_CCLASS_PUNCTUATION  1024
#define MVM_CCLASS_ALPHANUMERIC 2048
#define MVM_CCLASS_NEWLINE      4096
#define MVM_CCLASS_WORD         8192

MVMCodepoint32 MVM_string_get_codepoint_at_nocheck(MVMThreadContext *tc, MVMString *a, MVMint64 index);
MVMint64 MVM_string_equal(MVMThreadContext *tc, MVMString *a, MVMString *b);
MVMint64 MVM_string_index(MVMThreadContext *tc, MVMString *haystack, MVMString *needle, MVMint64 start);
MVMint64 MVM_string_index_from_end(MVMThreadContext *tc, MVMString *haystack, MVMString *needle, MVMint64 start);
MVMString * MVM_string_concatenate(MVMThreadContext *tc, MVMString *a, MVMString *b);
MVMString * MVM_string_repeat(MVMThreadContext *tc, MVMString *a, MVMint64 count);
MVMString * MVM_string_substring(MVMThreadContext *tc, MVMString *a, MVMint64 start, MVMint64 length);
void MVM_string_say(MVMThreadContext *tc, MVMString *a);
void MVM_string_print(MVMThreadContext *tc, MVMString *a);
MVMint64 MVM_string_equal_at(MVMThreadContext *tc, MVMString *a, MVMString *b, MVMint64 offset);
MVMint64 MVM_string_equal_at_ignore_case(MVMThreadContext *tc, MVMString *a, MVMString *b, MVMint64 offset);
MVMint64 MVM_string_have_at(MVMThreadContext *tc, MVMString *a, MVMint64 starta, MVMint64 length, MVMString *b, MVMint64 startb);
MVMint64 MVM_string_get_codepoint_at(MVMThreadContext *tc, MVMString *a, MVMint64 index);
MVMint64 MVM_string_index_of_codepoint(MVMThreadContext *tc, MVMString *a, MVMint64 codepoint);
MVMString * MVM_string_uc(MVMThreadContext *tc, MVMString *s);
MVMString * MVM_string_lc(MVMThreadContext *tc, MVMString *s);
MVMString * MVM_string_tc(MVMThreadContext *tc, MVMString *s);
MVMString * MVM_decode_C_buffer_to_string(MVMThreadContext *tc, MVMObject *type_object, char *Cbuf, MVMint64 byte_length, MVMint64 encoding_flag);
unsigned char * MVM_encode_string_to_C_buffer(MVMThreadContext *tc, MVMString *s, MVMint64 start, MVMint64 length, MVMuint64 *output_size, MVMint64 encoding_flag);
MVMObject * MVM_string_split(MVMThreadContext *tc, MVMString *separator, MVMString *input);
MVMString * MVM_string_join(MVMThreadContext *tc, MVMString *separator, MVMObject *input);
MVMint64 MVM_string_char_at_in_string(MVMThreadContext *tc, MVMString *a, MVMint64 offset, MVMString *b);
MVMint64 MVM_string_offset_has_unicode_property_value(MVMThreadContext *tc, MVMString *s, MVMint64 offset, MVMint64 property_code, MVMint64 property_value_code);
void MVM_string_flatten(MVMThreadContext *tc, MVMString *s);
MVMString * MVM_string_escape(MVMThreadContext *tc, MVMString *s);
MVMString * MVM_string_flip(MVMThreadContext *tc, MVMString *s);
MVMint64 MVM_string_compare(MVMThreadContext *tc, MVMString *a, MVMString *b);
void MVM_string_cclass_init(MVMThreadContext *tc);
MVMint64 MVM_string_iscclass(MVMThreadContext *tc, MVMint64 cclass, MVMString *s, MVMint64 offset);
MVMint64 MVM_string_findcclass(MVMThreadContext *tc, MVMint64 cclass, MVMString *s, MVMint64 offset, MVMint64 count);
MVMint64 MVM_string_findnotcclass(MVMThreadContext *tc, MVMint64 cclass, MVMString *s, MVMint64 offset, MVMint64 count);
MVMuint8 MVM_find_encoding_by_name(MVMThreadContext *tc, MVMString *name);
