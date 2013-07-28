#include "moarvm.h"

#define P6OMAX(x, y) ((y) > (x) ? (y) : (x))
#define REFVAR_VM_HASH_STR_VAR 10

/* This representation's function pointer table. */
static MVMREPROps *this_repr;

/* Some strings. */
static MVMString *str_name       = NULL;
static MVMString *str_type       = NULL;
static MVMString *str_box_target = NULL;
static MVMString *str_attribute  = NULL;

/* If an object gets mixed in to, we need to be sure we look at is real body,
 * which may have been moved to hang off the specified pointer. */
static void * real_data(void *data) {
    MVMP6opaqueBody *body = (MVMP6opaqueBody *)data;
    return body->replaced ? body->replaced : data;
}

/* Helpers for reading/writing values. */
static MVMint64 get_int_at_offset(void *data, MVMint64 offset) {
    void *location = (char *)data + offset;
    return *((MVMint64 *)location);
}
static void set_int_at_offset(void *data, MVMint64 offset, MVMint64 value) {
    void *location = (char *)data + offset;
    *((MVMint64 *)location) = value;
}
static MVMnum64 get_num_at_offset(void *data, MVMint64 offset) {
    void *location = (char *)data + offset;
    return *((MVMnum64 *)location);
}
static void set_num_at_offset(void *data, MVMint64 offset, MVMnum64 value) {
    void *location = (char *)data + offset;
    *((MVMnum64 *)location) = value;
}
static MVMString * get_str_at_offset(void *data, MVMint64 offset) {
    void *location = (char *)data + offset;
    return *((MVMString **)location);
}
static void set_str_at_offset(MVMThreadContext *tc, MVMObject *root, void *data, MVMint64 offset, MVMString *value) {
    void *location = (char *)data + offset;
    MVM_ASSIGN_REF(tc, root, *((MVMString **)location), value);
}
static MVMObject * get_obj_at_offset(void *data, MVMint64 offset) {
    void *location = (char *)data + offset;
    return *((MVMObject **)location);
}
static void set_obj_at_offset(MVMThreadContext *tc, MVMObject *root, void *data, MVMint64 offset, MVMObject *value) {
    void *location = (char *)data + offset;
    MVM_ASSIGN_REF(tc, root, *((MVMObject **)location), value);
}

/* Helper for finding a slot number. */
static MVMint64 try_get_slot(MVMThreadContext *tc, MVMP6opaqueREPRData *repr_data, MVMObject *class_key, MVMString *name) {
    MVMint64 slot = -1;
    if (repr_data->name_to_index_mapping) {
        P6opaqueNameMap *cur_map_entry = repr_data->name_to_index_mapping;
        while (cur_map_entry->class_key != NULL) {
            if (cur_map_entry->class_key == class_key) {
                MVMint16 i;
                for (i = 0; i < cur_map_entry->num_attrs; i++) {
                    if (MVM_string_equal(tc, cur_map_entry->names[i], name)) {
                        slot = cur_map_entry->slots[i];
                        break;
                    }
                }
            }
            cur_map_entry++;
        }
    }
    return slot;
}

/* Creates a new type object of this representation, and associates it with
 * the given HOW. */
static MVMObject * type_object_for(MVMThreadContext *tc, MVMObject *HOW) {
    MVMSTable *st;
    MVMObject *obj;

    st = MVM_gc_allocate_stable(tc, this_repr, HOW);
    MVMROOT(tc, st, {
        obj = MVM_gc_allocate_type_object(tc, st);
        MVM_ASSIGN_REF(tc, st, st->WHAT, obj);
        st->size = 0; /* Is updated later. */
    });

    return st->WHAT;
}

/* Creates a new instance based on the type object. */
static MVMObject * allocate(MVMThreadContext *tc, MVMSTable *st) {
    if (st->size)
        return MVM_gc_allocate_object(tc, st);
    else
        MVM_exception_throw_adhoc(tc, "P6opaque: must compose before allocating");
}

/* Initializes a new instance. */
static void initialize(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data) {
    MVMP6opaqueREPRData * repr_data = (MVMP6opaqueREPRData *)st->REPR_data;
    data = real_data(data);
    if (repr_data) {
        MVMint64 i;
        for (i = 0; repr_data->initialize_slots[i] >= 0; i++) {
            MVMint64   offset = repr_data->attribute_offsets[repr_data->initialize_slots[i]];
            MVMSTable *st     = repr_data->flattened_stables[repr_data->initialize_slots[i]];
            st->REPR->initialize(tc, st, root, (char *)data + offset);
        }
    }
    else {
        MVM_exception_throw_adhoc(tc, "P6opaque: must compose before using initialize");
    }
}

/* Copies the body of one object to another. */
static void copy_to(MVMThreadContext *tc, MVMSTable *st, void *src, MVMObject *dest_root, void *dest) {
    MVMP6opaqueREPRData *repr_data = (MVMP6opaqueREPRData *)st->REPR_data;
    MVMuint16 i;
    src = real_data(src);

    /* Flattened in REPRs need a chance to copy 'emselves. */
    for (i = 0; i < repr_data->num_attributes; i++) {
        MVMSTable *st_copy = repr_data->flattened_stables[i];
        MVMuint16  offset  = repr_data->attribute_offsets[i];
        if (st_copy)
            st_copy->REPR->copy_to(tc, st_copy, (char*)src + offset, dest_root, (char*)dest + offset);
        else
            set_obj_at_offset(tc, dest_root, dest, offset,
                get_obj_at_offset(src, offset));
    }
}

/* Called by the VM to mark any GCable items. */
static void gc_mark(MVMThreadContext *tc, MVMSTable *st, void *data, MVMGCWorklist *worklist) {
    MVMP6opaqueREPRData *repr_data = (MVMP6opaqueREPRData *)st->REPR_data;
    MVMint64 i;
    data = real_data(data);

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
    void *data = real_data(OBJECT_BODY(obj));

    /* Cleanup any nested reprs that need it. */
    for (i = 0; repr_data->gc_cleanup_slots[i] >= 0; i++) {
        MVMuint16  offset = repr_data->attribute_offsets[repr_data->gc_cleanup_slots[i]];
        MVMSTable *st     = repr_data->flattened_stables[repr_data->gc_cleanup_slots[i]];
        st->REPR->gc_cleanup(tc, st, (char *)data + offset);
    }
}

/* Marks the representation data in an STable.*/
static void gc_mark_repr_data(MVMThreadContext *tc, MVMSTable *st, MVMGCWorklist *worklist) {
    MVMP6opaqueREPRData *repr_data = (MVMP6opaqueREPRData *)st->REPR_data;

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
        P6opaqueNameMap *cur_map_entry = repr_data->name_to_index_mapping;
        while (cur_map_entry->class_key != NULL) {
            MVMint16 i;
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

    if (repr_data->name_to_index_mapping) {
        P6opaqueNameMap *cur_map_entry = repr_data->name_to_index_mapping;
        while (cur_map_entry->class_key != NULL) {
            free(cur_map_entry->names);
            free(cur_map_entry->slots);
            cur_map_entry++;
        }
        free(repr_data->name_to_index_mapping);
    }

    if (repr_data->attribute_offsets)
        free(repr_data->attribute_offsets);
    if (repr_data->flattened_stables)
        free(repr_data->flattened_stables);
    if (repr_data->auto_viv_values)
        free(repr_data->auto_viv_values);
    if (repr_data->unbox_slots)
        free(repr_data->unbox_slots);
    if (repr_data->gc_obj_mark_offsets)
        free(repr_data->gc_obj_mark_offsets);
    if (repr_data->initialize_slots)
        free(repr_data->initialize_slots);
    if (repr_data->gc_mark_slots)
        free(repr_data->gc_mark_slots);
    if (repr_data->gc_cleanup_slots)
        free(repr_data->gc_cleanup_slots);

    free(st->REPR_data);
    st->REPR_data = NULL;
}

/* Helper for complaining about attribute access errors. */
MVM_NO_RETURN
static void no_such_attribute(MVMThreadContext *tc, const char *action, MVMObject *class_handle, MVMString *name) {
    MVMuint64 output_size;
    MVM_exception_throw_adhoc(tc, "P6opaque: no such attribute '%s'", MVM_string_ascii_encode(tc, name, &output_size));
}

/* Gets the current value for an attribute. */
static void get_attribute(MVMThreadContext *tc, MVMSTable *st, MVMObject *root,
        void *data, MVMObject *class_handle, MVMString *name, MVMint64 hint,
        MVMRegister *result_reg, MVMuint16 kind) {
    MVMP6opaqueREPRData *repr_data = (MVMP6opaqueREPRData *)st->REPR_data;
    MVMint64 slot;
    data = real_data(data);

    if (!repr_data)
        MVM_exception_throw_adhoc(tc, "P6opaque: must compose before using get_attribute");

    /* Try the slot allocation first. */
    slot = hint >= 0 && !(repr_data->mi) ? hint :
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
                                MVMObject *cloned = REPR(value)->allocate(tc, STABLE(value));
                                REPR(value)->copy_to(tc, STABLE(value), OBJECT_BODY(value), cloned, OBJECT_BODY(cloned));
                                set_obj_at_offset(tc, root, data, repr_data->attribute_offsets[slot], cloned);
                                result_reg->o = cloned;
                            }
                            else {
                                set_obj_at_offset(tc, root, data, repr_data->attribute_offsets[slot], value);
                                result_reg->o = value;
                            }
                        }
                        else {
                            result_reg->o = NULL;
                        }
                    }
                    else {
                        result_reg->o = NULL;
                    }
                }
            }
            else {
                /* Need to produce a boxed version of this attribute. */
                MVMObject *result = attr_st->REPR->allocate(tc, st);
                st->REPR->copy_to(tc, attr_st, (char *)data + repr_data->attribute_offsets[slot],
                    result, OBJECT_BODY(result));
                result_reg->o = result;
            }
            break;
        }
        case MVM_reg_int64: {
            if (attr_st)
                result_reg->i64 = attr_st->REPR->box_funcs->get_int(tc, attr_st, root,
                    (char *)data + repr_data->attribute_offsets[slot]);
            else
                MVM_exception_throw_adhoc(tc, "P6opaque: invalid native access to object attribute");
            break;
        }
        case MVM_reg_num64: {
            if (attr_st)
                result_reg->n64 = attr_st->REPR->box_funcs->get_num(tc, attr_st, root,
                    (char *)data + repr_data->attribute_offsets[slot]);
            else
                MVM_exception_throw_adhoc(tc, "P6opaque: invalid native access to object attribute");
            break;
        }
        case MVM_reg_str: {
            if (attr_st)
                result_reg->s = attr_st->REPR->box_funcs->get_str(tc, attr_st, root,
                    (char *)data + repr_data->attribute_offsets[slot]);
            else
                MVM_exception_throw_adhoc(tc, "P6opaque: invalid native access to object attribute");
            break;
        }
        default: {
            MVM_exception_throw_adhoc(tc, "P6opaque: invalid kind in attribute lookup");
        }
        }
    }
    else {
        /* Otherwise, complain that the attribute doesn't exist. */
        no_such_attribute(tc, "get", class_handle, name);
    }
}

/* Binds the given value to the specified attribute. */
static void bind_attribute(MVMThreadContext *tc, MVMSTable *st, MVMObject *root,
        void *data, MVMObject *class_handle, MVMString *name, MVMint64 hint,
        MVMRegister value_reg, MVMuint16 kind) {
    MVMP6opaqueREPRData *repr_data = (MVMP6opaqueREPRData *)st->REPR_data;
    MVMint64 slot;
    data = real_data(data);

    if (!repr_data)
        MVM_exception_throw_adhoc(tc, "P6opaque: must compose before using bind_attribute_boxed");

    /* Try the slot allocation first. */
    slot = hint >= 0 && !(repr_data->mi) ? hint :
        try_get_slot(tc, repr_data, class_handle, name);
    if (slot >= 0) {
        MVMSTable *attr_st = repr_data->flattened_stables[slot];
        switch (kind) {
        case MVM_reg_obj: {
            MVMObject *value = value_reg.o;
            if (attr_st) {
                if (attr_st == STABLE(value))
                    st->REPR->copy_to(tc, attr_st, OBJECT_BODY(value), root,
                        (char *)data + repr_data->attribute_offsets[slot]);
                else
                    MVM_exception_throw_adhoc(tc,
                        "P6opaque: representation mismatch when storing value to attribute");
            }
            else {
                set_obj_at_offset(tc, root, data, repr_data->attribute_offsets[slot], value);
            }
            break;
        }
        case MVM_reg_int64: {
            if (attr_st)
                attr_st->REPR->box_funcs->set_int(tc, attr_st, root,
                    (char *)data + repr_data->attribute_offsets[slot],
                    value_reg.i64);
            else
                MVM_exception_throw_adhoc(tc, "P6opaque: invalid native binding to object attribute");
            break;
        }
        case MVM_reg_num64: {
            if (attr_st)
                attr_st->REPR->box_funcs->set_num(tc, attr_st, root,
                    (char *)data + repr_data->attribute_offsets[slot],
                    value_reg.n64);
            else
                MVM_exception_throw_adhoc(tc, "P6opaque: invalid native binding to object attribute");
            break;
        }
        case MVM_reg_str: {
            if (attr_st)
                attr_st->REPR->box_funcs->set_str(tc, attr_st, root,
                    (char *)data + repr_data->attribute_offsets[slot],
                    value_reg.s);
            else
                MVM_exception_throw_adhoc(tc, "P6opaque: invalid native binding to object attribute");
            break;
        }
        default: {
            MVM_exception_throw_adhoc(tc, "P6opaque: invalid kind in attribute bind");
        }
        }
    }
    else {
        /* Otherwise, complain that the attribute doesn't exist. */
        no_such_attribute(tc, "bind", class_handle, name);
    }
}

/* Checks if an attribute has been initialized. */
static MVMint64 is_attribute_initialized(MVMThreadContext *tc, MVMSTable *st, void *data, MVMObject *class_handle, MVMString *name, MVMint64 hint) {
    MVMP6opaqueREPRData *repr_data = (MVMP6opaqueREPRData *)st->REPR_data;
    MVMint64 slot = try_get_slot(tc, repr_data, class_handle, name);
    if (slot >= 0)
        return NULL != get_obj_at_offset(data, repr_data->attribute_offsets[slot]);
    else
        no_such_attribute(tc, "initializedness check", class_handle, name);
    return 0;
}

/* Gets the hint for the given attribute ID. */
static MVMint64 hint_for(MVMThreadContext *tc, MVMSTable *st, MVMObject *class_key, MVMString *name) {
    MVMint64 slot;
    MVMP6opaqueREPRData *repr_data = (MVMP6opaqueREPRData *)st->REPR_data;
    if (!repr_data)
        MVM_exception_throw_adhoc(tc, "P6opaque: must compose before using hint_for");
    slot = try_get_slot(tc, repr_data, class_key, name);
    return slot >= 0 ? slot : MVM_NO_HINT;
}

/* Used with boxing. Sets an integer value, for representations that can hold
 * one. */
static void set_int(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMint64 value) {
    MVMP6opaqueREPRData *repr_data = (MVMP6opaqueREPRData *)st->REPR_data;
    data = real_data(data);
    if (repr_data->unbox_int_slot >= 0) {
        MVMSTable *st = repr_data->flattened_stables[repr_data->unbox_int_slot];
        st->REPR->box_funcs->set_int(tc, st, root, (char *)data + repr_data->attribute_offsets[repr_data->unbox_int_slot], value);
    }
    else {
        MVM_exception_throw_adhoc(tc,
            "This type cannot box a native integer");
    }
}

/* Used with boxing. Gets an integer value, for representations that can
 * hold one. */
static MVMint64 get_int(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data) {
    MVMP6opaqueREPRData *repr_data = (MVMP6opaqueREPRData *)st->REPR_data;
    data = real_data(data);
    if (repr_data->unbox_int_slot >= 0) {
        MVMSTable *st = repr_data->flattened_stables[repr_data->unbox_int_slot];
        return st->REPR->box_funcs->get_int(tc, st, root, (char *)data + repr_data->attribute_offsets[repr_data->unbox_int_slot]);
    }
    else {
        MVM_exception_throw_adhoc(tc,
            "This type cannot unbox to a native integer");
    }
}

/* Used with boxing. Sets a floating point value, for representations that can
 * hold one. */
static void set_num(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMnum64 value) {
    MVMP6opaqueREPRData *repr_data = (MVMP6opaqueREPRData *)st->REPR_data;
    data = real_data(data);
    if (repr_data->unbox_num_slot >= 0) {
        MVMSTable *st = repr_data->flattened_stables[repr_data->unbox_num_slot];
        st->REPR->box_funcs->set_num(tc, st, root, (char *)data + repr_data->attribute_offsets[repr_data->unbox_num_slot], value);
    }
    else {
        MVM_exception_throw_adhoc(tc,
            "This type cannot box a native number");
    }
}

/* Used with boxing. Gets a floating point value, for representations that can
 * hold one. */
static MVMnum64 get_num(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data) {
    MVMP6opaqueREPRData *repr_data = (MVMP6opaqueREPRData *)st->REPR_data;
    data = real_data(data);
    if (repr_data->unbox_num_slot >= 0) {
        MVMSTable *st = repr_data->flattened_stables[repr_data->unbox_num_slot];
        return st->REPR->box_funcs->get_num(tc, st, root, (char *)data + repr_data->attribute_offsets[repr_data->unbox_num_slot]);
    }
    else {
        MVM_exception_throw_adhoc(tc,
            "This type cannot unbox to a native number");
    }
}

/* Used with boxing. Sets a string value, for representations that can hold
 * one. */
static void set_str(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMString *value) {
    MVMP6opaqueREPRData *repr_data = (MVMP6opaqueREPRData *)st->REPR_data;
    data = real_data(data);
    if (repr_data->unbox_str_slot >= 0) {
        MVMSTable *st = repr_data->flattened_stables[repr_data->unbox_str_slot];
        st->REPR->box_funcs->set_str(tc, st, root, (char *)data + repr_data->attribute_offsets[repr_data->unbox_str_slot], value);
    }
    else {
        MVM_exception_throw_adhoc(tc,
            "This type cannot box a native string");
    }
}

/* Used with boxing. Gets a string value, for representations that can hold
 * one. */
static MVMString * get_str(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data) {
    MVMP6opaqueREPRData *repr_data = (MVMP6opaqueREPRData *)st->REPR_data;
    data = real_data(data);
    if (repr_data->unbox_str_slot >= 0) {
        MVMSTable *st = repr_data->flattened_stables[repr_data->unbox_str_slot];
        return st->REPR->box_funcs->get_str(tc, st, root, (char *)data + repr_data->attribute_offsets[repr_data->unbox_str_slot]);
    }
    else {
        MVM_exception_throw_adhoc(tc,
            "This type cannot unbox to a native string");
    }
}

static void * get_boxed_ref(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMuint32 repr_id) {
    MVMP6opaqueREPRData *repr_data = (MVMP6opaqueREPRData *)st->REPR_data;
    data = real_data(data);
    if (repr_data->unbox_slots) {
        int i;
        for (i = 0; i < repr_data->num_attributes; i++) {
            if (repr_data->unbox_slots[i].repr_id == repr_id)
                return (char *)data + repr_data->attribute_offsets[i];
            else if (repr_data->unbox_slots[i].repr_id == 0)
                break;
        }
    }

    MVM_exception_throw_adhoc(tc,
        "P6opaque: get_boxed_ref could not unbox for the given representation");
}

/* Gets the storage specification for this representation. */
static MVMStorageSpec get_storage_spec(MVMThreadContext *tc, MVMSTable *st) {
    MVMP6opaqueREPRData *repr_data = (MVMP6opaqueREPRData *)st->REPR_data;
    MVMStorageSpec spec;
    spec.inlineable      = MVM_STORAGE_SPEC_REFERENCE;
    spec.boxed_primitive = MVM_STORAGE_SPEC_BP_NONE;
    spec.can_box         = 0;
    if (repr_data) {
        if (repr_data->unbox_int_slot >= 0)
            spec.can_box += MVM_STORAGE_SPEC_CAN_BOX_INT;
        if (repr_data->unbox_num_slot >= 0)
            spec.can_box += MVM_STORAGE_SPEC_CAN_BOX_NUM;
        if (repr_data->unbox_str_slot >= 0)
            spec.can_box += MVM_STORAGE_SPEC_CAN_BOX_STR;
    }
    return spec;
}

/* Compose the representation. */
static void compose(MVMThreadContext *tc, MVMSTable *st, MVMObject *info_hash) {
    MVMint64   mro_pos, mro_count, num_parents, total_attrs, num_attrs,
               cur_slot, cur_type, cur_alloc_addr, cur_obj_attr,
               cur_init_slot, cur_mark_slot, cur_cleanup_slot, cur_unbox_slot,
               unboxed_type, bits, i;
    MVMObject *info;

    /* Allocate the representation data. */
    MVMP6opaqueREPRData *repr_data = malloc(sizeof(MVMP6opaqueREPRData));
    memset(repr_data, 0, sizeof(MVMP6opaqueREPRData));

    /* Find attribute information. */
    info = REPR(info_hash)->ass_funcs->at_key_boxed(tc, STABLE(info_hash), info_hash,
        OBJECT_BODY(info_hash), (MVMObject *)str_attribute);
    if (info == NULL)
        MVM_exception_throw_adhoc(tc, "P6opaque: missing attribute protocol in compose");

    /* In this first pass, we'll over the MRO entries, looking for if
     * there is any multiple inheritance and counting the number of
     * attributes. */
    mro_count   = REPR(info)->elems(tc, STABLE(info), info, OBJECT_BODY(info));
    mro_pos     = mro_count;
    num_attrs   = 0;
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
        repr_data->attribute_offsets   = malloc(total_attrs * sizeof(MVMuint16));
        repr_data->flattened_stables   = malloc(total_attrs * sizeof(MVMSTable *));
        repr_data->auto_viv_values     = malloc(total_attrs * sizeof(MVMObject *));
        repr_data->gc_obj_mark_offsets = malloc(total_attrs * sizeof(MVMuint16));
        memset(repr_data->flattened_stables, 0, total_attrs * sizeof(MVMSTable *));
        memset(repr_data->auto_viv_values, 0, total_attrs * sizeof(MVMObject *));
    }
    repr_data->name_to_index_mapping = malloc((mro_count + 1) * sizeof(P6opaqueNameMap));
    repr_data->initialize_slots      = malloc((total_attrs + 1) * sizeof(MVMuint16));
    repr_data->gc_mark_slots         = malloc((total_attrs + 1) * sizeof(MVMuint16));
    repr_data->gc_cleanup_slots      = malloc((total_attrs + 1) * sizeof(MVMuint16));
    memset(repr_data->name_to_index_mapping, 0, (mro_count + 1) * sizeof(P6opaqueNameMap));

    /* -1 indicates no unboxing possible for a type. */
    repr_data->unbox_int_slot = -1;
    repr_data->unbox_num_slot = -1;
    repr_data->unbox_str_slot = -1;

    /* Second pass populates the rest of the REPR data. */
    mro_pos          = mro_count;
    cur_slot         = 0;
    cur_type         = 0;
    cur_alloc_addr   = sizeof(MVMP6opaqueBody);
    cur_obj_attr     = 0;
    cur_init_slot    = 0;
    cur_mark_slot    = 0;
    cur_cleanup_slot = 0;
    cur_unbox_slot   = 0;
    while (mro_pos--) {
        /* Get info for the class at the current position. */
        MVMObject *class_info = MVM_repr_at_pos_o(tc, info, mro_pos);
        MVMObject *type_obj = MVM_repr_at_pos_o(tc, class_info, 0);
        MVMObject *attr_list = MVM_repr_at_pos_o(tc, class_info, 1);

        /* Set up name map entry. */
        P6opaqueNameMap *name_map = &repr_data->name_to_index_mapping[cur_type];
        num_attrs = REPR(attr_list)->elems(tc, STABLE(attr_list),
            attr_list, OBJECT_BODY(attr_list));
        name_map->class_key = type_obj;
        name_map->num_attrs = num_attrs;
        if (num_attrs) {
            name_map->names = malloc(num_attrs * sizeof(MVMString *));
            name_map->slots = malloc(num_attrs * sizeof(MVMuint16));
        }

        /* Go over the attributes. */
        for (i = 0; i < num_attrs; i++) {
            MVMObject *attr_info = MVM_repr_at_pos_o(tc, attr_list, i);

            /* Extract name, type and if it's a box target. */
            MVMObject *name_obj = REPR(attr_info)->ass_funcs->at_key_boxed(tc,
                STABLE(attr_info), attr_info, OBJECT_BODY(attr_info), (MVMObject *)str_name);
            MVMObject *type = REPR(attr_info)->ass_funcs->at_key_boxed(tc,
                STABLE(attr_info), attr_info, OBJECT_BODY(attr_info), (MVMObject *)str_type);
            MVMint64 is_box_target = REPR(attr_info)->ass_funcs->exists_key(tc,
                STABLE(attr_info), attr_info, OBJECT_BODY(attr_info), (MVMObject *)str_box_target);
            MVMint8 inlined = 0;

            /* Ensure we have a name. */
            if (!name_obj)
                MVM_exception_throw_adhoc(tc, "P6opaque: missing attribute name");

            /* Attribute will live at the current position in the object. */
            repr_data->attribute_offsets[cur_slot] = cur_alloc_addr;
            if (REPR(name_obj)->ID == MVM_REPR_ID_MVMString)
                name_map->names[i] = (MVMString *)name_obj;
            else
                name_map->names[i] = MVM_repr_get_str(tc, name_obj);
            name_map->slots[i] = cur_slot;

            /* Consider the type. */
            unboxed_type = MVM_STORAGE_SPEC_BP_NONE;
            bits         = sizeof(MVMObject *) * 8;
            if (type != NULL) {
                /* Get the storage spec of the type and see what it wants. */
                MVMStorageSpec spec = REPR(type)->get_storage_spec(tc, STABLE(type));
                if (spec.inlineable == MVM_STORAGE_SPEC_INLINED) {
                    /* Yes, it's something we'll flatten. */
                    unboxed_type = spec.boxed_primitive;
                    bits = spec.bits;
                    repr_data->flattened_stables[i] = STABLE(type);
                    inlined = 1;

                    /* Does it need special initialization? */
                    if (REPR(type)->initialize) {
                        repr_data->initialize_slots[cur_init_slot] = i;
                        cur_init_slot++;
                    }

                    /* Does it have special GC needs? */
                    if (REPR(type)->gc_mark) {
                        repr_data->gc_mark_slots[cur_mark_slot] = i;
                        cur_mark_slot++;
                    }
                    if (REPR(type)->gc_cleanup) {
                        repr_data->gc_cleanup_slots[cur_cleanup_slot] = i;
                        cur_cleanup_slot++;
                    }

                    /* Is it a target for box/unbox operations? */
                    if (is_box_target) {
                        /* If it boxes a primitive, note that. */
                        switch (unboxed_type) {
                            case MVM_STORAGE_SPEC_BP_INT:
                                if (repr_data->unbox_int_slot >= 0)
                                    MVM_exception_throw_adhoc(tc,
                                        "Duplicate box_target for native int");
                                repr_data->unbox_int_slot = i;
                                break;
                            case MVM_STORAGE_SPEC_BP_NUM:
                                if (repr_data->unbox_num_slot >= 0)
                                    MVM_exception_throw_adhoc(tc,
                                        "Duplicate box_target for native num");
                                repr_data->unbox_num_slot = i;
                                break;
                            case MVM_STORAGE_SPEC_BP_STR:
                                if (repr_data->unbox_str_slot >= 0)
                                    MVM_exception_throw_adhoc(tc,
                                        "Duplicate box_target for native str");
                                repr_data->unbox_str_slot = i;
                                break;
                            default:
                                /* nothing, just suppress 'missing default' warning */
                                break;
                        }

                        /* Also list in the by-repr unbox list. */
                        if (repr_data->unbox_slots == NULL)
                            repr_data->unbox_slots = (P6opaqueBoxedTypeMap *)malloc(total_attrs * sizeof(P6opaqueBoxedTypeMap));
                        repr_data->unbox_slots[cur_unbox_slot].repr_id = REPR(type)->ID;
                        repr_data->unbox_slots[cur_unbox_slot].slot = i;
                        cur_unbox_slot++;
                    }
                }
            }

            /* Handle object attributes, which need marking and may have auto-viv needs. */
            if (!inlined) {
                repr_data->gc_obj_mark_offsets[cur_obj_attr] = cur_alloc_addr;
                cur_obj_attr++;
                /* XXX auto-viv stuff */
            }

            /* Add the required space for this type. */
            cur_alloc_addr += bits / 8;

            /* Increment slot count. */
            cur_slot++;
        }

        /* Increment name map type index. */
        cur_type++;
    }

    /* Add allocated amount for body to have total object size. */
    st->size = sizeof(MVMP6opaque) + (cur_alloc_addr - sizeof(MVMP6opaqueBody));

    /* Add sentinels/counts. */
    repr_data->gc_obj_mark_offsets_count = cur_obj_attr;
    repr_data->initialize_slots[cur_init_slot] = -1;
    repr_data->gc_mark_slots[cur_mark_slot] = -1;
    repr_data->gc_cleanup_slots[cur_cleanup_slot] = -1;

    /* Install representation data. */
    st->REPR_data = repr_data;
}

/* Set the size of the STable. */
static void deserialize_stable_size(MVMThreadContext *tc, MVMSTable *st, MVMSerializationReader *reader) {
    /* To calculate size, we need number of attributes and to know about
     * anything flattend in. */
    MVMint64  num_attributes = reader->read_int(tc, reader);
    MVMuint32 cur_offset = 0;
    MVMint64  i;
    for (i = 0; i < num_attributes; i++) {
        if (reader->read_int(tc, reader)) {
            MVMSTable *st = reader->read_stable_ref(tc, reader);
            MVMStorageSpec ss = st->REPR->get_storage_spec(tc, st);
            if (ss.inlineable)
                /* TODO: Review if/when we get sub-byte things. */
                cur_offset += ss.bits / 8;
            else
                cur_offset += sizeof(MVMObject *);
        }
        else {
            cur_offset += sizeof(MVMObject *);
        }
    }

    st->size = sizeof(MVMP6opaque) + cur_offset;
}

/* Deserializes representation data. */
static void deserialize_repr_data(MVMThreadContext *tc, MVMSTable *st, MVMSerializationReader *reader) {
    MVMuint16 i, j, num_classes, cur_offset;
    MVMint16 cur_initialize_slot, cur_gc_mark_slot, cur_gc_cleanup_slot;

    MVMP6opaqueREPRData *repr_data = malloc(sizeof(MVMP6opaqueREPRData));
    memset(repr_data, 0, sizeof(MVMP6opaqueREPRData));

    repr_data->num_attributes = (MVMuint16)reader->read_int(tc, reader);

    repr_data->flattened_stables = (MVMSTable **)malloc(P6OMAX(repr_data->num_attributes, 1) * sizeof(MVMSTable *));
    for (i = 0; i < repr_data->num_attributes; i++)
        if (reader->read_int(tc, reader)) {
            MVM_ASSIGN_REF(tc, st, repr_data->flattened_stables[i], reader->read_stable_ref(tc, reader));
        }
        else {
            repr_data->flattened_stables[i] = NULL;
        }

    repr_data->mi = reader->read_int(tc, reader);

    if (reader->read_int(tc, reader)) {
        repr_data->auto_viv_values = (MVMObject **)malloc(P6OMAX(repr_data->num_attributes, 1) * sizeof(MVMObject *));
        for (i = 0; i < repr_data->num_attributes; i++)
            MVM_ASSIGN_REF(tc, st, repr_data->auto_viv_values[i], reader->read_ref(tc, reader));
    }

    repr_data->unbox_int_slot = reader->read_int(tc, reader);
    repr_data->unbox_num_slot = reader->read_int(tc, reader);
    repr_data->unbox_str_slot = reader->read_int(tc, reader);

    if (reader->read_int(tc, reader)) {
        repr_data->unbox_slots = (P6opaqueBoxedTypeMap *)malloc(P6OMAX(repr_data->num_attributes, 1) * sizeof(P6opaqueBoxedTypeMap));
        for (i = 0; i < repr_data->num_attributes; i++) {
            repr_data->unbox_slots[i].repr_id = reader->read_int(tc, reader);
            repr_data->unbox_slots[i].slot = reader->read_int(tc, reader);
        }
    }

    num_classes = (MVMuint16)reader->read_int(tc, reader);
    repr_data->name_to_index_mapping = (P6opaqueNameMap *)malloc((num_classes + 1) * sizeof(P6opaqueNameMap));
    memset(repr_data->name_to_index_mapping, 0, (num_classes + 1) * sizeof(P6opaqueNameMap));
    for (i = 0; i < num_classes; i++) {
        MVMint32 num_attrs = 0;
        MVM_ASSIGN_REF(tc, st, repr_data->name_to_index_mapping[i].class_key,
            reader->read_ref(tc, reader));
        if (reader->read_int16(tc, reader) == REFVAR_VM_HASH_STR_VAR) {
            num_attrs = reader->read_int32(tc, reader);
            repr_data->name_to_index_mapping[i].names = (MVMString **)malloc(P6OMAX(num_attrs, 1) * sizeof(MVMString *));
            repr_data->name_to_index_mapping[i].slots = (MVMuint16 *)malloc(P6OMAX(num_attrs, 1) * sizeof(MVMuint16));
            for (j = 0; j < num_attrs; j++) {
                MVM_ASSIGN_REF(tc, st, repr_data->name_to_index_mapping[i].names[j],
                    reader->read_str(tc, reader));
                repr_data->name_to_index_mapping[i].slots[j] = (MVMuint16)MVM_repr_get_int(tc,
                    reader->read_ref(tc, reader));
            }
        }
        repr_data->name_to_index_mapping[i].num_attrs = num_attrs;
    }

    repr_data->pos_del_slot = (MVMint16)reader->read_int(tc, reader);
    repr_data->ass_del_slot = (MVMint16)reader->read_int(tc, reader);

    /* Re-calculate the remaining info, which is platform specific or
     * derived information. */
    repr_data->attribute_offsets   = (MVMuint16 *)malloc(P6OMAX(repr_data->num_attributes, 1) * sizeof(MVMuint16));
    repr_data->gc_obj_mark_offsets = (MVMuint16 *)malloc(P6OMAX(repr_data->num_attributes, 1) * sizeof(MVMuint16));
    repr_data->initialize_slots    = (MVMint16 *)malloc((repr_data->num_attributes + 1) * sizeof(MVMint16));
    repr_data->gc_mark_slots       = (MVMint16 *)malloc((repr_data->num_attributes + 1) * sizeof(MVMint16));
    repr_data->gc_cleanup_slots    = (MVMint16 *)malloc((repr_data->num_attributes + 1) * sizeof(MVMint16));
    repr_data->gc_obj_mark_offsets_count = 0;
    cur_offset          = sizeof(MVMP6opaqueBody);
    cur_initialize_slot = 0;
    cur_gc_mark_slot    = 0;
    cur_gc_cleanup_slot = 0;
    for (i = 0; i < repr_data->num_attributes; i++) {
        if (repr_data->flattened_stables[i] == NULL) {
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
            repr_data->attribute_offsets[i] = cur_offset;

            /* Set up flags for initialization and GC. */
            if (cur_st->REPR->initialize)
                repr_data->initialize_slots[cur_initialize_slot++] = i;
            if (cur_st->REPR->gc_mark)
                repr_data->gc_mark_slots[cur_gc_mark_slot++] = i;
            if (cur_st->REPR->gc_cleanup)
                repr_data->gc_cleanup_slots[cur_gc_cleanup_slot++] = i;

            /* Increment by size reported by representation. */
            cur_offset += cur_st->REPR->get_storage_spec(tc, cur_st).bits / 8;
        }
    }
    repr_data->initialize_slots[cur_initialize_slot] = -1;
    repr_data->gc_mark_slots[cur_gc_mark_slot] = -1;
    repr_data->gc_cleanup_slots[cur_gc_cleanup_slot] = -1;

    st->REPR_data = repr_data;
}

/* Deserializes the data. */
static void deserialize(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMSerializationReader *reader) {
    MVMP6opaqueREPRData *repr_data = (MVMP6opaqueREPRData *)st->REPR_data;
    MVMuint16 num_attributes = repr_data->num_attributes;
    MVMuint16 i;
    for (i = 0; i < num_attributes; i++) {
        MVMuint16 a_offset = repr_data->attribute_offsets[i];
        MVMSTable *a_st = repr_data->flattened_stables[i];
        if (a_st)
            a_st->REPR->deserialize(tc, a_st, root, (char *)data + a_offset, reader);
        else
            set_obj_at_offset(tc, root, data, a_offset, reader->read_ref(tc, reader));
    }
}

/* Performs a change of type, where possible. */
void change_type(MVMThreadContext *tc, MVMObject *obj, MVMObject *new_type) {
    MVMP6opaqueREPRData *cur_repr_data = (MVMP6opaqueREPRData *)STABLE(obj)->REPR_data;
    MVMP6opaqueREPRData *new_repr_data = (MVMP6opaqueREPRData *)STABLE(new_type)->REPR_data;
    P6opaqueNameMap *cur_map_entry, *new_map_entry;

    /* Ensure we don't have a type object. */
    if (!IS_CONCRETE(obj))
        MVM_exception_throw_adhoc(tc,
            "Cannot change the type of a type object");

    /* Ensure that the REPR of the new type is also P6opaque. */
    if (REPR(new_type)->ID != REPR(obj)->ID)
        MVM_exception_throw_adhoc(tc,
            "New type must have a matching representation");

    /* Ensure the MRO prefixes match up. */
    cur_map_entry = cur_repr_data->name_to_index_mapping;
    new_map_entry = new_repr_data->name_to_index_mapping;
    while (cur_map_entry->class_key != NULL) {
        if (new_map_entry->class_key == NULL || new_map_entry->class_key != cur_map_entry->class_key)
            MVM_exception_throw_adhoc(tc,
                "Incompatible MROs in P6opaque rebless");
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
        void   *new = malloc(new_size);
        memset(new, 0, new_size);

        /* Copy existing to new.
         * XXX Need more care here, as may have to re-barrier pointers. */
        memcpy(new, old, STABLE(obj)->size - sizeof(MVMObject));

        /* Pointer switch, taking care of existing body issues. */
        if (body->replaced) {
            body->replaced = new;
            free(old);
        }
        else {
            body->replaced = new;
        }
    }

    /* Finally, ready to switch over the STable. */
    MVM_ASSIGN_REF(tc, obj, obj->st, STABLE(new_type));
}

static void die_no_pos_del(MVMThreadContext *tc) {
    MVM_exception_throw_adhoc(tc, "This type does not support positional operations");
}

static void at_pos(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMint64 index, MVMRegister *value, MVMuint16 kind) {
    MVMP6opaqueREPRData *repr_data = (MVMP6opaqueREPRData *)st->REPR_data;
    MVMObject *del;
    if (repr_data->pos_del_slot == -1)
        die_no_pos_del(tc);
    del = get_obj_at_offset(data, repr_data->attribute_offsets[repr_data->pos_del_slot]);
    REPR(del)->pos_funcs->at_pos(tc, STABLE(del), del, OBJECT_BODY(del), index, value, kind);
}

static void bind_pos(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMint64 index, MVMRegister value, MVMuint16 kind) {
    MVMP6opaqueREPRData *repr_data = (MVMP6opaqueREPRData *)st->REPR_data;
    MVMObject *del;
    if (repr_data->pos_del_slot == -1)
        die_no_pos_del(tc);
    del = get_obj_at_offset(data, repr_data->attribute_offsets[repr_data->pos_del_slot]);
    REPR(del)->pos_funcs->bind_pos(tc, STABLE(del), del, OBJECT_BODY(del), index, value, kind);
}

static void set_elems(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMuint64 count) {
    MVMP6opaqueREPRData *repr_data = (MVMP6opaqueREPRData *)st->REPR_data;
    MVMObject *del;
    if (repr_data->pos_del_slot == -1)
        die_no_pos_del(tc);
    del = get_obj_at_offset(data, repr_data->attribute_offsets[repr_data->pos_del_slot]);
    REPR(del)->pos_funcs->set_elems(tc, STABLE(del), del, OBJECT_BODY(del), count);
}

static void push(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMRegister value, MVMuint16 kind) {
    MVMP6opaqueREPRData *repr_data = (MVMP6opaqueREPRData *)st->REPR_data;
    MVMObject *del;
    if (repr_data->pos_del_slot == -1)
        die_no_pos_del(tc);
    del = get_obj_at_offset(data, repr_data->attribute_offsets[repr_data->pos_del_slot]);
    REPR(del)->pos_funcs->push(tc, STABLE(del), del, OBJECT_BODY(del), value, kind);
}

static void pop(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMRegister *value, MVMuint16 kind) {
    MVMP6opaqueREPRData *repr_data = (MVMP6opaqueREPRData *)st->REPR_data;
    MVMObject *del;
    if (repr_data->pos_del_slot == -1)
        die_no_pos_del(tc);
    del = get_obj_at_offset(data, repr_data->attribute_offsets[repr_data->pos_del_slot]);
    REPR(del)->pos_funcs->pop(tc, STABLE(del), del, OBJECT_BODY(del), value, kind);
}

static void unshift(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMRegister value, MVMuint16 kind) {
    MVMP6opaqueREPRData *repr_data = (MVMP6opaqueREPRData *)st->REPR_data;
    MVMObject *del;
    if (repr_data->pos_del_slot == -1)
        die_no_pos_del(tc);
    del = get_obj_at_offset(data, repr_data->attribute_offsets[repr_data->pos_del_slot]);
    REPR(del)->pos_funcs->unshift(tc, STABLE(del), del, OBJECT_BODY(del), value, kind);
}

static void shift(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMRegister *value, MVMuint16 kind) {
    MVMP6opaqueREPRData *repr_data = (MVMP6opaqueREPRData *)st->REPR_data;
    MVMObject *del;
    if (repr_data->pos_del_slot == -1)
        die_no_pos_del(tc);
    del = get_obj_at_offset(data, repr_data->attribute_offsets[repr_data->pos_del_slot]);
    REPR(del)->pos_funcs->shift(tc, STABLE(del), del, OBJECT_BODY(del), value, kind);
}

static void splice(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMObject *target_array, MVMint64 offset, MVMuint64 elems) {
    MVMP6opaqueREPRData *repr_data = (MVMP6opaqueREPRData *)st->REPR_data;
    MVMObject *del;
    if (repr_data->pos_del_slot == -1)
        die_no_pos_del(tc);
    del = get_obj_at_offset(data, repr_data->attribute_offsets[repr_data->pos_del_slot]);
    REPR(del)->pos_funcs->splice(tc, STABLE(del), del, OBJECT_BODY(del), target_array, offset, elems);
}

static void die_no_ass_del(MVMThreadContext *tc) {
    MVM_exception_throw_adhoc(tc, "This type does not support associative operations");
}

void * at_key_ref(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMObject *key) {
    MVMP6opaqueREPRData *repr_data = (MVMP6opaqueREPRData *)st->REPR_data;
    MVMObject *del;
    if (repr_data->ass_del_slot == -1)
        die_no_ass_del(tc);
    del = get_obj_at_offset(data, repr_data->attribute_offsets[repr_data->ass_del_slot]);
    return REPR(del)->ass_funcs->at_key_ref(tc, STABLE(del), del, OBJECT_BODY(del), key);
}

MVMObject * at_key_boxed(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMObject *key) {
    MVMP6opaqueREPRData *repr_data = (MVMP6opaqueREPRData *)st->REPR_data;
    MVMObject *del;
    if (repr_data->ass_del_slot == -1)
        die_no_ass_del(tc);
    del = get_obj_at_offset(data, repr_data->attribute_offsets[repr_data->ass_del_slot]);
    return REPR(del)->ass_funcs->at_key_boxed(tc, STABLE(del), del, OBJECT_BODY(del), key);
}

void bind_key_ref(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMObject *key, void *value_addr) {
    MVMP6opaqueREPRData *repr_data = (MVMP6opaqueREPRData *)st->REPR_data;
    MVMObject *del;
    if (repr_data->ass_del_slot == -1)
        die_no_ass_del(tc);
    del = get_obj_at_offset(data, repr_data->attribute_offsets[repr_data->ass_del_slot]);
    REPR(del)->ass_funcs->bind_key_ref(tc, STABLE(del), del, OBJECT_BODY(del), key, value_addr);
}

void bind_key_boxed(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMObject *key, MVMObject *value) {
    MVMP6opaqueREPRData *repr_data = (MVMP6opaqueREPRData *)st->REPR_data;
    MVMObject *del;
    if (repr_data->ass_del_slot == -1)
        die_no_ass_del(tc);
    del = get_obj_at_offset(data, repr_data->attribute_offsets[repr_data->ass_del_slot]);
    REPR(del)->ass_funcs->bind_key_boxed(tc, STABLE(del), del, OBJECT_BODY(del), key, value);
}

MVMuint64 exists_key(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMObject *key) {
    MVMP6opaqueREPRData *repr_data = (MVMP6opaqueREPRData *)st->REPR_data;
    MVMObject *del;
    if (repr_data->ass_del_slot == -1)
        die_no_ass_del(tc);
    del = get_obj_at_offset(data, repr_data->attribute_offsets[repr_data->ass_del_slot]);
    return REPR(del)->ass_funcs->exists_key(tc, STABLE(del), del, OBJECT_BODY(del), key);
}

void delete_key(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMObject *key) {
    MVMP6opaqueREPRData *repr_data = (MVMP6opaqueREPRData *)st->REPR_data;
    MVMObject *del;
    if (repr_data->ass_del_slot == -1)
        die_no_ass_del(tc);
    del = get_obj_at_offset(data, repr_data->attribute_offsets[repr_data->ass_del_slot]);
    REPR(del)->ass_funcs->delete_key(tc, STABLE(del), del, OBJECT_BODY(del), key);
}

static MVMuint64 elems(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data) {
    MVMP6opaqueREPRData *repr_data = (MVMP6opaqueREPRData *)st->REPR_data;
    if (repr_data->pos_del_slot >= 0) {
        MVMObject *del = get_obj_at_offset(data, repr_data->attribute_offsets[repr_data->pos_del_slot]);
        return REPR(del)->elems(tc, STABLE(del), del, OBJECT_BODY(del));
    }
    else if (repr_data->ass_del_slot >= 0) {
        MVMObject *del = get_obj_at_offset(data, repr_data->attribute_offsets[repr_data->ass_del_slot]);
        return REPR(del)->elems(tc, STABLE(del), del, OBJECT_BODY(del));
    }
    else {
        MVM_exception_throw_adhoc(tc, "This type does not support elems");
    }
}

/* Initializes the representation. */
MVMREPROps * MVMP6opaque_initialize(MVMThreadContext *tc) {
    /* Set up some constant strings we'll need. */
    str_name     = MVM_string_ascii_decode_nt(tc, tc->instance->VMString, "name");
    MVM_gc_root_add_permanent(tc, (MVMCollectable **)&str_name);
    str_type     = MVM_string_ascii_decode_nt(tc, tc->instance->VMString, "type");
    MVM_gc_root_add_permanent(tc, (MVMCollectable **)&str_type);
    str_box_target = MVM_string_ascii_decode_nt(tc, tc->instance->VMString, "box_target");
    MVM_gc_root_add_permanent(tc, (MVMCollectable **)&str_box_target);
    str_attribute = MVM_string_ascii_decode_nt(tc, tc->instance->VMString, "attribute");
    MVM_gc_root_add_permanent(tc, (MVMCollectable **)&str_attribute);

    /* Allocate and populate the representation function table. */
    this_repr = malloc(sizeof(MVMREPROps));
    memset(this_repr, 0, sizeof(MVMREPROps));
    this_repr->type_object_for = type_object_for;
    this_repr->allocate = allocate;
    this_repr->initialize = initialize;
    this_repr->copy_to = copy_to;
    this_repr->gc_mark = gc_mark;
    this_repr->gc_free = gc_free;
    this_repr->gc_mark_repr_data = gc_mark_repr_data;
    this_repr->gc_free_repr_data = gc_free_repr_data;
    this_repr->get_storage_spec = get_storage_spec;
    this_repr->change_type = change_type;
    this_repr->compose = compose;
    this_repr->attr_funcs = malloc(sizeof(MVMREPROps_Attribute));
    memset(this_repr->attr_funcs, 0, sizeof(MVMREPROps_Attribute));
    this_repr->attr_funcs->get_attribute = get_attribute;
    this_repr->attr_funcs->bind_attribute = bind_attribute;
    this_repr->attr_funcs->is_attribute_initialized = is_attribute_initialized;
    this_repr->attr_funcs->hint_for = hint_for;
    this_repr->box_funcs = malloc(sizeof(MVMREPROps_Boxing));
    this_repr->box_funcs->set_int = set_int;
    this_repr->box_funcs->get_int = get_int;
    this_repr->box_funcs->set_num = set_num;
    this_repr->box_funcs->get_num = get_num;
    this_repr->box_funcs->set_str = set_str;
    this_repr->box_funcs->get_str = get_str;
    this_repr->box_funcs->get_boxed_ref = get_boxed_ref;
    this_repr->pos_funcs = malloc(sizeof(MVMREPROps_Positional));
    this_repr->pos_funcs->at_pos = at_pos;
    this_repr->pos_funcs->bind_pos = bind_pos;
    this_repr->pos_funcs->set_elems = set_elems;
    this_repr->pos_funcs->push = push;
    this_repr->pos_funcs->pop = pop;
    this_repr->pos_funcs->unshift = unshift;
    this_repr->pos_funcs->shift = shift;
    this_repr->pos_funcs->splice = splice;
    this_repr->ass_funcs = malloc(sizeof(MVMREPROps_Associative));
    this_repr->ass_funcs->at_key_ref = at_key_ref;
    this_repr->ass_funcs->at_key_boxed = at_key_boxed;
    this_repr->ass_funcs->bind_key_ref = bind_key_ref;
    this_repr->ass_funcs->bind_key_boxed = bind_key_boxed;
    this_repr->ass_funcs->exists_key = exists_key;
    this_repr->ass_funcs->delete_key = delete_key;
    this_repr->elems = elems;
    this_repr->deserialize_stable_size = deserialize_stable_size;
    this_repr->deserialize_repr_data = deserialize_repr_data;
    this_repr->deserialize = deserialize;
    return this_repr;
}
