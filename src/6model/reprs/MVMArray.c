#include "moarvm.h"

/* This representation's function pointer table. */
static MVMREPROps *this_repr;

/* Creates a new type object of this representation, and associates it with
 * the given HOW. */
static MVMObject * type_object_for(MVMThreadContext *tc, MVMObject *HOW) {
    MVMSTable *st;
    MVMObject *obj;
    
    st = MVM_gc_allocate_stable(tc, this_repr, HOW);
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&st);
    
    obj = MVM_gc_allocate_type_object(tc, st);
    st->WHAT = obj;
    st->size = sizeof(MVMArray);
    
    MVM_gc_root_temp_pop(tc);
    
    return st->WHAT;
}

/* Creates a new instance based on the type object. */
static MVMObject * allocate(MVMThreadContext *tc, MVMSTable *st) {
    return MVM_gc_allocate_object(tc, st);
}

/* Initialize a new instance. */
static void initialize(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data) {
}

/* Copies the body of one object to another. */
static void copy_to(MVMThreadContext *tc, MVMSTable *st, void *src, MVMObject *dest_root, void *dest) {
    MVMArrayBody *src_body  = (MVMArrayBody *)src;
    MVMArrayBody *dest_body = (MVMArrayBody *)dest;
    dest_body->elems = src_body->elems;
    dest_body->ssize = src_body->ssize;
    dest_body->start = 0;
    if (dest_body->elems > 0) {
        dest_body->slots = malloc(sizeof(MVMObject *) * dest_body->ssize);
        memcpy(dest_body->slots, src_body->slots + src_body->start,
            sizeof(MVMObject *) * src_body->elems);
    }
}

/* Adds held objects to the GC worklist. */
static void gc_mark(MVMThreadContext *tc, MVMSTable *st, void *data, MVMGCWorklist *worklist) {
    MVMArrayBody  *body  = (MVMArrayBody *)data;
    MVMuint64      elems = body->elems;
    MVMuint64      start = body->start;
    MVMuint64      i     = 0;
    MVMObject    **slots = body->slots;
    slots += start;
    while (i < elems) {
        MVM_gc_worklist_add(tc, worklist, &slots[i]);
        i++;
    }
}

/* Called by the VM in order to free memory associated with this object. */
static void gc_free(MVMThreadContext *tc, MVMObject *obj) {
    MVMArray *arr = (MVMArray *)obj;
    if (arr->body.slots) {
        free(arr->body.slots);
        arr->body.slots = NULL;
    }
}

/* Gets the storage specification for this representation. */
static MVMStorageSpec get_storage_spec(MVMThreadContext *tc, MVMSTable *st) {
    MVMStorageSpec spec;
    spec.inlineable      = MVM_STORAGE_SPEC_REFERENCE;
    spec.boxed_primitive = MVM_STORAGE_SPEC_BP_NONE;
    spec.can_box         = 0;
    return spec;
}

static void * at_pos_ref(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMint64 index, void *target) {
    MVM_exception_throw_adhoc(tc,
        "MVMArray representation does not support native type storage");
}

static MVMObject * at_pos_boxed(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMint64 index) {
    MVMArrayBody *body = (MVMArrayBody *)data;
    
    if (index < 0) {
        index += body->elems;
        if (index < 0)
            MVM_exception_throw_adhoc(tc, "MVMArray: Index out of bounds");
    }
    else if (index >= body->elems)
        return NULL;

    return body->slots[body->start + index];
}

static void bind_pos_ref(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMint64 index, void *addr) {
    MVM_exception_throw_adhoc(tc,
        "MVMArray representation does not support native type storage");
}

static void set_size_internal(MVMThreadContext *tc, MVMArrayBody *body, MVMint64 n) {
    MVMuint64   elems = body->elems;
    MVMuint64   start = body->start;
    MVMuint64   ssize = body->ssize;
    MVMObject **slots = body->slots;

    if (n < 0)
        MVM_exception_throw_adhoc(tc,
            "MVMArray: Can't resize to negative elements");

    if (n == elems)
        return;

    /* if there aren't enough slots at the end, shift off empty slots 
     * from the beginning first */
    if (start > 0 && n + start > ssize) {
        if (elems > 0) 
            memmove(slots, slots + start, elems * sizeof(MVMObject *));
        body->start = 0;
        /* fill out any unused slots with NULL pointers */
        while (elems < ssize) {
            slots[elems] = NULL;
            elems++;
        }
    }

    body->elems = n;
    if (n <= ssize) { 
        /* we already have n slots available, we can just return */
        return;
    }

    /* We need more slots.  If the current slot size is less
     * than 8K, use the larger of twice the current slot size
     * or the actual number of elements needed.  Otherwise,
     * grow the slots to the next multiple of 4096 (0x1000). */
    if (ssize < 8192) {
        ssize *= 2;
        if (n > ssize) ssize = n;
        if (ssize < 8) ssize = 8;
    }
    else {
        ssize = (n + 0x1000) & ~0xfff;
    }

    /* now allocate the new slot buffer */
    slots = (slots)
            ? realloc(slots, ssize * sizeof(MVMObject *))
            : malloc(ssize * sizeof(MVMObject *));

    /* fill out any unused slots with NULL pointers */
    while (elems < ssize) {
        slots[elems] = NULL;
        elems++;
    }

    body->ssize = ssize;
    body->slots = slots;
}

static void bind_pos_boxed(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMint64 index, MVMObject *obj) {
    MVMArrayBody *body = (MVMArrayBody *)data;
    
    if (index < 0) {
        index += body->elems;
        if (index < 0)
            MVM_exception_throw_adhoc(tc, "MVMArray: Index out of bounds");
    }
    else if (index >= body->elems)
        set_size_internal(tc, body, index + 1);

    MVM_ASSIGN_REF(tc, root, body->slots[body->start + index], obj);
}

static MVMint64 elems(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data) {
    MVMArrayBody *body = (MVMArrayBody *)data;
    return body->elems;
}

static void set_elems(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMint64 count) {
    MVMArrayBody *body = (MVMArrayBody *)data;
    set_size_internal(tc, body, count);
}

static void push_ref(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, void *addr) {
    MVM_exception_throw_adhoc(tc,
        "MVMArray representation not fully implemented yet");
}

static void push_boxed(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMObject *obj) {
    MVM_exception_throw_adhoc(tc,
        "MVMArray representation not fully implemented yet");
}

static void * pop_ref(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, void *target) {
    MVM_exception_throw_adhoc(tc,
        "MVMArray representation not fully implemented yet");
}

static MVMObject * pop_boxed(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data) {
    MVM_exception_throw_adhoc(tc,
        "MVMArray representation not fully implemented yet");
}

static void unshift_ref(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, void *addr) {
    MVM_exception_throw_adhoc(tc,
        "MVMArray representation not fully implemented yet");
}

static void unshift_boxed(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMObject *obj) {
    MVM_exception_throw_adhoc(tc,
        "MVMArray representation not fully implemented yet");
}

static void * shift_ref(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, void *target) {
    MVM_exception_throw_adhoc(tc,
        "MVMArray representation not fully implemented yet");
}

static MVMObject * shift_boxed(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data) {
    MVM_exception_throw_adhoc(tc,
        "MVMArray representation not fully implemented yet");
}

static void splice(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMObject *target_array, MVMint64 offset, MVMint64 elems) {
    MVM_exception_throw_adhoc(tc,
        "MVMArray representation not fully implemented yet");
}

static MVMStorageSpec get_elem_storage_spec(MVMThreadContext *tc, MVMSTable *st) {
    MVMStorageSpec spec;
    spec.inlineable      = MVM_STORAGE_SPEC_REFERENCE;
    spec.boxed_primitive = MVM_STORAGE_SPEC_BP_NONE;
    spec.can_box         = 0;
    return spec;
}

/* Initializes the representation. */
MVMREPROps * MVMArray_initialize(MVMThreadContext *tc) {
    /* Allocate and populate the representation function table. */
    this_repr = malloc(sizeof(MVMREPROps));
    memset(this_repr, 0, sizeof(MVMREPROps));
    this_repr->type_object_for = type_object_for;
    this_repr->allocate = allocate;
    this_repr->initialize = initialize;
    this_repr->copy_to = copy_to;
    this_repr->gc_mark = gc_mark;
    this_repr->gc_free = gc_free;
    this_repr->get_storage_spec = get_storage_spec; 
    this_repr->pos_funcs = malloc(sizeof(MVMREPROps_Positional));
    this_repr->pos_funcs->at_pos_ref = at_pos_ref;
    this_repr->pos_funcs->at_pos_boxed = at_pos_boxed;
    this_repr->pos_funcs->bind_pos_ref = bind_pos_ref;
    this_repr->pos_funcs->bind_pos_boxed = bind_pos_boxed;
    this_repr->pos_funcs->elems = elems;
    this_repr->pos_funcs->set_elems = set_elems;
    this_repr->pos_funcs->push_ref = push_ref;
    this_repr->pos_funcs->push_boxed = push_boxed;
    this_repr->pos_funcs->pop_ref = pop_ref;
    this_repr->pos_funcs->pop_boxed = pop_boxed;
    this_repr->pos_funcs->unshift_ref = unshift_ref;
    this_repr->pos_funcs->unshift_boxed = unshift_boxed;
    this_repr->pos_funcs->shift_ref = shift_ref;
    this_repr->pos_funcs->shift_boxed = shift_boxed;
    this_repr->pos_funcs->splice = splice;
    this_repr->pos_funcs->get_elem_storage_spec = get_elem_storage_spec;
    return this_repr;
}
