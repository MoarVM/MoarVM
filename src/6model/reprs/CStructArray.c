#include "moar.h"

/* This representation's function pointer table. */
static const MVMREPROps this_repr;

/* Creates a new type object of this representation, and associates it with
 * the given HOW. */
static MVMObject * type_object_for(MVMThreadContext *tc, MVMObject *HOW) {
    MVMSTable *st = MVM_gc_allocate_stable(tc, &this_repr, HOW);

    MVMROOT(tc, st, {
        MVMObject *obj = MVM_gc_allocate_type_object(tc, st);
        MVM_ASSIGN_REF(tc, &(st->header), st->WHAT, obj);
        st->size = sizeof(MVMCStructArray);
    });

    return st->WHAT;
}

/* Composes the representation. */
static void compose(MVMThreadContext *tc, MVMSTable *st, MVMObject *info_hash) {
    MVMStringConsts str_consts = tc->instance->str_consts;
    MVMObject *info = MVM_repr_at_key_o(tc, info_hash, str_consts.array);
    if (!MVM_is_null(tc, info)) {
        MVMCStructArrayREPRData *repr_data = MVM_malloc(sizeof(MVMCStructArrayREPRData));
        MVMObject *type    = MVM_repr_at_key_o(tc, info, str_consts.type);
        MVMint32 type_id   = REPR(type)->ID;

        MVM_ASSIGN_REF(tc, &(st->header), repr_data->elem_type, type);
        st->REPR_data = repr_data;

        if (type_id == MVM_REPR_ID_MVMCStruct) {
            MVMCStructREPRData *cstruct_repr_data = (MVMCStructREPRData *)STABLE(type)->REPR_data;
            repr_data->elem_size = cstruct_repr_data->struct_size * sizeof(char);
            repr_data->elem_kind = MVM_CSTRUCTARRAY_ELEM_KIND_CSTRUCT;
        }
        else {
            MVM_exception_throw_adhoc(tc,
                "CStructArray representation only handles attributes of type:\n"
                "  CStruct");
        }
    }
    else {
        MVM_exception_throw_adhoc(tc, "CStructArray representation requires a typed array");
    }
}

/* Initialize a new instance. */
static void initialize(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data) {
    /* If we're initialized, presumably we're going to be
     * managing the memory in this array ourself. */
    MVMCStructArrayREPRData *repr_data = (MVMCStructArrayREPRData *)st->REPR_data;
    MVMCStructArrayBody     *body      = (MVMCStructArrayBody *)data;

    if (!repr_data)
        MVM_exception_throw_adhoc(tc, "CStructArray type must be composed before use");

    body->storage = MVM_calloc(4, repr_data->elem_size);
    body->managed = 1;

    body->child_objs = (MVMObject **) MVM_calloc(4, sizeof(MVMObject *));

    body->allocated = 4;
    body->elems = 0;
}

/* Copies to the body of one object to another. */
static void copy_to(MVMThreadContext *tc, MVMSTable *st, void *src, MVMObject *dest_root, void *dest) {
    MVMCStructArrayREPRData *repr_data = (MVMCStructArrayREPRData *)st->REPR_data;
    MVMCStructArrayBody     *src_body  = (MVMCStructArrayBody *)src;
    MVMCStructArrayBody     *dest_body = (MVMCStructArrayBody *)dest;

    if (src_body->managed) {
        MVMint32 alsize = src_body->allocated * repr_data->elem_size;
        dest_body->storage = MVM_malloc(alsize);
        memcpy(dest_body->storage, src_body->storage, alsize);
    }
    else {
        dest_body->storage = src_body->storage;
    }
    dest_body->managed = src_body->managed;
    dest_body->allocated = src_body->allocated;
    dest_body->elems = src_body->elems;
}

/* This is called to do any cleanup of resources when an object gets
 * embedded inside another one. Never called on a top-level object. */
static void gc_cleanup(MVMThreadContext *tc, MVMSTable *st, void *data) {
    MVMCStructArrayBody *body = (MVMCStructArrayBody *)data;

    if (body->managed) {
        MVM_free(body->storage);

        if (body->child_objs)
            MVM_free(body->child_objs);
    }
}

static void gc_free(MVMThreadContext *tc, MVMObject *obj) {
    gc_cleanup(tc, STABLE(obj), OBJECT_BODY(obj));
}

static void gc_mark(MVMThreadContext *tc, MVMSTable *st, void *data, MVMGCWorklist *worklist) {
    MVMCStructArrayBody *body = (MVMCStructArrayBody *)data;
    const MVMint32 elems = body->elems;
    MVMint32 i;

    /* Don't traverse child_objs list if there isn't one. */
    if (!body->child_objs) return;

    for (i = 0; i < elems; i++)
        if (body->child_objs[i])
            MVM_gc_worklist_add(tc, worklist, &body->child_objs[i]);
}

/* Marks the representation data in an STable.*/
static void gc_mark_repr_data(MVMThreadContext *tc, MVMSTable *st, MVMGCWorklist *worklist) {
    MVMCStructArrayREPRData *repr_data = (MVMCStructArrayREPRData *)st->REPR_data;
    if (repr_data)
        MVM_gc_worklist_add(tc, worklist, &repr_data->elem_type);
}

/* Free representation data. */
static void gc_free_repr_data(MVMThreadContext *tc, MVMSTable *st) {
    MVM_free(st->REPR_data);
}

static const MVMStorageSpec storage_spec = {
    MVM_STORAGE_SPEC_REFERENCE, /* inlineable */
    sizeof(void *) * 8,         /* bits */
    ALIGNOF(void *),            /* align */
    MVM_STORAGE_SPEC_BP_NONE,   /* boxed_primitive */
    0,                          /* can_box */
    0,                          /* is_unsigned */
};

/* Gets the storage specification for this representation. */
static const MVMStorageSpec * get_storage_spec(MVMThreadContext *tc, MVMSTable *st) {
    return &storage_spec;
}

MVM_NO_RETURN static void die_pos_nyi(MVMThreadContext *tc) MVM_NO_RETURN_GCC;
static void die_pos_nyi(MVMThreadContext *tc) {
    MVM_exception_throw_adhoc(tc,
        "CStructArray representation does not fully positional storage yet");
}

static void expand(MVMThreadContext *tc, MVMCStructArrayREPRData *repr_data, MVMCStructArrayBody *body, MVMint32 min_size) {
    MVMint32 next_size = body->allocated? 2 * body->allocated: 4;
    size_t old_size, new_size;

    if (min_size > next_size)
        next_size = min_size;

    if (body->managed) {
        const size_t old_size = body->allocated * repr_data->elem_size;
        const size_t new_size = next_size * repr_data->elem_size;

        body->storage = MVM_realloc(body->storage, new_size);
        memset((char *)body->storage + old_size, 0, new_size - old_size);
    }

    old_size = body->allocated * sizeof(MVMObject *);
    new_size = next_size * sizeof(MVMObject *);

    body->child_objs = (MVMObject **) MVM_realloc(body->child_objs, new_size);
    memset((char *)body->child_objs + old_size, 0, new_size - old_size);

    body->allocated = next_size;
}

static MVMObject * make_wrapper(MVMThreadContext *tc, MVMSTable *st, void *data) {
    MVMCStructArrayREPRData *repr_data = (MVMCStructArrayREPRData *)st->REPR_data;
    switch (repr_data->elem_kind) {
        case MVM_CSTRUCTARRAY_ELEM_KIND_CSTRUCT:
            return MVM_nativecall_make_cstruct(tc, repr_data->elem_type, data);
        default:
            MVM_exception_throw_adhoc(tc, "Unknown element type in CStructArray");
    }
}

static void at_pos(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMint64 index, MVMRegister *value, MVMuint16 kind) {
    MVMCStructArrayREPRData *repr_data     = (MVMCStructArrayREPRData *)st->REPR_data;
    MVMCStructArrayBody     *body          = (MVMCStructArrayBody *)data;
    MVMint64               storage_index = index * repr_data->elem_size;
    switch (repr_data->elem_kind) {
        case MVM_CSTRUCTARRAY_ELEM_KIND_CSTRUCT: {
            if (kind != MVM_reg_obj)
                MVM_exception_throw_adhoc(tc, "Wrong kind of access to object CStructArray");
            if (body->managed) {
                /* We manage this array. If we're out of range, just use type object. */
                if (index >= body->elems) {
                    value->o = repr_data->elem_type;
                }

                /* Otherwise we may have a cached object result. */
                else if (body->child_objs[index]) {
                    value->o = body->child_objs[index];
                }

                /* If not, we need to produce and cache it. */
                else {
                    MVMint8 *storage = (MVMint8 *)body->storage;
                    fflush(stdout);
                    MVMROOT(tc, root, {
                        MVMObject **child_objs = body->child_objs;
                        MVMObject *wrapped = make_wrapper(tc, st, &storage[storage_index]);
                        MVM_ASSIGN_REF(tc, &(root->header), child_objs[index], wrapped);
                        value->o = wrapped;
                    });
                }
            }
            else {
                MVMint8 *storage;

                /* Array comes from C. Enlarge child_objs if needed. */
                if (index >= body->allocated)
                    expand(tc, repr_data, body, index + 1);
                if (index >= body->elems)
                    body->elems = index + 1;

                storage = (MVMint8 *)body->storage;

                /* We've already fetched this object; use cached one. */
                if (&storage[storage_index] && body->child_objs[index]) {
                    value->o = body->child_objs[index];
                }

                /* No cached object, but non-NULL pointer in array. Construct object,
                 * put it in the cache and return it. */
                else if (&storage[storage_index]) {
                    MVMROOT(tc, root, {
                        MVMObject **child_objs = body->child_objs;
                        MVMObject *wrapped = make_wrapper(tc, st, &storage[storage_index]);
                        MVM_ASSIGN_REF(tc, &(root->header), child_objs[index], wrapped);
                        value->o = wrapped;
                    });
                }

                /* NULL pointer in the array; result is the type object. */
                else {
                    value->o = repr_data->elem_type;
                }
            }
            break;
        }
        default:
            MVM_exception_throw_adhoc(tc, "Unknown element type in CStructArray");
    }
}

static void bind_pos(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMint64 index, MVMRegister value, MVMuint16 kind) {
    MVMCStructArrayREPRData *repr_data = (MVMCStructArrayREPRData *)st->REPR_data;
    MVMCStructArrayBody     *body      = (MVMCStructArrayBody *)data;

    if (body->managed && index >= body->allocated)
        expand(tc, repr_data, body, index + 1);
    if (index >= body->elems)
        body->elems = index + 1;

    switch (repr_data->elem_kind) {
        case MVM_CSTRUCTARRAY_ELEM_KIND_CSTRUCT: {
            MVMCStruct *cstruct       = (MVMCStruct *)value.o;
            MVMuint8  **storage_src_p = (MVMuint8 **) &cstruct->body.cstruct;
            MVMuint8   *storage_dest  = (MVMuint8 * ) (body->storage + index * repr_data->elem_size);

            if (REPR(value.o)->ID != MVM_REPR_ID_MVMCStruct)
                MVM_exception_throw_adhoc(tc, "CStructArray of CStruct passed non-CStruct object");

            memcpy(storage_dest, *storage_src_p, repr_data->elem_size);

            break;
        }
        default:
            MVM_exception_throw_adhoc(tc, "Unknown element type in CStructArray");
    }
}

static void push(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMRegister value, MVMuint16 kind) {
    die_pos_nyi(tc);
}

static void pop(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMRegister *value, MVMuint16 kind) {
    die_pos_nyi(tc);
}

static void unshift(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMRegister value, MVMuint16 kind) {
    die_pos_nyi(tc);
}

static void shift(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMRegister *value, MVMuint16 kind) {
    die_pos_nyi(tc);
}

static MVMuint64 elems(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data) {
    MVMCStructArrayBody *body = (MVMCStructArrayBody *)data;

    if (body->managed)
        return body->elems;

    MVM_exception_throw_adhoc(tc,
        "Don't know how many elements a C array returned from a library");
}

static void deserialize_stable_size(MVMThreadContext *tc, MVMSTable *st, MVMSerializationReader *reader) {
    st->size = sizeof(MVMCStructArray);
}

/* Serializes the REPR data. */
static void serialize_repr_data(MVMThreadContext *tc, MVMSTable *st, MVMSerializationWriter *writer) {
    MVMCStructArrayREPRData *repr_data = (MVMCStructArrayREPRData *)st->REPR_data;
    MVM_serialization_write_int(tc, writer, repr_data->elem_size);
    MVM_serialization_write_ref(tc, writer, repr_data->elem_type);
    MVM_serialization_write_int(tc, writer, repr_data->elem_kind);
}

/* Deserializes the REPR data. */
static void deserialize_repr_data(MVMThreadContext *tc, MVMSTable *st, MVMSerializationReader *reader) {
    MVMCStructArrayREPRData *repr_data =
        (MVMCStructArrayREPRData *) MVM_malloc(sizeof(MVMCStructArrayREPRData));

    if (reader->root.version >= 19) {
        repr_data->elem_size = MVM_serialization_read_int(tc, reader);
    } else {
        repr_data->elem_size = MVM_serialization_read_int64(tc, reader);
    }

    repr_data->elem_type = MVM_serialization_read_ref(tc, reader);

    if (reader->root.version >= 19) {
        repr_data->elem_kind = MVM_serialization_read_int(tc, reader);
    } else {
        repr_data->elem_kind = MVM_serialization_read_int64(tc, reader);
    }

    st->REPR_data = repr_data;
}

/* Initializes the CStructArray representation. */
const MVMREPROps * MVMCStructArray_initialize(MVMThreadContext *tc) {
    return &this_repr;
}

static const MVMREPROps this_repr = {
    type_object_for,
    MVM_gc_allocate_object,
    initialize,
    copy_to,
    MVM_REPR_DEFAULT_ATTR_FUNCS,
    MVM_REPR_DEFAULT_BOX_FUNCS,
    {
        at_pos,
        bind_pos,
        MVM_REPR_DEFAULT_SET_ELEMS,
        push,
        pop,
        unshift,
        shift,
        MVM_REPR_DEFAULT_SPLICE,
        MVM_REPR_DEFAULT_AT_POS_MULTIDIM,
        MVM_REPR_DEFAULT_BIND_POS_MULTIDIM,
        MVM_REPR_DEFAULT_DIMENSIONS,
        MVM_REPR_DEFAULT_SET_DIMENSIONS,
        MVM_REPR_DEFAULT_GET_ELEM_STORAGE_SPEC
    },    /* pos_funcs */
    MVM_REPR_DEFAULT_ASS_FUNCS,
    elems,
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
    gc_free_repr_data,
    compose,
    NULL, /* spesh */
    "CStructArray", /* name */
    MVM_REPR_ID_MVMCStructArray,           
    NULL, /* unmanaged_size */
    NULL, /* describe_refs */
};


