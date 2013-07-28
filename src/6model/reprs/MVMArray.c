#include "moarvm.h"

/* This representation's function pointer table. */
static MVMREPROps *this_repr;

/* Some strings. */
static MVMString *str_array = NULL;
static MVMString *str_type  = NULL;

/* Creates a new type object of this representation, and associates it with
 * the given HOW. */
static MVMObject * type_object_for(MVMThreadContext *tc, MVMObject *HOW) {
    MVMSTable        *st;
    MVMObject        *obj;
    MVMArrayREPRData *repr_data;

    st = MVM_gc_allocate_stable(tc, this_repr, HOW);
    MVMROOT(tc, st, {
        obj = MVM_gc_allocate_type_object(tc, st);
        MVM_ASSIGN_REF(tc, st, st->WHAT, obj);
        st->size = sizeof(MVMArray);

        repr_data = (MVMArrayREPRData *)malloc(sizeof(MVMArrayREPRData));
        repr_data->slot_type = MVM_ARRAY_OBJ;
        repr_data->elem_size = sizeof(MVMObject *);
        st->REPR_data = repr_data;
    });

    return st->WHAT;
}

/* Creates a new instance based on the type object. */
static MVMObject * allocate(MVMThreadContext *tc, MVMSTable *st) {
    return MVM_gc_allocate_object(tc, st);
}

/* Initialize a new instance. */
static void initialize(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data) {
}

/* Copies the body of one object to another. The result has the space
 * needed for the current number of elements, which may not be the
 * entire allocated slot size. */
static void copy_to(MVMThreadContext *tc, MVMSTable *st, void *src, MVMObject *dest_root, void *dest) {
    MVMArrayREPRData *repr_data = (MVMArrayREPRData *)st->REPR_data;
    MVMArrayBody     *src_body  = (MVMArrayBody *)src;
    MVMArrayBody     *dest_body = (MVMArrayBody *)dest;
    dest_body->elems = src_body->elems;
    dest_body->ssize = src_body->elems;
    dest_body->start = 0;
    if (dest_body->elems > 0) {
        size_t  mem_size     = dest_body->ssize * repr_data->elem_size;
        size_t  start_pos    = src_body->start * repr_data->elem_size;
        char   *copy_start   = ((char *)src_body->slots.any) + start_pos;
        dest_body->slots.any = malloc(mem_size);
        memcpy(dest_body->slots.any, copy_start, mem_size);
    }
    else {
        dest_body->slots.any = NULL;
    }
}

/* Adds held objects to the GC worklist. */
static void gc_mark(MVMThreadContext *tc, MVMSTable *st, void *data, MVMGCWorklist *worklist) {
    MVMArrayREPRData *repr_data = (MVMArrayREPRData *)st->REPR_data;
    MVMArrayBody     *body      = (MVMArrayBody *)data;
    MVMuint64         elems     = body->elems;
    MVMuint64         start     = body->start;
    MVMuint64         i         = 0;
    switch (repr_data->slot_type) {
        case MVM_ARRAY_OBJ: {
            MVMObject **slots = body->slots.o;
            slots += start;
            while (i < elems) {
                MVM_gc_worklist_add(tc, worklist, &slots[i]);
                i++;
            }
            break;
        }
        case MVM_ARRAY_STR: {
            MVMString **slots = body->slots.s;
            slots += start;
            while (i < elems) {
                MVM_gc_worklist_add(tc, worklist, &slots[i]);
                i++;
            }
            break;
        }
    }
}

/* Called by the VM in order to free memory associated with this object. */
static void gc_free(MVMThreadContext *tc, MVMObject *obj) {
    MVMArray *arr = (MVMArray *)obj;
    if (arr->body.slots.any) {
        free(arr->body.slots.any);
        arr->body.slots.any = NULL;
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

static void at_pos(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMint64 index, MVMRegister *value, MVMuint16 kind) {
    MVMArrayREPRData *repr_data = (MVMArrayREPRData *)st->REPR_data;
    MVMArrayBody     *body      = (MVMArrayBody *)data;

    /* Handle negative indexes. */
    if (index < 0) {
        index += body->elems;
        if (index < 0)
            MVM_exception_throw_adhoc(tc, "MVMArray: Index out of bounds");
    }

    /* Go by type. */
    switch (repr_data->slot_type) {
        case MVM_ARRAY_OBJ:
            if (kind != MVM_reg_obj)
                MVM_exception_throw_adhoc(tc, "MVMArray: atpos expected object register");
            if (index >= body->elems)
                value->o = NULL;
            else
                value->o = body->slots.o[body->start + index];
            break;
        case MVM_ARRAY_STR:
            if (kind != MVM_reg_str)
                MVM_exception_throw_adhoc(tc, "MVMArray: atpos expected string register");
            if (index >= body->elems)
                value->s = NULL;
            else
                value->s = body->slots.s[body->start + index];
            break;
        case MVM_ARRAY_I64:
            if (kind != MVM_reg_int64)
                MVM_exception_throw_adhoc(tc, "MVMArray: atpos expected int register");
            if (index >= body->elems)
                value->i64 = 0;
            else
                value->i64 = (MVMint64)body->slots.i64[body->start + index];
            break;
        case MVM_ARRAY_I32:
            if (kind != MVM_reg_int64)
                MVM_exception_throw_adhoc(tc, "MVMArray: atpos expected int register");
            if (index >= body->elems)
                value->i64 = 0;
            else
                value->i64 = (MVMint64)body->slots.i32[body->start + index];
            break;
        case MVM_ARRAY_I16:
            if (kind != MVM_reg_int64)
                MVM_exception_throw_adhoc(tc, "MVMArray: atpos expected int register");
            if (index >= body->elems)
                value->i64 = 0;
            else
                value->i64 = (MVMint64)body->slots.i16[body->start + index];
            break;
        case MVM_ARRAY_I8:
            if (kind != MVM_reg_int64)
                MVM_exception_throw_adhoc(tc, "MVMArray: atpos expected int register");
            if (index >= body->elems)
                value->i64 = 0;
            else
                value->i64 = (MVMint64)body->slots.i8[body->start + index];
            break;
        case MVM_ARRAY_N64:
            if (kind != MVM_reg_num64)
                MVM_exception_throw_adhoc(tc, "MVMArray: atpos expected num register");
            if (index >= body->elems)
                value->n64 = 0.0;
            else
                value->n64 = (MVMnum64)body->slots.n64[body->start + index];
            break;
        case MVM_ARRAY_N32:
            if (kind != MVM_reg_num64)
                MVM_exception_throw_adhoc(tc, "MVMArray: atpos expected num register");
            if (index >= body->elems)
                value->n64 = 0.0;
            else
                value->n64 = (MVMnum64)body->slots.n32[body->start + index];
            break;
        default:
            MVM_exception_throw_adhoc(tc, "MVMArray: Unhandled slot type");
    }
}

static MVMuint64 zero_slots(MVMThreadContext *tc, MVMArrayBody *body,
        MVMuint64 elems, MVMuint64 ssize, MVMuint8 slot_type) {
    switch (slot_type) {
        case MVM_ARRAY_OBJ:
            while (elems < ssize)
                body->slots.o[elems++] = NULL;
            break;
        case MVM_ARRAY_STR:
            while (elems < ssize)
                body->slots.s[elems++] = NULL;
            break;
        case MVM_ARRAY_I64:
            while (elems < ssize)
                body->slots.i64[elems++] = 0;
            break;
        case MVM_ARRAY_I32:
            while (elems < ssize)
                body->slots.i32[elems++] = 0;
            break;
        case MVM_ARRAY_I16:
            while (elems < ssize)
                body->slots.i16[elems++] = 0;
            break;
        case MVM_ARRAY_I8:
            while (elems < ssize)
                body->slots.i8[elems++] = 0;
            break;
        case MVM_ARRAY_N64:
            while (elems < ssize)
                body->slots.n64[elems++] = 0.0;
            break;
        case MVM_ARRAY_N32:
            while (elems < ssize)
                body->slots.n32[elems++] = 0.0;
            break;
        default:
            MVM_exception_throw_adhoc(tc, "MVMArray: Unhandled slot type");
    }
    return elems;
}

static void set_size_internal(MVMThreadContext *tc, MVMArrayBody *body, MVMint64 n, MVMArrayREPRData *repr_data) {
    MVMuint64   elems = body->elems;
    MVMuint64   start = body->start;
    MVMuint64   ssize = body->ssize;
    void       *slots = body->slots.any;

    if (n < 0)
        MVM_exception_throw_adhoc(tc,
            "MVMArray: Can't resize to negative elements");

    if (n == elems)
        return;

    /* if there aren't enough slots at the end, shift off empty slots
     * from the beginning first */
    if (start > 0 && n + start > ssize) {
        if (elems > 0)
            memmove(slots,
                (char *)slots + start * repr_data->elem_size,
                elems * repr_data->elem_size);
        body->start = 0;
        /* fill out any unused slots with NULL pointers or zero values */
        elems = zero_slots(tc, body, elems, ssize, repr_data->slot_type);
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
            ? realloc(slots, ssize * repr_data->elem_size)
            : malloc(ssize * repr_data->elem_size);

    /* fill out any unused slots with NULL pointers or zero values */
    body->slots.any = slots;
    elems = zero_slots(tc, body, elems, ssize, repr_data->slot_type);

    body->ssize = ssize;
}

static void bind_pos(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMint64 index, MVMRegister value, MVMuint16 kind) {
    MVMArrayREPRData *repr_data = (MVMArrayREPRData *)st->REPR_data;
    MVMArrayBody     *body      = (MVMArrayBody *)data;

    /* Handle negative indexes and resizing if needed. */
    if (index < 0) {
        index += body->elems;
        if (index < 0)
            MVM_exception_throw_adhoc(tc, "MVMArray: Index out of bounds");
    }
    else if (index >= body->elems)
        set_size_internal(tc, body, index + 1, repr_data);

    /* Go by type. */
    switch (repr_data->slot_type) {
        case MVM_ARRAY_OBJ:
            if (kind != MVM_reg_obj)
                MVM_exception_throw_adhoc(tc, "MVMArray: bindpos expected object register");
            MVM_ASSIGN_REF(tc, root, body->slots.o[body->start + index], value.o);
            break;
        case MVM_ARRAY_STR:
            if (kind != MVM_reg_str)
                MVM_exception_throw_adhoc(tc, "MVMArray: bindpos expected string register");
            MVM_ASSIGN_REF(tc, root, body->slots.s[body->start + index], value.s);
            break;
        case MVM_ARRAY_I64:
            if (kind != MVM_reg_int64)
                MVM_exception_throw_adhoc(tc, "MVMArray: bindpos expected int register");
            body->slots.i64[body->start + index] = value.i64;
            break;
        case MVM_ARRAY_I32:
            if (kind != MVM_reg_int64)
                MVM_exception_throw_adhoc(tc, "MVMArray: bindpos expected int register");
            body->slots.i32[body->start + index] = (MVMint32)value.i64;
            break;
        case MVM_ARRAY_I16:
            if (kind != MVM_reg_int64)
                MVM_exception_throw_adhoc(tc, "MVMArray: bindpos expected int register");
            body->slots.i16[body->start + index] = (MVMint16)value.i64;
            break;
        case MVM_ARRAY_I8:
            if (kind != MVM_reg_int64)
                MVM_exception_throw_adhoc(tc, "MVMArray: bindpos expected int register");
            body->slots.i8[body->start + index] = (MVMint8)value.i64;
            break;
        case MVM_ARRAY_N64:
            if (kind != MVM_reg_num64)
                MVM_exception_throw_adhoc(tc, "MVMArray: bindpos expected num register");
            body->slots.n64[body->start + index] = value.n64;
            break;
        case MVM_ARRAY_N32:
            if (kind != MVM_reg_num64)
                MVM_exception_throw_adhoc(tc, "MVMArray: bindpos expected num register");
            body->slots.n32[body->start + index] = (MVMnum32)value.n64;
            break;
        default:
            MVM_exception_throw_adhoc(tc, "MVMArray: Unhandled slot type");
    }
}

static MVMuint64 elems(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data) {
    MVMArrayBody *body = (MVMArrayBody *)data;
    return body->elems;
}

static void set_elems(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMuint64 count) {
    MVMArrayREPRData *repr_data = (MVMArrayREPRData *)st->REPR_data;
    MVMArrayBody     *body      = (MVMArrayBody *)data;
    set_size_internal(tc, body, count, repr_data);
}

MVMint64 exists_pos(struct _MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMint64 index) {
    MVMArrayBody     *body      = (MVMArrayBody *)data;

    /* Handle negative indexes. */
    if (index < 0) {
        index += body->elems;
    }

    if (index < 0 || index >= body->elems) {
        return 0;
    }

    return body->slots.o[body->start + index] != NULL;
}

static void push(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMRegister value, MVMuint16 kind) {
    MVMArrayBody     *body      = (MVMArrayBody *)data;
    MVMArrayREPRData *repr_data = (MVMArrayREPRData *)st->REPR_data;
    set_size_internal(tc, body, body->elems + 1, repr_data);
    switch (repr_data->slot_type) {
        case MVM_ARRAY_OBJ:
            if (kind != MVM_reg_obj)
                MVM_exception_throw_adhoc(tc, "MVMArray: push expected object register");
            MVM_ASSIGN_REF(tc, root, body->slots.o[body->start + body->elems - 1], value.o);
            break;
        case MVM_ARRAY_STR:
            if (kind != MVM_reg_str)
                MVM_exception_throw_adhoc(tc, "MVMArray: push expected string register");
            MVM_ASSIGN_REF(tc, root, body->slots.s[body->start + body->elems - 1], value.s);
            break;
        case MVM_ARRAY_I64:
            if (kind != MVM_reg_int64)
                MVM_exception_throw_adhoc(tc, "MVMArray: push expected int register");
            body->slots.i64[body->start + body->elems - 1] = value.i64;
            break;
        case MVM_ARRAY_I32:
            if (kind != MVM_reg_int64)
                MVM_exception_throw_adhoc(tc, "MVMArray: push expected int register");
            body->slots.i32[body->start + body->elems - 1] = (MVMint32)value.i64;
            break;
        case MVM_ARRAY_I16:
            if (kind != MVM_reg_int64)
                MVM_exception_throw_adhoc(tc, "MVMArray: push expected int register");
            body->slots.i16[body->start + body->elems - 1] = (MVMint16)value.i64;
            break;
        case MVM_ARRAY_I8:
            if (kind != MVM_reg_int64)
                MVM_exception_throw_adhoc(tc, "MVMArray: push expected int register");
            body->slots.i8[body->start + body->elems - 1] = (MVMint8)value.i64;
            break;
        case MVM_ARRAY_N64:
            if (kind != MVM_reg_num64)
                MVM_exception_throw_adhoc(tc, "MVMArray: push expected num register");
            body->slots.n64[body->start + body->elems - 1] = value.n64;
            break;
        case MVM_ARRAY_N32:
            if (kind != MVM_reg_num64)
                MVM_exception_throw_adhoc(tc, "MVMArray: push expected num register");
            body->slots.n32[body->start + body->elems - 1] = (MVMnum32)value.n64;
            break;
        default:
            MVM_exception_throw_adhoc(tc, "MVMArray: Unhandled slot type");
    }
}

static void pop(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMRegister *value, MVMuint16 kind) {
    MVMArrayREPRData *repr_data = (MVMArrayREPRData *)st->REPR_data;
    MVMArrayBody     *body      = (MVMArrayBody *)data;

    if (body->elems < 1)
        MVM_exception_throw_adhoc(tc,
            "MVMArray: Can't pop from an empty array");

    body->elems--;
    switch (repr_data->slot_type) {
        case MVM_ARRAY_OBJ:
            if (kind != MVM_reg_obj)
                MVM_exception_throw_adhoc(tc, "MVMArray: pop expected object register");
            value->o = body->slots.o[body->start + body->elems];
            break;
        case MVM_ARRAY_STR:
            if (kind != MVM_reg_str)
                MVM_exception_throw_adhoc(tc, "MVMArray: pop expected string register");
            value->s = body->slots.s[body->start + body->elems];
            break;
        case MVM_ARRAY_I64:
            if (kind != MVM_reg_int64)
                MVM_exception_throw_adhoc(tc, "MVMArray: pop expected int register");
            value->i64 = (MVMint64)body->slots.i64[body->start + body->elems];
            break;
        case MVM_ARRAY_I32:
            if (kind != MVM_reg_int64)
                MVM_exception_throw_adhoc(tc, "MVMArray: pop expected int register");
            value->i64 = (MVMint64)body->slots.i32[body->start + body->elems];
            break;
        case MVM_ARRAY_I16:
            if (kind != MVM_reg_int64)
                MVM_exception_throw_adhoc(tc, "MVMArray: pop expected int register");
            value->i64 = (MVMint64)body->slots.i16[body->start + body->elems];
            break;
        case MVM_ARRAY_I8:
            if (kind != MVM_reg_int64)
                MVM_exception_throw_adhoc(tc, "MVMArray: pop expected int register");
            value->i64 = (MVMint64)body->slots.i8[body->start + body->elems];
            break;
        case MVM_ARRAY_N64:
            if (kind != MVM_reg_num64)
                MVM_exception_throw_adhoc(tc, "MVMArray: pop expected num register");
            value->n64 = (MVMnum64)body->slots.n64[body->start + body->elems];
            break;
        case MVM_ARRAY_N32:
            if (kind != MVM_reg_num64)
                MVM_exception_throw_adhoc(tc, "MVMArray: pop expected num register");
            value->n64 = (MVMnum64)body->slots.n32[body->start + body->elems];
            break;
        default:
            MVM_exception_throw_adhoc(tc, "MVMArray: Unhandled slot type");
    }
}

static void unshift(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMRegister value, MVMuint16 kind) {
    MVMArrayREPRData *repr_data = (MVMArrayREPRData *)st->REPR_data;
    MVMArrayBody     *body      = (MVMArrayBody *)data;

    /* If we don't have room at the beginning of the slots,
     * make some room (8 slots) for unshifting */
    if (body->start < 1) {
        MVMuint64 n = 8;
        MVMuint64 elems = body->elems;
        MVMuint64 i;

        /* grow the array */
        set_size_internal(tc, body, elems + n, repr_data);

        /* move elements and set start */
        memmove(
            (char *)body->slots.any + n * repr_data->elem_size,
            body->slots.any,
            elems * repr_data->elem_size);
        body->start = n;
        body->elems = elems;

        /* clear out beginning elements */
        zero_slots(tc, body, 0, n, repr_data->slot_type);
    }

    /* Now do the unshift */
    body->start--;
    switch (repr_data->slot_type) {
        case MVM_ARRAY_OBJ:
            if (kind != MVM_reg_obj)
                MVM_exception_throw_adhoc(tc, "MVMArray: unshift expected object register");
            MVM_ASSIGN_REF(tc, root, body->slots.o[body->start], value.o);
            break;
        case MVM_ARRAY_STR:
            if (kind != MVM_reg_str)
                MVM_exception_throw_adhoc(tc, "MVMArray: unshift expected string register");
            MVM_ASSIGN_REF(tc, root, body->slots.s[body->start], value.s);
            break;
        case MVM_ARRAY_I64:
            if (kind != MVM_reg_int64)
                MVM_exception_throw_adhoc(tc, "MVMArray: unshift expected int register");
            body->slots.i64[body->start] = value.i64;
            break;
        case MVM_ARRAY_I32:
            if (kind != MVM_reg_int64)
                MVM_exception_throw_adhoc(tc, "MVMArray: unshift expected int register");
            body->slots.i32[body->start] = (MVMint32)value.i64;
            break;
        case MVM_ARRAY_I16:
            if (kind != MVM_reg_int64)
                MVM_exception_throw_adhoc(tc, "MVMArray: unshift expected int register");
            body->slots.i16[body->start] = (MVMint16)value.i64;
            break;
        case MVM_ARRAY_I8:
            if (kind != MVM_reg_int64)
                MVM_exception_throw_adhoc(tc, "MVMArray: unshift expected int register");
            body->slots.i8[body->start] = (MVMint8)value.i64;
            break;
        case MVM_ARRAY_N64:
            if (kind != MVM_reg_num64)
                MVM_exception_throw_adhoc(tc, "MVMArray: unshift expected num register");
            body->slots.n64[body->start] = value.n64;
            break;
        case MVM_ARRAY_N32:
            if (kind != MVM_reg_num64)
                MVM_exception_throw_adhoc(tc, "MVMArray: unshift expected num register");
            body->slots.n32[body->start] = (MVMnum32)value.n64;
            break;
        default:
            MVM_exception_throw_adhoc(tc, "MVMArray: Unhandled slot type");
    }
    body->elems++;
}

static void shift(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMRegister *value, MVMuint16 kind) {
    MVMArrayBody     *body      = (MVMArrayBody *)data;
    MVMArrayREPRData *repr_data = (MVMArrayREPRData *)st->REPR_data;

    if (body->elems < 1)
        MVM_exception_throw_adhoc(tc,
            "MVMArray: Can't shift from an empty array");

    switch (repr_data->slot_type) {
        case MVM_ARRAY_OBJ:
            if (kind != MVM_reg_obj)
                MVM_exception_throw_adhoc(tc, "MVMArray: shift expected object register");
            value->o = body->slots.o[body->start];
            break;
        case MVM_ARRAY_STR:
            if (kind != MVM_reg_str)
                MVM_exception_throw_adhoc(tc, "MVMArray: shift expected string register");
            value->s = body->slots.s[body->start];
            break;
        case MVM_ARRAY_I64:
            if (kind != MVM_reg_int64)
                MVM_exception_throw_adhoc(tc, "MVMArray: shift expected int register");
            value->i64 = (MVMint64)body->slots.i64[body->start];
            break;
        case MVM_ARRAY_I32:
            if (kind != MVM_reg_int64)
                MVM_exception_throw_adhoc(tc, "MVMArray: shift expected int register");
            value->i64 = (MVMint64)body->slots.i32[body->start];
            break;
        case MVM_ARRAY_I16:
            if (kind != MVM_reg_int64)
                MVM_exception_throw_adhoc(tc, "MVMArray: shift expected int register");
            value->i64 = (MVMint64)body->slots.i16[body->start];
            break;
        case MVM_ARRAY_I8:
            if (kind != MVM_reg_int64)
                MVM_exception_throw_adhoc(tc, "MVMArray: shift expected int register");
            value->i64 = (MVMint64)body->slots.i8[body->start];
            break;
        case MVM_ARRAY_N64:
            if (kind != MVM_reg_num64)
                MVM_exception_throw_adhoc(tc, "MVMArray: shift expected num register");
            value->n64 = (MVMnum64)body->slots.n64[body->start];
            break;
        case MVM_ARRAY_N32:
            if (kind != MVM_reg_num64)
                MVM_exception_throw_adhoc(tc, "MVMArray: shift expected num register");
            value->n64 = (MVMnum64)body->slots.n32[body->start];
            break;
        default:
            MVM_exception_throw_adhoc(tc, "MVMArray: Unhandled slot type");
    }
    body->start++;
    body->elems--;
}

/* This whole splice optimization can be optimized for the case we have two
 * MVMArray representation objects. */
static void splice(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMObject *from, MVMint64 offset, MVMuint64 count) {
    MVMArrayREPRData *repr_data = (MVMArrayREPRData *)st->REPR_data;
    MVMArrayBody     *body      = (MVMArrayBody *)data;

    MVMint64 elems0 = body->elems;
    MVMint64 elems1 = REPR(from)->elems(tc, STABLE(from), from, OBJECT_BODY(from));
    MVMint64 start;
    MVMint64 tail;

    /* start from end? */
    if (offset < 0) {
        offset += elems0;

        if (offset < 0)
            MVM_exception_throw_adhoc(tc,
                "MVMArray: Illegal splice offset");
    }

    /* When offset == 0, then we may be able to reduce the memmove
     * calls and reallocs by adjusting SELF's start, elems0, and
     * count to better match the incoming splice.  In particular,
     * we're seeking to adjust C<count> to as close to C<elems1>
     * as we can. */
    if (offset == 0) {
        MVMint64 n = elems1 - count;
        start = body->start;
        if (n > start)
            n = start;
        if (n <= -elems0) {
            elems0 = 0;
            count = 0;
            body->start = 0;
            body->elems = elems0;
        }
        else if (n != 0) {
            elems0 += n;
            count += n;
            body->start = start - n;
            body->elems = elems0;
        }
    }

    /* if count == 0 and elems1 == 0, there's nothing left
     * to copy or remove, so the splice is done! */
    if (count == 0 && elems1 == 0)
        return;

    /* number of elements to right of splice (the "tail") */
    tail = elems0 - offset - count;
    if (tail < 0)
        tail = 0;

    else if (tail > 0 && count > elems1) {
        /* We're shrinking the array, so first move the tail left */
        start = body->start;
        memmove(
            (char *)body->slots.any + (start + offset + elems1) * repr_data->elem_size,
            (char *)body->slots.any + (start + offset + count) * repr_data->elem_size,
            tail * repr_data->elem_size);
    }

    /* now resize the array */
    set_size_internal(tc, body, offset + elems1 + tail, repr_data);

    start = body->start;
    if (tail > 0 && count < elems1) {
        /* The array grew, so move the tail to the right */
        memmove(
            (char *)body->slots.any + (start + offset + elems1) * repr_data->elem_size,
            (char *)body->slots.any + (start + offset + count) * repr_data->elem_size,
            tail * repr_data->elem_size);
    }

    /* now copy C<from>'s elements into SELF */
    if (elems1 > 0) {
        MVMint64  i;
        MVMuint16 kind;
        switch (repr_data->slot_type) {
            case MVM_ARRAY_OBJ:
                kind = MVM_reg_obj;
                break;
            case MVM_ARRAY_STR:
                kind = MVM_reg_str;
                break;
            case MVM_ARRAY_I64:
            case MVM_ARRAY_I32:
            case MVM_ARRAY_I16:
            case MVM_ARRAY_I8:
                kind = MVM_reg_int64;
                break;
            case MVM_ARRAY_N64:
            case MVM_ARRAY_N32:
                kind = MVM_reg_num64;
                break;
        }
        for (i = 0; i < elems1; i++) {
            MVMRegister to_copy;
            REPR(from)->pos_funcs->at_pos(tc, STABLE(from), from,
                OBJECT_BODY(from), i, &to_copy, kind);
            bind_pos(tc, st, root, data, start + offset + i, to_copy, kind);
        }
    }
}

static MVMStorageSpec get_elem_storage_spec(MVMThreadContext *tc, MVMSTable *st) {
    MVMStorageSpec spec;
    spec.inlineable      = MVM_STORAGE_SPEC_REFERENCE;
    spec.boxed_primitive = MVM_STORAGE_SPEC_BP_NONE;
    spec.can_box         = 0;
    return spec;
}

/* Compose the representation. */
static void compose(MVMThreadContext *tc, MVMSTable *st, MVMObject *info_hash) {
    MVMArrayREPRData *repr_data = (MVMArrayREPRData *)st->REPR_data;

    MVMObject *info = REPR(info_hash)->ass_funcs->at_key_boxed(tc, STABLE(info_hash),
        info_hash, OBJECT_BODY(info_hash), (MVMObject *)str_array);
    if (info != NULL) {
        MVMObject *type = REPR(info)->ass_funcs->at_key_boxed(tc, STABLE(info),
            info, OBJECT_BODY(info), (MVMObject *)str_type);
        if (type != NULL) {
            MVMStorageSpec spec = REPR(type)->get_storage_spec(tc, STABLE(type));
            switch (spec.boxed_primitive) {
                case MVM_STORAGE_SPEC_BP_INT:
                    switch (spec.bits) {
                        case 64:
                            repr_data->slot_type = MVM_ARRAY_I64;
                            repr_data->elem_size = sizeof(MVMint64);
                            break;
                        case 32:
                            repr_data->slot_type = MVM_ARRAY_I32;
                            repr_data->elem_size = sizeof(MVMint32);
                            break;
                        case 16:
                            repr_data->slot_type = MVM_ARRAY_I16;
                            repr_data->elem_size = sizeof(MVMint16);
                            break;
                        case 8:
                            repr_data->slot_type = MVM_ARRAY_I8;
                            repr_data->elem_size = sizeof(MVMint8);
                            break;
                        default:
                            MVM_exception_throw_adhoc(tc,
                                "MVMArray: Unsupported int size");
                    }
                    break;
                case MVM_STORAGE_SPEC_BP_NUM:
                    switch (spec.bits) {
                        case 64:
                            repr_data->slot_type = MVM_ARRAY_N64;
                            repr_data->elem_size = sizeof(MVMnum64);
                            break;
                        case 32:
                            repr_data->slot_type = MVM_ARRAY_N32;
                            repr_data->elem_size = sizeof(MVMnum32);
                            break;
                        default:
                            MVM_exception_throw_adhoc(tc,
                                "MVMArray: Unsupported num size");
                    }
                    break;
                case MVM_STORAGE_SPEC_BP_STR:
                    repr_data->slot_type = MVM_ARRAY_STR;
                    repr_data->elem_size = sizeof(MVMString *);
                    break;
            }
        }
    }
}

/* Set the size of the STable. */
static void deserialize_stable_size(MVMThreadContext *tc, MVMSTable *st, MVMSerializationReader *reader) {
    st->size = sizeof(MVMArray);
}

/* Deserializes representation data. */
static void deserialize_repr_data(MVMThreadContext *tc, MVMSTable *st, MVMSerializationReader *reader) {
    MVMArrayREPRData *repr_data = (MVMArrayREPRData *)malloc(sizeof(MVMArrayREPRData));
    repr_data->slot_type = MVM_ARRAY_OBJ;
    repr_data->elem_size = sizeof(MVMObject *);
    st->REPR_data = repr_data;
}

/* Initializes the representation. */
MVMREPROps * MVMArray_initialize(MVMThreadContext *tc) {
    /* Set up some constant strings we'll need. */
    str_array = MVM_string_ascii_decode_nt(tc, tc->instance->VMString, "array");
    MVM_gc_root_add_permanent(tc, (MVMCollectable **)&str_array);
    str_type = MVM_string_ascii_decode_nt(tc, tc->instance->VMString, "type");
    MVM_gc_root_add_permanent(tc, (MVMCollectable **)&str_type);

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
    this_repr->pos_funcs->at_pos = at_pos;
    this_repr->pos_funcs->bind_pos = bind_pos;
    this_repr->pos_funcs->set_elems = set_elems;
    this_repr->pos_funcs->exists_pos = exists_pos;
    this_repr->pos_funcs->push = push;
    this_repr->pos_funcs->pop = pop;
    this_repr->pos_funcs->unshift = unshift;
    this_repr->pos_funcs->shift = shift;
    this_repr->pos_funcs->splice = splice;
    this_repr->pos_funcs->get_elem_storage_spec = get_elem_storage_spec;
    this_repr->compose = compose;
    this_repr->elems = elems;
    this_repr->deserialize_stable_size = deserialize_stable_size;
    this_repr->deserialize_repr_data = deserialize_repr_data;
    return this_repr;
}
