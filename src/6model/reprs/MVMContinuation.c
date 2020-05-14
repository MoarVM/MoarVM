#include "moar.h"

/* This representation's function pointer table. */
static const MVMREPROps MVMContinuation_this_repr;

/* Creates a new type object of this representation, and associates it with
 * the given HOW. Also sets the invocation protocol handler in the STable. */
static MVMObject * type_object_for(MVMThreadContext *tc, MVMObject *HOW) {
    MVMSTable *st = MVM_gc_allocate_stable(tc, &MVMContinuation_this_repr, HOW);

    MVMROOT(tc, st, {
        MVMObject *obj = MVM_gc_allocate_type_object(tc, st);
        MVM_ASSIGN_REF(tc, &(st->header), st->WHAT, obj);
        st->size = sizeof(MVMContinuation);
    });

    return st->WHAT;
}

/* Copies the body of one object to another. */
static void copy_to(MVMThreadContext *tc, MVMSTable *st, void *src, MVMObject *dest_root, void *dest) {
    MVM_panic(MVM_exitcode_NYI, "MVMContinuation cannot be cloned");
}

/* Adds held objects to the GC worklist. */
static void gc_mark(MVMThreadContext *tc, MVMSTable *st, void *data, MVMGCWorklist *worklist) {
    MVMContinuationBody *body = (MVMContinuationBody *)data;
    MVM_panic(1, "need to update continuation GC mark");
    if (body->active_handlers) {
        MVMActiveHandler *cur_ah = body->active_handlers;
        while (cur_ah != NULL) {
            MVM_gc_worklist_add(tc, worklist, &cur_ah->ex_obj);
            MVM_gc_worklist_add(tc, worklist, &cur_ah->frame);
            cur_ah = cur_ah->next_handler;
        }
    }
    MVM_gc_worklist_add(tc, worklist, &body->protected_tag);
    if (body->prof_cont) {
        MVMuint64 i;
        for (i = 0; i < body->prof_cont->num_sfs; i++)
            MVM_gc_worklist_add(tc, worklist, &(body->prof_cont->sfs[i]));
    }
}

/* Called by the VM in order to free memory associated with this object. */
static void gc_free(MVMThreadContext *tc, MVMObject *obj) {
    MVMContinuation *ctx = (MVMContinuation *)obj;
    MVM_panic(1, "need to update continuation gc_free");
    if (ctx->body.active_handlers) {
        MVMActiveHandler *cur_ah = ctx->body.active_handlers;
        while (cur_ah != NULL) {
            MVMActiveHandler *next_ah = cur_ah->next_handler;
            MVM_free(cur_ah);
            cur_ah = next_ah;
        }
    }
    if (ctx->body.prof_cont)
        MVM_free(ctx->body.prof_cont);
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
const MVMREPROps * MVMContinuation_initialize(MVMThreadContext *tc) {
    return &MVMContinuation_this_repr;
}

static const MVMREPROps MVMContinuation_this_repr = {
    type_object_for,
    MVM_gc_allocate_object,
    NULL, /* initialize */
    copy_to,
    MVM_REPR_DEFAULT_ATTR_FUNCS,
    MVM_REPR_DEFAULT_BOX_FUNCS,
    MVM_REPR_DEFAULT_POS_FUNCS,
    MVM_REPR_DEFAULT_ASS_FUNCS,
    NULL, /* elems */
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
    "MVMContinuation", /* name */
    MVM_REPR_ID_MVMContinuation,
    NULL, /* unmanaged_size */
    NULL, /* describe_refs */
};
