#include "moar.h"

/* This representation's function pointer table. */
static const MVMREPROps CArray_this_repr;

/* Creates a new type object of this representation, and associates it with
 * the given HOW. */
static MVMObject * type_object_for(MVMThreadContext *tc, MVMObject *HOW) {
    MVMSTable *st = MVM_gc_allocate_stable(tc, &CArray_this_repr, HOW);

    MVMROOT(tc, st, {
        MVMObject *obj = MVM_gc_allocate_type_object(tc, st);
        MVM_ASSIGN_REF(tc, &(st->header), st->WHAT, obj);
        st->size = sizeof(MVMCArray);
    });

    return st->WHAT;
}

/* Composes the representation. */
static void compose(MVMThreadContext *tc, MVMSTable *st, MVMObject *info_hash) {
    MVMStringConsts str_consts = tc->instance->str_consts;
    MVMObject *info = MVM_repr_at_key_o(tc, info_hash, str_consts.array);
    if (!MVM_is_null(tc, info)) {
              MVMCArrayREPRData *repr_data = MVM_malloc(sizeof(MVMCArrayREPRData));
              MVMObject         *type      = MVM_repr_at_key_o(tc, info, str_consts.type);
        const MVMStorageSpec    *ss        = REPR(type)->get_storage_spec(tc, STABLE(type));
              MVMint32           type_id   = REPR(type)->ID;

        MVM_ASSIGN_REF(tc, &(st->header), repr_data->elem_type, type);
        st->REPR_data = repr_data;

        if (ss->boxed_primitive == MVM_STORAGE_SPEC_BP_INT) {
            if (ss->bits == 8 || ss->bits == 16 || ss->bits == 32 || ss->bits == 64)
                repr_data->elem_size = ss->bits / 8;
            else
                MVM_exception_throw_adhoc(tc,
                    "CArray representation can only have 8, 16, 32 or 64 bit integer elements");
            repr_data->elem_kind = MVM_CARRAY_ELEM_KIND_NUMERIC;
        }
        else if (ss->boxed_primitive == MVM_STORAGE_SPEC_BP_NUM) {
            if (ss->bits == 32 || ss->bits == 64)
                repr_data->elem_size = ss->bits / 8;
            else
                MVM_exception_throw_adhoc(tc,
                    "CArray representation can only have 32 or 64 bit floating point elements");
            repr_data->elem_kind = MVM_CARRAY_ELEM_KIND_NUMERIC;
        }
        else if (ss->can_box & MVM_STORAGE_SPEC_CAN_BOX_STR) {
            /* It's a string of some kind. */
            MVMObject *string    = MVM_repr_at_key_o(tc, info, str_consts.string);
            MVMint32   nativetype;

            if (!MVM_is_null(tc, string)) {
                MVMObject *nativetype_o = MVM_repr_at_key_o(tc, string, str_consts.nativetype);
                if (!MVM_is_null(tc, nativetype_o)) {
                    nativetype = MVM_repr_get_int(tc, nativetype_o);
                } else {
                    nativetype = MVM_P6STR_C_TYPE_CHAR;
                }
            } else {
                nativetype = MVM_P6STR_C_TYPE_CHAR;
            }

            switch (nativetype) {
                case MVM_P6STR_C_TYPE_CHAR:     repr_data->elem_kind = MVM_CARRAY_ELEM_KIND_STRING;      break;
                case MVM_P6STR_C_TYPE_WCHAR_T:  repr_data->elem_kind = MVM_CARRAY_ELEM_KIND_WIDE_STRING; break;
                case MVM_P6STR_C_TYPE_CHAR16_T: repr_data->elem_kind = MVM_CARRAY_ELEM_KIND_U16_STRING;  break;
                case MVM_P6STR_C_TYPE_CHAR32_T: repr_data->elem_kind = MVM_CARRAY_ELEM_KIND_U32_STRING;  break;
            }

            repr_data->elem_size = sizeof(MVMObject *);
        }
        else if (type_id == MVM_REPR_ID_MVMCArray) {
            repr_data->elem_kind = MVM_CARRAY_ELEM_KIND_CARRAY;
            repr_data->elem_size = sizeof(void *);
        }
        else if (type_id == MVM_REPR_ID_MVMCStruct) {
            repr_data->elem_kind = MVM_CARRAY_ELEM_KIND_CSTRUCT;
            repr_data->elem_size = sizeof(void *);
        }
        else if (type_id == MVM_REPR_ID_MVMCPPStruct) {
            repr_data->elem_kind = MVM_CARRAY_ELEM_KIND_CPPSTRUCT;
            repr_data->elem_size = sizeof(void *);
        }
        else if (type_id == MVM_REPR_ID_MVMCUnion) {
            repr_data->elem_kind = MVM_CARRAY_ELEM_KIND_CUNION;
            repr_data->elem_size = sizeof(void *);
        }
        else if (type_id == MVM_REPR_ID_MVMCPointer) {
            repr_data->elem_kind = MVM_CARRAY_ELEM_KIND_CPOINTER;
            repr_data->elem_size = sizeof(void *);
        }
        else {
            MVM_exception_throw_adhoc(tc,
                "CArray representation only handles attributes of type:\n"
                "  (u)int8, (u)int16, (u)int32, (u)int64, (u)long, (u)longlong, num32, num64, (s)size_t, bool, Str\n"
                "  and types with representation: CArray, CPointer, CStruct, CPPStruct and CUnion");
        }
    }
    else {
        MVM_exception_throw_adhoc(tc, "CArray representation requires a typed array");
    }
}

/* Initialize a new instance. */
static void initialize(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data) {
    /* If we're initialized, presumably we're going to be
     * managing the memory in this array ourself. */
    MVMCArrayREPRData *repr_data = (MVMCArrayREPRData *)st->REPR_data;
    MVMCArrayBody     *body      = (MVMCArrayBody *)data;

    if (!repr_data)
        MVM_exception_throw_adhoc(tc, "CArray type must be composed before use");

    body->storage = MVM_calloc(4, repr_data->elem_size);
    body->managed = 1;

    /* Don't need child_objs for numerics. */
    if (repr_data->elem_kind == MVM_CARRAY_ELEM_KIND_NUMERIC)
        body->child_objs = NULL;
    else
        body->child_objs = (MVMObject **) MVM_calloc(4, sizeof(MVMObject *));

    body->allocated = 4;
    body->elems = 0;
}

/* Copies to the body of one object to another. */
static void copy_to(MVMThreadContext *tc, MVMSTable *st, void *src, MVMObject *dest_root, void *dest) {
    MVMCArrayREPRData *repr_data = (MVMCArrayREPRData *)st->REPR_data;
    MVMCArrayBody     *src_body  = (MVMCArrayBody *)src;
    MVMCArrayBody     *dest_body = (MVMCArrayBody *)dest;

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
    MVMCArrayBody     *body      = (MVMCArrayBody *)data;
    MVMCArrayREPRData *repr_data = (MVMCArrayREPRData *)st->REPR_data;
    MVMint32 i;

    if (body->managed) {
        if (repr_data->elem_kind == MVM_CARRAY_ELEM_KIND_STRING) {
            for (i = 0; i < body->elems; i++) {
                MVM_free( ((void **)body->storage)[i] );
            }
        }
        MVM_free(body->storage);
    }
    if (body->child_objs)
        MVM_free(body->child_objs);
}

static void gc_free(MVMThreadContext *tc, MVMObject *obj) {
    gc_cleanup(tc, STABLE(obj), OBJECT_BODY(obj));
}

static void gc_mark(MVMThreadContext *tc, MVMSTable *st, void *data, MVMGCWorklist *worklist) {
    MVMCArrayBody *body = (MVMCArrayBody *)data;
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
    MVMCArrayREPRData *repr_data = (MVMCArrayREPRData *)st->REPR_data;
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


MVM_NO_RETURN static void die_pos_nyi(MVMThreadContext *tc) MVM_NO_RETURN_ATTRIBUTE;
static void die_pos_nyi(MVMThreadContext *tc) {
    MVM_exception_throw_adhoc(tc,
        "CArray representation does not fully positional storage yet");
}


static void expand(MVMThreadContext *tc, MVMCArrayREPRData *repr_data, MVMCArrayBody *body, MVMint32 min_size) {
    MVMint8 is_complex;
    MVMint32 next_size = body->allocated? 2 * body->allocated: 4;

    if (min_size > next_size)
        next_size = min_size;

    if (body->managed) {
        const size_t old_size = body->allocated * repr_data->elem_size;
        const size_t new_size = next_size * repr_data->elem_size;

        body->storage = MVM_realloc(body->storage, new_size);
        memset((char *)body->storage + old_size, 0, new_size - old_size);
    }

    is_complex = (repr_data->elem_kind == MVM_CARRAY_ELEM_KIND_CARRAY
               || repr_data->elem_kind == MVM_CARRAY_ELEM_KIND_CPOINTER
               || repr_data->elem_kind == MVM_CARRAY_ELEM_KIND_CSTRUCT
               || repr_data->elem_kind == MVM_CARRAY_ELEM_KIND_CPPSTRUCT
               || repr_data->elem_kind == MVM_CARRAY_ELEM_KIND_CUNION
               || repr_data->elem_kind == MVM_CARRAY_ELEM_KIND_STRING
               || repr_data->elem_kind == MVM_CARRAY_ELEM_KIND_WIDE_STRING
               || repr_data->elem_kind == MVM_CARRAY_ELEM_KIND_U16_STRING
               || repr_data->elem_kind == MVM_CARRAY_ELEM_KIND_U32_STRING);

    if (is_complex) {
        const size_t old_size = body->allocated * sizeof(MVMObject *);
        const size_t new_size = next_size * sizeof(MVMObject *);

        body->child_objs = (MVMObject **) MVM_realloc(body->child_objs, new_size);
        memset((char *)body->child_objs + old_size, 0, new_size - old_size);
    }

    body->allocated = next_size;
}

static MVMObject * make_wrapper(MVMThreadContext *tc, MVMSTable *st, void *data) {
    MVMCArrayREPRData *repr_data = (MVMCArrayREPRData *)st->REPR_data;
    switch (repr_data->elem_kind) {
        case MVM_CARRAY_ELEM_KIND_STRING: {
            char      *cstr = (char *)data;
            MVMString *str  = MVM_string_utf8_decode(tc, tc->instance->VMString, cstr, strlen(cstr));
            return MVM_repr_box_str(tc, repr_data->elem_type, str);
        }
        case MVM_CARRAY_ELEM_KIND_WIDE_STRING: {
            MVMwchar  *wstr = (MVMwchar *)data;
            MVMString *str  = MVM_string_wide_decode(tc, data, wcslen(data));
            return MVM_repr_box_str(tc, repr_data->elem_type, str);
        }
        case MVM_CARRAY_ELEM_KIND_U16_STRING:
            MVM_exception_throw_adhoc(tc, "CArray: u16string support NYI");
        case MVM_CARRAY_ELEM_KIND_U32_STRING:
            MVM_exception_throw_adhoc(tc, "CArray: u32string support NYI");
        case MVM_CARRAY_ELEM_KIND_CPOINTER:
            return MVM_nativecall_make_cpointer(tc, repr_data->elem_type, data);
        case MVM_CARRAY_ELEM_KIND_CARRAY:
            return MVM_nativecall_make_carray(tc, repr_data->elem_type, data);
        case MVM_CARRAY_ELEM_KIND_CSTRUCT:
            return MVM_nativecall_make_cstruct(tc, repr_data->elem_type, data);
        default:
            MVM_exception_throw_adhoc(tc, "Unknown element type in CArray");
    }
}
static void at_pos(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMint64 index, MVMRegister *value, MVMuint16 kind) {
    MVMCArrayREPRData *repr_data = (MVMCArrayREPRData *)st->REPR_data;
    MVMCArrayBody     *body      = (MVMCArrayBody *)data;
    void              *ptr       = ((char *)body->storage) + index * repr_data->elem_size;
    switch (repr_data->elem_kind) {
        case MVM_CARRAY_ELEM_KIND_NUMERIC:
            if (kind == MVM_reg_int64)
                value->i64 = body->managed && index >= body->elems
                    ? 0
                    : REPR(repr_data->elem_type)->box_funcs.get_int(tc,
                        STABLE(repr_data->elem_type), root, ptr);
            else if (kind == MVM_reg_num64)
                value->n64 = body->managed && index >= body->elems
                    ? 0.0
                    : REPR(repr_data->elem_type)->box_funcs.get_num(tc,
                        STABLE(repr_data->elem_type), root, ptr);
            else
                MVM_exception_throw_adhoc(tc, "Wrong kind of access to numeric CArray");
            break;
        case MVM_CARRAY_ELEM_KIND_STRING:
        case MVM_CARRAY_ELEM_KIND_WIDE_STRING:
        case MVM_CARRAY_ELEM_KIND_CPOINTER:
        case MVM_CARRAY_ELEM_KIND_CARRAY:
        case MVM_CARRAY_ELEM_KIND_CSTRUCT: {
            if (kind != MVM_reg_obj)
                MVM_exception_throw_adhoc(tc, "Wrong kind of access to object CArray");
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
                    void **storage = (void **)body->storage;
                    MVMROOT(tc, root, {
                        MVMObject **child_objs = body->child_objs;
                        MVMObject *wrapped = make_wrapper(tc, st, storage[index]);
                        MVM_ASSIGN_REF(tc, &(root->header), child_objs[index], wrapped);
                        value->o = wrapped;
                    });
                }
            }
            else {
                void **storage;

                /* Array comes from C. Enlarge child_objs if needed. */
                if (index >= body->allocated)
                    expand(tc, repr_data, body, index + 1);
                if (index >= body->elems)
                    body->elems = index + 1;

                storage = (void **)body->storage;

                /* We've already fetched this object; use cached one. */
                if (storage[index] && body->child_objs[index]) {
                    value->o = body->child_objs[index];
                }

                /* No cached object, but non-NULL pointer in array. Construct object,
                 * put it in the cache and return it. */
                else if (storage[index]) {
                    MVMROOT(tc, root, {
                        MVMObject **child_objs = body->child_objs;
                        MVMObject *wrapped = make_wrapper(tc, st, storage[index]);
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
        case MVM_CARRAY_ELEM_KIND_U16_STRING:
            MVM_exception_throw_adhoc(tc, "CArray: u16string support NYI");
        case MVM_CARRAY_ELEM_KIND_U32_STRING:
            MVM_exception_throw_adhoc(tc, "CArray: u32string support NYI");
        default:
            MVM_exception_throw_adhoc(tc, "Unknown element type in CArray");
    }
}

static void bind_wrapper_and_ptr(MVMThreadContext *tc, MVMObject *root, MVMCArrayBody *body,
        MVMint64 index, MVMObject *wrapper, void *cptr) {
    if (index >= body->allocated)
        expand(tc, STABLE(root)->REPR_data, body, index + 1);
    if (index >= body->elems)
        body->elems = index + 1;
    MVM_ASSIGN_REF(tc, &(root->header), body->child_objs[index], wrapper);
    ((void **)body->storage)[index] = cptr;
}
static void bind_pos(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMint64 index, MVMRegister value, MVMuint16 kind) {
    MVMCArrayREPRData *repr_data = (MVMCArrayREPRData *)st->REPR_data;
    MVMCArrayBody     *body      = (MVMCArrayBody *)data;
    void *ptr;

    if (body->managed && index >= body->allocated)
        expand(tc, repr_data, body, index + 1);
    if (index >= body->elems)
        body->elems = index + 1;

    ptr = ((char *)body->storage) + index * repr_data->elem_size;

    switch (repr_data->elem_kind) {
        case MVM_CARRAY_ELEM_KIND_NUMERIC:
            if (kind == MVM_reg_int64)
                REPR(repr_data->elem_type)->box_funcs.set_int(tc,
                    STABLE(repr_data->elem_type), root, ptr, value.i64);
            else if (kind == MVM_reg_num64)
                REPR(repr_data->elem_type)->box_funcs.set_num(tc,
                    STABLE(repr_data->elem_type), root, ptr, value.n64);
            else
                MVM_exception_throw_adhoc(tc, "Wrong kind of access to numeric CArray");
            break;
        case MVM_CARRAY_ELEM_KIND_STRING: {
            char *string = IS_CONCRETE(value.o)
                         ? MVM_string_utf8_encode_C_string(tc, MVM_repr_get_str(tc, value.o))
                         : NULL;
            bind_wrapper_and_ptr(tc, root, body, index, value.o, string);
            break;
        }
        case MVM_CARRAY_ELEM_KIND_WIDE_STRING: {
            MVMwchar *string = IS_CONCRETE(value.o)
                             ? MVM_string_wide_encode(tc, MVM_repr_get_str(tc, value.o), NULL)
                             : NULL;
            bind_wrapper_and_ptr(tc, root, body, index, value.o, string);
            break;
        }
        case MVM_CARRAY_ELEM_KIND_U16_STRING:
            MVM_exception_throw_adhoc(tc, "CArray: u16string support NYI");
        case MVM_CARRAY_ELEM_KIND_U32_STRING:
            MVM_exception_throw_adhoc(tc, "CArray: u32string support NYI");
        case MVM_CARRAY_ELEM_KIND_CPOINTER:
            if (REPR(value.o)->ID != MVM_REPR_ID_MVMCPointer)
                MVM_exception_throw_adhoc(tc, "CArray of CPointer passed non-CPointer object");
            bind_wrapper_and_ptr(tc, root, body, index, value.o,
                IS_CONCRETE(value.o) ? ((MVMCPointer *)value.o)->body.ptr : NULL);
            break;
        case MVM_CARRAY_ELEM_KIND_CARRAY:
            if (REPR(value.o)->ID != MVM_REPR_ID_MVMCArray)
                MVM_exception_throw_adhoc(tc, "CArray of CArray passed non-CArray object");
            bind_wrapper_and_ptr(tc, root, body, index, value.o,
                IS_CONCRETE(value.o) ? ((MVMCArray *)value.o)->body.storage : NULL);
            break;
        case MVM_CARRAY_ELEM_KIND_CSTRUCT:
            if (REPR(value.o)->ID != MVM_REPR_ID_MVMCStruct)
                MVM_exception_throw_adhoc(tc, "CArray of CStruct passed non-CStruct object");
            bind_wrapper_and_ptr(tc, root, body, index, value.o,
                IS_CONCRETE(value.o) ? ((MVMCStruct *)value.o)->body.cstruct : NULL);
            break;
        case MVM_CARRAY_ELEM_KIND_CPPSTRUCT:
            if (REPR(value.o)->ID != MVM_REPR_ID_MVMCPPStruct)
                MVM_exception_throw_adhoc(tc, "CArray of CPPStruct passed non-CStruct object");
            bind_wrapper_and_ptr(tc, root, body, index, value.o,
                IS_CONCRETE(value.o) ? ((MVMCPPStruct *)value.o)->body.cppstruct : NULL);
            break;
        case MVM_CARRAY_ELEM_KIND_CUNION:
            if (REPR(value.o)->ID != MVM_REPR_ID_MVMCUnion)
                MVM_exception_throw_adhoc(tc, "CArray of CUnion passed non-CStruct object");
            bind_wrapper_and_ptr(tc, root, body, index, value.o,
                IS_CONCRETE(value.o) ? ((MVMCUnion *)value.o)->body.cunion : NULL);
            break;
        default:
            MVM_exception_throw_adhoc(tc, "Unknown element type in CArray");
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

static void aslice(MVMThreadContext *tc, MVMSTable *st, MVMObject *src, void *data, MVMObject *dest, MVMint64 start, MVMint64 end) {
    die_pos_nyi(tc);
}

static MVMuint64 elems(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data) {
    MVMCArrayBody *body = (MVMCArrayBody *)data;

    if (body->managed)
        return body->elems;

    MVM_exception_throw_adhoc(tc,
        "Don't know how many elements a C array returned from a library");
}

static void deserialize_stable_size(MVMThreadContext *tc, MVMSTable *st, MVMSerializationReader *reader) {
    st->size = sizeof(MVMCArray);
}

/* Serializes the REPR data. */
static void serialize_repr_data(MVMThreadContext *tc, MVMSTable *st, MVMSerializationWriter *writer) {
    MVMCArrayREPRData *repr_data = (MVMCArrayREPRData *)st->REPR_data;
    MVM_serialization_write_int(tc, writer, repr_data->elem_size);
    MVM_serialization_write_ref(tc, writer, repr_data->elem_type);
    MVM_serialization_write_int(tc, writer, repr_data->elem_kind);
}

/* Deserializes the REPR data. */
static void deserialize_repr_data(MVMThreadContext *tc, MVMSTable *st, MVMSerializationReader *reader) {
    MVMCArrayREPRData *repr_data = (MVMCArrayREPRData *) MVM_malloc(sizeof(MVMCArrayREPRData));

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

/* Initializes the CArray representation. */
const MVMREPROps * MVMCArray_initialize(MVMThreadContext *tc) {
    return &CArray_this_repr;
}

static const MVMREPROps CArray_this_repr = {
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
        aslice,
        MVM_REPR_DEFAULT_SPLICE,
        MVM_REPR_DEFAULT_AT_POS_MULTIDIM,
        MVM_REPR_DEFAULT_BIND_POS_MULTIDIM,
        MVM_REPR_DEFAULT_DIMENSIONS,
        MVM_REPR_DEFAULT_SET_DIMENSIONS,
        MVM_REPR_DEFAULT_GET_ELEM_STORAGE_SPEC,
        MVM_REPR_DEFAULT_POS_AS_ATOMIC,
        MVM_REPR_DEFAULT_POS_AS_ATOMIC_MULTIDIM
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
    "CArray", /* name */
    MVM_REPR_ID_MVMCArray,
    NULL, /* unmanaged_size */
    NULL, /* describe_refs */
};
