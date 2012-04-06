/* Stub a couple of types - definitions later. */
typedef struct _MVMREPROps MVMREPROps;
typedef struct _MVMSTable  MVMSTable;

/* Flags that may be set on an object. */
typedef enum {
    /* Is concrete instance. */
    MVMOF_CONCRETE = 1,
    
    /* Has already been seen once in GC nursery. */
    MVMOF_NURSERY_SEEN = 2,

    /* Has been promoted to the old generation. */
    MVMOF_SECOND_GEN = 4,
    
    /* Is shared - that is, more than one thread knows about it. */
    MVMOF_SHARED = 8
} MVMObjectFlags;

/* The common things every object has. */
typedef struct {
    /* The s-table for the object. */
    MVMSTable *st;
    
    /* Identifier for the serialization context this object lives in, if any. */
    uint16 sc_id;
    
    /* Identifier of the thread that currently owns the object, if any. If the
     * object is unshared, then this is always the creating thread. If it is
     * shared then it's whoever currently holds the mutex on it, or 0 if there
     * is no held mutex. */
    uint16 cur_owner;
    
    /* Flags. */
    uint32 flags;
    
    /* Forwarding pointer, for copying/compacting GC purposes. */
    MVMObject *forwarder;
} MVMObject;

/* S-table, representing a meta-object/representation pairing. Note that the
 * items are grouped in hope that it will pack decently and do decently in
 * terms of cache lines. */
typedef struct {
    /* The representation operation table. */
    REPROps *REPR;
    
    /* Any data specific to this type that the REPR wants to keep. */
    void *REPR_data;

    /* The meta-object. */
    MVMObject *HOW;

    /* The type-object. */
    MVMObject *WHAT;
    
    /* By-name method dispatch cache. */
    MVMObject *method_cache;

    /* The computed v-table for static dispatch. */
    MVMObject **vtable;
    
    /* Array of type objects. If this is set, then it is expected to contain
     * the type objects of all types that this type is equivalent to (e.g.
     * all the things it isa and all the things it does). */
    MVMObject **type_check_cache;
    
    /* The length of the v-table. */
    uint16 vtable_length;
    
    /* The length of the type check cache. */
    uint16 type_check_cache_length;
    
    /* The type checking mode and method cache mode (see flags for this
     * above). */
    uint16 mode_flags;

    /* An ID solely for use in caches that last a VM instance. Thus it
     * should never, ever be serialized and you should NEVER make a
     * type directory based upon this ID. Otherwise you'll create memory
     * leaks for anonymous types, and other such screwups. */
    uint16 type_cache_id;
    
    /* If this is a container, then this contains information needed in
     * order to fetch the value in it. If not, it'll be null, which can
     * be taken as a "not a container" indication. */
    MVMContainerSpec *container_spec;
    
    /* Information - if any - about how we can turn something of this type
     * into a boolean. */
    MVMBoolificationSpec *boolification_spec;
    
    /* The underlying package stash. */
    MVMObject *WHO;
} MVMSTable;
