/* Representation for a compilation unit in the VM. */
struct MVMCompUnitBody {
    /* The start and size of the raw data for this compilation unit. */
    MVMuint16  *data_start;
    MVMuint32  data_size;

    /* The various static frames in the compilation unit, along with a
     * code object for each one. */
    MVMStaticFrame **frames;
    MVMObject      **coderefs;
    MVMuint32        num_frames;
    MVMStaticFrame  *main_frame;
    MVMStaticFrame  *load_frame;
    MVMStaticFrame  *deserialize_frame;

    /* The callsites in the compilation unit. */
    MVMCallsite **callsites;
    MVMuint32     num_callsites;
    MVMuint16     max_callsite_size;

    /* The string heap and number of strings. */
    MVMString **strings;
    MVMuint32   num_strings;

    /* Array of the resolved serialization contexts, and how many we
     * have. A null in the list indicates not yet resolved */
    MVMSerializationContext **scs;
    MVMuint32                 num_scs;

    /* List of serialization contexts in need of resolution. This is an
     * array of string handles; its length is determined by num_scs above.
     * once an SC has been resolved, the entry on this list is NULLed. If
     * all are resolved, this pointer itself becomes NULL. */
    MVMSerializationContextBody **scs_to_resolve;

    /* HLL configuration for this compilation unit. */
    MVMHLLConfig *hll_config;
    MVMString    *hll_name;

    /* Filename, if any, that we loaded it from. */
    MVMString *filename;
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
MVMREPROps * MVMCompUnit_initialize(MVMThreadContext *tc);
