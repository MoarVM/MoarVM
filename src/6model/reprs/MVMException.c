#include "moar.h"

/* This representation's function pointer table. */
static const MVMREPROps MVMException_this_repr;

/* Creates a new type object of this representation, and associates it with
 * the given HOW. Also sets the invocation protocol handler in the STable. */
static MVMObject * type_object_for(MVMThreadContext *tc, MVMObject *HOW) {
    MVMSTable *st = MVM_gc_allocate_stable(tc, &MVMException_this_repr, HOW);

    MVMROOT(tc, st) {
        MVMObject *obj = MVM_gc_allocate_type_object(tc, st);
        MVM_ASSIGN_REF(tc, &(st->header), st->WHAT, obj);
        st->size = sizeof(MVMException);
    }

    return st->WHAT;
}

/* Copies the body of one object to another. */
static void copy_to(MVMThreadContext *tc, MVMSTable *st, void *src, MVMObject *dest_root, void *dest) {
    MVM_panic(MVM_exitcode_NYI, "MVMException copy_to NYI");
}

/* Adds held objects to the GC worklist. */
static void gc_mark(MVMThreadContext *tc, MVMSTable *st, void *data, MVMGCWorklist *worklist) {
    MVMExceptionBody *body = (MVMExceptionBody *)data;
    MVM_gc_worklist_add(tc, worklist, &body->message);
    MVM_gc_worklist_add(tc, worklist, &body->payload);
    MVM_gc_worklist_add(tc, worklist, &body->origin);
}

static const MVMStorageSpec storage_spec = {
    MVM_STORAGE_SPEC_REFERENCE, /* inlineable */
    0,                          /* bits */
    0,                          /* align */
    MVM_STORAGE_SPEC_BP_NONE,   /* boxed_primitive */
    0,                          /* can_box */
    0,                          /* is_unsigned */
};

/* These two functions were taken from string_copying(7). */
/* This code is in the public domain. */
/* Slightly modified to change size_t to ssize_t. */

ssize_t
strtcpy(char *restrict dst, const char *restrict src, ssize_t dsize)
{
    bool    trunc;
    ssize_t  dlen, slen;

    if (dsize == 0) {
        errno = ENOBUFS;
        return -1;
    }

    slen = strnlen(src, dsize);
    trunc = (slen == dsize);
    dlen = slen - trunc;

    stpcpy(mempcpy(dst, src, dlen), "");
    if (trunc)
        errno = E2BIG;
    return trunc ? -1 : slen;
}

char *
stpecpy(char *dst, char end[0], const char *restrict src)
{
    ssize_t  dlen;

    if (dst == NULL)
        return NULL;

    dlen = strtcpy(dst, src, end - dst);
    return (dlen == -1) ? NULL : dst + dlen;
}

/* We can't actually serialize an exception, but what we can do is give a
 * better error message than just "this type can't be serialized". */
static void serialize(MVMThreadContext *tc, MVMSTable *st, void *data, MVMSerializationWriter *writer) {
    MVMExceptionBody *body = (MVMExceptionBody *)data;

    char *ex_message = NULL;
    if (body->message)
        ex_message = MVM_string_utf8_c8_encode_C_string(tc, body->message);

    /* The limit for formatted strings in exception_throw_adhoc is 4096, so
     * there's no need to have a bigger buffer for just a part of our message. */
    char *full_backtrace_string = MVM_calloc(1, 3072);
    char *end = full_backtrace_string + 3072;
    char *bts_ptr = full_backtrace_string;

    char *waste[] = {full_backtrace_string, ex_message, NULL};

    MVMFrame *cur_frame;

    cur_frame = body->origin;

    MVMuint32 count = 0;
    while (cur_frame != NULL && bts_ptr != NULL) {
        char *line = MVM_exception_backtrace_line(tc, cur_frame, count++,
            body->throw_address);

        bts_ptr = stpecpy(bts_ptr, end, "\n  ");
        bts_ptr = stpecpy(bts_ptr, end, line);

        cur_frame = cur_frame->caller;
        MVM_free(line);
    }

    MVM_exception_throw_adhoc_free(tc, waste,
        "While trying to serialize precompiled objects, encountered an Exception object.\n"
        "Exception objects cannot be serialized.\n"
        "  Exception Message: %s\n"
        "  Original Backtrace:"
        "%s%s\n\n", ex_message ? ex_message : "(no message)", full_backtrace_string, bts_ptr == NULL ? " [...]" : "");
}

/* Gets the storage specification for this representation. */
static const MVMStorageSpec * get_storage_spec(MVMThreadContext *tc, MVMSTable *st) {
    return &storage_spec;
}

/* Compose the representation. */
static void compose(MVMThreadContext *tc, MVMSTable *st, MVMObject *info) {
    /* Nothing to do for this REPR. */
}

/* Initializes the representation. */
const MVMREPROps * MVMException_initialize(MVMThreadContext *tc) {
    return &MVMException_this_repr;
}

static const MVMREPROps MVMException_this_repr = {
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
    serialize, /* serialize */
    NULL, /* deserialize */
    NULL, /* serialize_repr_data */
    NULL, /* deserialize_repr_data */
    NULL, /* deserialize_stable_size */
    gc_mark,
    NULL, /* gc_free */
    NULL, /* gc_cleanup */
    NULL, /* gc_mark_repr_data */
    NULL, /* gc_free_repr_data */
    compose,
    NULL, /* spesh */
    "VMException", /* name */
    MVM_REPR_ID_MVMException,
    NULL, /* unmanaged_size */
    NULL, /* describe_refs */
};
