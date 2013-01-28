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
    
    /* The callsites in the compilation unit. */
    MVMCallsite **callsites;
    MVMuint32     num_callsites;
    MVMuint16     max_callsite_size;
    
    /* The string heap and number of strings. */
    struct _MVMString **strings;
    MVMuint32           num_strings;
    
    /* GC run sequence number during which we last saw this frame. */
    MVMuint32 gc_seq_number;
    
    /* HLL configuration for this compilation unit. */
    struct _MVMHLLConfig *hll_config;
    struct _MVMString    *hll_name;
} MVMCompUnit;

MVMCompUnit * MVM_cu_map_from_file(MVMThreadContext *tc, char *filename);
