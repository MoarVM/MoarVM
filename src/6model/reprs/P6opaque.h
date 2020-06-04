/* This is how an instance with the P6opaque representation starts. However, what
 * follows on from this depends on the declaration. For object attributes, it will
 * be a pointer size and point to another MVMObject. For native integers and
 * numbers, it will be the appropriate sized piece of memory to store them
 * right there in the object. Note that P6opaque does not do packed storage, so
 * an int2 gets as much space as an int. */
struct MVMP6opaqueBody {
    /* If we get mixed into, we may change size. If so, we can't really resize
     * the object, so instead we hang its post-resize form off this pointer.
     * In the future, more clever things are possible (like only putting the
     * new fields into this object). */
    void *replaced;
};
struct MVMP6opaque {
    MVMObject common;
    MVMP6opaqueBody body;
};

/* This is used in the name to slot mapping. Indicates the class key that
 * we have the mappings for, followed by arrays of names and slots. (Yeah,
 * could use a hash, but much code will resolve these statically to the
 * slots). */
struct MVMP6opaqueNameMap {
    MVMObject  *class_key;
    MVMString **names;
    MVMuint16  *slots;
    MVMuint32   num_attrs;
};

/* The P6opaque REPR data has the slot mapping, allocation size and
 * various other bits of info. It hangs off the REPR_data pointer
 * in the s-table. */
struct MVMP6opaqueREPRData {
    /* The number of attributes we have allocated slots for. Note that
     * slots can vary in size. */
    MVMuint16 num_attributes;

    /* Slot containing object to delegate for positional things. */
    MVMint16 pos_del_slot;

    /* Slot containing object to delegate for associative things. */
    MVMint16 ass_del_slot;

    /* Flags if we are MI or not. */
    MVMuint16 mi;

    /* Slot to delegate to when we need to unbox to a native integer. */
    MVMint16 unbox_int_slot;

    /* Slot to delegate to when we need to unbox to a native number. */
    MVMint16 unbox_num_slot;

    /* Slot to delegate to when we need to unbox to a native string. */
    MVMint16 unbox_str_slot;

    /* Offsets into the object that are eligible for GC marking, and how
     * many of them we have. */
    MVMuint16 gc_obj_mark_offsets_count;
    MVMuint16 *gc_obj_mark_offsets;

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

    /* If we have any other flattened boxings, this array can be indexed by
     * REPR ID to find the slot in the object where it is embedded. */
    MVMuint16 *unbox_slots;

    /* A table mapping attribute names to indexes (which can then be looked
     * up in the offset table). Uses a final null entry as a sentinel. */
    MVMP6opaqueNameMap *name_to_index_mapping;

    /* Slots holding flattened objects that need another REPR to initialize
     * them; terminated with -1. */
    MVMint16 *initialize_slots;

    /* Slots holding flattened objects that need another REPR to mark them;
     * terminated with -1. */
    MVMint16 *gc_mark_slots;

    /* Slots holding flattened objects that need another REPR to clean them;
     * terminated with -1. */
    MVMint16 *gc_cleanup_slots;

    /* Hold the storage spec */
    MVMStorageSpec storage_spec;
};

/* Function for REPR setup. */
const MVMREPROps * MVMP6opaque_initialize(MVMThreadContext *tc);

/* If an object gets mixed in to, we need to be sure we look at its real body,
 * which may have been moved to hang off the specified pointer.
 *
 * NB: This has been hardcoded into the jit compilation. Thus, consider it
 * set into stone :-). That is the price you pay for disintermediation. */
MVM_STATIC_INLINE void * MVM_p6opaque_real_data(MVMThreadContext *tc, void *data) {
    MVMP6opaqueBody *body = (MVMP6opaqueBody *)data;
    return body->replaced ? body->replaced : data;
}

/* Reads an attribute using an offset. This is only safe on an exact type
 * match. */
MVM_STATIC_INLINE MVMObject * MVM_p6opaque_read_object(MVMThreadContext *tc,
                                                       MVMObject *o, size_t offset) {
    char *data  = MVM_p6opaque_real_data(tc, OBJECT_BODY(o));
    return *((MVMObject **)(data + offset));
}
MVM_STATIC_INLINE MVMint64 MVM_p6opaque_read_int64(MVMThreadContext *tc,
                                                   MVMObject *o, size_t offset) {
    char *data  = MVM_p6opaque_real_data(tc, OBJECT_BODY(o));
    return *((MVMint64 *)(data + offset));
}
MVM_STATIC_INLINE MVMnum64 MVM_p6opaque_read_num64(MVMThreadContext *tc,
                                                   MVMObject *o, size_t offset) {
    char *data  = MVM_p6opaque_real_data(tc, OBJECT_BODY(o));
    return *((MVMnum64 *)(data + offset));
}
MVM_STATIC_INLINE MVMString * MVM_p6opaque_read_str(MVMThreadContext *tc,
                                                    MVMObject *o, size_t offset) {
    char *data  = MVM_p6opaque_real_data(tc, OBJECT_BODY(o));
    return *((MVMString **)(data + offset));
}

size_t MVM_p6opaque_attr_offset(MVMThreadContext *tc, MVMObject *type,
    MVMObject *class_handle, MVMString *name);
void MVM_p6opaque_attr_offset_and_arg_type(MVMThreadContext *tc, MVMObject *type,
    MVMObject *class_handle, MVMString *name, size_t *offset_out, MVMCallsiteFlags *type_out);
MVMuint16 MVM_p6opaque_get_bigint_offset(MVMThreadContext *tc, MVMSTable *st);
MVMuint32 MVM_p6opaque_offset_to_attr_idx(MVMThreadContext *tc, MVMObject *type, size_t offset);
void MVM_P6opaque_at_pos(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMint64 index, MVMRegister *value, MVMuint16 kind);
