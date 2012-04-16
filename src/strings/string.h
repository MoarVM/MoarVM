typedef struct _MVMString {
    /* The string data. */
    MVMint32 *data;
    
    /* The number of graphemes that make up the string (and in turn, the
     * length of data in terms of the number of 32-bit integers it has). */
    MVMuint32 graphs;
    
    /* The number of codepoints the string is made up of were it not in
     * NFG form. */
    MVMuint32 codes;
    
    /* XXX TODO: table for deriveds. */
} MVMString;
