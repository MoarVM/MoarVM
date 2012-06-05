/* Represents a compilation unit; essentially, the runtime representation
 * of a MAST::CompUnit. It may be mapped in from a file, created in memory
 * or something else. */
typedef struct _MVMCompUnit {
    /* The APR memory pool associated with this compilation unit,
     * if we need one. */
    apr_pool_t *pool;
    
    /* The start and size of the raw data for this compilation unit. */
    MVMuint8  *data_start;
    MVMuint32  data_size;
    
    /* The various static frames in the compilation unit, along with a
     * code object for each one. */
    MVMStaticFrame **frames;
    MVMObject      **coderefs;
    MVMuint32        num_frames;
    
    /* The string heap and number of strings. */
    struct _MVMString **strings;
    MVMuint32           num_strings;
} MVMCompUnit;

MVMCompUnit * MVM_cu_map_from_file(MVMThreadContext *tc, char *filename);
