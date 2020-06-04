#include "moar.h"

#ifndef MAX
    #define MAX(x,y) ((x)>(y)?(x):(y))
#endif

#define P6OMAX(x, y) ((y) > (x) ? (y) : (x))
#define REFVAR_VM_HASH_STR_VAR 10
#define MVM_P6OPAQUE_NO_UNBOX_SLOT 0xFFFF

/* This representation's function pointer table. */
static const MVMREPROps P6opaque_this_repr;

/* Helpers for reading/writing values. */
MVM_STATIC_INLINE MVMObject * get_obj_at_offset(void *data, MVMint64 offset) {
    void *location = (char *)data + offset;
    return *((MVMObject **)location);
}
MVM_STATIC_INLINE void set_obj_at_offset(MVMThreadContext *tc, MVMObject *root, void *data, MVMint64 offset, MVMObject *value) {
    void *location = (char *)data + offset;
    MVM_ASSIGN_REF(tc, &(root->header), *((MVMObject **)location), value);
}

/* Helper for aligning sizes */
MVM_STATIC_INLINE void align_to(MVMuint64 *size, MVMuint32 align) {
    if (*size % align) {
        *size += align - *size % align;
    }
}

/* Helper for finding a slot number. */
static MVMint64 try_get_slot(MVMThreadContext *tc, MVMP6opaqueREPRData *repr_data, MVMObject *class_key, MVMString *name) {
    if (repr_data->name_to_index_mapping) {
        MVMP6opaqueNameMap *cur_map_entry = repr_data->name_to_index_mapping;
        while (cur_map_entry->class_key != NULL) {
            if (cur_map_entry->class_key == class_key) {
                MVMuint16 i;
                for (i = 0; i < cur_map_entry->num_attrs; i++) {
                    if (MVM_string_equal(tc, cur_map_entry->names[i], name)) {
                        return cur_map_entry->slots[i];
                    }
                }
            }
            cur_map_entry++;
        }
    }
    return -1;
}

/* Creates a new type object of this representation, and associates it with
 * the given HOW. */
static MVMObject * type_object_for(MVMThreadContext *tc, MVMObject *HOW) {
    MVMSTable *st = MVM_gc_allocate_stable(tc, &P6opaque_this_repr, HOW);

    MVMROOT(tc, st, {
        MVMObject *obj = MVM_gc_allocate_type_object(tc, st);
        MVM_ASSIGN_REF(tc, &(st->header), st->WHAT, obj);
        st->size = 0; /* Is updated later. */
    });

    return st->WHAT;
}

/* Creates a new instance based on the type object. */
static MVMObject * allocate(MVMThreadContext *tc, MVMSTable *st) {
    if (st->size)
        return MVM_gc_allocate_object(tc, st);
    else
        MVM_exception_throw_adhoc(tc, "P6opaque: must compose %s before allocating", MVM_6model_get_stable_debug_name(tc, st));
}

/* Initializes a new instance. */
static void initialize(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data) {
    MVMP6opaqueREPRData * repr_data = (MVMP6opaqueREPRData *)st->REPR_data;
    data = MVM_p6opaque_real_data(tc, data);
    if (repr_data) {
        MVMint64 i;
        for (i = 0; repr_data->initialize_slots[i] >= 0; i++) {
            MVMint64   offset = repr_data->attribute_offsets[repr_data->initialize_slots[i]];
            MVMSTable *st     = repr_data->flattened_stables[repr_data->initialize_slots[i]];
            st->REPR->initialize(tc, st, root, (char *)data + offset);
        }
    }
    else {
        MVM_exception_throw_adhoc(tc, "P6opaque: must compose %s before using initialize", MVM_6model_get_stable_debug_name(tc, st));
    }
}

/* Copies the body of one object to another. */
static void copy_to(MVMThreadContext *tc, MVMSTable *st, void *src, MVMObject *dest_root, void *dest) {
    MVMP6opaqueREPRData *repr_data = (MVMP6opaqueREPRData *)st->REPR_data;
    MVMuint16 i;
    src = MVM_p6opaque_real_data(tc, src);

    /* Flattened in REPRs need a chance to copy 'emselves. */
    for (i = 0; i < repr_data->num_attributes; i++) {
        MVMSTable *st_copy = repr_data->flattened_stables[i];
        MVMuint16  offset  = repr_data->attribute_offsets[i];
        if (st_copy) {
            st_copy->REPR->copy_to(tc, st_copy, (char*)src + offset, dest_root, (char*)dest + offset);
        }
        else {
            MVMObject *ref = get_obj_at_offset(src, offset);
            if (ref)
                set_obj_at_offset(tc, dest_root, dest, offset, ref);
        }
    }
}

/* Called by the VM to mark any GCable items. */
static void gc_mark(MVMThreadContext *tc, MVMSTable *st, void *data, MVMGCWorklist *worklist) {
    MVMP6opaqueREPRData *repr_data = (MVMP6opaqueREPRData *)st->REPR_data;
    MVMint64 i;
    data = MVM_p6opaque_real_data(tc, data);

    /* Mark objects. */
    for (i = 0; i < repr_data->gc_obj_mark_offsets_count; i++) {
        MVMuint16 offset = repr_data->gc_obj_mark_offsets[i];
        MVM_gc_worklist_add(tc, worklist, (char *)data + offset);
    }

    /* Mark any nested reprs that need it. */
    for (i = 0; repr_data->gc_mark_slots[i] >= 0; i++) {
        MVMuint16  offset = repr_data->attribute_offsets[repr_data->gc_mark_slots[i]];
        MVMSTable *st     = repr_data->flattened_stables[repr_data->gc_mark_slots[i]];
        st->REPR->gc_mark(tc, st, (char *)data + offset, worklist);
    }
}

/* Called by the VM in order to free memory associated with this object. */
static void gc_free(MVMThreadContext *tc, MVMObject *obj) {
    MVMP6opaqueREPRData *repr_data = (MVMP6opaqueREPRData *)STABLE(obj)->REPR_data;
    MVMint64 i;
    void *data = MVM_p6opaque_real_data(tc, OBJECT_BODY(obj));

    /* Cleanup any nested reprs that need it. */
    for (i = 0; repr_data->gc_cleanup_slots[i] >= 0; i++) {
        MVMuint16  offset = repr_data->attribute_offsets[repr_data->gc_cleanup_slots[i]];
        MVMSTable *st     = repr_data->flattened_stables[repr_data->gc_cleanup_slots[i]];
        st->REPR->gc_cleanup(tc, st, (char *)data + offset);
    }

    /* If we replaced the object body, free the replacement. */
    MVM_free(((MVMP6opaque *)obj)->body.replaced);
}

/* Marks the representation data in an STable.*/
static void gc_mark_repr_data(MVMThreadContext *tc, MVMSTable *st, MVMGCWorklist *worklist) {
    MVMP6opaqueREPRData *repr_data = (MVMP6opaqueREPRData *)st->REPR_data;

    /* May not be composed yet. */
    if (repr_data == NULL)
        return;

    if (repr_data->flattened_stables) {
        int i;
        for (i = 0; i < repr_data->num_attributes; i++)
            if (repr_data->flattened_stables[i])
                MVM_gc_worklist_add(tc, worklist, &repr_data->flattened_stables[i]);
    }

    if (repr_data->auto_viv_values) {
        int i;
        for (i = 0; i < repr_data->num_attributes; i++)
            if (repr_data->auto_viv_values[i])
                MVM_gc_worklist_add(tc, worklist, &repr_data->auto_viv_values[i]);
    }

    if (repr_data->name_to_index_mapping) {
        MVMP6opaqueNameMap *cur_map_entry = repr_data->name_to_index_mapping;
        while (cur_map_entry->class_key != NULL) {
            MVMuint16 i;
            for (i = 0; i < cur_map_entry->num_attrs; i++) {
                MVM_gc_worklist_add(tc, worklist, &cur_map_entry->names[i]);
            }
            MVM_gc_worklist_add(tc, worklist, &cur_map_entry->class_key);
            cur_map_entry++;
        }
    }
}

/* Marks the representation data in an STable.*/
static void gc_free_repr_data(MVMThreadContext *tc, MVMSTable *st) {
    MVMP6opaqueREPRData *repr_data = (MVMP6opaqueREPRData *)st->REPR_data;

    /* May not have survived to composition. */
    if (repr_data == NULL)
        return;

    if (repr_data->name_to_index_mapping) {
        MVMP6opaqueNameMap *cur_map_entry = repr_data->name_to_index_mapping;
        while (cur_map_entry->class_key != NULL) {
            MVM_free(cur_map_entry->names);
            MVM_free(cur_map_entry->slots);
            cur_map_entry++;
        }
        MVM_free(repr_data->name_to_index_mapping);
    }

    MVM_free(repr_data->attribute_offsets);
    MVM_free(repr_data->flattened_stables);
    MVM_free(repr_data->auto_viv_values);
    MVM_free(repr_data->unbox_slots);
    MVM_free(repr_data->gc_obj_mark_offsets);
    MVM_free(repr_data->initialize_slots);
    MVM_free(repr_data->gc_mark_slots);
    MVM_free(repr_data->gc_cleanup_slots);

    MVM_free(st->REPR_data);
}

/* Frees the representation data.*/
static void free_repr_data(MVMP6opaqueREPRData *repr_data) {
    if (repr_data->name_to_index_mapping) {
        MVMP6opaqueNameMap *cur_map_entry = repr_data->name_to_index_mapping;
        while (cur_map_entry->class_key != NULL) {
            MVM_free(cur_map_entry->names);
            MVM_free(cur_map_entry->slots);
            cur_map_entry++;
        }
        MVM_free(repr_data->name_to_index_mapping);
    }

    MVM_free(repr_data->attribute_offsets);
    MVM_free(repr_data->flattened_stables);
    MVM_free(repr_data->auto_viv_values);
    MVM_free(repr_data->unbox_slots);
    MVM_free(repr_data->gc_obj_mark_offsets);
    MVM_free(repr_data->initialize_slots);
    MVM_free(repr_data->gc_mark_slots);
    MVM_free(repr_data->gc_cleanup_slots);

    MVM_free(repr_data);
}

/* Helper for complaining about attribute access errors. */
MVM_NO_RETURN static void no_such_attribute(MVMThreadContext *tc, const char *action, MVMObject *class_handle, MVMString *name, MVMSTable *target_type) MVM_NO_RETURN_ATTRIBUTE;
MVM_NO_RETURN static void no_such_attribute(MVMThreadContext *tc, const char *action, MVMObject *class_handle, MVMString *name, MVMSTable *target_type) {
    char *c_name = MVM_string_utf8_encode_C_string(tc, name);
    char *waste[] = { c_name, NULL };
    MVM_exception_throw_adhoc_free(tc, waste, "P6opaque: no such attribute '%s' on type %s in a %s when trying to %s", c_name, MVM_6model_get_debug_name(tc, class_handle), MVM_6model_get_stable_debug_name(tc, target_type), action);
}

MVM_NO_RETURN static void invalid_access_kind(MVMThreadContext *tc, const char *action, MVMObject *class_handle, MVMString *name, const char *kind_desc) MVM_NO_RETURN_ATTRIBUTE;
MVM_NO_RETURN static void invalid_access_kind(MVMThreadContext *tc, const char *action, MVMObject *class_handle, MVMString *name, const char *kind_desc) {
    char *c_name = MVM_string_utf8_encode_C_string(tc, name);
    char *waste[] = { c_name, NULL };
    MVM_exception_throw_adhoc_free(tc, waste, "P6opaque: invalid %s attribute '%s' in type %s for kind %s", action, c_name, MVM_6model_get_debug_name(tc, class_handle), kind_desc);
}

/* Gets the current value for an attribute. */
static void get_attribute(MVMThreadContext *tc, MVMSTable *st, MVMObject *root,
        void *data, MVMObject *class_handle, MVMString *name, MVMint64 hint,
        MVMRegister *result_reg, MVMuint16 kind) {
    MVMP6opaqueREPRData *repr_data = (MVMP6opaqueREPRData *)st->REPR_data;
    MVMint64 slot;
    data = MVM_p6opaque_real_data(tc, data);

    if (!repr_data)
        MVM_exception_throw_adhoc(tc, "P6opaque: must compose %s before using get_attribute", MVM_6model_get_stable_debug_name(tc, st));

    /* Try the slot allocation first. */
    slot = hint >= 0 && hint < repr_data->num_attributes && !(repr_data->mi) ? hint :
        try_get_slot(tc, repr_data, class_handle, name);
    if (slot >= 0) {
        MVMSTable *attr_st = repr_data->flattened_stables[slot];
        switch (kind) {
        case MVM_reg_obj:
        {
            if (!attr_st) {
                MVMObject *result = get_obj_at_offset(data, repr_data->attribute_offsets[slot]);
                if (result) {
                    result_reg->o = result;
                }
                else {
                    /* Maybe we know how to auto-viv it to a container. */
                    if (repr_data->auto_viv_values) {
                        MVMObject *value = repr_data->auto_viv_values[slot];
                        if (value != NULL) {
                            if (IS_CONCRETE(value)) {
                                MVMROOT2(tc, value, root, {
                                    MVMObject *cloned = REPR(value)->allocate(tc, STABLE(value));
                                    /* Ordering here matters. We write the object into the
                                    * register before calling copy_to. This is because
                                    * if copy_to allocates, obj may have moved after
                                    * we called it. This saves us having to put things on
                                    * the temporary stack. The GC will know to update it
                                    * in the register if it moved. */
                                    result_reg->o = cloned;
                                    REPR(value)->copy_to(tc, STABLE(value), OBJECT_BODY(value),
                                        cloned, OBJECT_BODY(cloned));
                                    set_obj_at_offset(tc, root, MVM_p6opaque_real_data(tc, OBJECT_BODY(root)),
                                        repr_data->attribute_offsets[slot], result_reg->o);
                                });
                            }
                            else {
                                set_obj_at_offset(tc, root, data, repr_data->attribute_offsets[slot], value);
                                result_reg->o = value;
                            }
                        }
                        else {
                            result_reg->o = tc->instance->VMNull;
                        }
                    }
                    else {
                        result_reg->o = tc->instance->VMNull;
                    }
                }
            }
            else {
                MVMROOT2(tc, root, attr_st, {
                    /* Need to produce a boxed version of this attribute. */
                    MVMObject *cloned = attr_st->REPR->allocate(tc, attr_st);

                    /* Ordering here matters too. see comments above */
                    result_reg->o = cloned;
                    attr_st->REPR->copy_to(tc, attr_st,
                        (char *)MVM_p6opaque_real_data(tc, OBJECT_BODY(root)) + repr_data->attribute_offsets[slot],
                        cloned, OBJECT_BODY(cloned));
                });
            }
            break;
        }
        case MVM_reg_int64: {
            if (attr_st)
                result_reg->i64 = attr_st->REPR->box_funcs.get_int(tc, attr_st, root,
                    (char *)data + repr_data->attribute_offsets[slot]);
            else
                invalid_access_kind(tc, "native access", class_handle, name, "int64");
            break;
        }
        case MVM_reg_num64: {
            if (attr_st)
                result_reg->n64 = attr_st->REPR->box_funcs.get_num(tc, attr_st, root,
                    (char *)data + repr_data->attribute_offsets[slot]);
            else
                invalid_access_kind(tc, "native access", class_handle, name, "num64");
            break;
        }
        case MVM_reg_str: {
            if (attr_st)
                result_reg->s = attr_st->REPR->box_funcs.get_str(tc, attr_st, root,
                    (char *)data + repr_data->attribute_offsets[slot]);
            else
                invalid_access_kind(tc, "native access", class_handle, name, "str");
            break;
        }
        default: {
            MVM_exception_throw_adhoc(tc, "P6opaque: invalid kind in attribute lookup in %s", MVM_6model_get_stable_debug_name(tc, st));
        }
        }
    }
    else {
        /* Otherwise, complain that the attribute doesn't exist. */
        no_such_attribute(tc, "get a value", class_handle, name, st);
    }
}

/* Binds the given value to the specified attribute. */
static void bind_attribute(MVMThreadContext *tc, MVMSTable *st, MVMObject *root,
        void *data, MVMObject *class_handle, MVMString *name, MVMint64 hint,
        MVMRegister value_reg, MVMuint16 kind) {
    MVMP6opaqueREPRData *repr_data = (MVMP6opaqueREPRData *)st->REPR_data;
    MVMint64 slot;
    data = MVM_p6opaque_real_data(tc, data);

    if (!repr_data)
        MVM_exception_throw_adhoc(tc, "P6opaque: must compose %s before using bind_attribute_boxed", MVM_6model_get_stable_debug_name(tc, st));

    /* Try the slot allocation first. */
    slot = hint >= 0 && hint < repr_data->num_attributes && !(repr_data->mi) ? hint :
        try_get_slot(tc, repr_data, class_handle, name);
    if (slot >= 0) {
        MVMSTable *attr_st = repr_data->flattened_stables[slot];
        switch (kind) {
        case MVM_reg_obj: {
            MVMObject *value = value_reg.o;
            if (attr_st) {
                MVMSTable *value_st = STABLE(value);
                if (attr_st == value_st)
                    value_st->REPR->copy_to(tc, attr_st, OBJECT_BODY(value), root,
                        (char *)data + repr_data->attribute_offsets[slot]);
                else
                    MVM_exception_throw_adhoc(tc,
                        "P6opaque: representation mismatch when storing value (of type %s) to attribute (of type %s)",
                        MVM_6model_get_stable_debug_name(tc, value_st), MVM_6model_get_stable_debug_name(tc, attr_st));
            }
            else {
                set_obj_at_offset(tc, root, data, repr_data->attribute_offsets[slot], value);
            }
            break;
        }
        case MVM_reg_int64: {
            if (attr_st)
                attr_st->REPR->box_funcs.set_int(tc, attr_st, root,
                    (char *)data + repr_data->attribute_offsets[slot],
                    value_reg.i64);
            else
                invalid_access_kind(tc, "native bind to", class_handle, name, "int64");
            break;
        }
        case MVM_reg_num64: {
            if (attr_st)
                attr_st->REPR->box_funcs.set_num(tc, attr_st, root,
                    (char *)data + repr_data->attribute_offsets[slot],
                    value_reg.n64);
            else
                invalid_access_kind(tc, "native bind to", class_handle, name, "num64");
            break;
        }
        case MVM_reg_str: {
            if (attr_st)
                attr_st->REPR->box_funcs.set_str(tc, attr_st, root,
                    (char *)data + repr_data->attribute_offsets[slot],
                    value_reg.s);
            else
                invalid_access_kind(tc, "native bind to", class_handle, name, "str");
            break;
        }
        default: {
            MVM_exception_throw_adhoc(tc, "P6opaque: invalid kind in attribute bind in %s", MVM_6model_get_stable_debug_name(tc, st));
        }
        }
    }
    else {
        /* Otherwise, complain that the attribute doesn't exist. */
        no_such_attribute(tc, "bind a value", class_handle, name, st);
    }
}

/* Checks if an attribute has been initialized. */
static MVMint64 is_attribute_initialized(MVMThreadContext *tc, MVMSTable *st, void *data, MVMObject *class_handle, MVMString *name, MVMint64 hint) {
    MVMP6opaqueREPRData *repr_data = (MVMP6opaqueREPRData *)st->REPR_data;
    MVMint64 slot;

    if (!repr_data)
        MVM_exception_throw_adhoc(tc, "P6opaque: must compose %s before using bind_attribute_boxed", MVM_6model_get_stable_debug_name(tc, st));

    data = MVM_p6opaque_real_data(tc, data);
    /* This can stay commented out until we actually pass something other than NO_HINT
    slot = hint >= 0 && hint < repr_data->num_attributes && !(repr_data->mi) ? hint :
        try_get_slot(tc, repr_data, class_handle, name);
    */
    slot = try_get_slot(tc, repr_data, class_handle, name);
    if (slot >= 0)
        return NULL != get_obj_at_offset(data, repr_data->attribute_offsets[slot]);
    else
        no_such_attribute(tc, "check if it's initialized", class_handle, name, st);
    return 0;
}

/* Gets the hint for the given attribute ID. */
static MVMint64 hint_for(MVMThreadContext *tc, MVMSTable *st, MVMObject *class_key, MVMString *name) {
    MVMint64 slot;
    MVMP6opaqueREPRData *repr_data = (MVMP6opaqueREPRData *)st->REPR_data;
    if (!repr_data)
        return MVM_NO_HINT;
    slot = try_get_slot(tc, repr_data, class_key, name);
    return slot >= 0 ? slot : MVM_NO_HINT;
}

/* Gets an architecture atomic sized native integer attribute as an atomic
 * reference. */
static AO_t * attribute_as_atomic(MVMThreadContext *tc, MVMSTable *st, void *data,
                                  MVMObject *class_handle, MVMString *name,
                                  MVMuint16 kind) {
    MVMP6opaqueREPRData *repr_data = (MVMP6opaqueREPRData *)st->REPR_data;
    MVMint64 slot;
    data = MVM_p6opaque_real_data(tc, data);
    if (!repr_data)
        MVM_exception_throw_adhoc(tc,
            "P6opaque: must compose %s before using get_attribute", MVM_6model_get_stable_debug_name(tc, st));
    slot = try_get_slot(tc, repr_data, class_handle, name);
    if (slot >= 0) {
        if (kind == MVM_reg_int64) {
            MVMSTable *attr_st = repr_data->flattened_stables[slot];
            if (attr_st) {
                const MVMStorageSpec *ss = attr_st->REPR->get_storage_spec(tc, attr_st);
                if (ss->inlineable && ss->boxed_primitive == MVM_STORAGE_SPEC_BP_INT &&
                        ss->bits / 8 == sizeof(AO_t))
                    return (AO_t *)((char *)data + repr_data->attribute_offsets[slot]);
            }
            MVM_exception_throw_adhoc(tc,
                "Can only do an atomic integer operation on an atomicint attribute");
        }
        else if (kind == MVM_reg_obj) {
            return (AO_t *)((char *)data + repr_data->attribute_offsets[slot]);
        }
        else {
            MVM_exception_throw_adhoc(tc,
                "Can only perform atomic operations on object or atomicint attributes");
        }
    }
    else {
        no_such_attribute(tc, "get atomic reference to", class_handle, name, st);
    }
}

/* Used with boxing. Sets an integer value, for representations that can hold
 * one. */
static void set_int(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMint64 value) {
    MVMP6opaqueREPRData *repr_data = (MVMP6opaqueREPRData *)st->REPR_data;
    data = MVM_p6opaque_real_data(tc, data);
    if (repr_data->unbox_int_slot >= 0) {
        MVMSTable *st = repr_data->flattened_stables[repr_data->unbox_int_slot];
        st->REPR->box_funcs.set_int(tc, st, root, (char *)data + repr_data->attribute_offsets[repr_data->unbox_int_slot], value);
    }
    else {
        MVM_exception_throw_adhoc(tc,
            "This type cannot box a native integer: P6opaque, %s", MVM_6model_get_stable_debug_name(tc, st));
    }
}

/* Used with boxing. Gets an integer value, for representations that can
 * hold one. */
static MVMint64 get_int(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data) {
    MVMP6opaqueREPRData *repr_data = (MVMP6opaqueREPRData *)st->REPR_data;
    data = MVM_p6opaque_real_data(tc, data);
    if (repr_data->unbox_int_slot >= 0) {
        MVMSTable *st = repr_data->flattened_stables[repr_data->unbox_int_slot];
        return st->REPR->box_funcs.get_int(tc, st, root, (char *)data + repr_data->attribute_offsets[repr_data->unbox_int_slot]);
    }
    else {
        MVM_exception_throw_adhoc(tc,
            "This type cannot unbox to a native integer: P6opaque, %s", MVM_6model_get_stable_debug_name(tc, st));
    }
}

/* Used with boxing. Sets a floating point value, for representations that can
 * hold one. */
static void set_num(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMnum64 value) {
    MVMP6opaqueREPRData *repr_data = (MVMP6opaqueREPRData *)st->REPR_data;
    data = MVM_p6opaque_real_data(tc, data);
    if (repr_data->unbox_num_slot >= 0) {
        MVMSTable *st = repr_data->flattened_stables[repr_data->unbox_num_slot];
        st->REPR->box_funcs.set_num(tc, st, root, (char *)data + repr_data->attribute_offsets[repr_data->unbox_num_slot], value);
    }
    else {
        MVM_exception_throw_adhoc(tc,
            "This type cannot box a native number: P6opaque, %s", MVM_6model_get_stable_debug_name(tc, st));
    }
}

/* Used with boxing. Gets a floating point value, for representations that can
 * hold one. */
static MVMnum64 get_num(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data) {
    MVMP6opaqueREPRData *repr_data = (MVMP6opaqueREPRData *)st->REPR_data;
    data = MVM_p6opaque_real_data(tc, data);
    if (repr_data->unbox_num_slot >= 0) {
        MVMSTable *st = repr_data->flattened_stables[repr_data->unbox_num_slot];
        return st->REPR->box_funcs.get_num(tc, st, root, (char *)data + repr_data->attribute_offsets[repr_data->unbox_num_slot]);
    }
    else {
        MVM_exception_throw_adhoc(tc,
            "This type cannot unbox to a native number: P6opaque, %s", MVM_6model_get_stable_debug_name(tc, st));
    }
}

/* Used with boxing. Sets a string value, for representations that can hold
 * one. */
static void set_str(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMString *value) {
    MVMP6opaqueREPRData *repr_data = (MVMP6opaqueREPRData *)st->REPR_data;
    data = MVM_p6opaque_real_data(tc, data);
    if (repr_data->unbox_str_slot >= 0) {
        MVMSTable *st = repr_data->flattened_stables[repr_data->unbox_str_slot];
        st->REPR->box_funcs.set_str(tc, st, root, (char *)data + repr_data->attribute_offsets[repr_data->unbox_str_slot], value);
    }
    else {
        MVM_exception_throw_adhoc(tc,
            "This type cannot box a native string: P6opaque, %s", MVM_6model_get_stable_debug_name(tc, st));
    }
}

/* Used with boxing. Gets a string value, for representations that can hold
 * one. */
static MVMString * get_str(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data) {
    MVMP6opaqueREPRData *repr_data = (MVMP6opaqueREPRData *)st->REPR_data;
    data = MVM_p6opaque_real_data(tc, data);
    if (repr_data->unbox_str_slot >= 0) {
        MVMSTable *st = repr_data->flattened_stables[repr_data->unbox_str_slot];
        return st->REPR->box_funcs.get_str(tc, st, root, (char *)data + repr_data->attribute_offsets[repr_data->unbox_str_slot]);
    }
    else {
        MVM_exception_throw_adhoc(tc,
            "This type cannot unbox to a native string: P6opaque, %s", MVM_6model_get_stable_debug_name(tc, st));
    }
}

/* Used with boxing. Sets an unsigned integer value, for representations that can hold
 * one. */
static void set_uint(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMuint64 value) {
    MVMP6opaqueREPRData *repr_data = (MVMP6opaqueREPRData *)st->REPR_data;
    data = MVM_p6opaque_real_data(tc, data);
    if (repr_data->unbox_int_slot >= 0) {
        MVMSTable *st = repr_data->flattened_stables[repr_data->unbox_int_slot];
        st->REPR->box_funcs.set_uint(tc, st, root, (char *)data + repr_data->attribute_offsets[repr_data->unbox_int_slot], value);
    }
    else {
        MVM_exception_throw_adhoc(tc,
            "This type cannot box a native integer: P6opaque, %s", MVM_6model_get_stable_debug_name(tc, st));
    }
}

/* Used with boxing. Gets an unsigned integer value, for representations that can
 * hold one. */
static MVMuint64 get_uint(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data) {
    MVMP6opaqueREPRData *repr_data = (MVMP6opaqueREPRData *)st->REPR_data;
    data = MVM_p6opaque_real_data(tc, data);
    if (repr_data->unbox_int_slot >= 0) {
        MVMSTable *st = repr_data->flattened_stables[repr_data->unbox_int_slot];
        return st->REPR->box_funcs.get_uint(tc, st, root, (char *)data + repr_data->attribute_offsets[repr_data->unbox_int_slot]);
    }
    else {
        MVM_exception_throw_adhoc(tc,
            "This type cannot unbox to a native integer: P6opaque, %s", MVM_6model_get_stable_debug_name(tc, st));
    }
}

static void * get_boxed_ref(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMuint32 repr_id) {
    MVMP6opaqueREPRData *repr_data = (MVMP6opaqueREPRData *)st->REPR_data;
    data = MVM_p6opaque_real_data(tc, data);
    if (repr_data->unbox_slots) {
        MVMuint16 offset = repr_data->unbox_slots[repr_id];
        if (offset != MVM_P6OPAQUE_NO_UNBOX_SLOT)
            return (char *)data + repr_data->attribute_offsets[offset];
    }

    MVM_exception_throw_adhoc(tc,
        "P6opaque: get_boxed_ref could not unbox for the representation '%s' of type %s", MVM_repr_get_by_id(tc, repr_id)->name, MVM_6model_get_stable_debug_name(tc, st));
}

static const MVMStorageSpec default_storage_spec = {
    MVM_STORAGE_SPEC_REFERENCE, /* inlineable */
    0,                          /* bits */
    ALIGNOF(void *),            /* align */
    MVM_STORAGE_SPEC_BP_NONE,   /* boxed_primitive */
    0,                          /* can_box */
    0,                          /* is_unsigned */
};


/* Gets the storage specification for this representation. */
static const MVMStorageSpec * get_storage_spec(MVMThreadContext *tc, MVMSTable *st) {
    MVMP6opaqueREPRData *repr_data = (MVMP6opaqueREPRData *)st->REPR_data;
    if (repr_data)
        return &repr_data->storage_spec;
    return &default_storage_spec;
}

static void mk_storage_spec(MVMThreadContext *tc, MVMP6opaqueREPRData * repr_data, MVMStorageSpec *spec) {

    spec->inlineable      = MVM_STORAGE_SPEC_REFERENCE;
    spec->boxed_primitive = MVM_STORAGE_SPEC_BP_NONE;
    spec->can_box         = 0;

    if (repr_data->unbox_int_slot >= 0)
        spec->can_box += MVM_STORAGE_SPEC_CAN_BOX_INT;
    if (repr_data->unbox_num_slot >= 0)
        spec->can_box += MVM_STORAGE_SPEC_CAN_BOX_NUM;
    if (repr_data->unbox_str_slot >= 0)
        spec->can_box += MVM_STORAGE_SPEC_CAN_BOX_STR;
}

/* Compose the representation. */
static MVMuint16 * allocate_unbox_slots(void) {
    MVMuint16 *slots = MVM_malloc(MVM_REPR_MAX_COUNT * sizeof(MVMuint16));
    MVMuint16 i;
    for (i = 0; i < MVM_REPR_MAX_COUNT; i++)
        slots[i] = MVM_P6OPAQUE_NO_UNBOX_SLOT;
    return slots;
}
static void compose(MVMThreadContext *tc, MVMSTable *st, MVMObject *info_hash) {
    MVMint64   mro_pos, mro_count, num_parents, total_attrs, num_attrs,
               cur_slot, cur_type, cur_obj_attr,
               cur_init_slot, cur_mark_slot, cur_cleanup_slot,
               unboxed_type, i;
    MVMuint64  cur_alloc_addr;
    MVMObject *info;
    MVMP6opaqueREPRData *repr_data;

    MVMStringConsts       str_consts = tc->instance->str_consts;
    MVMString        * const str_avc = str_consts.auto_viv_container;
    MVMString       * const str_name = str_consts.name;
    MVMString       * const str_type = str_consts.type;
    MVMString    * const str_ass_del = str_consts.associative_delegate;
    MVMString    * const str_pos_del = str_consts.positional_delegate;
    MVMString  * const str_attribute = str_consts.attribute;
    MVMString * const str_box_target = str_consts.box_target;

    /* Check not already composed. */
    if (st->REPR_data)
        MVM_exception_throw_adhoc(tc, "Type %s is already composed", MVM_6model_get_stable_debug_name(tc, st));

    /* Find attribute information. */
    info = MVM_repr_at_key_o(tc, info_hash, str_attribute);
    if (MVM_is_null(tc, info))
        MVM_exception_throw_adhoc(tc, "P6opaque: missing attribute protocol in compose of %s", MVM_6model_get_stable_debug_name(tc, st));

    /* Allocate the representation data. */
    repr_data = (MVMP6opaqueREPRData *)MVM_calloc(1, sizeof(MVMP6opaqueREPRData));

    /* In this first pass, we'll loop over the MRO entries, looking for
     * if there is any multiple inheritance and counting the number of
     * attributes. */
    mro_count   = REPR(info)->elems(tc, STABLE(info), info, OBJECT_BODY(info));
    mro_pos     = mro_count;
    total_attrs = 0;
    while (mro_pos--) {
        /* Get info for the class at the current position. */
        MVMObject *class_info = MVM_repr_at_pos_o(tc, info, mro_pos);

        /* Get its list of attributes and parents. */
        MVMObject *attr_list = MVM_repr_at_pos_o(tc, class_info, 1);
        MVMObject *parent_list = MVM_repr_at_pos_o(tc, class_info, 2);

        /* If there's more than one parent, set the multiple inheritance
         * flag (this means we have non-linear layout). */
        num_parents = REPR(parent_list)->elems(tc, STABLE(parent_list),
            parent_list, OBJECT_BODY(parent_list));
        if (num_parents > 1)
            repr_data->mi = 1;

        /* Add attribute count to the running total. */
        total_attrs += REPR(attr_list)->elems(tc, STABLE(attr_list),
            attr_list, OBJECT_BODY(attr_list));
    }

    /* Fill out and allocate other things we now can. */
    repr_data->num_attributes = total_attrs;
    if (total_attrs) {
        repr_data->attribute_offsets   = MVM_malloc(total_attrs * sizeof(MVMuint16));
        repr_data->flattened_stables   = (MVMSTable **)MVM_calloc(total_attrs, sizeof(MVMSTable *));
        repr_data->auto_viv_values     = (MVMObject **)MVM_calloc(total_attrs, sizeof(MVMObject *));
        repr_data->gc_obj_mark_offsets = MVM_malloc(total_attrs * sizeof(MVMuint16));
    }
    repr_data->name_to_index_mapping = (MVMP6opaqueNameMap *)MVM_calloc((mro_count + 1), sizeof(MVMP6opaqueNameMap));
    repr_data->initialize_slots      = MVM_malloc((total_attrs + 1) * sizeof(MVMuint16));
    repr_data->gc_mark_slots         = MVM_malloc((total_attrs + 1) * sizeof(MVMuint16));
    repr_data->gc_cleanup_slots      = MVM_malloc((total_attrs + 1) * sizeof(MVMuint16));

    /* -1 indicates no unboxing or delegate possible for a type. */
    repr_data->unbox_int_slot = -1;
    repr_data->unbox_num_slot = -1;
    repr_data->unbox_str_slot = -1;
    repr_data->pos_del_slot   = -1;
    repr_data->ass_del_slot   = -1;

    /* Second pass populates the rest of the REPR data. */
    mro_pos          = mro_count;
    cur_slot         = 0;
    cur_type         = 0;
    cur_alloc_addr   = sizeof(MVMP6opaqueBody);
    cur_obj_attr     = 0;
    cur_init_slot    = 0;
    cur_mark_slot    = 0;
    cur_cleanup_slot = 0;
    while (mro_pos--) {
        /* Get info for the class at the current position. */
        MVMObject *class_info = MVM_repr_at_pos_o(tc, info, mro_pos);
        MVMObject *type_obj = MVM_repr_at_pos_o(tc, class_info, 0);
        MVMObject *attr_list = MVM_repr_at_pos_o(tc, class_info, 1);

        /* Set up name map entry. */
        MVMP6opaqueNameMap *name_map = &repr_data->name_to_index_mapping[cur_type];
        num_attrs = REPR(attr_list)->elems(tc, STABLE(attr_list),
            attr_list, OBJECT_BODY(attr_list));
        MVM_ASSIGN_REF(tc, &(st->header), name_map->class_key, type_obj);
        name_map->num_attrs = num_attrs;
        if (num_attrs) {
            name_map->names = MVM_malloc(num_attrs * sizeof(MVMString *));
            name_map->slots = MVM_malloc(num_attrs * sizeof(MVMuint16));
        }

        /* Go over the attributes. */
        for (i = 0; i < num_attrs; i++) {
            MVMObject *attr_info = MVM_repr_at_pos_o(tc, attr_list, i);

            /* Extract name, type and if it's a box target. */
            MVMObject *name_obj = MVM_repr_at_key_o(tc, attr_info, str_name);
            MVMObject *type = MVM_repr_at_key_o(tc, attr_info, str_type);
            MVMint64 is_box_target = REPR(attr_info)->ass_funcs.exists_key(tc,
                STABLE(attr_info), attr_info, OBJECT_BODY(attr_info), (MVMObject *)str_box_target);
            MVMint8 inlined = 0;
            MVMuint32 bits;
            MVMuint32 align;

            /* Ensure we have a name. */
            if (MVM_is_null(tc, name_obj)) {
                if (num_attrs) {
                    MVM_free_null(name_map->names);
                    MVM_free_null(name_map->slots);
                }
                free_repr_data(repr_data);
                MVM_exception_throw_adhoc(tc, "P6opaque: %s missing attribute name for attribute %"PRId64, MVM_6model_get_stable_debug_name(tc, st), i);
            }

            if (REPR(name_obj)->ID == MVM_REPR_ID_MVMString) {
                MVM_ASSIGN_REF(tc, &(st->header), name_map->names[i], (MVMString *)name_obj);
            }
            else {
                MVM_ASSIGN_REF(tc, &(st->header), name_map->names[i], MVM_repr_get_str(tc, name_obj));
            }
            name_map->slots[i] = cur_slot;

            /* Consider the type. */
            unboxed_type = MVM_STORAGE_SPEC_BP_NONE;
            bits         = sizeof(MVMObject *) * 8;
            align        = ALIGNOF(void *);
            if (!MVM_is_null(tc, type)) {
                /* Get the storage spec of the type and see what it wants. */
                const MVMStorageSpec *spec = REPR(type)->get_storage_spec(tc, STABLE(type));
                if (spec->inlineable == MVM_STORAGE_SPEC_INLINED) {
                    /* Yes, it's something we'll flatten. */
                    unboxed_type = spec->boxed_primitive;
                    bits = spec->bits;
                    align = spec->align;
                    MVM_ASSIGN_REF(tc, &(st->header), repr_data->flattened_stables[cur_slot], STABLE(type));
                    inlined = 1;

                    /* Does it need special initialization? */
                    if (REPR(type)->initialize) {
                        repr_data->initialize_slots[cur_init_slot] = cur_slot;
                        cur_init_slot++;
                    }

                    /* Does it have special GC needs? */
                    if (REPR(type)->gc_mark) {
                        repr_data->gc_mark_slots[cur_mark_slot] = cur_slot;
                        cur_mark_slot++;
                    }
                    if (REPR(type)->gc_cleanup) {
                        repr_data->gc_cleanup_slots[cur_cleanup_slot] = cur_slot;
                        cur_cleanup_slot++;
                    }

                    /* Is it a target for box/unbox operations? */
                    if (is_box_target) {
                        /* If it boxes a primitive, note that. */
                        switch (unboxed_type) {
                            case MVM_STORAGE_SPEC_BP_INT:
                                if (repr_data->unbox_int_slot >= 0) {
                                    if (num_attrs) {
                                        MVM_free_null(name_map->names);
                                        MVM_free_null(name_map->slots);
                                    }
                                    MVMint16 unbox_int_slot = repr_data->unbox_int_slot;
                                    free_repr_data(repr_data);
                                    MVM_exception_throw_adhoc(tc,
                                        "While composing %s: Duplicate box_target for native int: attributes %d and %"PRId64, MVM_6model_get_stable_debug_name(tc, st), unbox_int_slot, i);
                                }
                                repr_data->unbox_int_slot = cur_slot;
                                break;
                            case MVM_STORAGE_SPEC_BP_NUM:
                                if (repr_data->unbox_num_slot >= 0) {
                                    if (num_attrs) {
                                        MVM_free_null(name_map->names);
                                        MVM_free_null(name_map->slots);
                                    }
                                    MVMint16 unbox_num_slot = repr_data->unbox_num_slot;
                                    free_repr_data(repr_data);
                                    MVM_exception_throw_adhoc(tc,
                                        "While composing %s: Duplicate box_target for native num: attributes %d and %"PRId64, MVM_6model_get_stable_debug_name(tc, st), unbox_num_slot, i);
                                }
                                repr_data->unbox_num_slot = cur_slot;
                                break;
                            case MVM_STORAGE_SPEC_BP_STR:
                                if (repr_data->unbox_str_slot >= 0) {
                                    if (num_attrs) {
                                        MVM_free_null(name_map->names);
                                        MVM_free_null(name_map->slots);
                                    }
                                    MVMint16 unbox_str_slot = repr_data->unbox_str_slot;
                                    free_repr_data(repr_data);
                                    MVM_exception_throw_adhoc(tc,
                                        "While composing %s: Duplicate box_target for native str: attributes %d and %"PRId64, MVM_6model_get_stable_debug_name(tc, st), unbox_str_slot, i);
                                }
                                repr_data->unbox_str_slot = cur_slot;
                                break;
                            default:
                                /* nothing, just suppress 'missing default' warning */
                                break;
                        }

                        /* Also list in the by-repr unbox list. */
                        if (repr_data->unbox_slots == NULL)
                            repr_data->unbox_slots = allocate_unbox_slots();
                        repr_data->unbox_slots[REPR(type)->ID] = cur_slot;
                    }
                }
            }

            /* C structure needs careful alignment. If cur_alloc_addr is not
             * aligned to align bytes (cur_alloc_addr % align), make sure it is
             * before we add the next element. */
            align_to(&cur_alloc_addr, align);

            /* Attribute will live at the current position in the object. */
            repr_data->attribute_offsets[cur_slot] = cur_alloc_addr;

            /* Handle object attributes, which need marking and may have auto-viv needs. */
            if (!inlined) {
                repr_data->gc_obj_mark_offsets[cur_obj_attr] = cur_alloc_addr;
                if (MVM_repr_exists_key(tc, attr_info, str_avc))
                    MVM_ASSIGN_REF(tc, &(st->header), repr_data->auto_viv_values[cur_slot],
                        MVM_repr_at_key_o(tc, attr_info, str_avc));
                cur_obj_attr++;
            }

            /* Is it a positional or associative delegate? */
            if (MVM_repr_exists_key(tc, attr_info, str_pos_del)) {
                if (repr_data->pos_del_slot != -1) {
                    if (num_attrs) {
                        MVM_free_null(name_map->names);
                        MVM_free_null(name_map->slots);
                    }
                    MVMint16 pos_del_slot = repr_data->pos_del_slot;
                    free_repr_data(repr_data);
                    MVM_exception_throw_adhoc(tc,
                        "While composing %s: Duplicate positional delegate attributes: %d and %"PRId64"", MVM_6model_get_stable_debug_name(tc, st), pos_del_slot, cur_slot);
                }
                if (unboxed_type == MVM_STORAGE_SPEC_BP_NONE)
                    repr_data->pos_del_slot = cur_slot;
                else {
                    if (num_attrs) {
                        MVM_free_null(name_map->names);
                        MVM_free_null(name_map->slots);
                    }
                    free_repr_data(repr_data);
                    MVM_exception_throw_adhoc(tc,
                        "While composing %s: Positional delegate attribute must be a reference type", MVM_6model_get_stable_debug_name(tc, st));
                }
            }
            if (MVM_repr_exists_key(tc, attr_info, str_ass_del)) {
                if (repr_data->ass_del_slot != -1) {
                    if (num_attrs) {
                        MVM_free_null(name_map->names);
                        MVM_free_null(name_map->slots);
                    }
                    MVMint16 pos_del_slot = repr_data->pos_del_slot;
                    free_repr_data(repr_data);
                    MVM_exception_throw_adhoc(tc,
                        "While composing %s: Duplicate associative delegate attributes: %d and %"PRId64, MVM_6model_get_stable_debug_name(tc, st), pos_del_slot, cur_slot);
                }
                if (unboxed_type == MVM_STORAGE_SPEC_BP_NONE)
                    repr_data->ass_del_slot = cur_slot;
                else {
                    if (num_attrs) {
                        MVM_free_null(name_map->names);
                        MVM_free_null(name_map->slots);
                    }
                    free_repr_data(repr_data);
                    MVM_exception_throw_adhoc(tc,
                        "While composing %s: Associative delegate attribute must be a reference type", MVM_6model_get_stable_debug_name(tc, st));
                }
            }

            /* Add the required space for this type.
             *
             * Note: This is the only place in this function where cur_alloc_addr can
             * become unaligned.
             */
            cur_alloc_addr += bits / 8;

            /* Increment slot count. */
            cur_slot++;
        }

        /* Increment name map type index. */
        cur_type++;
    }

    align_to(&cur_alloc_addr, ALIGNOF(void *));

    /* Add allocated amount for body to have total object size. */
    st->size = sizeof(MVMP6opaque) + (cur_alloc_addr - sizeof(MVMP6opaqueBody));
    MVM_ASSERT_ALIGNED(st->size, ALIGNOF(void *));

    /* Add sentinels/counts. */
    repr_data->gc_obj_mark_offsets_count = cur_obj_attr;
    repr_data->initialize_slots[cur_init_slot] = -1;
    repr_data->gc_mark_slots[cur_mark_slot] = -1;
    repr_data->gc_cleanup_slots[cur_cleanup_slot] = -1;

    /* Add storage spec */
    mk_storage_spec(tc, repr_data, &repr_data->storage_spec);

    /* Install representation data. */
    st->REPR_data = repr_data;
}

/* Set the size of the STable. */
static void deserialize_stable_size(MVMThreadContext *tc, MVMSTable *st, MVMSerializationReader *reader) {
    /* To calculate size, we need number of attributes and to know about
     * anything flattend in. */
    MVMint64  num_attributes = MVM_serialization_read_int(tc, reader);
    MVMuint64 cur_offset = sizeof(MVMP6opaque);
    MVMint64  i;
    for (i = 0; i < num_attributes; i++) {
        if (MVM_serialization_read_int(tc, reader)) {
            MVMSTable *st = MVM_serialization_read_stable_ref(tc, reader);
            const MVMStorageSpec *ss = st->REPR->get_storage_spec(tc, st);
            if (ss->inlineable) {
                /* TODO: Review if/when we get sub-byte things. */
                align_to(&cur_offset, (MVMuint32)ss->align);
                cur_offset += ss->bits / 8;
            }
            else {
                align_to(&cur_offset, ALIGNOF(MVMObject *));
                cur_offset += sizeof(MVMObject *);
            }
        }
        else {
            align_to(&cur_offset, ALIGNOF(MVMObject *));
            cur_offset += sizeof(MVMObject *);
        }
    }

    align_to(&cur_offset, ALIGNOF(void *));
    st->size = cur_offset;
    MVM_ASSERT_ALIGNED(st->size, ALIGNOF(void *));
}

/* Serializes the REPR data. */
static void serialize_repr_data(MVMThreadContext *tc, MVMSTable *st, MVMSerializationWriter *writer) {
    MVMP6opaqueREPRData *repr_data = (MVMP6opaqueREPRData *)st->REPR_data;
    MVMuint16 i, num_classes;

    if (!repr_data) {
        MVM_gc_allocate_gen2_default_clear(tc);
        MVM_exception_throw_adhoc(tc,
            "Representation for %s must be composed before it can be serialized", MVM_6model_get_stable_debug_name(tc, st));
    }

    MVM_serialization_write_int(tc, writer, repr_data->num_attributes);

    for (i = 0; i < repr_data->num_attributes; i++) {
        MVM_serialization_write_int(tc, writer, repr_data->flattened_stables[i] != NULL);
        if (repr_data->flattened_stables[i])
            MVM_serialization_write_stable_ref(tc, writer, repr_data->flattened_stables[i]);
    }

    MVM_serialization_write_int(tc, writer, repr_data->mi);

    if (repr_data->auto_viv_values) {
        MVM_serialization_write_int(tc, writer, 1);
        for (i = 0; i < repr_data->num_attributes; i++)
            MVM_serialization_write_ref(tc, writer, repr_data->auto_viv_values[i]);
    }
    else {
        MVM_serialization_write_int(tc, writer, 0);
    }

    MVM_serialization_write_int(tc, writer, repr_data->unbox_int_slot);
    MVM_serialization_write_int(tc, writer, repr_data->unbox_num_slot);
    MVM_serialization_write_int(tc, writer, repr_data->unbox_str_slot);

    if (repr_data->unbox_slots) {
        MVMuint32 num_written = 0;
        MVM_serialization_write_int(tc, writer, 1);
        for (i = 0; i < MVM_REPR_MAX_COUNT; i++) {
            if (repr_data->unbox_slots[i] != MVM_P6OPAQUE_NO_UNBOX_SLOT) {
                MVM_serialization_write_int(tc, writer, i);
                MVM_serialization_write_int(tc, writer, repr_data->unbox_slots[i]);
                num_written++;
            }
        }
        for (i = num_written; i < repr_data->num_attributes; i++) {
            MVM_serialization_write_int(tc, writer, 0);
            MVM_serialization_write_int(tc, writer, 0);
        }
    }
    else {
        MVM_serialization_write_int(tc, writer, 0);
    }

    i = 0;
    while (repr_data->name_to_index_mapping[i].class_key)
        i++;
    num_classes = i;
    MVM_serialization_write_int(tc, writer, num_classes);
    for (i = 0; i < num_classes; i++) {
        const MVMuint32 num_attrs = repr_data->name_to_index_mapping[i].num_attrs;
        MVMuint32 j;
        MVM_serialization_write_ref(tc, writer, repr_data->name_to_index_mapping[i].class_key);
        MVM_serialization_write_int(tc, writer, num_attrs);
        for (j = 0; j < num_attrs; j++) {
            MVM_serialization_write_str(tc, writer, repr_data->name_to_index_mapping[i].names[j]);
            MVM_serialization_write_int(tc, writer, repr_data->name_to_index_mapping[i].slots[j]);
        }
    }

    MVM_serialization_write_int(tc, writer, repr_data->pos_del_slot);
    MVM_serialization_write_int(tc, writer, repr_data->ass_del_slot);
}

/* Deserializes representation data. */
static void deserialize_repr_data(MVMThreadContext *tc, MVMSTable *st, MVMSerializationReader *reader) {
    MVMuint16 i, j, num_classes;
    MVMuint64 cur_offset;
    MVMint16 cur_initialize_slot, cur_gc_mark_slot, cur_gc_cleanup_slot;

    MVMP6opaqueREPRData *repr_data = MVM_calloc(1, sizeof(MVMP6opaqueREPRData));

    repr_data->num_attributes = (MVMuint16)MVM_serialization_read_int(tc, reader);

    repr_data->flattened_stables = (MVMSTable **)MVM_malloc(P6OMAX(repr_data->num_attributes, 1) * sizeof(MVMSTable *));
    for (i = 0; i < repr_data->num_attributes; i++)
        if (MVM_serialization_read_int(tc, reader)) {
            MVM_ASSIGN_REF(tc, &(st->header), repr_data->flattened_stables[i], MVM_serialization_read_stable_ref(tc, reader));
        }
        else {
            repr_data->flattened_stables[i] = NULL;
        }

    repr_data->mi = MVM_serialization_read_int(tc, reader);

    if (MVM_serialization_read_int(tc, reader)) {
        repr_data->auto_viv_values = (MVMObject **)MVM_malloc(P6OMAX(repr_data->num_attributes, 1) * sizeof(MVMObject *));
        for (i = 0; i < repr_data->num_attributes; i++)
            MVM_ASSIGN_REF(tc, &(st->header), repr_data->auto_viv_values[i], MVM_serialization_read_ref(tc, reader));
    } else {
        repr_data->auto_viv_values = NULL;
    }

    repr_data->unbox_int_slot = MVM_serialization_read_int(tc, reader);
    repr_data->unbox_num_slot = MVM_serialization_read_int(tc, reader);
    repr_data->unbox_str_slot = MVM_serialization_read_int(tc, reader);

    if (MVM_serialization_read_int(tc, reader)) {
        repr_data->unbox_slots = allocate_unbox_slots();
        for (i = 0; i < repr_data->num_attributes; i++) {
            MVMuint16 repr_id = MVM_serialization_read_int(tc, reader);
            MVMuint16 slot = MVM_serialization_read_int(tc, reader);
            if (slot > repr_data->num_attributes) {
                MVMuint16 num_attributes = repr_data->num_attributes;
                free_repr_data(repr_data);
                MVM_exception_throw_adhoc(tc, "Serialization error: P6opaque's unbox slot out of range (slot %d > %d attributes).", slot, num_attributes);
            }
            if (repr_id < MVM_REPR_MAX_COUNT)
                repr_data->unbox_slots[repr_id] = slot;
            else {
                free_repr_data(repr_data);
                MVM_exception_throw_adhoc(tc, "Serialization error: P6opaque's unbox slot repr id out of range (repr id %d >= %d).", repr_id, MVM_REPR_MAX_COUNT);
            }
        }
    } else {
        repr_data->unbox_slots = NULL;
    }

    num_classes = (MVMuint16)MVM_serialization_read_int(tc, reader);
    repr_data->name_to_index_mapping = (MVMP6opaqueNameMap *)MVM_malloc((num_classes + 1) * sizeof(MVMP6opaqueNameMap));
    for (i = 0; i < num_classes; i++) {
        MVMint32 num_attrs = 0;

        MVM_ASSIGN_REF(tc, &(st->header), repr_data->name_to_index_mapping[i].class_key,
            MVM_serialization_read_ref(tc, reader));

        num_attrs = MVM_serialization_read_int(tc, reader);
        repr_data->name_to_index_mapping[i].names = (MVMString **)MVM_malloc(P6OMAX(num_attrs, 1) * sizeof(MVMString *));
        repr_data->name_to_index_mapping[i].slots = (MVMuint16 *)MVM_malloc(P6OMAX(num_attrs, 1) * sizeof(MVMuint16));
        for (j = 0; j < num_attrs; j++) {
            MVM_ASSIGN_REF(tc, &(st->header), repr_data->name_to_index_mapping[i].names[j],
                MVM_serialization_read_str(tc, reader));

            repr_data->name_to_index_mapping[i].slots[j] = (MVMuint16)MVM_serialization_read_int(tc, reader);
        }

        repr_data->name_to_index_mapping[i].num_attrs = num_attrs;
    }

    /* set the last one to be NULL */
    repr_data->name_to_index_mapping[i].class_key = NULL;

    repr_data->pos_del_slot = (MVMint16)MVM_serialization_read_int(tc, reader);
    repr_data->ass_del_slot = (MVMint16)MVM_serialization_read_int(tc, reader);

    /* Re-calculate the remaining info, which is platform specific or
     * derived information. */
    repr_data->attribute_offsets   = (MVMuint16 *)MVM_malloc(P6OMAX(repr_data->num_attributes, 1) * sizeof(MVMuint16));
    repr_data->gc_obj_mark_offsets = (MVMuint16 *)MVM_malloc(P6OMAX(repr_data->num_attributes, 1) * sizeof(MVMuint16));
    repr_data->initialize_slots    = (MVMint16 *)MVM_malloc((repr_data->num_attributes + 1) * sizeof(MVMint16));
    repr_data->gc_mark_slots       = (MVMint16 *)MVM_malloc((repr_data->num_attributes + 1) * sizeof(MVMint16));
    repr_data->gc_cleanup_slots    = (MVMint16 *)MVM_malloc((repr_data->num_attributes + 1) * sizeof(MVMint16));
    repr_data->gc_obj_mark_offsets_count = 0;
    cur_offset          = sizeof(MVMP6opaqueBody);
    cur_initialize_slot = 0;
    cur_gc_mark_slot    = 0;
    cur_gc_cleanup_slot = 0;
    for (i = 0; i < repr_data->num_attributes; i++) {
        if (repr_data->flattened_stables[i] == NULL) {
            align_to(&cur_offset, ALIGNOF(MVMObject *));
            /* Store position. */
            repr_data->attribute_offsets[i] = cur_offset;

            /* Reference type. Needs marking. */
            repr_data->gc_obj_mark_offsets[repr_data->gc_obj_mark_offsets_count] = cur_offset;
            repr_data->gc_obj_mark_offsets_count++;

            /* Increment by pointer size. */
            cur_offset += sizeof(MVMObject *);
        }
        else {
            /* Store position. */
            MVMSTable *cur_st = repr_data->flattened_stables[i];
            const MVMStorageSpec *spec = cur_st->REPR->get_storage_spec(tc, cur_st);
            /* Set up flags for initialization and GC. */
            if (cur_st->REPR->initialize)
                repr_data->initialize_slots[cur_initialize_slot++] = i;
            if (cur_st->REPR->gc_mark)
                repr_data->gc_mark_slots[cur_gc_mark_slot++] = i;
            if (cur_st->REPR->gc_cleanup)
                repr_data->gc_cleanup_slots[cur_gc_cleanup_slot++] = i;

            if (spec->align == 0) {
                free_repr_data(repr_data);
                MVM_exception_throw_adhoc(tc, "Serialization error: Storage Spec of P6opaque must not have align set to 0.");
            }

            align_to(&cur_offset, spec->align);

            repr_data->attribute_offsets[i] = cur_offset;

            /* Increment by size reported by representation. */
            cur_offset += spec->bits / 8;
        }
    }
    assert(cur_offset <= st->size + sizeof(MVMP6opaqueBody) - sizeof(MVMP6opaque));
    repr_data->initialize_slots[cur_initialize_slot] = -1;
    repr_data->gc_mark_slots[cur_gc_mark_slot] = -1;
    repr_data->gc_cleanup_slots[cur_gc_cleanup_slot] = -1;

    mk_storage_spec(tc, repr_data, &repr_data->storage_spec);

    st->REPR_data = repr_data;
}

/* Deserializes the data. */
static void deserialize(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMSerializationReader *reader) {
    MVMP6opaqueREPRData *repr_data = (MVMP6opaqueREPRData *)st->REPR_data;
    MVMuint16 num_attributes = repr_data->num_attributes;
    MVMuint16 i;
    data = MVM_p6opaque_real_data(tc, data);
    for (i = 0; i < num_attributes; i++) {
        MVMuint16 a_offset = repr_data->attribute_offsets[i];
        MVMSTable *a_st = repr_data->flattened_stables[i];
        if (a_st)
            a_st->REPR->deserialize(tc, a_st, root, (char *)data + a_offset, reader);
        else
            set_obj_at_offset(tc, root, data, a_offset, MVM_serialization_read_ref(tc, reader));
    }
}

/* Serializes the object's body. */
static void serialize(MVMThreadContext *tc, MVMSTable *st, void *data, MVMSerializationWriter *writer) {
    MVMP6opaqueREPRData *repr_data = (MVMP6opaqueREPRData *)st->REPR_data;
    MVMuint16 num_attributes;
    MVMuint16 i;

    if (!repr_data)
        MVM_exception_throw_adhoc(tc,
            "Representation of %s must be composed before it can be serialized", MVM_6model_get_stable_debug_name(tc, st));

    num_attributes = repr_data->num_attributes;

    data = MVM_p6opaque_real_data(tc, data);

    for (i = 0; i < num_attributes; i++) {
        MVMuint16  a_offset = repr_data->attribute_offsets[i];
        MVMSTable *a_st     = repr_data->flattened_stables[i];
        if (a_st) {
            if (a_st->REPR->serialize)
                a_st->REPR->serialize(tc, a_st, (char *)data + a_offset, writer);
            else
                MVM_exception_throw_adhoc(tc, "Missing serialize REPR function for REPR %s in type %s", a_st->REPR->name, MVM_6model_get_stable_debug_name(tc, a_st));
        }
        else
            MVM_serialization_write_ref(tc, writer, get_obj_at_offset(data, a_offset));
    }
}

/* Performs a change of type, where possible. */
static void change_type(MVMThreadContext *tc, MVMObject *obj, MVMObject *new_type) {
    MVMP6opaqueREPRData *cur_repr_data = (MVMP6opaqueREPRData *)STABLE(obj)->REPR_data;
    MVMP6opaqueREPRData *new_repr_data = (MVMP6opaqueREPRData *)STABLE(new_type)->REPR_data;
    MVMP6opaqueNameMap *cur_map_entry, *new_map_entry;

    /* Ensure we don't have a type object. */
    if (!IS_CONCRETE(obj))
        MVM_exception_throw_adhoc(tc,
            "Cannot change the type of a %s type object", MVM_6model_get_debug_name(tc, obj));

    /* If the type is already the same, nothing to do. */
    if (STABLE(obj) == STABLE(new_type))
        return;

    /* Ensure that the REPR of the new type is also P6opaque. */
    if (REPR(new_type)->ID != REPR(obj)->ID)
        MVM_exception_throw_adhoc(tc,
            "New type for %s must have a matching representation (P6opaque vs %s)", MVM_6model_get_debug_name(tc, obj), REPR(new_type)->name);

    /* Ensure the target type is a mixin type. */
    if (!STABLE(new_type)->is_mixin_type)
        MVM_exception_throw_adhoc(tc,
            "New type %s for %s is not a mixin type",
            MVM_6model_get_debug_name(tc, new_type),
            MVM_6model_get_debug_name(tc, obj));

    /* Ensure the MRO prefixes match up. */
    cur_map_entry = cur_repr_data->name_to_index_mapping;
    new_map_entry = new_repr_data->name_to_index_mapping;
    while (cur_map_entry->class_key != NULL && cur_map_entry->num_attrs == 0)
        cur_map_entry++;
    while (new_map_entry->class_key != NULL && new_map_entry->num_attrs == 0)
        new_map_entry++;
    while (cur_map_entry->class_key != NULL) {
        if (new_map_entry->class_key == NULL || new_map_entry->class_key != cur_map_entry->class_key)
            MVM_exception_throw_adhoc(tc,
                "Incompatible MROs in P6opaque rebless for types %s and %s", MVM_6model_get_debug_name(tc, obj), MVM_6model_get_debug_name(tc, new_type));
        cur_map_entry++;
        new_map_entry++;
    }

    /* Resize if needed. */
    if (STABLE(obj)->size != STABLE(new_type)->size) {
        /* Get current object body. */
        MVMP6opaqueBody *body = (MVMP6opaqueBody *)OBJECT_BODY(obj);
        void            *old  = body->replaced ? body->replaced : body;

        /* Allocate new memory. */
        size_t  new_size = STABLE(new_type)->size - sizeof(MVMObject);
        void   *new = MVM_malloc(new_size);
        memset((char *)new + (STABLE(obj)->size - sizeof(MVMObject)),
            0, new_size - (STABLE(obj)->size - sizeof(MVMObject)));

        /* Copy existing to new.
         * XXX Need more care here, as may have to re-barrier pointers. */
        memcpy(new, old, STABLE(obj)->size - sizeof(MVMObject));

        /* Pointer switch, taking care of existing body issues. */
        if (body->replaced) {
            body->replaced = new;
            MVM_free(old);
        }
        else {
            body->replaced = new;
        }
    }

    /* Finally, ready to switch over the STable. */
    MVM_ASSIGN_REF(tc, &(obj->header), obj->st, STABLE(new_type));
}

static void die_no_pos_del(MVMThreadContext *tc, MVMSTable *st) {
    MVM_exception_throw_adhoc(tc, "This type (%s) does not support positional operations", MVM_6model_get_stable_debug_name(tc, st));
}

static void at_pos(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMint64 index, MVMRegister *value, MVMuint16 kind) {
    MVMP6opaqueREPRData *repr_data = (MVMP6opaqueREPRData *)st->REPR_data;
    MVMObject *del;
    if (repr_data->pos_del_slot == -1)
        die_no_pos_del(tc, st);
    data = MVM_p6opaque_real_data(tc, data);
    del = get_obj_at_offset(data, repr_data->attribute_offsets[repr_data->pos_del_slot]);
    REPR(del)->pos_funcs.at_pos(tc, STABLE(del), del, OBJECT_BODY(del), index, value, kind);
}
void MVM_P6opaque_at_pos(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMint64 index, MVMRegister *value, MVMuint16 kind) {
    at_pos(tc, st, root, data, index, value, kind);
}

static void bind_pos(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMint64 index, MVMRegister value, MVMuint16 kind) {
    MVMP6opaqueREPRData *repr_data = (MVMP6opaqueREPRData *)st->REPR_data;
    MVMObject *del;
    if (repr_data->pos_del_slot == -1)
        die_no_pos_del(tc, st);
    data = MVM_p6opaque_real_data(tc, data);
    del = get_obj_at_offset(data, repr_data->attribute_offsets[repr_data->pos_del_slot]);
    REPR(del)->pos_funcs.bind_pos(tc, STABLE(del), del, OBJECT_BODY(del), index, value, kind);
}

static void set_elems(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMuint64 count) {
    MVMP6opaqueREPRData *repr_data = (MVMP6opaqueREPRData *)st->REPR_data;
    MVMObject *del;
    if (repr_data->pos_del_slot == -1)
        die_no_pos_del(tc, st);
    data = MVM_p6opaque_real_data(tc, data);
    del = get_obj_at_offset(data, repr_data->attribute_offsets[repr_data->pos_del_slot]);
    REPR(del)->pos_funcs.set_elems(tc, STABLE(del), del, OBJECT_BODY(del), count);
}

static void push(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMRegister value, MVMuint16 kind) {
    MVMP6opaqueREPRData *repr_data = (MVMP6opaqueREPRData *)st->REPR_data;
    MVMObject *del;
    if (repr_data->pos_del_slot == -1)
        die_no_pos_del(tc, st);
    data = MVM_p6opaque_real_data(tc, data);
    del = get_obj_at_offset(data, repr_data->attribute_offsets[repr_data->pos_del_slot]);
    REPR(del)->pos_funcs.push(tc, STABLE(del), del, OBJECT_BODY(del), value, kind);
}

static void pop(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMRegister *value, MVMuint16 kind) {
    MVMP6opaqueREPRData *repr_data = (MVMP6opaqueREPRData *)st->REPR_data;
    MVMObject *del;
    if (repr_data->pos_del_slot == -1)
        die_no_pos_del(tc, st);
    data = MVM_p6opaque_real_data(tc, data);
    del = get_obj_at_offset(data, repr_data->attribute_offsets[repr_data->pos_del_slot]);
    REPR(del)->pos_funcs.pop(tc, STABLE(del), del, OBJECT_BODY(del), value, kind);
}

static void unshift(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMRegister value, MVMuint16 kind) {
    MVMP6opaqueREPRData *repr_data = (MVMP6opaqueREPRData *)st->REPR_data;
    MVMObject *del;
    if (repr_data->pos_del_slot == -1)
        die_no_pos_del(tc, st);
    data = MVM_p6opaque_real_data(tc, data);
    del = get_obj_at_offset(data, repr_data->attribute_offsets[repr_data->pos_del_slot]);
    REPR(del)->pos_funcs.unshift(tc, STABLE(del), del, OBJECT_BODY(del), value, kind);
}

static void shift(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMRegister *value, MVMuint16 kind) {
    MVMP6opaqueREPRData *repr_data = (MVMP6opaqueREPRData *)st->REPR_data;
    MVMObject *del;
    if (repr_data->pos_del_slot == -1)
        die_no_pos_del(tc, st);
    data = MVM_p6opaque_real_data(tc, data);
    del = get_obj_at_offset(data, repr_data->attribute_offsets[repr_data->pos_del_slot]);
    REPR(del)->pos_funcs.shift(tc, STABLE(del), del, OBJECT_BODY(del), value, kind);
}

static void oslice(MVMThreadContext *tc, MVMSTable *st, MVMObject *src, void *data, MVMObject *dest, MVMint64 start, MVMint64 end) {
    MVMP6opaqueREPRData *repr_data = (MVMP6opaqueREPRData *)st->REPR_data;
    MVMObject *del;
    if (repr_data->pos_del_slot == -1)
        die_no_pos_del(tc, st);
    data = MVM_p6opaque_real_data(tc, data);
    del = get_obj_at_offset(data, repr_data->attribute_offsets[repr_data->pos_del_slot]);
    REPR(del)->pos_funcs.slice(tc, STABLE(del), del, OBJECT_BODY(del), dest, start, end);
}

static void osplice(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMObject *target_array, MVMint64 offset, MVMuint64 elems) {
    MVMP6opaqueREPRData *repr_data = (MVMP6opaqueREPRData *)st->REPR_data;
    MVMObject *del;
    if (repr_data->pos_del_slot == -1)
        die_no_pos_del(tc, st);
    data = MVM_p6opaque_real_data(tc, data);
    del = get_obj_at_offset(data, repr_data->attribute_offsets[repr_data->pos_del_slot]);
    REPR(del)->pos_funcs.splice(tc, STABLE(del), del, OBJECT_BODY(del), target_array, offset, elems);
}

static void die_no_ass_del(MVMThreadContext *tc, MVMSTable *st) {
    MVM_exception_throw_adhoc(tc, "This type (%s) does not support associative operations", MVM_6model_get_stable_debug_name(tc, st));
}

static void at_key(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMObject *key, MVMRegister *result, MVMuint16 kind) {
    MVMP6opaqueREPRData *repr_data = (MVMP6opaqueREPRData *)st->REPR_data;
    MVMObject *del;
    if (repr_data->ass_del_slot == -1)
        die_no_ass_del(tc, st);
    data = MVM_p6opaque_real_data(tc, data);
    del = get_obj_at_offset(data, repr_data->attribute_offsets[repr_data->ass_del_slot]);
    REPR(del)->ass_funcs.at_key(tc, STABLE(del), del, OBJECT_BODY(del), key, result, kind);
}

static void bind_key(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMObject *key, MVMRegister value, MVMuint16 kind) {
    MVMP6opaqueREPRData *repr_data = (MVMP6opaqueREPRData *)st->REPR_data;
    MVMObject *del;
    if (repr_data->ass_del_slot == -1)
        die_no_ass_del(tc, st);
    data = MVM_p6opaque_real_data(tc, data);
    del = get_obj_at_offset(data, repr_data->attribute_offsets[repr_data->ass_del_slot]);
    REPR(del)->ass_funcs.bind_key(tc, STABLE(del), del, OBJECT_BODY(del), key, value, kind);
}

static MVMint64 exists_key(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMObject *key) {
    MVMP6opaqueREPRData *repr_data = (MVMP6opaqueREPRData *)st->REPR_data;
    MVMObject *del;
    if (repr_data->ass_del_slot == -1)
        die_no_ass_del(tc, st);
    data = MVM_p6opaque_real_data(tc, data);
    del = get_obj_at_offset(data, repr_data->attribute_offsets[repr_data->ass_del_slot]);
    return REPR(del)->ass_funcs.exists_key(tc, STABLE(del), del, OBJECT_BODY(del), key);
}

static void delete_key(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMObject *key) {
    MVMP6opaqueREPRData *repr_data = (MVMP6opaqueREPRData *)st->REPR_data;
    MVMObject *del;
    if (repr_data->ass_del_slot == -1)
        die_no_ass_del(tc, st);
    data = MVM_p6opaque_real_data(tc, data);
    del = get_obj_at_offset(data, repr_data->attribute_offsets[repr_data->ass_del_slot]);
    REPR(del)->ass_funcs.delete_key(tc, STABLE(del), del, OBJECT_BODY(del), key);
}

static MVMuint64 elems(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data) {
    MVMP6opaqueREPRData *repr_data = (MVMP6opaqueREPRData *)st->REPR_data;
    data = MVM_p6opaque_real_data(tc, data);
    if (repr_data->pos_del_slot >= 0) {
        MVMObject *del = get_obj_at_offset(data, repr_data->attribute_offsets[repr_data->pos_del_slot]);
        return REPR(del)->elems(tc, STABLE(del), del, OBJECT_BODY(del));
    }
    else if (repr_data->ass_del_slot >= 0) {
        MVMObject *del = get_obj_at_offset(data, repr_data->attribute_offsets[repr_data->ass_del_slot]);
        return REPR(del)->elems(tc, STABLE(del), del, OBJECT_BODY(del));
    }
    else {
        MVM_exception_throw_adhoc(tc, "This type (%s) does not support elems", MVM_6model_get_stable_debug_name(tc, st));
    }
}

/* Bytecode specialization for this REPR. */
static void add_slot_name_comment(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshIns *ins,
                                  MVMString *name, MVMSpeshFacts *type_handle_facts, MVMSTable *st) {
    if (MVM_spesh_debug_enabled(tc)) {
        char *name_cstr = MVM_string_utf8_encode_C_string(tc, name);
        if (type_handle_facts->type != st->WHAT) {
            MVM_spesh_graph_add_comment(tc, g, ins, "%s of '%s' in %s of a %s",
                    ins->info->name,
                    name_cstr,
                    MVM_6model_get_debug_name(tc, type_handle_facts->type),
                    MVM_6model_get_stable_debug_name(tc, st));
        }
        else {
            MVM_spesh_graph_add_comment(tc, g, ins, "%s of '%s' in %s",
                    ins->info->name,
                    name_cstr,
                    MVM_6model_get_debug_name(tc, type_handle_facts->type));
        }
        MVM_free(name_cstr);
    }
}
static MVMString * spesh_attr_name(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshOperand o, MVMint32 indirect) {
    if (indirect) {
        MVMSpeshFacts *name_facts = MVM_spesh_get_and_use_facts(tc, g, o);
        if (name_facts->flags & MVM_SPESH_FACT_KNOWN_VALUE)
            return name_facts->value.s;
        else
            return NULL;
    }
    else {
        return MVM_spesh_get_string(tc, g, o);
    }
}
static void spesh(MVMThreadContext *tc, MVMSTable *st, MVMSpeshGraph *g, MVMSpeshBB *bb, MVMSpeshIns *ins) {
    MVMP6opaqueREPRData * repr_data = (MVMP6opaqueREPRData *)st->REPR_data;
    MVMuint16             opcode    = ins->info->opcode;
    if (!repr_data)
        return;
    switch (opcode) {
    case MVM_OP_create: {
        /* Create can be optimized if there are no initialization slots. */
        if (repr_data->initialize_slots[0] < 0 && !(st->mode_flags & MVM_FINALIZE_TYPE)) {
            MVMSpeshOperand target   = ins->operands[0];
            MVMSpeshOperand type     = ins->operands[1];
            MVMSpeshFacts *tgt_facts = MVM_spesh_get_facts(tc, g, target);

            ins->info                = MVM_op_get_op(MVM_OP_sp_fastcreate);
            ins->operands            = MVM_spesh_alloc(tc, g, 3 * sizeof(MVMSpeshOperand));
            ins->operands[0]         = target;
            ins->operands[1].lit_i16 = st->size;
            ins->operands[2].lit_i16 = MVM_spesh_add_spesh_slot(tc, g, (MVMCollectable *)st);
            MVM_spesh_usages_delete_by_reg(tc, g, type, ins);

            MVM_spesh_graph_add_comment(tc, g, ins, "%s of a %s",
                    ins->info->name,
                    MVM_6model_get_stable_debug_name(tc, st));

            tgt_facts->flags |= MVM_SPESH_FACT_KNOWN_TYPE | MVM_SPESH_FACT_CONCRETE;
            tgt_facts->type = st->WHAT;
        }
        break;
    }
    case MVM_OP_getattr_o:
    case MVM_OP_getattrs_o: {
        MVMSpeshFacts *obj_facts = MVM_spesh_get_and_use_facts(tc, g, ins->operands[1]);
        MVMSpeshFacts *ch_facts  = MVM_spesh_get_and_use_facts(tc, g, ins->operands[2]);
        MVMString     *name      = spesh_attr_name(tc, g, ins->operands[3], opcode == MVM_OP_getattrs_o);
        if (name && ch_facts->flags & MVM_SPESH_FACT_KNOWN_TYPE && ch_facts->type
            && obj_facts->flags & MVM_SPESH_FACT_CONCRETE) {
            MVMint64 slot = try_get_slot(tc, repr_data, ch_facts->type, name);
            if (slot >= 0 && !repr_data->flattened_stables[slot]) {
                MVMint8 mixin = st->is_mixin_type;
                add_slot_name_comment(tc, g, ins, name, ch_facts, st);
                if (repr_data->auto_viv_values && repr_data->auto_viv_values[slot]) {
                    MVMObject *av_value = repr_data->auto_viv_values[slot];
                    if (IS_CONCRETE(av_value)) {
                        ins->info = MVM_op_get_op(mixin
                                ? MVM_OP_sp_p6ogetvc_o
                                : MVM_OP_sp_getvc_o);
                    }
                    else {
                        ins->info = MVM_op_get_op(mixin
                                ? MVM_OP_sp_p6ogetvt_o
                                : MVM_OP_sp_getvt_o);
                    }
                    if (opcode == MVM_OP_getattrs_o)
                        MVM_spesh_usages_delete_by_reg(tc, g, ins->operands[3], ins);
                    MVM_spesh_usages_delete_by_reg(tc, g, ins->operands[2], ins);
                    ins->operands[2].lit_i16 = mixin
                        ? repr_data->attribute_offsets[slot]
                        : sizeof(MVMObject) + repr_data->attribute_offsets[slot];
                    ins->operands[3].lit_i16 = MVM_spesh_add_spesh_slot(tc, g,
                        (MVMCollectable *)av_value);
                }
                else {
                    if (opcode == MVM_OP_getattrs_o)
                        MVM_spesh_usages_delete_by_reg(tc, g, ins->operands[3], ins);
                    MVM_spesh_usages_delete_by_reg(tc, g, ins->operands[2], ins);
                    if (mixin) {
                        ins->info = MVM_op_get_op(MVM_OP_sp_p6oget_o);
                        ins->operands[2].lit_i16 = repr_data->attribute_offsets[slot];
                    }
                    else {
                        ins->info = MVM_op_get_op(MVM_OP_sp_get_o);
                        ins->operands[2].lit_i16 = sizeof(MVMObject) +
                            repr_data->attribute_offsets[slot];
                    }
                }
            }
        }
        break;
    }
    case MVM_OP_getattr_i:
    case MVM_OP_getattrs_i: {
        MVMSpeshFacts *obj_facts = MVM_spesh_get_and_use_facts(tc, g, ins->operands[1]);
        MVMSpeshFacts *ch_facts = MVM_spesh_get_and_use_facts(tc, g, ins->operands[2]);
        MVMString     *name     = spesh_attr_name(tc, g, ins->operands[3], opcode == MVM_OP_getattrs_i);
        if (name && ch_facts->flags & MVM_SPESH_FACT_KNOWN_TYPE && ch_facts->type
            && obj_facts->flags & MVM_SPESH_FACT_CONCRETE) {
            MVMint64 slot = try_get_slot(tc, repr_data, ch_facts->type, name);
            if (slot >= 0 && repr_data->flattened_stables[slot]) {
                MVMSTable      *flat_st = repr_data->flattened_stables[slot];
                const MVMStorageSpec *flat_ss = flat_st->REPR->get_storage_spec(tc, flat_st);
                add_slot_name_comment(tc, g, ins, name, ch_facts, st);
                if (flat_st->REPR->ID == MVM_REPR_ID_P6int &&
                        (flat_ss->bits == 64 || flat_ss->bits == 32)) {
                    if (opcode == MVM_OP_getattrs_i)
                        MVM_spesh_usages_delete_by_reg(tc, g, ins->operands[3], ins);
                    MVM_spesh_usages_delete_by_reg(tc, g, ins->operands[2], ins);
                    ins->info = MVM_op_get_op(
                            flat_ss->bits == 64 ? MVM_OP_sp_p6oget_i
                                                : MVM_OP_sp_p6oget_i32
                            );
                    ins->operands[2].lit_i16 = repr_data->attribute_offsets[slot];
                }
            }
        }
        break;
    }
    case MVM_OP_getattr_n:
    case MVM_OP_getattrs_n: {
        MVMSpeshFacts *obj_facts = MVM_spesh_get_and_use_facts(tc, g, ins->operands[1]);
        MVMSpeshFacts *ch_facts = MVM_spesh_get_and_use_facts(tc, g, ins->operands[2]);
        MVMString     *name     = spesh_attr_name(tc, g, ins->operands[3], opcode == MVM_OP_getattrs_n);
        if (name && ch_facts->flags & MVM_SPESH_FACT_KNOWN_TYPE && ch_facts->type
            && obj_facts->flags & MVM_SPESH_FACT_CONCRETE) {
            MVMint64 slot = try_get_slot(tc, repr_data, ch_facts->type, name);
            if (slot >= 0 && repr_data->flattened_stables[slot]) {
                MVMSTable      *flat_st = repr_data->flattened_stables[slot];
                const MVMStorageSpec *flat_ss = flat_st->REPR->get_storage_spec(tc, flat_st);
                if (flat_st->REPR->ID == MVM_REPR_ID_P6num && flat_ss->bits == 64) {
                    add_slot_name_comment(tc, g, ins, name, ch_facts, st);
                    if (opcode == MVM_OP_getattrs_n)
                        MVM_spesh_usages_delete_by_reg(tc, g, ins->operands[3], ins);
                    MVM_spesh_usages_delete_by_reg(tc, g, ins->operands[2], ins);
                    ins->info = MVM_op_get_op(MVM_OP_sp_p6oget_n);
                    ins->operands[2].lit_i16 = repr_data->attribute_offsets[slot];
                }
            }
        }
        break;
    }
    case MVM_OP_getattr_s:
    case MVM_OP_getattrs_s: {
        MVMSpeshFacts *obj_facts = MVM_spesh_get_and_use_facts(tc, g, ins->operands[1]);
        MVMSpeshFacts *ch_facts = MVM_spesh_get_and_use_facts(tc, g, ins->operands[2]);
        MVMString     *name     = spesh_attr_name(tc, g, ins->operands[3], opcode == MVM_OP_getattrs_s);
        if (name && ch_facts->flags & MVM_SPESH_FACT_KNOWN_TYPE && ch_facts->type
            && obj_facts->flags & MVM_SPESH_FACT_CONCRETE) {
            MVMint64 slot = try_get_slot(tc, repr_data, ch_facts->type, name);
            if (slot >= 0 && repr_data->flattened_stables[slot]) {
                MVMSTable      *flat_st = repr_data->flattened_stables[slot];
                if (flat_st->REPR->ID == MVM_REPR_ID_P6str) {
                    add_slot_name_comment(tc, g, ins, name, ch_facts, st);
                    if (opcode == MVM_OP_getattrs_s)
                        MVM_spesh_usages_delete_by_reg(tc, g, ins->operands[3], ins);
                    MVM_spesh_usages_delete_by_reg(tc, g, ins->operands[2], ins);
                    ins->info = MVM_op_get_op(MVM_OP_sp_p6oget_s);
                    ins->operands[2].lit_i16 = repr_data->attribute_offsets[slot];
                }
            }
        }
        break;
    }
    case MVM_OP_bindattr_o:
    case MVM_OP_bindattrs_o: {
        MVMSpeshFacts *obj_facts = MVM_spesh_get_and_use_facts(tc, g, ins->operands[0]);
        MVMSpeshFacts *ch_facts = MVM_spesh_get_and_use_facts(tc, g, ins->operands[1]);
        MVMString     *name     = spesh_attr_name(tc, g, ins->operands[2], opcode == MVM_OP_bindattrs_o);
        if (name && ch_facts->flags & MVM_SPESH_FACT_KNOWN_TYPE && ch_facts->type
            && obj_facts->flags & MVM_SPESH_FACT_CONCRETE) {
            MVMint64 slot = try_get_slot(tc, repr_data, ch_facts->type, name);
            if (slot >= 0 && !repr_data->flattened_stables[slot]) {
                MVMint8 mixin = st->is_mixin_type;
                add_slot_name_comment(tc, g, ins, name, ch_facts, st);
                if (opcode == MVM_OP_bindattrs_o)
                    MVM_spesh_usages_delete_by_reg(tc, g, ins->operands[2], ins);
                MVM_spesh_usages_delete_by_reg(tc, g, ins->operands[1], ins);
                if (mixin) {
                    ins->info = MVM_op_get_op(MVM_OP_sp_p6obind_o);
                    ins->operands[1].lit_i16 = repr_data->attribute_offsets[slot];
                }
                else {
                    ins->info = MVM_op_get_op(MVM_OP_sp_bind_o);
                    ins->operands[1].lit_i16 = sizeof(MVMObject) + repr_data->attribute_offsets[slot];
                }
                ins->operands[2] = ins->operands[3];
            }
        }
        break;
    }
    case MVM_OP_bindattr_i:
    case MVM_OP_bindattrs_i: {
        MVMSpeshFacts *obj_facts = MVM_spesh_get_and_use_facts(tc, g, ins->operands[0]);
        MVMSpeshFacts *ch_facts = MVM_spesh_get_and_use_facts(tc, g, ins->operands[1]);
        MVMString     *name     = spesh_attr_name(tc, g, ins->operands[2], opcode == MVM_OP_bindattrs_i);
        if (name && ch_facts->flags & MVM_SPESH_FACT_KNOWN_TYPE && ch_facts->type
            && obj_facts->flags & MVM_SPESH_FACT_CONCRETE) {
            MVMint64 slot = try_get_slot(tc, repr_data, ch_facts->type, name);
            if (slot >= 0 && repr_data->flattened_stables[slot]) {
                MVMSTable      *flat_st = repr_data->flattened_stables[slot];
                const MVMStorageSpec *flat_ss = flat_st->REPR->get_storage_spec(tc, flat_st);
                if (flat_st->REPR->ID == MVM_REPR_ID_P6int &&
                        (flat_ss->bits == 64 || flat_ss->bits == 32)) {
                    add_slot_name_comment(tc, g, ins, name, ch_facts, st);
                    if (opcode == MVM_OP_bindattrs_i)
                        MVM_spesh_usages_delete_by_reg(tc, g, ins->operands[2], ins);
                    MVM_spesh_usages_delete_by_reg(tc, g, ins->operands[1], ins);
                    ins->info = MVM_op_get_op(
                            flat_ss->bits == 64 ? MVM_OP_sp_p6obind_i
                                                : MVM_OP_sp_p6obind_i32
                            );
                    ins->operands[1].lit_i16 = repr_data->attribute_offsets[slot];
                    ins->operands[2] = ins->operands[3];
                }
            }
        }
        break;
    }
    case MVM_OP_bindattr_n:
    case MVM_OP_bindattrs_n: {
        MVMSpeshFacts *obj_facts = MVM_spesh_get_and_use_facts(tc, g, ins->operands[0]);
        MVMSpeshFacts *ch_facts = MVM_spesh_get_and_use_facts(tc, g, ins->operands[1]);
        MVMString     *name     = spesh_attr_name(tc, g, ins->operands[2], opcode == MVM_OP_bindattrs_n);
        if (name && ch_facts->flags & MVM_SPESH_FACT_KNOWN_TYPE && ch_facts->type
            && obj_facts->flags & MVM_SPESH_FACT_CONCRETE) {
            MVMint64 slot = try_get_slot(tc, repr_data, ch_facts->type, name);
            if (slot >= 0 && repr_data->flattened_stables[slot]) {
                MVMSTable      *flat_st = repr_data->flattened_stables[slot];
                const MVMStorageSpec *flat_ss = flat_st->REPR->get_storage_spec(tc, flat_st);
                if (flat_st->REPR->ID == MVM_REPR_ID_P6num && flat_ss->bits == 64) {
                    add_slot_name_comment(tc, g, ins, name, ch_facts, st);
                    if (opcode == MVM_OP_bindattrs_n)
                        MVM_spesh_usages_delete_by_reg(tc, g, ins->operands[2], ins);
                    MVM_spesh_usages_delete_by_reg(tc, g, ins->operands[1], ins);
                    ins->info = MVM_op_get_op(MVM_OP_sp_p6obind_n);
                    ins->operands[1].lit_i16 = repr_data->attribute_offsets[slot];
                    ins->operands[2] = ins->operands[3];
                }
            }
        }
        break;
    }
    case MVM_OP_bindattr_s:
    case MVM_OP_bindattrs_s: {
        MVMSpeshFacts *obj_facts = MVM_spesh_get_and_use_facts(tc, g, ins->operands[0]);
        MVMSpeshFacts *ch_facts = MVM_spesh_get_and_use_facts(tc, g, ins->operands[1]);
        MVMString     *name     = spesh_attr_name(tc, g, ins->operands[2], opcode == MVM_OP_bindattrs_s);
        if (name && ch_facts->flags & MVM_SPESH_FACT_KNOWN_TYPE && ch_facts->type
            && obj_facts->flags & MVM_SPESH_FACT_CONCRETE) {
            MVMint64 slot = try_get_slot(tc, repr_data, ch_facts->type, name);
            if (slot >= 0 && repr_data->flattened_stables[slot]) {
                MVMSTable      *flat_st = repr_data->flattened_stables[slot];
                if (flat_st->REPR->ID == MVM_REPR_ID_P6str) {
                    add_slot_name_comment(tc, g, ins, name, ch_facts, st);
                    if (opcode == MVM_OP_bindattrs_s)
                        MVM_spesh_usages_delete_by_reg(tc, g, ins->operands[2], ins);
                    MVM_spesh_usages_delete_by_reg(tc, g, ins->operands[1], ins);
                    ins->info = MVM_op_get_op(MVM_OP_sp_p6obind_s);
                    ins->operands[1].lit_i16 = repr_data->attribute_offsets[slot];
                    ins->operands[2] = ins->operands[3];
                }
            }
        }
        break;
    }
    case MVM_OP_box_i:
        if (repr_data->num_attributes == 1 && repr_data->unbox_int_slot >= 0 &&
                !(st->mode_flags & MVM_FINALIZE_TYPE)) {
            MVMSTable *embedded_st = repr_data->flattened_stables[repr_data->unbox_int_slot];
            if (embedded_st->REPR->ID == MVM_REPR_ID_P6bigint) {
                MVMSpeshFacts *value_facts = MVM_spesh_get_facts(tc, g, ins->operands[1]);

                /* Turn into a sp_fastbox_bi[_ic] instruction. */
                MVMint32 int_cache_type_idx = MVM_intcache_type_index(tc, st->WHAT);
                MVMSpeshFacts *tgt_facts = MVM_spesh_get_facts(tc, g, ins->operands[0]);
                MVMSpeshOperand *orig_operands = ins->operands;
                ins->info = MVM_op_get_op(int_cache_type_idx < 0
                        ? MVM_OP_sp_fastbox_bi
                        : MVM_OP_sp_fastbox_bi_ic);
                ins->operands = MVM_spesh_alloc(tc, g, 6 * sizeof(MVMSpeshOperand));
                ins->operands[0] = orig_operands[0];
                ins->operands[1].lit_i16 = st->size;
                ins->operands[2].lit_i16 = MVM_spesh_add_spesh_slot(tc, g, (MVMCollectable *)st);
                ins->operands[3].lit_i16 = sizeof(MVMObject) +
                    repr_data->attribute_offsets[repr_data->unbox_int_slot];
                ins->operands[4] = orig_operands[1];
                ins->operands[5].lit_i16 = (MVMint16)int_cache_type_idx;
                MVM_spesh_usages_delete_by_reg(tc, g, orig_operands[2], ins);
                tgt_facts->flags |= MVM_SPESH_FACT_KNOWN_TYPE | MVM_SPESH_FACT_CONCRETE | MVM_SPESH_FACT_KNOWN_BOX_SRC;
                tgt_facts->type = st->WHAT;

                MVM_spesh_graph_add_comment(tc, g, ins, "box_i into a %s",
                        MVM_6model_get_stable_debug_name(tc, st));

                if (value_facts->flags & MVM_SPESH_FACT_KNOWN_VALUE) {
                    MVM_spesh_graph_add_comment(tc, g, ins, "could have made literal");
                }
            }
        }
        break;
    case MVM_OP_box_n:
        if (repr_data->num_attributes == 1 && repr_data->unbox_num_slot >= 0 &&
                !(st->mode_flags & MVM_FINALIZE_TYPE)) {
            MVMSTable *embedded_st = repr_data->flattened_stables[repr_data->unbox_num_slot];
            if (embedded_st->REPR->ID == MVM_REPR_ID_P6num) {
                /* Prepend a fastcreate instruction. */
                MVMSpeshIns *fastcreate = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshIns));
                MVMSpeshFacts *tgt_facts = MVM_spesh_get_facts(tc, g, ins->operands[0]);
                fastcreate->info = MVM_op_get_op(MVM_OP_sp_fastcreate);
                fastcreate->operands = MVM_spesh_alloc(tc, g, 3 * sizeof(MVMSpeshOperand));
                fastcreate->operands[0] = ins->operands[0];
                tgt_facts->writer = fastcreate;
                fastcreate->operands[1].lit_i16 = st->size;
                fastcreate->operands[2].lit_i16 = MVM_spesh_add_spesh_slot(tc, g, (MVMCollectable *)st);
                MVM_spesh_manipulate_insert_ins(tc, bb, ins->prev, fastcreate);

                MVM_spesh_graph_add_comment(tc, g, fastcreate, "box_n into a %s",
                        MVM_6model_get_stable_debug_name(tc, st));

                tgt_facts->flags |= MVM_SPESH_FACT_KNOWN_TYPE | MVM_SPESH_FACT_CONCRETE | MVM_SPESH_FACT_KNOWN_BOX_SRC;
                tgt_facts->type = st->WHAT;

                /* Change instruction to a bind. */
                MVM_spesh_usages_delete_by_reg(tc, g, ins->operands[2], ins);
                ins->info = MVM_op_get_op(MVM_OP_sp_bind_n);
                ins->operands[2] = ins->operands[1];
                ins->operands[1].lit_i16 = sizeof(MVMObject) +
                    repr_data->attribute_offsets[repr_data->unbox_num_slot];
                MVM_spesh_usages_add_by_reg(tc, g, ins->operands[0], ins);
            }
        }
        break;
    case MVM_OP_box_s:
        if (repr_data->num_attributes == 1 && repr_data->unbox_str_slot >= 0 &&
                !(st->mode_flags & MVM_FINALIZE_TYPE)) {
            MVMSTable *embedded_st = repr_data->flattened_stables[repr_data->unbox_str_slot];
            if (embedded_st->REPR->ID == MVM_REPR_ID_P6str) {
                MVMSpeshFacts *value_facts = MVM_spesh_get_facts(tc, g, ins->operands[1]);

                /* Prepend a fastcreate instruction. */
                MVMSpeshIns *fastcreate = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshIns));
                MVMSpeshFacts *tgt_facts = MVM_spesh_get_facts(tc, g, ins->operands[0]);
                fastcreate->info = MVM_op_get_op(MVM_OP_sp_fastcreate);
                fastcreate->operands = MVM_spesh_alloc(tc, g, 3 * sizeof(MVMSpeshOperand));
                fastcreate->operands[0] = ins->operands[0];
                tgt_facts->writer = fastcreate;
                fastcreate->operands[1].lit_i16 = st->size;
                fastcreate->operands[2].lit_i16 = MVM_spesh_add_spesh_slot(tc, g, (MVMCollectable *)st);
                MVM_spesh_manipulate_insert_ins(tc, bb, ins->prev, fastcreate);

                MVM_spesh_graph_add_comment(tc, g, fastcreate, "box_s into a %s",
                        MVM_6model_get_stable_debug_name(tc, st));

                tgt_facts->flags |= MVM_SPESH_FACT_KNOWN_TYPE | MVM_SPESH_FACT_CONCRETE | MVM_SPESH_FACT_KNOWN_BOX_SRC;
                tgt_facts->type = st->WHAT;

                /* Change instruction to a bind. */
                MVM_spesh_usages_delete_by_reg(tc, g, ins->operands[2], ins);
                ins->info = MVM_op_get_op(MVM_OP_sp_bind_s_nowb);
                ins->operands[2] = ins->operands[1];
                ins->operands[1].lit_i16 = sizeof(MVMObject) +
                    repr_data->attribute_offsets[repr_data->unbox_str_slot];
                MVM_spesh_usages_add_by_reg(tc, g, ins->operands[0], ins);

                if (value_facts->flags & MVM_SPESH_FACT_KNOWN_VALUE) {
                    MVM_spesh_graph_add_comment(tc, g, ins, "would have known literal value");
                }
            }
        }
        break;
    case MVM_OP_unbox_i:
    case MVM_OP_decont_i:
        if (repr_data->unbox_int_slot >= 0) {
            MVMSTable *embedded_st = repr_data->flattened_stables[repr_data->unbox_int_slot];
            if (embedded_st->REPR->ID == MVM_REPR_ID_P6bigint) {
                MVMSpeshFacts *facts = MVM_spesh_get_and_use_facts(tc, g, ins->operands[1]);
                int have_value = 0;
                MVMint64 value;

                if ((facts->flags & MVM_SPESH_FACT_KNOWN_VALUE) && IS_CONCRETE(facts->value.o)) {
                    /* Effectively this is the call chain for
                     * MVM_repr_get_int(tc, facts->value.o)
                     * inlined all the way to P6bigint.c:mp_get_int64
                     * We have to do this because that will throw for an out of
                     * range integer, whereas we need to handle that case here.
                     */
                    void * data = OBJECT_BODY(facts->value.o);
                    data = MVM_p6opaque_real_data(tc, data);
                    MVMP6bigintBody *body = (MVMP6bigintBody *)((char *)data + repr_data->attribute_offsets[repr_data->unbox_int_slot]);

                    if (MVM_BIGINT_IS_BIG(body)) {
                        mp_int *i = body->u.bigint;
                        const int bits = mp_count_bits(i);
                        if (bits <= 63) {
                            MVMuint64 res = mp_get_mag_ull(i);
                            value = MP_NEG == i->sign ? -res : res;
                            have_value = 1;
                        }
                        else if (bits == 64
                                 && MP_NEG == i->sign
                                 && mp_get_mag_ull(i) == 9223372036854775808ULL) {
                            value = -9223372036854775807ULL - 1;
                            have_value = 1;
                        }
                    }
                    else {
                        value = body->u.smallint.value;
                        have_value = 1;
                    }
                }

                if (have_value) {
                    MVMSpeshFacts *target_facts = MVM_spesh_get_and_use_facts(tc, g, ins->operands[0]);
                    MVM_spesh_graph_add_comment(tc, g, ins, "unboxed literal to value %ld", value);
                    ins->info = MVM_op_get_op(MVM_OP_const_i64);
                    MVM_spesh_usages_delete_by_reg(tc, g, ins->operands[1], ins);
                    ins->operands[1].lit_i64 = value;
                    target_facts->flags |= MVM_SPESH_FACT_KNOWN_VALUE;
                    target_facts->value.i = value;
                }
                else if (facts->flags & MVM_SPESH_FACT_CONCRETE) {
                    MVMSpeshOperand *orig_operands = ins->operands;
                    MVM_spesh_graph_add_comment(tc, g, ins, "%s a %s",
                        ins->info->name, MVM_6model_get_stable_debug_name(tc, st));
                    ins->info = MVM_op_get_op(MVM_OP_sp_p6oget_bi);
                    ins->operands = MVM_spesh_alloc(tc, g, 3 * sizeof(MVMSpeshOperand));
                    ins->operands[0] = orig_operands[0];
                    ins->operands[1] = orig_operands[1];
                    ins->operands[2].lit_i16 = repr_data->attribute_offsets[repr_data->unbox_int_slot];
                }
            }
        }
        break;
    case MVM_OP_unbox_n:
    case MVM_OP_decont_n:
        if (repr_data->unbox_num_slot >= 0) {
            MVMSTable *embedded_st = repr_data->flattened_stables[repr_data->unbox_num_slot];
            if (embedded_st->REPR->ID == MVM_REPR_ID_P6num) {
                MVMSpeshFacts *facts = MVM_spesh_get_and_use_facts(tc, g, ins->operands[1]);
                if ((facts->flags & MVM_SPESH_FACT_KNOWN_VALUE) && IS_CONCRETE(facts->value.o)) {
                    MVMnum64 result = MVM_repr_get_num(tc, facts->value.o);
                    MVMSpeshFacts *target_facts = MVM_spesh_get_and_use_facts(tc, g, ins->operands[0]);
                    MVM_spesh_graph_add_comment(tc, g, ins, "unboxed literal to value %f", result);
                    ins->info = MVM_op_get_op(MVM_OP_const_n64);
                    MVM_spesh_usages_delete_by_reg(tc, g, ins->operands[1], ins);
                    ins->operands[1].lit_n64 = result;
                    target_facts->flags |= MVM_SPESH_FACT_KNOWN_VALUE;
                    target_facts->value.n = result;
                }
                else if (facts->flags & MVM_SPESH_FACT_CONCRETE) {
                    MVMSpeshOperand *orig_operands = ins->operands;
                    MVM_spesh_graph_add_comment(tc, g, ins, "%s a %s",
                        ins->info->name, MVM_6model_get_stable_debug_name(tc, st));
                    ins->info = MVM_op_get_op(MVM_OP_sp_p6oget_n);
                    ins->operands = MVM_spesh_alloc(tc, g, 3 * sizeof(MVMSpeshOperand));
                    ins->operands[0] = orig_operands[0];
                    ins->operands[1] = orig_operands[1];
                    ins->operands[2].lit_i16 = repr_data->attribute_offsets[repr_data->unbox_num_slot];
                }
            }
        }
        break;
    case MVM_OP_unbox_s:
    case MVM_OP_decont_s:
        if (repr_data->unbox_str_slot >= 0) {
            MVMSTable *embedded_st = repr_data->flattened_stables[repr_data->unbox_str_slot];
            if (embedded_st->REPR->ID == MVM_REPR_ID_P6str) {
                MVMSpeshFacts *facts = MVM_spesh_get_and_use_facts(tc, g, ins->operands[1]);
                if ((facts->flags & MVM_SPESH_FACT_KNOWN_VALUE) && IS_CONCRETE(facts->value.o)) {
                    MVMString *result = MVM_repr_get_str(tc, facts->value.o);
                    MVMSpeshFacts *target_facts = MVM_spesh_get_and_use_facts(tc, g, ins->operands[0]);
                    MVM_spesh_graph_add_comment(tc, g, ins, "unboxed literal string", result);
                    ins->info = MVM_op_get_op(MVM_OP_sp_getspeshslot);
                    MVM_spesh_usages_delete_by_reg(tc, g, ins->operands[1], ins);
                    ins->operands[1].lit_i16 = MVM_spesh_add_spesh_slot_try_reuse(tc, g, (MVMCollectable *)result);
                    target_facts->flags |= MVM_SPESH_FACT_KNOWN_VALUE;
                    target_facts->value.s = result;
                }
                else if (facts->flags & MVM_SPESH_FACT_CONCRETE) {
                    MVMSpeshOperand *orig_operands = ins->operands;
                    MVM_spesh_graph_add_comment(tc, g, ins, "%s a %s",
                        ins->info->name, MVM_6model_get_stable_debug_name(tc, st));
                    ins->info = MVM_op_get_op(MVM_OP_sp_p6oget_s);
                    ins->operands = MVM_spesh_alloc(tc, g, 3 * sizeof(MVMSpeshOperand));
                    ins->operands[0] = orig_operands[0];
                    ins->operands[1] = orig_operands[1];
                    ins->operands[2].lit_i16 = repr_data->attribute_offsets[repr_data->unbox_str_slot];
                }
            }
        }
        break;
    default:
        MVM_spesh_graph_add_comment(tc, g, ins, "reprop %s unsupported in P6Opaque %s",
                ins->info->name,
                MVM_6model_get_stable_debug_name(tc, st));
    }
}

/* Initializes the representation. */
const MVMREPROps * MVMP6opaque_initialize(MVMThreadContext *tc) {
    return &P6opaque_this_repr;
}

static const MVMREPROps P6opaque_this_repr = {
    type_object_for,
    allocate,
    initialize,
    copy_to,
    {
        get_attribute,
        bind_attribute,
        hint_for,
        is_attribute_initialized,
        attribute_as_atomic
    },    /* attr_funcs */
    {
        set_int,
        get_int,
        set_num,
        get_num,
        set_str,
        get_str,
        set_uint,
        get_uint,
        get_boxed_ref
    },    /* box_funcs */
    {
        at_pos,
        bind_pos,
        set_elems,
        push,
        pop,
        unshift,
        shift,
        oslice,
        osplice,
        MVM_REPR_DEFAULT_AT_POS_MULTIDIM_NO_MULTIDIM,
        MVM_REPR_DEFAULT_BIND_POS_MULTIDIM_NO_MULTIDIM,
        MVM_REPR_DEFAULT_DIMENSIONS_NO_MULTIDIM,
        MVM_REPR_DEFAULT_SET_DIMENSIONS_NO_MULTIDIM,
        MVM_REPR_DEFAULT_GET_ELEM_STORAGE_SPEC,
        MVM_REPR_DEFAULT_POS_AS_ATOMIC,
        MVM_REPR_DEFAULT_POS_AS_ATOMIC_MULTIDIM,
        MVM_REPR_DEFAULT_POS_WRITE_BUF,
        MVM_REPR_DEFAULT_POS_READ_BUF,
    },    /* pos_funcs */
    {
        at_key,
        bind_key,
        exists_key,
        delete_key,
        NULL
    },    /* ass_funcs */
    elems,
    get_storage_spec,
    change_type,
    serialize,
    deserialize, /* deserialize */
    serialize_repr_data,
    deserialize_repr_data,
    deserialize_stable_size,
    gc_mark,
    gc_free,
    NULL, /* gc_cleanup */
    gc_mark_repr_data,
    gc_free_repr_data,
    compose,
    spesh,
    "P6opaque", /* name */
    MVM_REPR_ID_P6opaque,
    NULL, /* unmanaged_size */
    NULL, /* describe_refs */
};

/* Get the pointer offset of an attribute. Used for optimizing access to it on
 * precisely known types. */
size_t MVM_p6opaque_attr_offset(MVMThreadContext *tc, MVMObject *type,
                                MVMObject *class_handle, MVMString *name) {
    MVMP6opaqueREPRData *repr_data = (MVMP6opaqueREPRData *)type->st->REPR_data;
    size_t slot = try_get_slot(tc, repr_data, class_handle, name);
    return repr_data->attribute_offsets[slot];
}

/* Get the pointer offset of an attribute along with the kind of arg type it
 * is. Only valid on attribute types that can be passed as arguments. */
void MVM_p6opaque_attr_offset_and_arg_type(MVMThreadContext *tc, MVMObject *type,
        MVMObject *class_handle, MVMString *name, size_t *offset_out, MVMCallsiteFlags *type_out) {
    MVMP6opaqueREPRData *repr_data = (MVMP6opaqueREPRData *)type->st->REPR_data;
    size_t slot = try_get_slot(tc, repr_data, class_handle, name);
    *offset_out = repr_data->attribute_offsets[slot];
    MVMSTable *flattened = repr_data->flattened_stables[slot];
    if (flattened == NULL) {
        *type_out = MVM_CALLSITE_ARG_OBJ;
    }
    else if (flattened->REPR->ID == MVM_REPR_ID_P6int) {
        *type_out = MVM_CALLSITE_ARG_INT;
    }
    else if (flattened->REPR->ID == MVM_REPR_ID_P6num) {
        *type_out = MVM_CALLSITE_ARG_NUM;
    }
    else if (flattened->REPR->ID == MVM_REPR_ID_P6str) {
        *type_out = MVM_CALLSITE_ARG_STR;
    }
    else {
        MVM_exception_throw_adhoc(tc, "Cannot use this kind of attribute like an argument");
    }
}

/* Find the offset into the object of a bigint attribute. */
MVMuint16 MVM_p6opaque_get_bigint_offset(MVMThreadContext *tc, MVMSTable *st) {
    MVMP6opaqueREPRData *repr_data = (MVMP6opaqueREPRData *)st->REPR_data;
    if (repr_data->unbox_slots) {
        MVMuint16 slot = repr_data->unbox_slots[MVM_REPR_ID_P6bigint];
        if (slot != MVM_P6OPAQUE_NO_UNBOX_SLOT)
            return sizeof(MVMObject) + repr_data->attribute_offsets[slot];
    }
    return 0;
}

/* Gets the attribute index given we know the slot offset. */
MVMuint32 MVM_p6opaque_offset_to_attr_idx(MVMThreadContext *tc, MVMObject *type, size_t offset) {
    MVMP6opaqueREPRData *repr_data = (MVMP6opaqueREPRData *)type->st->REPR_data;
    MVMuint32 i;
    for (i = 0; i < repr_data->num_attributes; i++)
        if (repr_data->attribute_offsets[i] == offset)
            return i;
    MVM_oops(tc, "P6opaque: slot offset not found");
}

#ifdef DEBUG_HELPERS
/* This is meant to be called in a debugging session and not used anywhere else.
 * Please don't delete. */
MVM_VECTOR_DECL(MVMObject*, dump_p6opaque_seen);
static void dump_p6opaque(MVMThreadContext *tc, MVMObject *obj, int nested) {
    MVMP6opaqueREPRData *repr_data = (MVMP6opaqueREPRData *)STABLE(obj)->REPR_data;
    MVMP6opaqueBody *data = MVM_p6opaque_real_data(tc, OBJECT_BODY(obj));
    MVM_VECTOR_PUSH(dump_p6opaque_seen, obj);
    if (repr_data) {
        MVMint16 const num_attributes = repr_data->num_attributes;
        MVMint16 cur_attribute = 0;
        MVMP6opaqueNameMap * const name_to_index_mapping = repr_data->name_to_index_mapping;
        if (!IS_CONCRETE(obj)) {
            fprintf(stderr, "(%s", MVM_6model_get_debug_name(tc, obj));
            fprintf(stderr, nested ? ")" : ")\n");
            return;
        }
        fprintf(stderr, "%s.new(", MVM_6model_get_debug_name(tc, obj));
        if (name_to_index_mapping != NULL) {
            MVMP6opaqueNameMap *cur_map_entry = name_to_index_mapping;

            while (cur_map_entry->class_key != NULL) {
                MVMuint32 i;
                MVMint64 slot;
                if (cur_map_entry->num_attrs > 0) {
                    fprintf(stderr, "#`(from %s) ", MVM_6model_get_stable_debug_name(tc, cur_map_entry->class_key->st));
                }
                for (i = 0; i < cur_map_entry->num_attrs; i++) {
                    char * name = MVM_string_utf8_encode_C_string(tc, cur_map_entry->names[i]);
                    fprintf(stderr, "%s", name);
                    MVM_free(name);

                    slot = cur_map_entry->slots[i];
                    if (slot >= 0) {
                        MVMuint16 const offset = repr_data->attribute_offsets[slot];
                        MVMSTable * const attr_st = repr_data->flattened_stables[slot];
                        if (attr_st == NULL) {
                            MVMObject *value = get_obj_at_offset(data, offset);
                            if (value != NULL && REPR(value)->ID == MVM_REPR_ID_P6opaque) {
                                fprintf(stderr, "=");
                                size_t i = 0;
                                int recurse = 1;
                                for (; i < MVM_VECTOR_ELEMS(dump_p6opaque_seen); i++)
                                    if (dump_p6opaque_seen[i] == value)
                                        recurse = 0;
                                if (recurse)
                                    dump_p6opaque(tc, value, 1);
                                else
                                    fprintf(stderr, "<already seen>");
                            }
                            if (value != NULL && REPR(value)->ID == MVM_REPR_ID_MVMCode) {
                                MVMCode *code = (MVMCode*)value;
                                MVMStaticFrame *sf = code->body.sf;
                                fprintf(
                                    stderr,
                                    "=%p %s (%s/cuuid %s)",
                                    code,
                                    code->body.name ? MVM_string_utf8_maybe_encode_C_string(tc, code->body.name) : "<null>",
                                    sf->body.name   ? MVM_string_utf8_maybe_encode_C_string(tc, sf->body.name)   : "<null>",
                                    sf->body.cuuid  ? MVM_string_utf8_maybe_encode_C_string(tc, sf->body.cuuid)  : "<null>"
                                );
                            }
                        }
                        else {
                            if (attr_st->REPR->ID == MVM_REPR_ID_P6str) {
                                MVMString *str = (MVMString *)get_obj_at_offset(data, offset);
                                char * const c_str = str ? MVM_string_utf8_encode_C_string(tc, str) : "<null>";
                                fprintf(stderr, "='%s'", c_str);
                                if (str)
                                    MVM_free(c_str);
                            }
                            else if (attr_st->REPR->ID == MVM_REPR_ID_P6int) {
                                MVMint64 val = attr_st->REPR->box_funcs.get_int(tc, attr_st, obj, (char *)data + offset);
                                fprintf(stderr, "=%"PRIi64, val);
                            }
                            else {
                                fprintf(stderr, "[%d]=%s", repr_data->attribute_offsets[slot], MVM_6model_get_stable_debug_name(tc, attr_st));
                            }
                            /*MVMString * const s = attr_st->REPR->box_funcs.get_str(tc, attr_st, obj, (char *)data + offset);*/
                            /*char * const str = MVM_string_utf8_encode_C_string(tc, s);*/
                            /*fprintf(stderr, "='%s'", str);*/
                            /*MVM_free(str);*/
                        }
                    }
                    if (cur_attribute++ < num_attributes - 1)
                        fprintf(stderr, ", ");
                }
                cur_map_entry++;
            }
        }
        fprintf(stderr, nested ? ")" : ")\n");
    }
    else {
        fprintf(stderr, "%s%s", MVM_6model_get_debug_name(tc, obj), nested ? "" : "\n");
    }
}

void MVM_dump_p6opaque(MVMThreadContext *tc, MVMObject *obj) {
    MVM_VECTOR_INIT(dump_p6opaque_seen, 8);
    dump_p6opaque(tc, obj, 0);
    MVM_VECTOR_DESTROY(dump_p6opaque_seen);
}
#endif
