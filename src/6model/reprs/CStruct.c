#include "moar.h"
#include "math.h"

/* This representation's function pointer table. */
static const MVMREPROps this_repr;

/* Locates all of the attributes. Puts them onto a flattened, ordered
 * list of attributes (populating the passed flat_list). Also builds
 * the index mapping for doing named lookups. Note index is not related
 * to the storage position. */
static MVMObject * index_mapping_and_flat_list(MVMThreadContext *tc, MVMObject *mro, MVMCStructREPRData *repr_data) {
    MVMInstance *instance  = tc->instance;
    MVMObject *flat_list, *class_list, *attr_map_list;
    MVMint32  num_classes, i, current_slot = 0;
    MVMCStructNameMap *result;

    MVMint32 mro_idx = MVM_repr_elems(tc, mro);

    MVM_gc_root_temp_push(tc, (MVMCollectable **)&mro);

    flat_list = MVM_repr_alloc_init(tc, MVM_hll_current(tc)->slurpy_array_type);
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&flat_list);

    class_list = MVM_repr_alloc_init(tc, MVM_hll_current(tc)->slurpy_array_type);
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&class_list);

    attr_map_list = MVM_repr_alloc_init(tc, MVM_hll_current(tc)->slurpy_array_type);
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&attr_map_list);

    /* Walk through the parents list. */
    while (mro_idx)
    {
        /* Get current class in MRO. */
        MVMObject *type_info     = MVM_repr_at_pos_o(tc, mro, --mro_idx);
        MVMObject *current_class = MVM_repr_at_pos_o(tc, type_info, 0);

        /* Get its local parents; make sure we're not doing MI. */
        MVMObject *parents     = MVM_repr_at_pos_o(tc, type_info, 2);
        MVMint32  num_parents = MVM_repr_elems(tc, parents);
        if (num_parents <= 1) {
            /* Get attributes and iterate over them. */
            MVMObject *attributes     = MVM_repr_at_pos_o(tc, type_info, 1);
            MVMIter * const attr_iter = (MVMIter *)MVM_iter(tc, attributes);
            MVMObject *attr_map = NULL;

            if (MVM_iter_istrue(tc, attr_iter)) {
                MVM_gc_root_temp_push(tc, (MVMCollectable **)&attr_iter);
                attr_map = MVM_repr_alloc_init(tc, MVM_hll_current(tc)->slurpy_hash_type);
                MVM_gc_root_temp_push(tc, (MVMCollectable **)&attr_map);
            }

            while (MVM_iter_istrue(tc, attr_iter)) {
                MVMObject *current_slot_obj = MVM_repr_box_int(tc, MVM_hll_current(tc)->int_box_type, current_slot);
                MVMObject *attr, *name_obj;
                MVMString *name;

                MVM_repr_shift_o(tc, (MVMObject *)attr_iter);

                /* Get attribute. */
                attr = MVM_iterval(tc, attr_iter);

                /* Get its name. */
                name_obj = MVM_repr_at_key_o(tc, attr, instance->str_consts.name);
                name     = MVM_repr_get_str(tc, name_obj);

                MVM_repr_bind_key_o(tc, attr_map, name, current_slot_obj);

                current_slot++;

                /* Push attr onto the flat list. */
                MVM_repr_push_o(tc, flat_list, attr);
            }

            if (attr_map) {
                MVM_gc_root_temp_pop_n(tc, 2);
            }

            /* Add to class list and map list. */
            MVM_repr_push_o(tc, class_list, current_class);
            MVM_repr_push_o(tc, attr_map_list, attr_map);
        }
        else {
            MVM_exception_throw_adhoc(tc,
                "CStruct representation does not support multiple inheritance");
        }
    }

    MVM_gc_root_temp_pop_n(tc, 4);

    /* We can now form the name map. */
    num_classes = MVM_repr_elems(tc, class_list);
    result = (MVMCStructNameMap *) MVM_malloc(sizeof(MVMCStructNameMap) * (1 + num_classes));

    for (i = 0; i < num_classes; i++) {
        result[i].class_key = MVM_repr_at_pos_o(tc, class_list, i);
        result[i].name_map  = MVM_repr_at_pos_o(tc, attr_map_list, i);
    }

    /* set the end to be NULL, it's useful for iteration. */
    result[i].class_key = NULL;

    repr_data->name_to_index_mapping = result;

    return flat_list;
}

/* This works out an allocation strategy for the object. It takes care of
 * "inlining" storage of attributes that are natively typed, as well as
 * noting unbox targets. */
static void compute_allocation_strategy(MVMThreadContext *tc, MVMObject *repr_info, MVMCStructREPRData *repr_data) {
    /* Compute index mapping table and get flat list of attributes. */
    MVMObject *flat_list = index_mapping_and_flat_list(tc, repr_info, repr_data);

    /* If we have no attributes in the index mapping, then just the header. */
    if (repr_data->name_to_index_mapping[0].class_key == NULL) {
        repr_data->struct_size = 1; /* avoid 0-byte malloc */
    }

    /* Otherwise, we need to compute the allocation strategy.  */
    else {
        /* We track the size of the struct, which is what we'll want offsets into. */
        MVMint32 cur_size    = 0;
        /* The structure itself will be the multiple of its biggest element in size.
         * So we keep track of that biggest element. */
        MVMint32 multiple_of = 0;

        /* Get number of attributes and set up various counters. */
        MVMint32 num_attrs        = MVM_repr_elems(tc, flat_list);
        MVMint32 info_alloc       = num_attrs == 0 ? 1 : num_attrs;
        MVMint32 cur_obj_attr     = 0;
        MVMint32 cur_init_slot    = 0;
        MVMint32 i;

        /* Allocate location/offset arrays and GC mark info arrays. */
        repr_data->num_attributes      = num_attrs;
        repr_data->attribute_locations = (MVMint32 *)   MVM_malloc(info_alloc * sizeof(MVMint32));
        repr_data->struct_offsets      = (MVMint32 *)   MVM_malloc(info_alloc * sizeof(MVMint32));
        repr_data->flattened_stables   = (MVMSTable **) MVM_calloc(info_alloc, sizeof(MVMObject *));
        repr_data->member_types        = (MVMObject **) MVM_calloc(info_alloc, sizeof(MVMObject *));

        /* Go over the attributes and arrange their allocation. */
        for (i = 0; i < num_attrs; i++) {
            /* Fetch its type; see if it's some kind of unboxed type. */
            MVMObject *attr  = MVM_repr_at_pos_o(tc, flat_list, i);
            MVMObject *type  = MVM_repr_at_key_o(tc, attr, tc->instance->str_consts.type);
            MVMint32   bits  = sizeof(void *) * 8;
            MVMint32   align = ALIGNOF(void *);
            if (!MVM_is_null(tc, type)) {
                /* See if it's a type that we know how to handle in a C struct. */
                const MVMStorageSpec *spec = REPR(type)->get_storage_spec(tc, STABLE(type));
                MVMint32  type_id    = REPR(type)->ID;
                if (spec->inlineable == MVM_STORAGE_SPEC_INLINED &&
                        (spec->boxed_primitive == MVM_STORAGE_SPEC_BP_INT ||
                         spec->boxed_primitive == MVM_STORAGE_SPEC_BP_NUM)) {
                    /* It's a boxed int or num; pretty easy. It'll just live in the
                     * body of the struct. Instead of masking in i here (which
                     * would be the parallel to how we handle boxed types) we
                     * repurpose it to store the bit-width of the type, so
                     * that get_attribute_ref can find it later. */
                    bits = spec->bits;
                    align = spec->align;

                    if (bits % 8) {
                         MVM_exception_throw_adhoc(tc,
                            "CStruct only supports native types that are a multiple of 8 bits wide (was passed: %"PRId32")", bits);
                    }

                    repr_data->attribute_locations[i] = (bits << MVM_CSTRUCT_ATTR_SHIFT) | MVM_CSTRUCT_ATTR_IN_STRUCT;
                    repr_data->flattened_stables[i] = STABLE(type);
                    if (REPR(type)->initialize) {
                        if (!repr_data->initialize_slots)
                            repr_data->initialize_slots = (MVMint32 *) MVM_calloc(info_alloc + 1, sizeof(MVMint32));
                        repr_data->initialize_slots[cur_init_slot] = i;
                        cur_init_slot++;
                    }
                }
                else if (spec->can_box & MVM_STORAGE_SPEC_CAN_BOX_STR) {
                    /* It's a string of some kind.  */
                    repr_data->num_child_objs++;
                    repr_data->attribute_locations[i] = (cur_obj_attr++ << MVM_CSTRUCT_ATTR_SHIFT) | MVM_CSTRUCT_ATTR_STRING;
                    repr_data->member_types[i] = type;
                }
                else if (type_id == MVM_REPR_ID_MVMCArray) {
                    /* It's a CArray of some kind.  */
                    repr_data->num_child_objs++;
                    repr_data->attribute_locations[i] = (cur_obj_attr++ << MVM_CSTRUCT_ATTR_SHIFT) | MVM_CSTRUCT_ATTR_CARRAY;
                    repr_data->member_types[i] = type;
                }
                else if (type_id == MVM_REPR_ID_MVMCStruct) {
                    /* It's a CStruct. */
                    repr_data->num_child_objs++;
                    repr_data->attribute_locations[i] = (cur_obj_attr++ << MVM_CSTRUCT_ATTR_SHIFT) | MVM_CSTRUCT_ATTR_CSTRUCT;
                    repr_data->member_types[i] = type;
                }
                else if (type_id == MVM_REPR_ID_MVMCPointer) {
                    /* It's a CPointer. */
                    repr_data->num_child_objs++;
                    repr_data->attribute_locations[i] = (cur_obj_attr++ << MVM_CSTRUCT_ATTR_SHIFT) | MVM_CSTRUCT_ATTR_CPTR;
                    repr_data->member_types[i] = type;
                }
                else {
                    MVM_exception_throw_adhoc(tc,
                        "CStruct representation only handles int, num, CArray, CPointer and CStruct");
                }
            }
            else {
                MVM_exception_throw_adhoc(tc,
                    "CStruct representation requires the types of all attributes to be specified");
            }

            /* Do allocation. */
            /* C structure needs careful alignment. If cur_size is not aligned
             * to align bytes (cur_size % align), make sure it is before we
             * add the next element. */
            if (cur_size % align) {
                cur_size += align - cur_size % align;
            }

            repr_data->struct_offsets[i] = cur_size;
            cur_size += bits / 8;

            if (bits / 8 > multiple_of)
                multiple_of = bits / 8;
        }

        /* Finally, put computed allocation size in place; it's body size plus
         * header size. Also number of markables and sentinels. */
        repr_data->struct_size = ceil((double)cur_size / (double)multiple_of) * multiple_of;
        if (repr_data->initialize_slots)
            repr_data->initialize_slots[cur_init_slot] = -1;
    }
}

/* Helper for reading a pointer at the specified offset. */
static void * get_ptr_at_offset(void *data, MVMint32 offset) {
    void *location = (char *)data + offset;
    return *((void **)location);
}

/* Helper for writing a pointer at the specified offset. */
static void set_ptr_at_offset(void *data, MVMint32 offset, void *value) {
    void *location = (char *)data + offset;
    *((void **)location) = value;
}

/* Helper for finding a slot number. */
static MVMint32 try_get_slot(MVMThreadContext *tc, MVMCStructREPRData *repr_data, MVMObject *class_key, MVMString *name) {
    if (repr_data->name_to_index_mapping) {
        MVMCStructNameMap *cur_map_entry = repr_data->name_to_index_mapping;
        while (cur_map_entry->class_key != NULL) {
            if (cur_map_entry->class_key == class_key) {
                MVMObject *slot_obj = MVM_repr_at_key_o(tc, cur_map_entry->name_map, name);
                if (IS_CONCRETE(slot_obj))
                    return MVM_repr_get_int(tc, slot_obj);
                break;
            }
            cur_map_entry++;
        }
    }
    return -1;
}

/* Creates a new type object of this representation, and associates it with
 * the given HOW. */
static MVMObject * type_object_for(MVMThreadContext *tc, MVMObject *HOW) {
    MVMSTable *st  = MVM_gc_allocate_stable(tc, &this_repr, HOW);

    MVMROOT(tc, st, {
        MVMObject *obj = MVM_gc_allocate_type_object(tc, st);
        MVM_ASSIGN_REF(tc, &(st->header), st->WHAT, obj);
        st->size = sizeof(MVMCStruct);
    });

    return st->WHAT;
}

/* Composes the representation. */
static void compose(MVMThreadContext *tc, MVMSTable *st, MVMObject *repr_info) {
    /* Compute allocation strategy. */
    MVMCStructREPRData *repr_data = MVM_calloc(1, sizeof(MVMCStructREPRData));
    MVMObject *attr_info = MVM_repr_at_key_o(tc, repr_info, tc->instance->str_consts.attribute);
    compute_allocation_strategy(tc, attr_info, repr_data);
    st->REPR_data = repr_data;
}

/* Initialize a new instance. */
static void initialize(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data) {
    MVMCStructREPRData * repr_data = (MVMCStructREPRData *)st->REPR_data;

    /* Allocate object body. */
    MVMCStructBody *body = (MVMCStructBody *)data;
    body->cstruct = MVM_calloc(1, repr_data->struct_size > 0 ? repr_data->struct_size : 1);

    /* Allocate child obj array. */
    if (repr_data->num_child_objs > 0)
        body->child_objs = (MVMObject **)MVM_calloc(repr_data->num_child_objs,
            sizeof(MVMObject *));

    /* Initialize the slots. */
    if (repr_data->initialize_slots) {
        MVMint32 i;
        for (i = 0; repr_data->initialize_slots[i] >= 0; i++) {
            MVMint32  offset = repr_data->struct_offsets[repr_data->initialize_slots[i]];
            MVMSTable *st     = repr_data->flattened_stables[repr_data->initialize_slots[i]];
            st->REPR->initialize(tc, st, root, (char *)body->cstruct + offset);
        }
    }
}

/* Copies to the body of one object to another. */
static void copy_to(MVMThreadContext *tc, MVMSTable *st, void *src, MVMObject *dest_root, void *dest) {
    MVM_exception_throw_adhoc(tc, "cloning a CStruct is NYI");
}

/* Helper for complaining about attribute access errors. */
MVM_NO_RETURN static void no_such_attribute(MVMThreadContext *tc, const char *action, MVMObject *class_handle, MVMString *name) MVM_NO_RETURN_GCC;
static void no_such_attribute(MVMThreadContext *tc, const char *action, MVMObject *class_handle, MVMString *name) {
    MVM_exception_throw_adhoc(tc, "Can not %s non-existent attribute '%s'",
        action, MVM_string_utf8_encode_C_string(tc, name));
}

/* Helper to die because this type doesn't support attributes. */
MVM_NO_RETURN static void die_no_attrs(MVMThreadContext *tc) MVM_NO_RETURN_GCC;
static void die_no_attrs(MVMThreadContext *tc) {
    MVM_exception_throw_adhoc(tc,
        "CStruct representation attribute not yet fully implemented");
}

static void get_attribute(MVMThreadContext *tc, MVMSTable *st, MVMObject *root,
        void *data, MVMObject *class_handle, MVMString *name, MVMint64 hint,
        MVMRegister *result_reg, MVMuint16 kind) {
    MVMCStructREPRData *repr_data = (MVMCStructREPRData *)st->REPR_data;
    MVMCStructBody *body = (MVMCStructBody *)data;
    MVMint64 slot;

    if (!repr_data)
        MVM_exception_throw_adhoc(tc, "P6opaque: must compose before using get_attribute");

    slot = hint >= 0 ? hint : try_get_slot(tc, repr_data, class_handle, name);
    if (slot >= 0) {
        MVMSTable *attr_st = repr_data->flattened_stables[slot];
        switch (kind) {
        case MVM_reg_obj: {
            MVMint32 type      = repr_data->attribute_locations[slot] & MVM_CSTRUCT_ATTR_MASK;
            MVMint32 real_slot = repr_data->attribute_locations[slot] >> MVM_CSTRUCT_ATTR_SHIFT;

            if (type == MVM_CSTRUCT_ATTR_IN_STRUCT) {
                MVM_exception_throw_adhoc(tc,
                    "CStruct can't perform boxed get on flattened attributes yet");
            }
            else {
                MVMObject *typeobj = repr_data->member_types[slot];
                MVMObject *obj     = body->child_objs[real_slot];
                if (!obj) {
                    /* No cached object. */
                    void *cobj = get_ptr_at_offset(body->cstruct, repr_data->struct_offsets[slot]);
                    if (cobj) {
                        MVMObject **child_objs = body->child_objs;
                        if (type == MVM_CSTRUCT_ATTR_CARRAY) {
                            obj = MVM_nativecall_make_carray(tc, typeobj, cobj);
                        }
                        else if(type == MVM_CSTRUCT_ATTR_CSTRUCT) {
                            obj = MVM_nativecall_make_cstruct(tc, typeobj, cobj);
                        }
                        else if(type == MVM_CSTRUCT_ATTR_CPTR) {
                            obj = MVM_nativecall_make_cpointer(tc, typeobj, cobj);
                        }
                        else if(type == MVM_CSTRUCT_ATTR_STRING) {
                            MVMROOT(tc, typeobj, {
                                MVMString *str = MVM_string_utf8_decode(tc, tc->instance->VMString,
                                    cobj, strlen(cobj));
                                obj = MVM_repr_box_str(tc, typeobj, str);
                            });
                        }
                        child_objs[real_slot] = obj;
                    }
                    else {
                        obj = typeobj;
                    }
                }
                result_reg->o = obj;
            }
            break;
        }
        case MVM_reg_int64: {
            if (attr_st)
                result_reg->i64 = attr_st->REPR->box_funcs.get_int(tc, attr_st, root,
                    ((char *)body->cstruct) + repr_data->struct_offsets[slot]);
            else
                MVM_exception_throw_adhoc(tc, "CStruct: invalid native get of object attribute");
            break;
        }
        case MVM_reg_num64: {
            if (attr_st)
                result_reg->n64 = attr_st->REPR->box_funcs.get_num(tc, attr_st, root,
                    ((char *)body->cstruct) + repr_data->struct_offsets[slot]);
            else
                MVM_exception_throw_adhoc(tc, "CStruct: invalid native get of object attribute");
            break;
        }
        case MVM_reg_str: {
            if (attr_st)
                result_reg->s = attr_st->REPR->box_funcs.get_str(tc, attr_st, root,
                    ((char *)body->cstruct) + repr_data->struct_offsets[slot]);
            else
                MVM_exception_throw_adhoc(tc, "CStruct: invalid native get of object attribute");
            break;
        }
        default:
            MVM_exception_throw_adhoc(tc, "CStruct: invalid kind in attribute get");
        }
    }
    else {
        no_such_attribute(tc, "bind", class_handle, name);
    }
}

/* Binds the given value to the specified attribute. */
static void bind_attribute(MVMThreadContext *tc, MVMSTable *st, MVMObject *root,
        void *data, MVMObject *class_handle, MVMString *name, MVMint64 hint,
        MVMRegister value_reg, MVMuint16 kind) {
    MVMCStructREPRData *repr_data = (MVMCStructREPRData *)st->REPR_data;
    MVMCStructBody *body = (MVMCStructBody *)data;
    MVMint64 slot;

    if (!repr_data)
        MVM_exception_throw_adhoc(tc, "P6opaque: must compose before using bind_attribute");

    slot = hint >= 0 ? hint : try_get_slot(tc, repr_data, class_handle, name);
    if (slot >= 0) {
        MVMSTable *attr_st = repr_data->flattened_stables[slot];
        switch (kind) {
        case MVM_reg_obj: {
            MVMObject *value = value_reg.o;

            if (attr_st) {
                MVM_exception_throw_adhoc(tc,
                    "CStruct can't perform boxed bind on flattened attributes yet");
            }
            else {
                MVMint32   type      = repr_data->attribute_locations[slot] & MVM_CSTRUCT_ATTR_MASK;
                MVMint32   real_slot = repr_data->attribute_locations[slot] >> MVM_CSTRUCT_ATTR_SHIFT;

                if (IS_CONCRETE(value)) {
                    void *cobj       = NULL;

                    MVM_ASSIGN_REF(tc, &(root->header), body->child_objs[real_slot], value);

                    /* Set cobj to correct pointer based on type of value. */
                    if (type == MVM_CSTRUCT_ATTR_CARRAY) {
                        if (REPR(value)->ID != MVM_REPR_ID_MVMCArray)
                            MVM_exception_throw_adhoc(tc,
                                "Can only store CArray attribute in CArray slot in CStruct");
                        cobj = ((MVMCArray *)value)->body.storage;
                    }
                    else if (type == MVM_CSTRUCT_ATTR_CSTRUCT) {
                        if (REPR(value)->ID != MVM_REPR_ID_MVMCStruct)
                            MVM_exception_throw_adhoc(tc,
                                "Can only store CStruct attribute in CStruct slot in CStruct");
                        cobj = ((MVMCStruct *)value)->body.cstruct;
                    }
                    else if (type == MVM_CSTRUCT_ATTR_CPTR) {
                        if (REPR(value)->ID != MVM_REPR_ID_MVMCPointer)
                            MVM_exception_throw_adhoc(tc,
                                "Can only store CPointer attribute in CPointer slot in CStruct");
                        cobj = ((MVMCPointer *)value)->body.ptr;
                    }
                    else if (type == MVM_CSTRUCT_ATTR_STRING) {
                        MVMString *str  = MVM_repr_get_str(tc, value);
                        cobj = MVM_string_utf8_encode_C_string(tc, str);
                    }

                    set_ptr_at_offset(body->cstruct, repr_data->struct_offsets[slot], cobj);
                }
                else {
                    body->child_objs[real_slot] = NULL;
                    set_ptr_at_offset(body->cstruct, repr_data->struct_offsets[slot], NULL);
                }
            }
            break;
        }
        case MVM_reg_int64: {
            if (attr_st)
                attr_st->REPR->box_funcs.set_int(tc, attr_st, root,
                    ((char *)body->cstruct) + repr_data->struct_offsets[slot], value_reg.i64);
            else
                MVM_exception_throw_adhoc(tc, "CStruct: invalid native binding to object attribute");
            break;
        }
        case MVM_reg_num64: {
            if (attr_st)
                attr_st->REPR->box_funcs.set_num(tc, attr_st, root,
                    ((char *)body->cstruct) + repr_data->struct_offsets[slot], value_reg.n64);
            else
                MVM_exception_throw_adhoc(tc, "CStruct: invalid native binding to object attribute");
            break;
        }
        case MVM_reg_str: {
            if (attr_st)
                attr_st->REPR->box_funcs.set_str(tc, attr_st, root,
                    ((char *)body->cstruct) + repr_data->struct_offsets[slot], value_reg.s);
            else
                MVM_exception_throw_adhoc(tc, "CStruct: invalid native binding to object attribute");
            break;
        }
        default:
            MVM_exception_throw_adhoc(tc, "CStruct: invalid kind in attribute bind");
        }
    }
    else {
        no_such_attribute(tc, "bind", class_handle, name);
    }
}


/* Checks if an attribute has been initialized. */
static MVMint64 is_attribute_initialized(MVMThreadContext *tc, MVMSTable *st, void *data, MVMObject *class_handle, MVMString *name, MVMint64 hint) {
    die_no_attrs(tc);
}

/* Gets the hint for the given attribute ID. */
static MVMint64 hint_for(MVMThreadContext *tc, MVMSTable *st, MVMObject *class_handle, MVMString *name) {
    return MVM_NO_HINT;
}

/* Adds held objects to the GC worklist. */
static void gc_mark(MVMThreadContext *tc, MVMSTable *st, void *data, MVMGCWorklist *worklist) {
    MVMCStructREPRData *repr_data = (MVMCStructREPRData *) st->REPR_data;
    MVMCStructBody *body = (MVMCStructBody *)data;
    MVMint32 i;
    for (i = 0; i < repr_data->num_child_objs; i++)
        MVM_gc_worklist_add(tc, worklist, &body->child_objs[i]);
}

/* Marks the representation data in an STable.*/
static void gc_mark_repr_data(MVMThreadContext *tc, MVMSTable *st, MVMGCWorklist *worklist) {
    MVMCStructREPRData *repr_data = (MVMCStructREPRData *)st->REPR_data;
    if (repr_data) {
        MVMint32 i;
        if (repr_data->name_to_index_mapping) {
            MVMCStructNameMap *map = repr_data->name_to_index_mapping;
            for (i = 0; map[i].class_key; i++) {
                MVM_gc_worklist_add(tc, worklist, &map[i].class_key);
                MVM_gc_worklist_add(tc, worklist, &map[i].name_map);
            }
        }

        if (repr_data->flattened_stables) {
            MVMSTable **flattened_stables = repr_data->flattened_stables;
            for (i = 0; i < repr_data->num_attributes; i++)
                MVM_gc_worklist_add(tc, worklist, &flattened_stables[i]);
        }

        if (repr_data->member_types) {
            MVMObject **member_types = repr_data->member_types;
            for (i = 0; i < repr_data->num_attributes; i++)
                MVM_gc_worklist_add(tc, worklist, &member_types[i]);
        }
    }
}

/* This is called to do any cleanup of resources when an object gets
 * embedded inside another one. Never called on a top-level object. */
static void gc_cleanup(MVMThreadContext *tc, MVMSTable *st, void *data) {
    MVMCStructBody *body = (MVMCStructBody *)data;
    if (body->child_objs)
        MVM_free(body->child_objs);
    /* XXX For some reason, this causes crashes at the moment. Need to
     * work out why. */
    /*if (body->cstruct)
        MVM_free(body->cstruct);*/
}

/* Called by the VM in order to free memory associated with this object. */
static void gc_free(MVMThreadContext *tc, MVMObject *obj) {
	gc_cleanup(tc, STABLE(obj), OBJECT_BODY(obj));
}

static const MVMStorageSpec storage_spec = {
    MVM_STORAGE_SPEC_REFERENCE, /* inlineable */
    sizeof(void*) * 8,          /* bits */
    ALIGNOF(void*),             /* align */
    MVM_STORAGE_SPEC_BP_NONE,   /* boxed_primitive */
    0,                          /* can_box */
    0,                          /* is_unsigned */
};

/* Gets the storage specification for this representation. */
static const MVMStorageSpec * get_storage_spec(MVMThreadContext *tc, MVMSTable *st) {
    return &storage_spec;
}

/* Serializes the REPR data. */
static void serialize_repr_data(MVMThreadContext *tc, MVMSTable *st, MVMSerializationWriter *writer) {
    MVMCStructREPRData *repr_data = (MVMCStructREPRData *)st->REPR_data;
    MVMint32 i, num_classes, num_slots;

    MVM_serialization_write_varint(tc, writer, repr_data->struct_size);
    MVM_serialization_write_varint(tc, writer, repr_data->num_attributes);
    MVM_serialization_write_varint(tc, writer, repr_data->num_child_objs);
    for(i = 0; i < repr_data->num_attributes; i++){
        MVM_serialization_write_varint(tc, writer, repr_data->attribute_locations[i]);
        MVM_serialization_write_varint(tc, writer, repr_data->struct_offsets[i]);

        MVM_serialization_write_varint(tc, writer, repr_data->flattened_stables[i] != NULL);
        if (repr_data->flattened_stables[i])
            MVM_serialization_write_stable_ref(tc, writer, repr_data->flattened_stables[i]);

        MVM_serialization_write_ref(tc, writer, repr_data->member_types[i]);
    }

    i=0;
    while (repr_data->name_to_index_mapping[i].class_key)
        i++;
    num_classes = i;
    MVM_serialization_write_varint(tc, writer, num_classes);
    for(i = 0; i < num_classes; i++){
        MVM_serialization_write_ref(tc, writer, repr_data->name_to_index_mapping[i].class_key);
        MVM_serialization_write_ref(tc, writer, repr_data->name_to_index_mapping[i].name_map);
    }

    i=0;
    while(repr_data->initialize_slots && repr_data->initialize_slots[i] != -1)
        i++;
    num_slots = i;
    MVM_serialization_write_varint(tc, writer, num_slots);
    for(i = 0; i < num_slots; i++){
        MVM_serialization_write_varint(tc, writer, repr_data->initialize_slots[i]);
    }
}

/* Deserializes the REPR data. */
static void deserialize_repr_data(MVMThreadContext *tc, MVMSTable *st, MVMSerializationReader *reader) {
    MVMCStructREPRData *repr_data = (MVMCStructREPRData *) MVM_malloc(sizeof(MVMCStructREPRData));
    MVMint32 i, num_classes, num_slots;

    repr_data->struct_size = MVM_serialization_read_varint(tc, reader);
    repr_data->num_attributes = MVM_serialization_read_varint(tc, reader);
    repr_data->num_child_objs = MVM_serialization_read_varint(tc, reader);

    repr_data->attribute_locations = (MVMint32 *)MVM_malloc(sizeof(MVMint32) * repr_data->num_attributes);
    repr_data->struct_offsets      = (MVMint32 *)MVM_malloc(sizeof(MVMint32) * repr_data->num_attributes);
    repr_data->flattened_stables   = (MVMSTable **)MVM_malloc(repr_data->num_attributes * sizeof(MVMSTable *));
    repr_data->member_types        = (MVMObject **)MVM_malloc(repr_data->num_attributes * sizeof(MVMObject *));

    for(i = 0; i < repr_data->num_attributes; i++) {
        repr_data->attribute_locations[i] = MVM_serialization_read_varint(tc, reader);
        repr_data->struct_offsets[i] = MVM_serialization_read_varint(tc, reader);

        if(MVM_serialization_read_varint(tc, reader)){
            repr_data->flattened_stables[i] = MVM_serialization_read_stable_ref(tc, reader);
        }
        else {
            repr_data->flattened_stables[i] = NULL;
        }

        repr_data->member_types[i] = MVM_serialization_read_ref(tc, reader);
    }

    num_classes = MVM_serialization_read_varint(tc, reader);
    repr_data->name_to_index_mapping = (MVMCStructNameMap *)MVM_malloc(sizeof(MVMCStructNameMap) * (1 + num_classes));
    for(i = 0; i < num_classes; i++){
        repr_data->name_to_index_mapping[i].class_key = MVM_serialization_read_ref(tc, reader);
        repr_data->name_to_index_mapping[i].name_map = MVM_serialization_read_ref(tc, reader);
    }
    repr_data->name_to_index_mapping[i].class_key = NULL;
    repr_data->name_to_index_mapping[i].name_map = NULL;

    num_slots = MVM_serialization_read_varint(tc, reader);
    repr_data->initialize_slots = (MVMint32 *)MVM_malloc(sizeof(MVMint32) * (1 + num_slots));
    for(i = 0; i < num_slots; i++){
        repr_data->initialize_slots[i] = MVM_serialization_read_varint(tc, reader);
    }
    repr_data->initialize_slots[i] = -1;

    st->REPR_data = repr_data;
}

static void deserialize_stable_size(MVMThreadContext *tc, MVMSTable *st, MVMSerializationReader *reader) {
    st->size = sizeof(MVMCStruct);
}

/* Initializes the representation. */
const MVMREPROps * MVMCStruct_initialize(MVMThreadContext *tc) {
    return &this_repr;
}

static const MVMREPROps this_repr = {
    type_object_for,
    MVM_gc_allocate_object,
    initialize,
    copy_to,
    {
        get_attribute,
        bind_attribute,
        hint_for,
        is_attribute_initialized
    },   /* attr_funcs */
    MVM_REPR_DEFAULT_BOX_FUNCS,
    MVM_REPR_DEFAULT_POS_FUNCS,
    MVM_REPR_DEFAULT_ASS_FUNCS,
    MVM_REPR_DEFAULT_ELEMS,
    get_storage_spec,
    NULL, /* change_type */
    NULL, /* serialize */
    NULL, /* deserialize */
    serialize_repr_data,
    deserialize_repr_data,
    deserialize_stable_size,
    gc_mark,
    gc_free,
    gc_cleanup,
    gc_mark_repr_data,
    NULL, /* gc_free_repr_data */
    compose,
    NULL, /* spesh */
    "CStruct", /* name */
    MVM_REPR_ID_MVMCStruct,
    0, /* refs_frames */
};
