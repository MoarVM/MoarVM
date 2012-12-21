/* Representation used by VM-level strings. */

struct _MVMStringBody;
typedef MVMuint8 MVMStrandIndex;
typedef MVMint32 MVMCodepoint32;
typedef MVMuint8 MVMCodepoint8;
typedef MVMuint64 MVMStringIndex;

/* An entry in the strands table of a rope. */
typedef struct _MVMStrand {
    /* The offset to compare the desired index against. */
    MVMStringIndex compare_offset;
    
    /* The string to which this strand refers. */
    struct _MVMString *string;
    
    /* The offset into the referred string. The length
        is calculated by subtracting the compare_offset
        from the compare_offset of the next entry. */
    MVMStringIndex string_offset;
    
    /* The destinations in the table to branch when
        binary searching for an offset into the string. 
        If they are equal, it means use the compare_offset
        string at that strand's row. */
    /* The index of the strand table for lower than. */
    /* on the first strand record, this value is the
        index of the last strand record. */
    MVMStrandIndex lower_index;
    
    /* The index of the strand table for higher than or equal to. */
    MVMStrandIndex higher_index;
} MVMStrand;

#define MVM_STRING_TYPE_INT32 0
#define MVM_STRING_TYPE_UINT8 1
#define MVM_STRING_TYPE_ROPE 2
#define MVM_STRING_TYPE_MASK 3

typedef struct _MVMStringBody {
    /* The string data (signed integer or unsigned char array
        of graphemes or strands). */
    union {
        /* Array of the codepoints in a string. */
        MVMCodepoint32 *int32s;
        
        /* An optimization so strings containing only codepoints
            that fit in 8 bits can take up only 1 byte each */
        MVMCodepoint8 *uint8s;
        
        /* For a non-rope that qualified as large during a particular
            GC run, a array of MVMStrand* that point to this large string.
            Built up during gc_mark */
        /* For a rope, An array of MVMStrand, each representing a
            segment of the string, up to the last one, which
            represents the end of the string and has values
            compare_offset=#graphs, string=null, string_offset=0,
            lower_index=0, higher_index=0.  The first one has
            compare_offset=0, and lower/higher_index=midpoint of
            strand array. */
        MVMStrand *strands;
    };
    
    /* The number of graphemes that make up the string
        (and in turn, the length of data in terms of the
        number of 32-bit integers or bytes it has, or when
        a rope, the value of the last MVMStrand's compare_offset). */
    MVMStringIndex graphs;
    
    /* The number of codepoints the string is
        made up of were it not in NFG form. Lazily populated and cached.
     */
    MVMStringIndex codes;
    
    /* Lowest 2 bits: type of string: int32, uint8, or Rope. */
    MVMuint8 flags;
} MVMStringBody;
typedef struct _MVMString {
    MVMObject common;
    MVMStringBody body;
} MVMString;

typedef struct _MVMStringStateContainers {
    MVMuint32 dummy;
} MVMStringStateContainers;

/* Function for REPR setup. */
MVMREPROps * MVMString_initialize(MVMThreadContext *tc);
