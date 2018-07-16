/* Representation used by VM-level strings.
 *
 * Strings come in one of 3 forms today, with 1 expected future form:
 *   - 32-bit buffer of graphemes (Unicode codepoints or synthetic codepoints)
 *   - 8-bit buffer of codepoints that all fall in the ASCII range
 *   - Buffer of strands
 *   - (LATER) 8-bit buffer of codepoints with negatives as synthetics (we
 *     draw out a distinction with the ASCII range buffer because we can do
 *     some I/O simplifications when we know all is in the ASCII range).
 *
 * A buffer of strands represents a string made up of other non-strand
 * strings. That is, there's no recursive strands. This simplifies the
 * process of iteration enormously. A strand may refer to just part of
 * another string by specifying offsets. Furthermore, it may specify a
 * repetition count.
 */

/* Kinds of grapheme we may hold in a string. */
typedef MVMint32 MVMGrapheme32;
typedef MVMint8  MVMGraphemeASCII;
typedef MVMint8  MVMGrapheme8;       /* Future use */

/* What kind of data is a string storing? */
#define MVM_STRING_GRAPHEME_32      0
#define MVM_STRING_GRAPHEME_ASCII   1
#define MVM_STRING_GRAPHEME_8       2
#define MVM_STRING_STRAND           3

/* String index data type, for when we talk about indexes. */
typedef MVMuint32 MVMStringIndex;

/* Data type for a Unicode codepoint. */
typedef MVMint32 MVMCodepoint;

/* Maximum number of strands we will have. */
#define MVM_STRING_MAX_STRANDS  64

/* The body of a string. */
struct MVMStringBody {
    union {
        MVMGrapheme32    *blob_32;
        MVMGraphemeASCII *blob_ascii;
        MVMGrapheme8     *blob_8;
        MVMStringStrand  *strands;
        void             *any;
    } storage;
    MVMuint16 storage_type;
    MVMuint16 num_strands;
    MVMuint32 num_graphs;
    MVMHashv  cached_hash_code;
};

/* A strand of a string. */
struct MVMStringStrand {
    /* Another string that must be some kind of grapheme string. */
    MVMString *blob_string;

    /* Start and end indexes we refer to in the blob string. */
    MVMStringIndex start;
    MVMStringIndex end;

    /* Number of repetitions. */
    MVMuint32 repetitions;
};

/* The MVMString, with header and body. */
struct MVMString {
    MVMObject common;
    MVMStringBody body;
};

/* Function for REPR setup. */
const MVMREPROps * MVMString_initialize(MVMThreadContext *tc);
