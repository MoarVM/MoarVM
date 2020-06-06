struct MVMExtOpRecord {
    /* Used to query the extop registry. */
    MVMString *name;

    /* Resolved by the validator. */
    MVMOpInfo *info;

    /* The actual function executed by the interpreter.
     * Resolved by the validator. */
    MVMExtOpFunc *func;

    /* Tells the interpreter by how much to increment
     * the instruction pointer. */
    MVMuint16 operand_bytes;

    /* Indicates the JIT should not emit a call to this op, because it needs
     * to be used in an interpreter context. */
    MVMuint16 no_jit;

    /* Indicates the extop allocates and that its output is some allocated
     * object. Used by allocation profiling. */
    MVMuint16 allocating;

    /* Read from the bytecode stream. */
    MVMuint8 operand_descriptor[MVM_MAX_OPERANDS];

    /* Specialization function. */
    MVMExtOpSpesh *spesh;

    /* Discover facts for spesh. */
    MVMExtOpFactDiscover *discover;
};

/* How to release memory. */
typedef enum {
    MVM_DEALLOCATE_NOOP,
    MVM_DEALLOCATE_FREE,
    MVM_DEALLOCATE_UNMAP
} MVMDeallocate;

/* Representation for a compilation unit in the VM. */
struct MVMCompUnitBody {
    /* The start and size of the raw data for this compilation unit. */
    MVMuint8  *data_start;
    MVMuint32  data_size;

    /* Refers to the extops pointer below. Lives here for struct layout */
    MVMuint16       num_extops;

    /* See callsites, num_callsites, and orig_callsites below. */
    MVMuint16       max_callsite_size;

    /* The code objects for each frame, along with counts of frames. */
    MVMObject      **coderefs;
    MVMuint32        num_frames;    /* Total, inc. added by inliner. */
    MVMuint32        orig_frames;   /* Original from loading comp unit. */

    /* Special frames. */
    MVMStaticFrame  *mainline_frame;
    MVMStaticFrame  *main_frame;
    MVMStaticFrame  *load_frame;
    MVMStaticFrame  *deserialize_frame;

    /* The callsites in the compilation unit. */
    MVMCallsite **callsites;
    MVMuint32     num_callsites;
    MVMuint32     orig_callsites;

    /* The extension ops used by the compilation unit. */
    MVMExtOpRecord *extops;

    /* The string heap and number of strings. */
    MVMString **strings;
    MVMuint32   num_strings;
    MVMuint32   orig_strings;

    /* We decode strings on first use. Scanning through the string heap every
     * time to find where a string lives, however, would be extremely time
     * consuming. So, we keep a table that has the offsets into the string heap
     * every MVM_STRING_FAST_TABLE_SPAN strings. For example, were it 16, then
     * string_heap_fast_table[1] is where we'd look to find out how to locate
     * strings 16..31, then scanning through the string blob itself to get to
     * the string within that region. string_heap_fast_table_top contains the
     * top location in string_heap_fast_table that has been initialized so far.
     * It starts out at 0, which is safe even if we don't do anything since the
     * first string will be at the start of the blob anyway. Finally, we don't
     * do any concurrency control over this table, since all threads will be
     * working towards the same result anyway. Duplicate work occasionally will
     * almost always be cheaper than unrequired synchronization every time. A
     * memory barrier before updating string_heap_fast_table_top makes sure we
     * never have its update getting moved ahead of writes into the table. */
    MVMuint32 *string_heap_fast_table;
    MVMuint32  string_heap_fast_table_top;

    /* Refers to serialized below. sneaked in here to optimize struct layout */
    MVMint32  serialized_size;

    MVMuint8  *string_heap_start;
    MVMuint8  *string_heap_read_limit;

    /* Serialized data, if any. */
    /* For its size, see serialized_size above. */
    MVMuint8 *serialized;

    /* Array of the resolved serialization contexts, and how many we
     * have. A null in the list indicates not yet resolved */
    MVMSerializationContext **scs;
    MVMuint32                 num_scs;

    /* How we should deallocate data_start. */
    MVMDeallocate deallocate;

    /* List of serialization contexts in need of resolution. This is an
     * array of string handles; its length is determined by num_scs above.
     * once an SC has been resolved, the entry on this list is NULLed. If
     * all are resolved, this pointer itself becomes NULL. */
    MVMSerializationContextBody **scs_to_resolve;

    /* List of SC handle string indexes. */
    MVMint32 *sc_handle_idxs;

    /* HLL configuration for this compilation unit. */
    MVMHLLConfig *hll_config;
    MVMString    *hll_name;

    /* Filename, if any, that we loaded it from. */
    MVMString *filename;

    /* Handle, if any, associated with a mapped file. */
    void *handle;

    /* Unmanaged (so not GC-aware) mutex taken if we want to add extra string,
     * callsite, extop, or coderef constants to the pools. This is done in
     * some cases of cross-compilation-unit inlining. We are never at risk of
     * recursion on this mutex, and since spesh can never GC it's important we
     * do not use a GC-aware mutex, which could trigger GC. */
    uv_mutex_t *inline_tweak_mutex;

    /* MVMReentrantLock to be taken when we want to finish deserializing a
     * frame inside of the compilation unit. */
    MVMObject *deserialize_frame_mutex;

    /* Version of the bytecode format we deserialized this comp unit from. */
    MVMuint16 bytecode_version;

    /* Was a frame in this compilation unit invoked yet? */
    MVMuint8 invoked;
};
struct MVMCompUnit {
    MVMObject common;
    MVMCompUnitBody body;
};

/* Strings per entry in the fast table; see above for details. */
#define MVM_STRING_FAST_TABLE_SPAN 16

struct MVMLoadedCompUnitName {
    /* Loaded filename. */
    MVMString *filename;

    /* Inline handle to the loaded filenames hash (in MVMInstance). */
    UT_hash_handle hash_handle;
};

/* Function for REPR setup. */
const MVMREPROps * MVMCompUnit_initialize(MVMThreadContext *tc);
