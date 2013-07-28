/* This is how an instance with the P6opaque representation starts. However, what
 * follows on from this depends on the declaration. For object attributes, it will
 * be a pointer size and point to another MVMObject. For native integers and
 * numbers, it will be the appropriate sized piece of memory to store them
 * right there in the object. Note that P6opaque does not do packed storage, so
 * an int2 gets as much space as an int. */
typedef struct _MVMP6opaqueBody {
    /* If we get mixed into, we may change size. If so, we can't really resize
     * the object, so instead we hang its post-resize form off this pointer.
     * In the future, more clever things are possible (like only putting the
     * new fields into this object). */
    void *replaced;
} MVMP6opaqueBody;
 typedef struct _MVMP6opaque {
    MVMObject common;
    MVMP6opaqueBody body;
} MVMP6opaque;

/* This is used in the name to slot mapping. Indicates the class key that
 * we have the mappings for, followed by arrays of names and slots. (Yeah,
 * could use a hash, but much code will resolve these statically to the
 * slots). */
typedef struct {
    MVMObject  *class_key;
    MVMString **names;
    MVMuint16  *slots;
    MVMuint32   num_attrs;
} P6opaqueNameMap;

/* This is used in boxed type mappings. */
typedef struct {
    MVMuint32 repr_id;
    MVMuint16 slot;
} P6opaqueBoxedTypeMap;

/* The P6opaque REPR data has the slot mapping, allocation size and
 * various other bits of info. It hangs off the REPR_data pointer
 * in the s-table. */
typedef struct {
    /* The number of attributes we have allocated slots for. Note that
     * slots can vary in size. */
    MVMuint16 num_attributes;

    /* Maps attribute position numbers to the byte offset in the object. */
    MVMuint16 *attribute_offsets;

    /* If the attribute was actually flattened in to this object from another
     * representation, this is the s-table of the type of that attribute. NULL
     * for attributes that are just reference types. */
    MVMSTable **flattened_stables;

    /* Instantiated objects are just a blank piece of memory that needs to
     * be set up. However, in some cases we'd like them to magically turn in
     * to some container type. */
    MVMObject **auto_viv_values;

    /* Flags if we are MI or not. */
    MVMuint16 mi;

    /* Slot to delegate to when we need to unbox to a native integer. */
    MVMint16 unbox_int_slot;

    /* Slot to delegate to when we need to unbox to a native number. */
    MVMint16 unbox_num_slot;

    /* Slot to delegate to when we need to unbox to a native string. */
    MVMint16 unbox_str_slot;

    /* If we have any other boxings, this maps repr ID to slot. */
    P6opaqueBoxedTypeMap *unbox_slots;

    /* Slot containing object to delegate for positional things. */
    MVMint16 pos_del_slot;

    /* Slot containing object to delegate for associative things. */
    MVMint16 ass_del_slot;

    /* A table mapping attribute names to indexes (which can then be looked
     * up in the offset table). Uses a final null entry as a sentinel. */
    P6opaqueNameMap *name_to_index_mapping;

    /* Offsets into the object that are eligible for GC marking, and how
     * many of them we have. */
    MVMuint16 *gc_obj_mark_offsets;
    MVMuint16 gc_obj_mark_offsets_count;

    /* Slots holding flattened objects that need another REPR to initialize
     * them; terminated with -1. */
    MVMint16 *initialize_slots;

    /* Slots holding flattened objects that need another REPR to mark them;
     * terminated with -1. */
    MVMint16 *gc_mark_slots;

    /* Slots holding flattened objects that need another REPR to clean them;
     * terminated with -1. */
    MVMint16 *gc_cleanup_slots;
} MVMP6opaqueREPRData;

/* Function for REPR setup. */
MVMREPROps * MVMP6opaque_initialize(MVMThreadContext *tc);
