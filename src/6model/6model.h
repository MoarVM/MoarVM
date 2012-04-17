/* Stub a couple of types - definitions later. */
struct _MVMREPROps;
struct _MVMSTable;
struct _MVMString;

/* Boolification mode flags. */
#define MVM_BOOL_MODE_CALL_METHOD                   0
#define MVM_BOOL_MODE_UNBOX_INT                     1
#define MVM_BOOL_MODE_UNBOX_NUM                     2
#define MVM_BOOL_MODE_UNBOX_STR_NOT_EMPTY           3
#define MVM_BOOL_MODE_UNBOX_STR_NOT_EMPTY_OR_ZERO   4
#define MVM_BOOL_MODE_NOT_TYPE_OBJECT               5
#define MVM_BOOL_MODE_BIGINT                        6

/* Controls the way that type checks are performed. By default, if there is
 * a type check cache we treat it as definitive. However, it's possible to
 * declare that in the case the type check cache has no entry we should fall
 * back to asking the .HOW.type_check method (set TYPE_CHECK_CACHE_THEN_METHOD).
 * While a normal type check asks a value if it supports another type, the
 * TYPE_CHECK_NEEDS_ACCEPTS flag results in a call to .accepts_type on the
 * HOW of the thing we're checking the value against, giving it a chance to
 * decide answer. */
#define MVM_TYPE_CHECK_CACHE_DEFINITIVE    0
#define MVM_TYPE_CHECK_CACHE_THEN_METHOD   1
#define MVM_TYPE_CHECK_NEEDS_ACCEPTS       2
#define MVM_TYPE_CHECK_CACHE_FLAG_MASK     3

/* This flag is set if we consider the method cche authoritative. */
#define MVM_METHOD_CACHE_AUTHORITATIVE     4

/* Hint value to indicate the absence of an attribute lookup or method
 * dispatch hint. */
#define MVM_NO_HINT -1

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
typedef struct _MVMObject {
    /* The s-table for the object. */
    struct _MVMSTable *st;
    
    /* Identifier for the serialization context this object lives in, if any. */
    MVMuint16 sc_id;
    
    /* Identifier of the thread that currently owns the object, if any. If the
     * object is unshared, then this is always the creating thread. If it is
     * shared then it's whoever currently holds the mutex on it, or 0 if there
     * is no held mutex. */
    MVMuint16 cur_owner;
    
    /* Flags. */
    MVMuint32 flags;
    
    /* Forwarding pointer, for copying/compacting GC purposes. */
    struct _MVMObject *forwarder;
} MVMObject;

/* This is used to identify an attribute for various types of cache. */
typedef struct {
    MVMObject         *class_handle;   /* Class handle */
    struct _MVMString *attr_name;      /* Name of the attribute. */
    MVMint64           hint;           /* Hint for use in static/gradual typing. */
} MVMAttributeIdentifier;

/* Information that we hold if the type is declaring a scalar container
 * of some sort. */
typedef struct {
    MVMAttributeIdentifier  value_slot;
    MVMObject              *fetch_method;
} MVMContainerSpec;

/* How do we turn something of this type into a boolean? */
typedef struct {
    MVMObject *method;
    MVMuint32  mode;
} MVMBoolificationSpec;

/* The representation operations table. Note that representations are not
 * classes - there's no inheritance, so there's no polymprhism. If you know
 * a representation statically, you can statically dereferene the call to
 * the representation op in question. In the dynamic case, you have to go
 * following the pointer, however. */
typedef struct _MVMREPROps {
    /* XXX TODO: REPR ops. */
    
    /* The representation's name. */
    struct _MVMString *name;

    /* The representation's ID. */
    MVMint32 ID;
} MVMREPROps;

/* S-table, representing a meta-object/representation pairing. Note that the
 * items are grouped in hope that it will pack decently and do decently in
 * terms of cache lines. */
typedef struct _MVMSTable {
    /* The representation operation table. */
    MVMREPROps *REPR;
    
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
    MVMuint16 vtable_length;
    
    /* The length of the type check cache. */
    MVMuint16 type_check_cache_length;
    
    /* The type checking mode and method cache mode (see flags for this
     * above). */
    MVMuint16 mode_flags;
    
    /* An ID solely for use in caches that last a VM instance. Thus it
     * should never, ever be serialized and you should NEVER make a
     * type directory based upon this ID. Otherwise you'll create memory
     * leaks for anonymous types, and other such screwups. */
    MVMuint64 type_cache_id;

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

