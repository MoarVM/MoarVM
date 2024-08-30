#include "moar.h"

/* This representation's function pointer table. */
static const MVMREPROps MVMCode_this_repr;

/* Creates a new type object of this representation, and associates it with
 * the given HOW. */
static MVMObject * type_object_for(MVMThreadContext *tc, MVMObject *HOW) {
    MVMSTable *st = MVM_gc_allocate_stable(tc, &MVMCode_this_repr, HOW);

    MVMROOT(tc, st) {
        MVMObject *obj = MVM_gc_allocate_type_object(tc, st);
        MVM_ASSIGN_REF(tc, &(st->header), st->WHAT, obj);
        st->size = sizeof(MVMCode);
    }

    return st->WHAT;
}

/* Copies the body of one object to another. */
static void copy_to(MVMThreadContext *tc, MVMSTable *st, void *src, MVMObject *dest_root, void *dest) {
    MVMCodeBody *src_body  = (MVMCodeBody *)src;
    MVMCodeBody *dest_body = (MVMCodeBody *)dest;
    MVM_ASSIGN_REF(tc, &(dest_root->header), dest_body->sf, src_body->sf);
    if (src_body->outer) {
        MVM_ASSIGN_REF(tc, &(dest_root->header), dest_body->outer, src_body->outer);
    }
    MVM_ASSIGN_REF(tc, &(dest_root->header), dest_body->name, src_body->name);
    /* Explicitly do *not* copy state vars in a (presumably closure) clone. */
}

/* Adds held objects to the GC worklist. */
static void gc_mark(MVMThreadContext *tc, MVMSTable *st, void *data, MVMGCWorklist *worklist) {
    MVMCodeBody *body = (MVMCodeBody *)data;
    MVM_gc_worklist_add(tc, worklist, &body->outer);
    MVM_gc_worklist_add(tc, worklist, &body->code_object);
    MVM_gc_worklist_add(tc, worklist, &body->sf);
    MVM_gc_worklist_add(tc, worklist, &body->name);
    if (body->state_vars) {
        MVMuint8 *flags  = body->sf->body.static_env_flags;
        MVMuint16 *types = body->sf->body.lexical_types;
        MVMint64 numlex  = body->sf->body.num_lexicals;
        MVMint64 i;
        for (i = 0; i < numlex; i++) {
            if (flags[i] == 2) {
                if (types[i] == MVM_reg_obj)
                    MVM_gc_worklist_add(tc, worklist, &body->state_vars[i].o);
                else if (types[i] == MVM_reg_str)
                    MVM_gc_worklist_add(tc, worklist, &body->state_vars[i].s);
            }
        }
    }
}

/* Called by the VM in order to free memory associated with this object. */
static void gc_free(MVMThreadContext *tc, MVMObject *obj) {
    MVMCode *code_obj = (MVMCode *)obj;
    MVM_free(code_obj->body.state_vars);
}

static const MVMStorageSpec storage_spec = {
    MVM_STORAGE_SPEC_REFERENCE, /* inlineable */
    0,                          /* bits */
    0,                          /* align */
    MVM_STORAGE_SPEC_BP_NONE,   /* boxed_primitive */
    0,                          /* can_box */
    0,                          /* is_unsigned */
};


/* Gets the storage specification for this representation. */
static const MVMStorageSpec * get_storage_spec(MVMThreadContext *tc, MVMSTable *st) {
    return &storage_spec;
}

/* Compose the representation. */
static void compose(MVMThreadContext *tc, MVMSTable *st, MVMObject *info) {
    /* Nothing to do for this REPR. */
}

/* Initializes the representation. */
const MVMREPROps * MVMCode_initialize(MVMThreadContext *tc) {
    return &MVMCode_this_repr;
}

static const MVMREPROps MVMCode_this_repr = {
    type_object_for,
    MVM_gc_allocate_object,
    NULL, /* initialize */
    copy_to,
    MVM_REPR_DEFAULT_ATTR_FUNCS,
    MVM_REPR_DEFAULT_BOX_FUNCS,
    MVM_REPR_DEFAULT_POS_FUNCS,
    MVM_REPR_DEFAULT_ASS_FUNCS,
    MVM_REPR_DEFAULT_ELEMS,
    get_storage_spec,
    NULL, /* change_type */
    NULL, /* serialize */
    NULL, /* deserialize */
    NULL, /* serialize_repr_data */
    NULL, /* deserialize_repr_data */
    NULL, /* deserialize_stable_size */
    gc_mark,
    gc_free,
    NULL, /* gc_cleanup */
    NULL, /* gc_mark_repr_data */
    NULL, /* gc_free_repr_data */
    compose,
    NULL, /* spesh */
    "MVMCode", /* name */
    MVM_REPR_ID_MVMCode,
    NULL, /* unmanaged_size */
    NULL, /* describe_refs */
};

MVM_PUBLIC MVMObject * MVM_code_location(MVMThreadContext *tc, MVMObject *code) {
    MVMObject *BOOTHash = tc->instance->boot_types.BOOTHash;
    MVMObject *result;
    MVMString *file;
    MVMint32   line;
    MVMObject *filename_boxed;
    MVMObject *linenumber_boxed;
    MVMString *filename_key, *linenumber_key;

    MVM_code_location_out(tc, code, &file, &line);

    result = REPR(BOOTHash)->allocate(tc, STABLE(BOOTHash));

    MVM_gc_root_temp_push(tc, (MVMCollectable **)&file);
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&result);

    filename_key = MVM_string_ascii_decode_nt(tc, tc->instance->VMString, "file");
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&filename_key);

    linenumber_key = MVM_string_ascii_decode_nt(tc, tc->instance->VMString, "line");
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&linenumber_key);

    filename_boxed = MVM_repr_box_str(tc, tc->instance->boot_types.BOOTStr, file);
    MVM_repr_bind_key_o(tc, result, filename_key, filename_boxed);

    linenumber_boxed = MVM_repr_box_int(tc, tc->instance->boot_types.BOOTInt, line);
    MVM_repr_bind_key_o(tc, result, linenumber_key, linenumber_boxed);

    MVM_gc_root_temp_pop_n(tc, 4);

    return result;
}

void MVM_code_location_out(MVMThreadContext *tc, MVMObject *code,
                           MVMString **file_out, MVMint32 *line_out) {
    if (REPR(code)->ID != MVM_REPR_ID_MVMCode) {
        MVM_exception_throw_adhoc(tc, "getcodelocation needs an object of MVMCode REPR, got %s instead", REPR(code)->name);
    } else {
        MVMCodeBody          *body = &((MVMCode*)code)->body;
        MVMBytecodeAnnotation *ann = MVM_bytecode_resolve_annotation(tc, &body->sf->body, 0);
        MVMCompUnit            *cu = body->sf->body.cu;
        MVMuint32          str_idx = ann ? ann->filename_string_heap_index : 0;

        *line_out = ann ? ann->line_number : 1;
        if (ann && str_idx < cu->body.num_strings) {
            *file_out = MVM_cu_string(tc, cu, str_idx);
        } else {
            *file_out = cu->body.filename;
        }
        MVM_free(ann);
    }
}
