/* A serialization context exists (optionally) per compilation unit.
 * It contains the declarative objects for the compilation unit, and
 * they are serialized if code is pre-compiled. */

struct MVMSerializationContextBody {
    /* The handle of this SC. */
    MVMString *handle;

    /* Description (probably the file name) if any. */
    MVMString *description;

    /* The root set of objects that live in this SC. */
    MVMuint64   num_objects;
    MVMuint64   alloc_objects;
    MVMObject **root_objects;

    /* The root set of STables that live in this SC. */
    MVMSTable **root_stables;
    MVMuint64   num_stables;
    MVMuint64   alloc_stables;

    /* The root set of code refs that live in this SC. */
    MVMObject *root_codes;

    /* Repossession info. The following lists have matching indexes, each
     * representing the integer of an object in our root set along with the SC
     * that the object was originally from. */
    MVMObject *rep_indexes;
    MVMObject *rep_scs;

    /* Some things we deserialize are not directly in an SC root set, but
     * rather are owned by others. This is mostly thanks to Parrot legacy,
     * where not everything was a 6model object. This is an array in twos,
     * mapping an object to its owner. */
    MVMObject *owned_objects;

    /* Backlink to the (memory-managed) SC itself. If
     * this is null, it is unresolved. */
    MVMSerializationContext *sc;

    /* Inline handle to the SCs hash (in MVMInstance). */
    UT_hash_handle hash_handle;

    /* SC's index in the all_scs list in instance. */
    MVMuint32 sc_idx;

    /* Set to be true once some compilation unit claims the SC during resolution. */
    MVMuint32 claimed;

    /* Reference to the SerializationReader we'll use to deserialize objects,
     * if this is an SC that we loaded. */
    MVMSerializationReader *sr;

    /* Reentrant mutex protecting lazy deserialization of the SC. */
    MVMObject *mutex;
};

struct MVMSerializationContext {
    /* Normal header. */
    MVMObject common;

    /* Body is a level of indirection away to ease memory management of the
     * weak hash. */
    MVMSerializationContextBody *body;
};

/* Function for REPR setup. */
const MVMREPROps * MVMSCRef_initialize(MVMThreadContext *tc);
