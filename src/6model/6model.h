/* Stub a couple of types - definitions later. */
struct _MVMREPROps;
struct _MVMSTable;
struct _MVMString;
struct _MVMSerializationReader;
struct _MVMSerializationWriter;
struct _MVMThreadContext;
struct _MVMCallsite;
union  _MVMArg;

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

/* Flags that may be set on any collectable. */
typedef enum {
    /* Is a type object (and thus not a concrete instance). */
    MVM_CF_TYPE_OBJECT = 1,
    
    /* Is an STable. */
    MVM_CF_STABLE = 2,
    
    /* Is a serialization context. */
    MVM_CF_SC = 4,
    
    /* Has already been seen once in GC nursery. */
    MVM_CF_NURSERY_SEEN = 8,

    /* Has been promoted to the old generation. */
    MVM_CF_SECOND_GEN = 16,
    
    /* Is shared - that is, more than one thread knows about it. */
    MVM_CF_SHARED = 32
} MVMCollectableFlags;

/* Things that every GC-collectable entity has. These fall into three
 * categories:
 *   * MVMObject - objects. Almost everything is one of these.
 *   * MVMSTable - shared tables; one per (HOW, REPR) pairing.
 *   * MVMSerializationContext - serialization contexts; one per comp unit.
 * Only the first can vary in size.
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
    struct _MVMObject *forwarder;
    
    /* Pointer to the serialization context this collectable lives in, if any. */
    struct _MVMSerializationContext *sc;
} MVMCollectable;

/* The common things every object has. */
typedef struct _MVMObject {
    /* Commonalities that all collectable entities have. */
    MVMCollectable header;
    
    /* The s-table for the object. */
    struct _MVMSTable *st;
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
        struct _MVMCallsite *callsite, union _MVMArg *args);

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

/* The representation operations table. Note that representations are not
 * classes - there's no inheritance, so there's no polymprhism. If you know
 * a representation statically, you can statically dereference the call to
 * the representation op in question. In the dynamic case, you have to go
 * following the pointer, however. */
typedef struct _MVMREPROps_Attribute {
    /* Gets the current value for an object attribute. For non-flattened
     * objects - that is, reference types - this just returns the object
     * stored in the attribute. For the flattened case, this will auto-box. */
    MVMObject * (*get_attribute_boxed) (struct _MVMThreadContext *tc, MVMSTable *st,
        MVMObject *root, void *data, MVMObject *class_handle, struct _MVMString *name,
        MVMint64 hint);

    /* Gets a reference to the memory location of an attribute. Note
     * that this is only valid so long as the object itself is alive. */
    void * (*get_attribute_ref) (struct _MVMThreadContext *tc, MVMSTable *st,
        MVMObject *root, void *data, MVMObject *class_handle, struct _MVMString *name,
        MVMint64 hint);

    /* Binds the given object value to the specified attribute. If it's
     * a reference type attribute, this just simply sets the value in 
     * place. If instead it's some other flattened in representation, then
     * the value should be a boxed form of the data to store.*/
    void (*bind_attribute_boxed) (struct _MVMThreadContext *tc, MVMSTable *st,
        MVMObject *root, void *data, MVMObject *class_handle, struct _MVMString *name,
        MVMint64 hint, MVMObject *value);

    /* Binds a flattened in attribute to the value at the passed reference.
     * Like with the get_attribute_ref function, presumably the thing calling
     * this knows about the type of the attribute it is supplying data for.
     * copy_to will be used to copy the data in to place. */
    void (*bind_attribute_ref) (struct _MVMThreadContext *tc, MVMSTable *st,
        MVMObject *root, void *data, MVMObject *class_handle, struct _MVMString *name,
        MVMint64 hint, void *value);

    /* Gets the hint for the given attribute ID. */
    MVMint64 (*hint_for) (struct _MVMThreadContext *tc, MVMSTable *st,
        MVMObject *class_handle, struct _MVMString *name);

    /* Checks if an attribute has been initialized. */
    MVMint32 (*is_attribute_initialized) (struct _MVMThreadContext *tc, MVMSTable *st,
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
    /* Get the address of the element at the specified position. May return null if
     * nothing is there, or throw to indicate out of bounds, or vivify. */
    void * (*at_pos_ref) (struct _MVMThreadContext *tc, MVMSTable *st,
        MVMObject *root, void *data, MVMuint64 index);

    /* Get a boxed object representing the element at the specified position. If the
     * object is already a reference type, simply returns that. */
    MVMObject * (*at_pos_boxed) (struct _MVMThreadContext *tc, MVMSTable *st,
        MVMObject *root, void *data, MVMuint64 index);

    /* Binds the value at the specified address into the array at the specified index.
     * may auto-vivify or throw. */
    void (*bind_pos_ref) (struct _MVMThreadContext *tc, MVMSTable *st,
        MVMObject *root, void *data, MVMuint64 index, void *addr);

    /* Binds the object at the specified address into the array at the specified index.
     * For arrays of non-reference types, expects a compatible type. */
    void (*bind_pos_boxed) (struct _MVMThreadContext *tc, MVMSTable *st,
        MVMObject *root, void *data, MVMuint64 index, MVMObject *obj);

    /* Gets the number of elements. */
    MVMuint64 (*elems) (struct _MVMThreadContext *tc, MVMSTable *st,
        MVMObject *root, void *data);

    /* Pre-allocates the specified number of slots. */
    void (*preallocate) (struct _MVMThreadContext *tc, MVMSTable *st,
        MVMObject *root, void *data, MVMuint64 count);

    /* Trim to the specified number of slots. */
    void (*trim_to) (struct _MVMThreadContext *tc, MVMSTable *st,
        MVMObject *root, void *data, MVMuint64 count);

    /* Make a "hole" the specified number of elements in size at the specified index.
     * Used for implementing things like unshift, splice, etc. */
    void (*make_hole) (struct _MVMThreadContext *tc, MVMSTable *st,
        MVMObject *root, void *data, MVMuint64 at_index, MVMuint64 count);

    /* Delete the specified number of elements (that is, actually shuffle the ones
     * after them into their place). Used for implementing things like shift, splice,
     * etc. */
    void (*delete_elems) (struct _MVMThreadContext *tc, MVMSTable *st,
        MVMObject *root, void *data, MVMuint64 at_index, MVMuint64 count);

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

    /* Gets the number of elements. */
    MVMuint64 (*elems) (struct _MVMThreadContext *tc, MVMSTable *st,
        MVMObject *root, void *data);

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
    
    /* Gets the storage specification for this representation. */
    MVMStorageSpec (*get_storage_spec) (struct _MVMThreadContext *tc, MVMSTable *st);
    
    /* Handles an object changing its type. The representation is responsible
     * for doing any changes to the underlying data structure, and may reject
     * changes that it's not willing to do (for example, a representation may
     * choose to only handle switching to a subclass). It is also left to update
     * the S-Table pointer as needed; while in theory this could be factored
     * out, the representation probably knows more about timing issues and
     * thread safety requirements. */
    void (*change_type) (struct _MVMThreadContext *tc, MVMObject *Object, MVMObject *NewType);
    
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
    
    /* MoarVM-specific REPR API addition used to mark an object. */
    void (*gc_mark) (struct _MVMThreadContext *tc, MVMSTable *st, void *data);

    /* MoarVM-specific REPR API addition used to free an object. */
    void (*gc_free) (struct _MVMThreadContext *tc, MVMObject *object);

    /* This is called to do any cleanup of resources when an object gets
     * embedded inside another one. Never called on a top-level object. */
    void (*gc_cleanup) (struct _MVMThreadContext *tc, MVMSTable *st, void *data);

    /* MoarVM-specific REPR API addition used to mark a REPR instance. */
    void (*gc_mark_repr_data) (struct _MVMThreadContext *tc, MVMSTable *st);

    /* MoarVM-specific REPR API addition used to free a REPR instance. */
    void (*gc_free_repr_data) (struct _MVMThreadContext *tc, MVMSTable *st);
    
    /* The representation's name. */
    struct _MVMString *name;

    /* The representation's ID. */
    MVMuint32 ID;
} MVMREPROps;

/* Various handy macros for getting at important stuff. */
#define STABLE(o)        (((MVMObject *)o)->st)
#define REPR(o)          (STABLE(o)->REPR)
#define OBJECT_BODY(o)   (&(((struct _MVMObjectStooge *)o)->data))

/* Macros for getting/setting type-objectness. */
#define IS_CONCRETE(o)   (!(((MVMObject *)o)->header.flags & MVM_CF_TYPE_OBJECT))
