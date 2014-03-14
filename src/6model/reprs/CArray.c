#include "moar.h"

/* This representation's function pointer table. */
static const MVMREPROps this_repr;

static MVMCallsiteEntry obj_arg_flags[] = { MVM_CALLSITE_ARG_OBJ };
static MVMCallsite     inv_arg_callsite = { obj_arg_flags, 1, 1, 0 };

/* Gets size and type information to put it into the REPR data. */
static void fill_repr_data(MVMThreadContext *tc, MVMSTable *st) {
    MVMCArrayREPRData *repr_data = (MVMCArrayREPRData *)st->REPR_data;
    MVMObject   *code;
    MVMRegister  res;
    MVMStorageSpec ss;
    MVMint32 type_id;
    /* Look up "of" method. */
    MVMObject *of_method = MVM_6model_find_method_cache_only(tc, st->WHAT,
        tc->instance->str_consts.of);

    if (!of_method)
        MVM_exception_throw_adhoc(tc,
            "CArray representation expects an 'of' method, specifying the element type");

    /* Call it to get the type. */
    code = MVM_frame_find_invokee(tc, of_method, NULL);
    MVM_args_setup_thunk(tc, &res, MVM_CALLSITE_ARG_OBJ, &inv_arg_callsite);
    tc->cur_frame->args[0].o = st->WHAT;
    STABLE(code)->invoke(tc, code, &inv_arg_callsite, tc->cur_frame->args);

    /* Ensure we got a type. */
    if (!IS_CONCRETE(res.o))
        MVM_exception_throw_adhoc(tc,
            "CArray representation expects an 'of' method, specifying the element type");

    /* What we do next depends on what kind of type we have. */
    ss = REPR(res.o)->get_storage_spec(tc, STABLE(res.o));
    type_id = REPR(res.o)->ID;

    repr_data->elem_type = res.o;
    if (ss.boxed_primitive == MVM_STORAGE_SPEC_BP_INT) {
        if (ss.bits == 8 || ss.bits == 16 || ss.bits == 32 || ss.bits == 64)
            repr_data->elem_size = ss.bits / 8;
        else
            MVM_exception_throw_adhoc(tc,
                "CArray representation can only have 8, 16, 32 or 64 bit integer elements");
        repr_data->elem_kind = MVM_CARRAY_ELEM_KIND_NUMERIC;
    }
    else if (ss.boxed_primitive == MVM_STORAGE_SPEC_BP_NUM) {
        if (ss.bits == 32 || ss.bits == 64)
            repr_data->elem_size = ss.bits / 8;
        else
            MVM_exception_throw_adhoc(tc,
                "CArray representation can only have 32 or 64 bit floating point elements");
        repr_data->elem_kind = MVM_CARRAY_ELEM_KIND_NUMERIC;
    }
    else if (ss.can_box & MVM_STORAGE_SPEC_CAN_BOX_STR) {
        repr_data->elem_size = sizeof(MVMObject *);
        repr_data->elem_kind = MVM_CARRAY_ELEM_KIND_STRING;
    }
    else if (type_id == MVM_REPR_ID_MVMCArray) {
        repr_data->elem_kind = MVM_CARRAY_ELEM_KIND_CARRAY;
        repr_data->elem_size = sizeof(void *);
    }
    else if (type_id == MVM_REPR_ID_MVMCStruct) {
        repr_data->elem_kind = MVM_CARRAY_ELEM_KIND_CSTRUCT;
        repr_data->elem_size = sizeof(void *);
    }
    else if (type_id == MVM_REPR_ID_MVMCPointer) {
        repr_data->elem_kind = MVM_CARRAY_ELEM_KIND_CPOINTER;
        repr_data->elem_size = sizeof(void *);
    }
    else {
        MVM_exception_throw_adhoc(tc,
            "CArray may only contain native integers and numbers, strings, C Structs or C Pointers");
    }
}

/* Creates a new type object of this representation, and associates it with
 * the given HOW. */
static MVMObject * type_object_for(MVMThreadContext *tc, MVMObject *HOW) {
    MVMSTable *st = MVM_gc_allocate_stable(tc, &this_repr, HOW);

    MVMROOT(tc, st, {
        MVMObject *obj = MVM_gc_allocate_type_object(tc, st);
        MVMArrayREPRData *repr_data = (MVMArrayREPRData *)malloc(sizeof(MVMArrayREPRData));

        MVM_ASSIGN_REF(tc, &(st->header), st->WHAT, obj);

        repr_data->elem_size = 0;
        st->size = sizeof(MVMCArray);
        st->REPR_data = repr_data;
    });

    return st->WHAT;
}

/* Composes the representation. */
static void compose(MVMThreadContext *tc, MVMSTable *st, MVMObject *info) {
    /* TODO */
}

/* Initialize a new instance. */
static void initialize(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data) {
    /* If we're initialized, presumably we're going to be
     * managing the memory in this array ourself. */
    MVMCArrayREPRData *repr_data = (MVMCArrayREPRData *)st->REPR_data;
    MVMCArrayBody     *body      = (MVMCArrayBody *)data;

    if (!repr_data->elem_size)
        fill_repr_data(tc, st);

    body->storage = malloc(4 * repr_data->elem_size);
    body->managed = 1;

    /* Don't need child_objs for numerics or strings. */
    if (repr_data->elem_kind == MVM_CARRAY_ELEM_KIND_NUMERIC)
        body->child_objs = NULL;
    else
        body->child_objs = (MVMObject **) calloc(4, sizeof(MVMObject *));

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
        dest_body->storage = malloc(alsize);
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
    MVMCArrayBody *body = (MVMCArrayBody *)data;

    if (body->managed) {
        free(body->storage);

        if (body->child_objs)
            free(body->child_objs);
    }
}

/* This Parrot-specific addition to the API is used to free an object. */
static void gc_free(MVMThreadContext *tc, MVMObject *obj) {
    gc_cleanup(tc, STABLE(obj), OBJECT_BODY(obj));
}

static void gc_mark(MVMThreadContext *tc, MVMSTable *st, void *data, MVMGCWorklist *worklist) {
    MVMCArrayREPRData *repr_data = (MVMCArrayREPRData *) st->REPR_data;
    MVMCArrayBody *body = (MVMCArrayBody *)data;
    const MVMint32 elems = body->elems;
    MVMint32 i;

    /* Don't traverse child_objs list if there isn't one. */
    if (!body->child_objs) return;

    for (i = 0; i < elems; i++)
        if (body->child_objs[i])
            MVM_gc_worklist_add(tc, worklist, &body->child_objs[i]);
}

/* Gets the storage specification for this representation. */
static MVMStorageSpec get_storage_spec(MVMThreadContext *tc, MVMSTable *st) {
    MVMStorageSpec spec;
    spec.inlineable = MVM_STORAGE_SPEC_REFERENCE;
    spec.boxed_primitive = MVM_STORAGE_SPEC_BP_NONE;
    spec.can_box = 0;
    spec.bits = sizeof(void *) * 8;
    spec.align = ALIGNOF(void *);
    return spec;
}


MVM_NO_RETURN static void die_pos_nyi(MVMThreadContext *tc) MVM_NO_RETURN_GCC;
static void die_pos_nyi(MVMThreadContext *tc) {
    MVM_exception_throw_adhoc(tc,
        "CArray representation does not fully positional storage yet");
}


static void expand(MVMThreadContext *tc, MVMCArrayREPRData *repr_data, MVMCArrayBody *body, MVMint32 min_size) {
    MVMint8 is_complex;
    MVMint32 next_size = body->allocated? 2 * body->allocated: 4;

    if (min_size > next_size)
        next_size = min_size;

    if (body->managed)
        body->storage = realloc(body->storage, next_size * repr_data->elem_size);

    is_complex = (repr_data->elem_kind == MVM_CARRAY_ELEM_KIND_CARRAY
               || repr_data->elem_kind == MVM_CARRAY_ELEM_KIND_CPOINTER
               || repr_data->elem_kind == MVM_CARRAY_ELEM_KIND_CSTRUCT
               || repr_data->elem_kind == MVM_CARRAY_ELEM_KIND_STRING);

    if (is_complex) {
        const size_t old_size = body->allocated * sizeof(MVMObject *);
        const size_t new_size = next_size * sizeof(MVMObject *);

        body->child_objs = (MVMObject **) realloc(body->child_objs, new_size);
        memset((char *)body->child_objs + old_size, 0, new_size - old_size);
    }

    body->allocated = next_size;
}

static void at_pos(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMint64 index, MVMRegister *value, MVMuint16 kind) {
    die_pos_nyi(tc);
}

static void bind_pos(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMint64 index, MVMRegister value, MVMuint16 kind) {
    die_pos_nyi(tc);
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
    MVMCArrayBody *body = (MVMCArrayBody *)data;

    if (body->managed)
        return body->elems;

    MVM_exception_throw_adhoc(tc,
        "Don't know how many elements a C array returned from a library has");
}

/* Serializes the REPR data. */
static void serialize_repr_data(MVMThreadContext *tc, MVMSTable *st, MVMSerializationWriter *writer) {
    MVMCArrayREPRData *repr_data = (MVMCArrayREPRData *)st->REPR_data;
    writer->write_int(tc, writer, repr_data->elem_size);
    writer->write_ref(tc, writer, repr_data->elem_type);
    writer->write_int(tc, writer, repr_data->elem_kind);
}

/* Deserializes the REPR data. */
static void deserialize_repr_data(MVMThreadContext *tc, MVMSTable *st, MVMSerializationReader *reader) {
    MVMCArrayREPRData *repr_data = (MVMCArrayREPRData *) malloc(sizeof(MVMCArrayREPRData));
    repr_data->elem_size = reader->read_int(tc, reader);
    repr_data->elem_type = reader->read_ref(tc, reader);
    repr_data->elem_kind = reader->read_int(tc, reader);
    st->REPR_data = (MVMCArrayREPRData *) repr_data;
}

/* Initializes the CArray representation. */
const MVMREPROps * MVMCArray_initialize(MVMThreadContext *tc) {
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
        MVM_REPR_DEFAULT_EXISTS_POS,
        push,
        pop,
        unshift,
        shift,
        MVM_REPR_DEFAULT_SPLICE,
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
    NULL, /* deserialize_stable_size */
    gc_mark,
    gc_free,
    gc_cleanup,
    NULL, /* gc_mark_repr_data */
    NULL, /* gc_free_repr_data */
    compose,
    "CArray", /* name */
    MVM_REPR_ID_MVMCArray,
    0, /* refs_frames */
};