/* Stub a couple of types - definitions later. */
struct _MVMREPROps;
struct _MVMSTable;
struct _MVMString;
struct _MVMSerializationReader;
struct _MVMSerializationWriter;
struct _MVMThreadContext;
struct _MVMCallsite;
struct _MVMGCWorklist;
struct _MVMContainerSpec;
union  _MVMRegister;

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
 * decide answer. */
#define MVM_TYPE_CHECK_CACHE_DEFINITIVE    0
#define MVM_TYPE_CHECK_CACHE_THEN_METHOD   1
#define MVM_TYPE_CHECK_NEEDS_ACCEPTS       2
#define MVM_TYPE_CHECK_CACHE_FLAG_MASK     3

/* This flag is set if we consider the method cache authoritative. */
#define MVM_METHOD_CACHE_AUTHORITATIVE     4

/* Hint value to indicate the absence of an attribute lookup or method
 * dispatch hint. */
#define MVM_NO_HINT -1

/* This data structure describes what storage a given representation
 * needs if something of that representation is to be embedded in
 * another place. For any representation that expects to be used
 * as a kind of reference type, it will just want to be a pointer.
 * But for other things, they would prefer to be "inlined" into
 * the object. */
typedef struct _MVMStorageSpec {
    /* 0 if this is to be referenced, anything else otherwise. */
    MVMuint16 inlineable;

    /* For things that want to be inlined, the number of bits of
     * storage they need. Ignored otherwise. */
    MVMuint16 bits;

    /* For things that are inlined, if they are just storage of a
     * primitive type and can unbox, this says what primitive type
     * that they unbox to. */
    MVMuint16 boxed_primitive;

    /* The types that this one can box/unbox to. */
    MVMuint16 can_box;
} MVMStorageSpec;

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
    MVM_CF_IN_GEN2_ROOT_LIST = 32
} MVMCollectableFlags;

/* Things that every GC-collectable entity has. These fall into two
 * categories:
 *   * MVMObject - objects. Almost everything is one of these.
 *   * MVMSTable - shared tables; one per (HOW, REPR) pairing.
 * Only the first can vary in size, and even then only if it's not a
 * type object.
 */
typedef struct _MVMCollectable {
    /* Identifier of the thread that currently owns the object, if any. If the
     * object is unshared, then this is always the creating thread. If it is
     * shared then it's whoever currently holds the mutex on it, or 0 if there
     * is no held mutex. */
    MVMuint32 owner;

    /* Collectable flags (see MVMCollectableFlags). */
    MVMuint32 flags;

    /* Forwarding pointer, for copying/compacting GC purposes. */
    struct _MVMCollectable *forwarder;

    /* Pointer to the serialization context this collectable lives in, if any. */
    struct _MVMSerializationContext *sc;
} MVMCollectable;

/* The common things every object has. */
typedef struct _MVMObject {
    /* Commonalities that all collectable entities have. */
    MVMCollectable header;

    /* The s-table for the object. */
    struct _MVMSTable *st;

    /* Padding for 32-bit systems. */
#if !defined(_M_X64) && !defined(__amd64__)
    MVMuint32 pad;
#endif
} MVMObject;

/* An dummy object, mostly used to compute the offset of the data part of
 * a 6model object. */
struct _MVMObjectStooge {
    MVMObject common;
    void *data;
};

/* This is used to identify an attribute for various types of cache. */
typedef struct {
    MVMObject         *class_handle;   /* Class handle */
    struct _MVMString *attr_name;      /* Name of the attribute. */
    MVMint64           hint;           /* Hint for use in static/gradual typing. */
} MVMAttributeIdentifier;

/* How do we turn something of this type into a boolean? */
typedef struct {
    MVMObject *method;
    MVMuint32  mode;
} MVMBoolificationSpec;

/* S-table, representing a meta-object/representation pairing. Note that the
 * items are grouped in hope that it will pack decently and do decently in
 * terms of cache lines. */
typedef struct _MVMSTable {
    /* Commonalities that all collectable entities have. */
    MVMCollectable header;

    /* The representation operation table. */
    struct _MVMREPROps *REPR;

    /* Any data specific to this type that the REPR wants to keep. */
    void *REPR_data;

    /* The size of an object of this type in bytes, including the
     * header. */
    MVMuint32 size;

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

    /* Invocation handler. If something tries to invoke this object,
     * whatever hangs off this function pointer gets invoked to handle
     * the invocation. If it's a call into C code it may do stuff right
     * off the bat. However, normally it will do whatever is needed to
     * arrange for setting up a callframe, twiddle the interpreter's
     * PC as needed and return. */
    void (*invoke) (struct _MVMThreadContext *tc, MVMObject *invokee,
        struct _MVMCallsite *callsite, union _MVMRegister *args);

    /* If this is a container, then this contains information needed in
     * order to fetch the value in it. If not, it'll be null, which can
     * be taken as a "not a container" indication. */
    struct _MVMContainerSpec *container_spec;

    /* Data that the container spec may need to function. */
    /* Any data specific to this type that the REPR wants to keep. */
    void *container_data;

    /*
     * If this is invokable, then this contains information needed to
     * figure out how to invoke it. If not, it'll be null.
     */
    struct _MVMInvocationSpec *invocation_spec;

    /* Information - if any - about how we can turn something of this type
     * into a boolean. */
    MVMBoolificationSpec *boolification_spec;

    /* The underlying package stash. */
    MVMObject *WHO;
} MVMSTable;

/* The representation operations table. Note that representations are not
 * classes - there's no inheritance, so there's no polymprhism. If you know
 * a representation statically, you can statically dereference the call to
 * the representation op in question. In the dynamic case, you have to go
 * following the pointer, however. */
typedef struct _MVMREPROps_Attribute {
    /* Gets the current value for an attribute and places it in the passed
     * location (specified as a register). Expects to be passed a kind flag
     * that matches the kind of the attribute that is being fetched. */
    void (*get_attribute) (struct _MVMThreadContext *tc, MVMSTable *st,
        MVMObject *root, void *data, MVMObject *class_handle, struct _MVMString *name,
        MVMint64 hint, union _MVMRegister *result, MVMuint16 kind);

    /* Binds the given object or value to the specified attribute. The
     * kind flag specifies the type of value being passed to be bound.*/
    void (*bind_attribute) (struct _MVMThreadContext *tc, MVMSTable *st,
        MVMObject *root, void *data, MVMObject *class_handle, struct _MVMString *name,
        MVMint64 hint, union _MVMRegister value, MVMuint16 kind);

    /* Gets the hint for the given attribute ID. */
    MVMint64 (*hint_for) (struct _MVMThreadContext *tc, MVMSTable *st,
        MVMObject *class_handle, struct _MVMString *name);

    /* Checks if an attribute has been initialized. */
    MVMint64 (*is_attribute_initialized) (struct _MVMThreadContext *tc, MVMSTable *st,
        void *data, MVMObject *class_handle, struct _MVMString *name,
        MVMint64 hint);
} MVMREPROps_Attribute;
typedef struct _MVMREPROps_Boxing {
    /* Used with boxing. Sets an integer value, for representations that
     * can hold one. */
    void (*set_int) (struct _MVMThreadContext *tc, MVMSTable *st,
        MVMObject *root, void *data, MVMint64 value);

    /* Used with boxing. Gets an integer value, for representations that
     * can hold one. */
    MVMint64 (*get_int) (struct _MVMThreadContext *tc, MVMSTable *st,
        MVMObject *root, void *data);

    /* Used with boxing. Sets a floating point value, for representations that
     * can hold one. */
    void (*set_num) (struct _MVMThreadContext *tc, MVMSTable *st,
        MVMObject *root, void *data, MVMnum64 value);

    /* Used with boxing. Gets a floating point value, for representations that
     * can hold one. */
    MVMnum64 (*get_num) (struct _MVMThreadContext *tc, MVMSTable *st,
        MVMObject *root, void *data);

    /* Used with boxing. Sets a string value, for representations that
     * can hold one. */
    void (*set_str) (struct _MVMThreadContext *tc, MVMSTable *st,
        MVMObject *root, void *data, struct _MVMString *value);

    /* Used with boxing. Gets a string value, for representations that
     * can hold one. */
    struct _MVMString * (*get_str) (struct _MVMThreadContext *tc, MVMSTable *st,
        MVMObject *root, void *data);

    /* Some objects serve primarily as boxes of others, inlining them. This gets
     * gets the reference to such things, using the representation ID to distinguish
     * them. */
    void * (*get_boxed_ref) (struct _MVMThreadContext *tc, MVMSTable *st,
        MVMObject *root, void *data, MVMuint32 repr_id);
} MVMREPROps_Boxing;
typedef struct _MVMREPROps_Positional {
    /* Gets the element and the specified index and places it in the passed
     * location (specified as a register). Expects to be passed a kind flag
     * that matches the kind of the attribute that is being fetched. */
    void (*at_pos) (struct _MVMThreadContext *tc, MVMSTable *st,
        MVMObject *root, void *data, MVMint64 index,
        union _MVMRegister *result, MVMuint16 kind);

    /* Binds the given object or value to the specified index. The
     * kind flag specifies the type of value being passed to be bound.*/
    void (*bind_pos) (struct _MVMThreadContext *tc, MVMSTable *st,
        MVMObject *root, void *data, MVMint64 index,
        union _MVMRegister value, MVMuint16 kind);

    /* Sets the element count of the array, expanding or shrinking
     * it as needed. */
    void (*set_elems) (struct _MVMThreadContext *tc, MVMSTable *st,
        MVMObject *root, void *data, MVMuint64 count);

    /* Returns a true value of the specified index exists, and a false one if not. */
    MVMint64 (*exists_pos) (struct _MVMThreadContext *tc, MVMSTable *st,
        MVMObject *root, void *data, MVMint64 index);

    /* Pushes the specified value onto the array. */
    void (*push) (struct _MVMThreadContext *tc, MVMSTable *st,
        MVMObject *root, void *data, union _MVMRegister value, MVMuint16 kind);

    /* Pops the value at the end of the array off it. */
    void (*pop) (struct _MVMThreadContext *tc, MVMSTable *st,
        MVMObject *root, void *data, union _MVMRegister *value, MVMuint16 kind);

    /* Unshifts the value onto the array. */
    void (*unshift) (struct _MVMThreadContext *tc, MVMSTable *st,
        MVMObject *root, void *data, union _MVMRegister value, MVMuint16 kind);

    /* Gets the value at the start of the array, and moves the starting point of
     * the array so that the next element is element zero. */
    void (*shift) (struct _MVMThreadContext *tc, MVMSTable *st,
        MVMObject *root, void *data, union _MVMRegister *value, MVMuint16 kind);

    /* Splices the specified array into this one. Representations may optimize if
     * they know the type of the passed array, otherwise they should use the REPR
     * API. */
    void (*splice) (struct _MVMThreadContext *tc, MVMSTable *st,
        MVMObject *root, void *data, MVMObject *target_array,
        MVMint64 offset, MVMuint64 elems);

    /* Gets the STable representing the declared element type. */
    MVMStorageSpec (*get_elem_storage_spec) (struct _MVMThreadContext *tc, MVMSTable *st);
} MVMREPROps_Positional;
typedef struct _MVMREPROps_Associative {
    /* Get the address of the element at the specified key. May return null if
     * nothing is there, or throw to indicate the key does not exist, or vivify. */
    void * (*at_key_ref) (struct _MVMThreadContext *tc, MVMSTable *st,
        MVMObject *root, void *data, MVMObject *key);

    /* Get a boxed object representing the element at the specified key. If the
     * object is already a reference type, simply returns that. */
    MVMObject * (*at_key_boxed) (struct _MVMThreadContext *tc, MVMSTable *st,
        MVMObject *root, void *data, MVMObject *key);

    /* Binds the value at the specified address into the hash at the specified
     * key. */
    void (*bind_key_ref) (struct _MVMThreadContext *tc, MVMSTable *st,
        MVMObject *root, void *data, MVMObject *key, void *value_addr);

    /* Binds the object at the specified address into the hash at the specified
     * key. */
    void (*bind_key_boxed) (struct _MVMThreadContext *tc, MVMSTable *st,
        MVMObject *root, void *data, MVMObject *key, MVMObject *value);

    /* Returns a true value of the key exists, and a false one if not. */
    MVMuint64 (*exists_key) (struct _MVMThreadContext *tc, MVMSTable *st,
        MVMObject *root, void *data, MVMObject *key);

    /* Deletes the specified key. */
    void (*delete_key) (struct _MVMThreadContext *tc, MVMSTable *st,
        MVMObject *root, void *data, MVMObject *key);

    /* Gets the storage spec of the hash value type. */
    MVMStorageSpec (*get_value_storage_spec) (struct _MVMThreadContext *tc, MVMSTable *st);
} MVMREPROps_Associative;
typedef struct _MVMREPROps {
    /* Creates a new type object of this representation, and
     * associates it with the given HOW. Also sets up a new
     * representation instance if needed. */
    MVMObject * (*type_object_for) (struct _MVMThreadContext *tc, MVMObject *HOW);

    /* Allocates a new, but uninitialized object, based on the
     * specified s-table. */
    MVMObject * (*allocate) (struct _MVMThreadContext *tc, MVMSTable *st);

    /* Used to initialize the body of an object representing the type
     * describe by the specified s-table. DATA points to the body. It
     * may recursively call initialize for any flattened objects. */
    void (*initialize) (struct _MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data);

    /* For the given type, copies the object data from the source memory
     * location to the destination one. Note that it may actually be more
     * involved than a straightforward bit of copying; what's important is
     * that the representation knows about that. Note that it may have to
     * call copy_to recursively on representations of any flattened objects
     * within its body. */
    void (*copy_to) (struct _MVMThreadContext *tc, MVMSTable *st, void *src, MVMObject *dest_root, void *dest);

    /* Attribute access REPR function table. */
    MVMREPROps_Attribute *attr_funcs;

    /* Boxing REPR function table. */
    MVMREPROps_Boxing *box_funcs;

    /* Positional indexing REPR function table. */
    MVMREPROps_Positional *pos_funcs;

    /* Associative indexing REPR function table. */
    MVMREPROps_Associative *ass_funcs;

    /* Gets the number of elements, for any aggregate types. */
    MVMuint64 (*elems) (struct _MVMThreadContext *tc, MVMSTable *st,
        MVMObject *root, void *data);

    /* Gets the storage specification for this representation. */
    MVMStorageSpec (*get_storage_spec) (struct _MVMThreadContext *tc, MVMSTable *st);

    /* Handles an object changing its type. The representation is responsible
     * for doing any changes to the underlying data structure, and may reject
     * changes that it's not willing to do (for example, a representation may
     * choose to only handle switching to a subclass). It is also left to update
     * the S-Table pointer as needed; while in theory this could be factored
     * out, the representation probably knows more about timing issues and
     * thread safety requirements. */
    void (*change_type) (struct _MVMThreadContext *tc, MVMObject *object, MVMObject *new_type);

    /* Object serialization. Writes the object's body out using the passed
     * serialization writer. */
    void (*serialize) (struct _MVMThreadContext *tc, MVMSTable *st, void *data, struct _MVMSerializationWriter *writer);

    /* Object deserialization. Reads the object's body in using the passed
     * serialization reader. */
    void (*deserialize) (struct _MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, struct _MVMSerializationReader *reader);

    /* REPR data serialization. Serializes the per-type representation data that
     * is attached to the supplied STable. */
    void (*serialize_repr_data) (struct _MVMThreadContext *tc, MVMSTable *st, struct _MVMSerializationWriter *writer);

    /* REPR data deserialization. Deserializes the per-type representation data and
     * attaches it to the supplied STable. */
    void (*deserialize_repr_data) (struct _MVMThreadContext *tc, MVMSTable *st, struct _MVMSerializationReader *reader);

    /* Deserialization of STable size. */
    void (*deserialize_stable_size) (struct _MVMThreadContext *tc, MVMSTable *st, struct _MVMSerializationReader *reader);

    /* MoarVM-specific REPR API addition used to mark an object. This involves
     * adding all pointers it contains to the worklist. */
    void (*gc_mark) (struct _MVMThreadContext *tc, MVMSTable *st, void *data, struct _MVMGCWorklist *worklist);

    /* MoarVM-specific REPR API addition used to free an object. */
    void (*gc_free) (struct _MVMThreadContext *tc, MVMObject *object);

    /* This is called to do any cleanup of resources when an object gets
     * embedded inside another one. Never called on a top-level object. */
    void (*gc_cleanup) (struct _MVMThreadContext *tc, MVMSTable *st, void *data);

    /* MoarVM-specific REPR API addition used to mark a REPR instance. */
    void (*gc_mark_repr_data) (struct _MVMThreadContext *tc, MVMSTable *st, struct _MVMGCWorklist *worklist);

    /* MoarVM-specific REPR API addition used to free a REPR instance. */
    void (*gc_free_repr_data) (struct _MVMThreadContext *tc, MVMSTable *st);

    /* Causes the representation to be composed. Composition involves
     * passing the representation information that it needs in order
     * to compute memory layout. */
    void (*compose) (struct _MVMThreadContext *tc, MVMSTable *st, MVMObject *info);

    /* The representation's name. */
    struct _MVMString *name;

    /* The representation's ID. */
    MVMuint32 ID;

    /* Does this representation reference frames (either MVMStaticFrame or
     * MVMFrame)? */
    MVMuint32 refs_frames;
} MVMREPROps;

/* Various handy macros for getting at important stuff. */
#define STABLE(o)        (((MVMObject *)(o))->st)
#define REPR(o)          (STABLE((o))->REPR)
#define OBJECT_BODY(o)   (&(((struct _MVMObjectStooge *)(o))->data))

/* Macros for getting/setting type-objectness. */
#define IS_CONCRETE(o)   (!(((MVMObject *)o)->header.flags & MVM_CF_TYPE_OBJECT))

/* Some functions related to 6model core functionality. */
MVMObject * MVM_6model_find_method(struct _MVMThreadContext *tc, MVMObject *obj, struct _MVMString *name);
MVMObject * MVM_6model_find_method_cache_only(struct _MVMThreadContext *tc, MVMObject *obj, struct _MVMString *name);
MVMint64 MVM_6model_can_method(struct _MVMThreadContext *tc, MVMObject *obj, struct _MVMString *name);
MVMint64 MVM_6model_istype_cache_only(struct _MVMThreadContext *tc, MVMObject *obj, MVMObject *type);
void MVM_6model_invoke_default(struct _MVMThreadContext *tc, MVMObject *invokee, struct _MVMCallsite *callsite, union _MVMRegister *args);
