/* Boolification mode flags. */
#define MVM_BOOL_MODE_CALL_METHOD                   0
#define MVM_BOOL_MODE_UNBOX_INT                     1
#define MVM_BOOL_MODE_UNBOX_NUM                     2
#define MVM_BOOL_MODE_UNBOX_STR_NOT_EMPTY           3
#define MVM_BOOL_MODE_UNBOX_STR_NOT_EMPTY_OR_ZERO   4
#define MVM_BOOL_MODE_NOT_TYPE_OBJECT               5
#define MVM_BOOL_MODE_BIGINT                        6
#define MVM_BOOL_MODE_ITER                          7
#define MVM_BOOL_MODE_HAS_ELEMS                     8

/* Controls the way that type checks are performed. By default, if there is
 * a type check cache we treat it as definitive. However, it's possible to
 * declare that in the case the type check cache has no entry we should fall
 * back to asking the .HOW.type_check method (set TYPE_CHECK_CACHE_THEN_METHOD).
 * While a normal type check asks a value if it supports another type, the
 * TYPE_CHECK_NEEDS_ACCEPTS flag results in a call to .accepts_type on the
 * HOW of the thing we're checking the value against, giving it a chance to
 * decide answer. These are set as the lower bits of mode_flags in MVMSTable. */
#define MVM_TYPE_CHECK_CACHE_DEFINITIVE    0
#define MVM_TYPE_CHECK_CACHE_THEN_METHOD   1
#define MVM_TYPE_CHECK_NEEDS_ACCEPTS       2
#define MVM_TYPE_CHECK_CACHE_FLAG_MASK     3

/* This STable mode flag is set if we consider the method cache authoritative. */
#define MVM_METHOD_CACHE_AUTHORITATIVE     4

/* This STable mode flag is set if the type needs finalization. */
#define MVM_FINALIZE_TYPE                  8

/* This STable mode flag is set if the type is parametric (and so can be
 * parameterized). */
#define MVM_PARAMETRIC_TYPE                 16

/* This STable mode flag is set if the type is a parameterization of some
 * parametric type. */
#define MVM_PARAMETERIZED_TYPE              32

/* HLL type roles. */
#define MVM_HLL_ROLE_NONE                   0
#define MVM_HLL_ROLE_INT                    1
#define MVM_HLL_ROLE_NUM                    2
#define MVM_HLL_ROLE_STR                    3
#define MVM_HLL_ROLE_ARRAY                  4
#define MVM_HLL_ROLE_HASH                   5
#define MVM_HLL_ROLE_CODE                   6

/* Hint value to indicate the absence of an attribute lookup or method
 * dispatch hint. */
#define MVM_NO_HINT -1

/* This data structure describes what storage a given representation
 * needs if something of that representation is to be embedded in
 * another place. For any representation that expects to be used
 * as a kind of reference type, it will just want to be a pointer.
 * But for other things, they would prefer to be "inlined" into
 * the object. */
struct MVMStorageSpec {
    /* 0 if this is to be referenced, anything else otherwise. */
    MVMuint16 inlineable;

    /* For things that want to be inlined, the number of bits of
     * storage they need and what kind of byte-boundary they want to
     * be aligned to. Ignored otherwise. */
    MVMuint16 bits;
    MVMuint16 align;

    /* For things that are inlined, if they are just storage of a
     * primitive type and can unbox, this says what primitive type
     * that they unbox to. */
    MVMuint16 boxed_primitive;

    /* The types that this one can box/unbox to. */
    MVMuint16 can_box;

    /* For ints, whether it's an unsigned value. */
    MVMuint8 is_unsigned;
};

/* Inlined or not. */
#define MVM_STORAGE_SPEC_REFERENCE      0
#define MVM_STORAGE_SPEC_INLINED        1

/* Possible options for boxed primitives. */
#define MVM_STORAGE_SPEC_BP_NONE        0
#define MVM_STORAGE_SPEC_BP_INT         1
#define MVM_STORAGE_SPEC_BP_NUM         2
#define MVM_STORAGE_SPEC_BP_STR         3

/* can_box bit field values. */
#define MVM_STORAGE_SPEC_CAN_BOX_INT    1
#define MVM_STORAGE_SPEC_CAN_BOX_NUM    2
#define MVM_STORAGE_SPEC_CAN_BOX_STR    4
#define MVM_STORAGE_SPEC_CAN_BOX_MASK   7

/* Flags that may be set on any collectable. */
typedef enum {
    /* Is a type object (and thus not a concrete instance). */
    MVM_CF_TYPE_OBJECT = 1,

    /* Is an STable. */
    MVM_CF_STABLE = 2,

    /* Has already been seen once in GC nursery. */
    MVM_CF_NURSERY_SEEN = 4,

    /* Has been promoted to the old generation. */
    MVM_CF_SECOND_GEN = 8,

    /* Is shared - that is, more than one thread knows about it. */
    MVM_CF_SHARED = 16,

    /* Has already been added to the gen2 aggregates pointing to nursery
     * objects list. */
    MVM_CF_IN_GEN2_ROOT_LIST = 32,

    /* A full GC run has found this object to be live. */
    MVM_CF_GEN2_LIVE = 64,

    /* This object in fromspace is live with a valid forwarder. */
    /* TODO - should be possible to use the same bit for this and GEN2_LIVE. */
    MVM_CF_FORWARDER_VALID = 128,

    /* Have we allocated memory to store a serialization index? */
    MVM_CF_SERIALZATION_INDEX_ALLOCATED = 256,

    /* Have we arranged a persistent object ID for this object? */
    MVM_CF_HAS_OBJECT_ID = 512,

    /* Have we flagged this object as something we must never repossess? */
    /* Note: if you're hunting for a flag, some day in the future when we
     * have used them all, this one is easy enough to eliminate by having the
     * tiny number of objects marked this way in a remembered set. */
    MVM_CF_NEVER_REPOSSESS = 1024
} MVMCollectableFlags;

#ifdef MVM_USE_OVERFLOW_SERIALIZATION_INDEX
struct MVMSerializationIndex {
    MVMuint32 sc_idx;
    MVMuint32 idx;
};
#endif

/* Things that every GC-collectable entity has. These fall into two
 * categories:
 *   * MVMObject - objects. Almost everything is one of these.
 *   * MVMSTable - shared tables; one per (HOW, REPR) pairing.
 * Only the first can vary in size, and even then only if it's not a
 * type object.
 */
struct MVMCollectable {
    /* Put this union first, as these pointers/indexes are relatively "cold",
       whereas "flags" is accessed relatively frequently, as are the fields
       that follow in the structures into which MVMCollectable is embedded.
       Shrinking the size of the active part of the structure slightly
       increases the chance that it fits into the CPU's L1 cache, which is a
       "free" performance win. */
    union {
        /* Forwarding pointer, for copying/compacting GC purposes. */
        MVMCollectable *forwarder;
        /* Index of the serialization context this collectable lives in, if
         * any, and then location within that. */
#ifdef MVM_USE_OVERFLOW_SERIALIZATION_INDEX
        struct {
            MVMuint16 sc_idx;
            MVMuint16 idx;
        } sc;
        struct MVMSerializationIndex *sci;
#else
        struct {
            MVMuint32 sc_idx;
            MVMuint32 idx;
        } sc;
#endif
        /* Used to chain STables queued to be freed. */
        MVMSTable *st;
    } sc_forward_u;

    /* Identifier of the thread that currently owns the object, if any. If the
     * object is unshared, then this is always the creating thread. If it is
     * shared then it's whoever currently holds the mutex on it, or 0 if there
     * is no held mutex. */
    MVMuint32 owner;

    /* Collectable flags (see MVMCollectableFlags). */
    MVMuint16 flags;

    /* Object size, in bytes. */
    MVMuint16 size;
};
#ifdef MVM_USE_OVERFLOW_SERIALIZATION_INDEX
#  define MVM_DIRECT_SC_IDX_SENTINEL 0xFFFF
#else
#  define MVM_DIRECT_SC_IDX_SENTINEL ~0
#endif

/* The common things every object has.
 *
 * NB - the assumption that MVMObject* can be safely cast into
 * MVMCollectable* is spread throughout the codebase, as well
 * as used directly in JIT. Thus, nothing may preceed the header!
 */
struct MVMObject {
    /* Commonalities that all collectable entities have. */
    MVMCollectable header;

    /* The s-table for the object. */
    MVMSTable *st;
};

/* An dummy object, mostly used to compute the offset of the data part of
 * a 6model object. */
struct MVMObjectStooge {
    MVMObject common;
    void *data;
};

/* This is used to identify an attribute for various types of cache. */
struct MVMAttributeIdentifier {
    MVMObject         *class_handle;   /* Class handle */
    MVMString         *attr_name;      /* Name of the attribute. */
    MVMint64           hint;           /* Hint for use in static/gradual typing. */
};

/* How do we turn something of this type into a boolean? */
struct MVMBoolificationSpec {
    MVMObject *method;
    MVMuint32  mode;
};

/* S-table, representing a meta-object/representation pairing. Note that the
 * items are grouped in hope that it will pack decently and do decently in
 * terms of cache lines. */
struct MVMSTable {
    /* Commonalities that all collectable entities have. */
    MVMCollectable header;

    /* The representation operation table. */
    const MVMREPROps *REPR;

    /* Any data specific to this type that the REPR wants to keep. */
    void *REPR_data;

    /* The size of an object of this type in bytes, including the
     * header. */
    MVMuint32 size;

    /* The length of the type check cache. */
    MVMuint16 type_check_cache_length;

    /* The type checking mode and method cache mode (see flags for this
     * above). */
    MVMuint16 mode_flags;

    /* Array of type objects. If this is set, then it is expected to contain
     * the type objects of all types that this type is equivalent to (e.g.
     * all the things it isa and all the things it does). */
    MVMObject **type_check_cache;

    /* By-name method dispatch cache. */
    MVMObject *method_cache;

    /* An ID solely for use in caches that last a VM instance. Thus it
     * should never, ever be serialized and you should NEVER make a
     * type directory based upon this ID. Otherwise you'll create memory
     * leaks for anonymous types, and other such screwups. */
    MVMuint64 type_cache_id;

    /* If this is a container, then this contains information needed in
     * order to fetch the value in it. If not, it'll be null, which can
     * be taken as a "not a container" indication. */
    const MVMContainerSpec *container_spec;

    /* Data that the container spec may need to function. */
    /* Any data specific to this type that the REPR wants to keep. */
    void *container_data;

    /* Information - if any - about how we can turn something of this type
     * into a boolean. */
    MVMBoolificationSpec *boolification_spec;
    
    /* The HLL that this type is owned by, if any. */
    MVMHLLConfig *hll_owner;
    
    /* The role that the type plays in the HLL, if any. */
    MVMint64 hll_role;

    /* Invocation handler. If something tries to invoke this object,
     * whatever hangs off this function pointer gets invoked to handle
     * the invocation. If it's a call into C code it may do stuff right
     * off the bat. However, normally it will do whatever is needed to
     * arrange for setting up a callframe, twiddle the interpreter's
     * PC as needed and return. */
    void (*invoke) (MVMThreadContext *tc, MVMObject *invokee,
        MVMCallsite *callsite, MVMRegister *args);

    /*
     * If this is invokable, then this contains information needed to
     * figure out how to invoke it. If not, it'll be null.
     */
    MVMInvocationSpec *invocation_spec;

    /* The type-object. */
    MVMObject *WHAT;

    /* The underlying package stash. */
    MVMObject *WHO;

    /* The meta-object. */
    MVMObject *HOW;

    /* Parametricity. Mode flags indicate what, if any, of this union is valid. */
    union {
        struct {
            /* The code object to use to produce a new parameterization. */
            MVMObject *parameterizer;

            /* Lookup table of existing parameterizations. For now, just a VM
             * array with alternating pairs of [arg array], object. Could in
             * the future we something lower level or hashy; we've yet to see
             * how hot-path lookups end up being in reality. */
            MVMObject *lookup;
        } ric;
        struct {
            /* The type that we are a parameterization of. */
            MVMObject *parametric_type;

            /* Our type parameters. */
            MVMObject *parameters;
        } erized;
    } paramet;

    /* We lazily deserialize HOW; this is the SC and index if needed. */
    MVMSerializationContext *HOW_sc;
    MVMuint32                HOW_idx;

    /* Also info we need to lazily deserialize the method cache. */
    MVMuint32                method_cache_offset;
    MVMSerializationContext *method_cache_sc;
};

/* The representation operations table. Note that representations are not
 * classes - there's no inheritance, so there's no polymorphism. If you know
 * a representation statically, you can statically dereference the call to
 * the representation op in question. In the dynamic case, you have to go
 * following the pointer, however. */
struct MVMREPROps_Attribute {
    /* Gets the current value for an attribute and places it in the passed
     * location (specified as a register). Expects to be passed a kind flag
     * that matches the kind of the attribute that is being fetched. */
    void (*get_attribute) (MVMThreadContext *tc, MVMSTable *st,
        MVMObject *root, void *data, MVMObject *class_handle, MVMString *name,
        MVMint64 hint, MVMRegister *result, MVMuint16 kind);

    /* Binds the given object or value to the specified attribute. The
     * kind flag specifies the type of value being passed to be bound.*/
    void (*bind_attribute) (MVMThreadContext *tc, MVMSTable *st,
        MVMObject *root, void *data, MVMObject *class_handle, MVMString *name,
        MVMint64 hint, MVMRegister value, MVMuint16 kind);

    /* Gets the hint for the given attribute ID. */
    MVMint64 (*hint_for) (MVMThreadContext *tc, MVMSTable *st,
        MVMObject *class_handle, MVMString *name);

    /* Checks if an attribute has been initialized. */
    MVMint64 (*is_attribute_initialized) (MVMThreadContext *tc, MVMSTable *st,
        void *data, MVMObject *class_handle, MVMString *name,
        MVMint64 hint);
};
struct MVMREPROps_Boxing {
    /* Used with boxing. Sets an integer value, for representations that
     * can hold one. */
    void (*set_int) (MVMThreadContext *tc, MVMSTable *st,
        MVMObject *root, void *data, MVMint64 value);

    /* Used with boxing. Gets an integer value, for representations that
     * can hold one. */
    MVMint64 (*get_int) (MVMThreadContext *tc, MVMSTable *st,
        MVMObject *root, void *data);

    /* Used with boxing. Sets a floating point value, for representations that
     * can hold one. */
    void (*set_num) (MVMThreadContext *tc, MVMSTable *st,
        MVMObject *root, void *data, MVMnum64 value);

    /* Used with boxing. Gets a floating point value, for representations that
     * can hold one. */
    MVMnum64 (*get_num) (MVMThreadContext *tc, MVMSTable *st,
        MVMObject *root, void *data);

    /* Used with boxing. Sets a string value, for representations that
     * can hold one. */
    void (*set_str) (MVMThreadContext *tc, MVMSTable *st,
        MVMObject *root, void *data, MVMString *value);

    /* Used with boxing. Gets a string value, for representations that
     * can hold one. */
    MVMString * (*get_str) (MVMThreadContext *tc, MVMSTable *st,
        MVMObject *root, void *data);

    /* Some objects serve primarily as boxes of others, inlining them. This gets
     * gets the reference to such things, using the representation ID to distinguish
     * them. */
    void * (*get_boxed_ref) (MVMThreadContext *tc, MVMSTable *st,
        MVMObject *root, void *data, MVMuint32 repr_id);
};
struct MVMREPROps_Positional {
    /* Gets the element and the specified index and places it in the passed
     * location (specified as a register). Expects to be passed a kind flag
     * that matches the kind of the attribute that is being fetched. */
    void (*at_pos) (MVMThreadContext *tc, MVMSTable *st,
        MVMObject *root, void *data, MVMint64 index,
        MVMRegister *result, MVMuint16 kind);

    /* Binds the given object or value to the specified index. The
     * kind flag specifies the type of value being passed to be bound.*/
    void (*bind_pos) (MVMThreadContext *tc, MVMSTable *st,
        MVMObject *root, void *data, MVMint64 index,
        MVMRegister value, MVMuint16 kind);

    /* Sets the element count of the array, expanding or shrinking
     * it as needed. */
    void (*set_elems) (MVMThreadContext *tc, MVMSTable *st,
        MVMObject *root, void *data, MVMuint64 count);

    /* Returns a true value of the specified index exists, and a false one if not. */
    MVMint64 (*exists_pos) (MVMThreadContext *tc, MVMSTable *st,
        MVMObject *root, void *data, MVMint64 index);

    /* Pushes the specified value onto the array. */
    void (*push) (MVMThreadContext *tc, MVMSTable *st,
        MVMObject *root, void *data, MVMRegister value, MVMuint16 kind);

    /* Pops the value at the end of the array off it. */
    void (*pop) (MVMThreadContext *tc, MVMSTable *st,
        MVMObject *root, void *data, MVMRegister *value, MVMuint16 kind);

    /* Unshifts the value onto the array. */
    void (*unshift) (MVMThreadContext *tc, MVMSTable *st,
        MVMObject *root, void *data, MVMRegister value, MVMuint16 kind);

    /* Gets the value at the start of the array, and moves the starting point of
     * the array so that the next element is element zero. */
    void (*shift) (MVMThreadContext *tc, MVMSTable *st,
        MVMObject *root, void *data, MVMRegister *value, MVMuint16 kind);

    /* Splices the specified array into this one. Representations may optimize if
     * they know the type of the passed array, otherwise they should use the REPR
     * API. */
    void (*splice) (MVMThreadContext *tc, MVMSTable *st,
        MVMObject *root, void *data, MVMObject *target_array,
        MVMint64 offset, MVMuint64 elems);

    /* Gets the STable representing the declared element type. */
    MVMStorageSpec (*get_elem_storage_spec) (MVMThreadContext *tc, MVMSTable *st);
};
struct MVMREPROps_Associative {
    /* Gets the value at the specified key and places it in the passed
     * location (specified as a register). Expects to be passed a kind flag
     * that matches the kind of the attribute that is being fetched. */
    void (*at_key) (MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data,
        MVMObject *key, MVMRegister *result, MVMuint16 kind);

    /* Binds the object at the specified address into the hash at the specified
     * key. */
    void (*bind_key) (MVMThreadContext *tc, MVMSTable *st, MVMObject *root,
        void *data, MVMObject *key, MVMRegister value, MVMuint16 kind);

    /* Returns a true value of the key exists, and a false one if not. */
    MVMint64 (*exists_key) (MVMThreadContext *tc, MVMSTable *st,
        MVMObject *root, void *data, MVMObject *key);

    /* Deletes the specified key. */
    void (*delete_key) (MVMThreadContext *tc, MVMSTable *st,
        MVMObject *root, void *data, MVMObject *key);

    /* Gets the storage spec of the hash value type. */
    MVMStorageSpec (*get_value_storage_spec) (MVMThreadContext *tc, MVMSTable *st);
};
struct MVMREPROps {
    /* Creates a new type object of this representation, and
     * associates it with the given HOW. Also sets up a new
     * representation instance if needed. */
    MVMObject * (*type_object_for) (MVMThreadContext *tc, MVMObject *HOW);

    /* Allocates a new, but uninitialized object, based on the
     * specified s-table. */
    MVMObject * (*allocate) (MVMThreadContext *tc, MVMSTable *st);

    /* Used to initialize the body of an object representing the type
     * describe by the specified s-table. DATA points to the body. It
     * may recursively call initialize for any flattened objects. */
    void (*initialize) (MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data);

    /* For the given type, copies the object data from the source memory
     * location to the destination one. Note that it may actually be more
     * involved than a straightforward bit of copying; what's important is
     * that the representation knows about that. Note that it may have to
     * call copy_to recursively on representations of any flattened objects
     * within its body. */
    void (*copy_to) (MVMThreadContext *tc, MVMSTable *st, void *src, MVMObject *dest_root, void *dest);

    /* Attribute access REPR function table. */
    MVMREPROps_Attribute attr_funcs;

    /* Boxing REPR function table. */
    MVMREPROps_Boxing box_funcs;

    /* Positional indexing REPR function table. */
    MVMREPROps_Positional pos_funcs;

    /* Associative indexing REPR function table. */
    MVMREPROps_Associative ass_funcs;

    /* Gets the number of elements, for any aggregate types. */
    MVMuint64 (*elems) (MVMThreadContext *tc, MVMSTable *st,
        MVMObject *root, void *data);

    /* Gets the storage specification for this representation. */
    const MVMStorageSpec * (*get_storage_spec) (MVMThreadContext *tc, MVMSTable *st);

    /* Handles an object changing its type. The representation is responsible
     * for doing any changes to the underlying data structure, and may reject
     * changes that it's not willing to do (for example, a representation may
     * choose to only handle switching to a subclass). It is also left to update
     * the S-Table pointer as needed; while in theory this could be factored
     * out, the representation probably knows more about timing issues and
     * thread safety requirements. */
    void (*change_type) (MVMThreadContext *tc, MVMObject *object, MVMObject *new_type);

    /* Object serialization. Writes the object's body out using the passed
     * serialization writer. */
    void (*serialize) (MVMThreadContext *tc, MVMSTable *st, void *data, MVMSerializationWriter *writer);

    /* Object deserialization. Reads the object's body in using the passed
     * serialization reader. */
    void (*deserialize) (MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMSerializationReader *reader);

    /* REPR data serialization. Serializes the per-type representation data that
     * is attached to the supplied STable. */
    void (*serialize_repr_data) (MVMThreadContext *tc, MVMSTable *st, MVMSerializationWriter *writer);

    /* REPR data deserialization. Deserializes the per-type representation data and
     * attaches it to the supplied STable. */
    void (*deserialize_repr_data) (MVMThreadContext *tc, MVMSTable *st, MVMSerializationReader *reader);

    /* Deserialization of STable size. */
    void (*deserialize_stable_size) (MVMThreadContext *tc, MVMSTable *st, MVMSerializationReader *reader);

    /* MoarVM-specific REPR API addition used to mark an object. This involves
     * adding all pointers it contains to the worklist. */
    void (*gc_mark) (MVMThreadContext *tc, MVMSTable *st, void *data, MVMGCWorklist *worklist);

    /* MoarVM-specific REPR API addition used to free an object. */
    void (*gc_free) (MVMThreadContext *tc, MVMObject *object);

    /* This is called to do any cleanup of resources when an object gets
     * embedded inside another one. Never called on a top-level object. */
    void (*gc_cleanup) (MVMThreadContext *tc, MVMSTable *st, void *data);

    /* MoarVM-specific REPR API addition used to mark a REPR instance. */
    void (*gc_mark_repr_data) (MVMThreadContext *tc, MVMSTable *st, MVMGCWorklist *worklist);

    /* MoarVM-specific REPR API addition used to free a REPR instance. */
    void (*gc_free_repr_data) (MVMThreadContext *tc, MVMSTable *st);

    /* Causes the representation to be composed. Composition involves
     * passing the representation information that it needs in order
     * to compute memory layout. */
    void (*compose) (MVMThreadContext *tc, MVMSTable *st, MVMObject *info);

    /* Allows the REPR to produce specialized bytecode versions of various
     * instructions, when we know some of the types involved. */
    void (*spesh) (MVMThreadContext *tc, MVMSTable *st, MVMSpeshGraph *g,
        MVMSpeshBB *bb, MVMSpeshIns *ins);

    /* The representation's name. */
    const char *name;

    /* The representation's ID. */
    MVMuint32 ID;

    /* Does this representation reference frames (either MVMStaticFrame or
     * MVMFrame)? */
    MVMuint32 refs_frames;
};

/* Various handy macros for getting at important stuff. */
#define STABLE(o)        (((MVMObject *)(o))->st)
#define REPR(o)          (STABLE((o))->REPR)
#define OBJECT_BODY(o)   (&(((MVMObjectStooge *)(o))->data))

/* Macros for getting/setting type-objectness. */
#define IS_CONCRETE(o)   (!(((MVMObject *)o)->header.flags & MVM_CF_TYPE_OBJECT))

/* Some functions related to 6model core functionality. */
MVMObject * MVM_6model_get_how(MVMThreadContext *tc, MVMSTable *st);
MVMObject * MVM_6model_get_how_obj(MVMThreadContext *tc, MVMObject *st);
void MVM_6model_find_method(MVMThreadContext *tc, MVMObject *obj, MVMString *name, MVMRegister *res);
MVM_PUBLIC MVMObject * MVM_6model_find_method_cache_only(MVMThreadContext *tc, MVMObject *obj, MVMString *name);
MVMint32 MVM_6model_find_method_spesh(MVMThreadContext *tc, MVMObject *obj, MVMString *name,
                                      MVMint32 ss_idx, MVMRegister *res);
MVMint64 MVM_6model_can_method_cache_only(MVMThreadContext *tc, MVMObject *obj, MVMString *name);
void MVM_6model_can_method(MVMThreadContext *tc, MVMObject *obj, MVMString *name, MVMRegister *res);
void MVM_6model_istype(MVMThreadContext *tc, MVMObject *obj, MVMObject *type, MVMRegister *res);
MVM_PUBLIC MVMint64 MVM_6model_istype_cache_only(MVMThreadContext *tc, MVMObject *obj, MVMObject *type);
MVMint64 MVM_6model_try_cache_type_check(MVMThreadContext *tc, MVMObject *obj, MVMObject *type, MVMint32 *result);
void MVM_6model_invoke_default(MVMThreadContext *tc, MVMObject *invokee, MVMCallsite *callsite, MVMRegister *args);
void MVM_6model_stable_gc_free(MVMThreadContext *tc, MVMSTable *st);
MVMuint64 MVM_6model_next_type_cache_id(MVMThreadContext *tc);
void MVM_6model_never_repossess(MVMThreadContext *tc, MVMObject *obj);
