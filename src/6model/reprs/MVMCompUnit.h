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

    /* The various static frames in the compilation unit, along with a
     * code object for each one. */
    MVMStaticFrame **frames;
    MVMObject      **coderefs;
    MVMuint32        num_frames;    /* Total, inc. added by inliner. */
    MVMuint32        orig_frames;   /* Original from loading comp unit. */

    /* Special frames. */
    MVMStaticFrame  *main_frame;
    MVMStaticFrame  *load_frame;
    MVMStaticFrame  *deserialize_frame;

    /* The callsites in the compilation unit. */
    MVMCallsite **callsites;
    MVMuint32     num_callsites;
    MVMuint32     orig_callsites;
    MVMuint16     max_callsite_size;

    /* The extension ops used by the compilation unit. */
    MVMuint16       num_extops;
    MVMExtOpRecord *extops;

    /* The string heap and number of strings. */
    MVMString **strings;
    MVMuint32   num_strings;
    MVMuint32   orig_strings;

    /* Serialized data, if any. */
    MVMint32  serialized_size;
    char     *serialized;

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

    /* MVMReentrantLock to be taken if we want to add extra string,
     * callsite, or coderef constants to the pools (done during
     * inlining) or when we finish deserializing a frame, thus
     * vivifying its lexicals. */
    MVMObject *update_mutex;

    /* Version of the bytecode format we deserialized this comp unit from. */
    MVMuint16 bytecode_version;
};
struct MVMCompUnit {
    MVMObject common;
    MVMCompUnitBody body;
};

struct MVMLoadedCompUnitName {
    /* Loaded filename. */
    MVMString *filename;

    /* Inline handle to the loaded filenames hash (in MVMInstance). */
    UT_hash_handle hash_handle;
};

/* Function for REPR setup. */
const MVMREPROps * MVMCompUnit_initialize(MVMThreadContext *tc);
