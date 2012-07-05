/* Represents data about a code point. */
typedef struct _MVMCodePoint {
    /* Name of the character in UTF-8. */
    char *name;
    
    /* Uppercase mapping. If it's a positive number, it's a simple
     * single character mapping. If it's negative, the absolute value
     * is an index into a complex uppercase mapping. */
    MVMint32 uc;
    
    /* Lowercase mapping. If it's a positive number, it's a simple
     * single character mapping. If it's negative, the absolute value
     * is an index into a complex lowercase mapping. */
    MVMint32 lc;
    
    /* Titlecase mapping. If it's a positive number, it's a simple
     * single character mapping. If it's negative, the absolute value
     * is an index into a complex titlecase mapping. */
    MVMint32 tc;
} MVMCodePoint;

/* Holds data about a Unicode plane. */
typedef struct _MVMUnicodePlane {
    /* Index into the codepoints list where this plane starts. */
    MVMuint32 first_codepoint;
    
    /* The total number of codepoints in the plane. */
    MVMuint32 num_codepoints;
} MVMUnicodePlane;
