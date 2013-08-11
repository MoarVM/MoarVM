/* A serialization context exists (optionally) per compilation unit.
 * It contains the declarative objects for the compilation unit, and
 * they are serialized if code is pre-compiled. */

struct MVMSerializationContextBody {
    /* The handle of this SC. */
    MVMString *handle;

    /* Description (probably the file name) if any. */
    MVMString *description;

    /* The root set of objects that live in this SC. */
    MVMObject *root_objects;

    /* The root set of STables that live in this SC. */
    MVMSTable **root_stables;
    MVMuint64   num_stables;
    MVMuint64   alloc_stables;

    /* The root set of code refs that live in this SC. */
    MVMObject *root_codes;

    /* XXX Repossession info. */

    /* Backlink to the (memory-managed) SC itself. */
    MVMSerializationContext *sc;

    /* Inline handle to the SCs hash (in MVMInstance). */
    UT_hash_handle hash_handle;
};

struct MVMSerializationContext {
    /* Normal header. */
    MVMObject common;

    /* Body is a level of indirection away to ease memory management of the
     * weak hash. */
    MVMSerializationContextBody *body;
};

/* Function for REPR setup. */
MVMREPROps * MVMSCRef_initialize(MVMThreadContext *tc);
