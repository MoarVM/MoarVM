#include "moar.h"
#include "limits.h"

/* This representation's function pointer table. */
static const MVMREPROps VMArray_this_repr;

MVM_STATIC_INLINE void enter_single_user(MVMThreadContext *tc, MVMArrayBody *arr) {
#if MVM_ARRAY_CONC_DEBUG
    if (!MVM_trycas(&(arr->in_use), 0, 1)) {
        MVM_dump_backtrace(tc);
        MVM_exception_throw_adhoc(tc, "Array may not be used concurrently");
    }
#endif
}
static void exit_single_user(MVMThreadContext *tc, MVMArrayBody *arr) {
#if MVM_ARRAY_CONC_DEBUG
    arr->in_use = 0;
#endif
}

#define MVM_MAX(a,b) ((a)>(b)?(a):(b))
#define MVM_MIN(a,b) ((a)<(b)?(a):(b))

/* Creates a new type object of this representation, and associates it with
 * the given HOW. */
static MVMObject * type_object_for(MVMThreadContext *tc, MVMObject *HOW) {
    MVMSTable        *st = MVM_gc_allocate_stable(tc, &VMArray_this_repr, HOW);

    MVMROOT(tc, st) {
        MVMObject *obj = MVM_gc_allocate_type_object(tc, st);
        MVMArrayREPRData *repr_data = (MVMArrayREPRData *)MVM_malloc(sizeof(MVMArrayREPRData));

        repr_data->slot_type = MVM_ARRAY_OBJ;
        repr_data->elem_size = sizeof(MVMObject *);
        repr_data->elem_type = NULL;

        MVM_ASSIGN_REF(tc, &(st->header), st->WHAT, obj);
        st->size = sizeof(MVMArray);
        st->REPR_data = repr_data;
    }

    return st->WHAT;
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
        dest_body->slots.any = MVM_malloc(mem_size);
        memcpy(dest_body->slots.any, copy_start, mem_size);
    }
    else {
        dest_body->slots.any = NULL;
    }
}

/* Adds held objects to the GC worklist. */
static void VMArray_gc_mark(MVMThreadContext *tc, MVMSTable *st, void *data, MVMGCWorklist *worklist) {
    MVMArrayREPRData *repr_data = (MVMArrayREPRData *)st->REPR_data;
    MVMArrayBody     *body      = (MVMArrayBody *)data;
    MVMuint64         elems     = body->elems;
    MVMuint64         start     = body->start;
    MVMuint64         i         = 0;

    /* Aren't holding anything, nothing to do. */
    if (elems == 0)
        return;

    switch (repr_data->slot_type) {
        case MVM_ARRAY_OBJ: {
            MVMObject **slots = body->slots.o;
            slots += start;
            MVM_gc_worklist_presize_for(tc, worklist, elems);
            if (worklist->include_gen2) {
                for (; i < elems; i++)
                    MVM_gc_worklist_add_include_gen2_nocheck(tc, worklist, &slots[i]);
            }
            else {
                for (; i < elems; i++)
                    MVM_gc_worklist_add_no_include_gen2_nocheck(tc, worklist, &slots[i]);
            }
            break;
        }
        case MVM_ARRAY_STR: {
            MVMString **slots = body->slots.s;
            slots += start;
            MVM_gc_worklist_presize_for(tc, worklist, elems);
            if (worklist->include_gen2) {
                for (; i < elems; i++)
                    MVM_gc_worklist_add_include_gen2_nocheck(tc, worklist, &slots[i]);
            }
            else {
                for (; i < elems; i++)
                    MVM_gc_worklist_add_no_include_gen2_nocheck(tc, worklist, &slots[i]);
            }
            break;
        }
    }
}

/* Called by the VM in order to free memory associated with this object. */
static void gc_free(MVMThreadContext *tc, MVMObject *obj) {
    MVMArray *arr = (MVMArray *)obj;
    MVM_free(arr->body.slots.any);
}

/* Marks the representation data in an STable.*/
static void gc_mark_repr_data(MVMThreadContext *tc, MVMSTable *st, MVMGCWorklist *worklist) {
    MVMArrayREPRData *repr_data = (MVMArrayREPRData *)st->REPR_data;
    if (repr_data == NULL)
        return;
    MVM_gc_worklist_add(tc, worklist, &repr_data->elem_type);
}

/* Frees the representation data in an STable.*/
static void gc_free_repr_data(MVMThreadContext *tc, MVMSTable *st) {
    MVM_free(st->REPR_data);
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

void MVM_VMArray_at_pos_s(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMint64 index, MVMRegister *value) {
    MVMArrayREPRData *repr_data = (MVMArrayREPRData *)st->REPR_data;
    MVMArrayBody     *body      = (MVMArrayBody *)data;

    /* Handle negative indexes. */
    if (index < 0) {
        index += body->elems;
        if (index < 0)
            MVM_exception_throw_adhoc(tc, "MVMArray: Index out of bounds");
    }

    /* Go by type. */
    if (repr_data->slot_type != MVM_ARRAY_STR)
        MVM_exception_throw_adhoc(tc, "MVMArray: atpos expected a string register, but %u is not MVM_ARRAY_STR", repr_data->slot_type);
    if ((MVMuint64)index >= body->elems)
        value->s = NULL;
    else
        value->s = body->slots.s[body->start + index];
}

void MVM_VMArray_at_pos_i(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMint64 index, MVMRegister *value) {
    MVMArrayBody     *body      = (MVMArrayBody *)data;

    /* Handle negative indexes. */
    if (index < 0) {
        index += body->elems;
        if (index < 0)
            MVM_exception_throw_adhoc(tc, "MVMArray: Index out of bounds");
    }

    /* Go by type. */
    if ((MVMuint64)index >= body->elems)
        value->i64 = 0;
    else
        value->i64 = (MVMint64)body->slots.i64[body->start + index];
}

void MVM_VMArray_at_pos_u(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMint64 index, MVMRegister *value) {
    MVMArrayBody     *body      = (MVMArrayBody *)data;

    /* Handle negative indexes. */
    if (index < 0) {
        index += body->elems;
        if (index < 0)
            MVM_exception_throw_adhoc(tc, "MVMArray: Index out of bounds");
    }

    /* Go by type. */
    if ((MVMuint64)index >= body->elems)
        value->u64 = 0;
    else
        value->u64 = (MVMuint64)body->slots.u64[body->start + index];
}

void MVM_VMArray_at_pos_n(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMint64 index, MVMRegister *value) {
    MVMArrayBody     *body      = (MVMArrayBody *)data;

    /* Handle negative indexes. */
    if (index < 0) {
        index += body->elems;
        if (index < 0)
            MVM_exception_throw_adhoc(tc, "MVMArray: Index out of bounds");
    }

    /* Go by type. */
    if ((MVMuint64)index >= body->elems)
        value->n64 = 0;
    else
        value->n64 = (MVMnum64)body->slots.n64[body->start + index];
}

void MVM_VMArray_at_pos_o(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMint64 index, MVMRegister *value) {
    MVMArrayREPRData *repr_data = (MVMArrayREPRData *)st->REPR_data;
    MVMArrayBody     *body      = (MVMArrayBody *)data;

    /* Handle negative indexes. */
    if (index < 0) {
        index += body->elems;
        if (index < 0)
            MVM_exception_throw_adhoc(tc, "MVMArray: Index out of bounds");
    }

    /* Go by type. */
    if (repr_data->slot_type != MVM_ARRAY_OBJ)
        MVM_exception_throw_adhoc(tc, "MVMArray: atpos with an object register, but array type %u is not MVM_ARRAY_OBJ", repr_data->slot_type);
    if ((MVMuint64)index >= body->elems) {
        value->o = tc->instance->VMNull;
    }
    else {
        MVMObject *found = body->slots.o[body->start + index];
        value->o = found ? found : tc->instance->VMNull;
    }
}

void MVM_VMArray_at_pos(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMint64 index, MVMRegister *value, MVMuint16 kind) {
    MVMArrayREPRData *repr_data = (MVMArrayREPRData *)st->REPR_data;
    MVMArrayBody     *body      = (MVMArrayBody *)data;
    MVMuint64        real_index;

    /* Handle negative indexes. */
    if (index < 0) {
        index += body->elems;
        if (index < 0)
            MVM_exception_throw_adhoc(tc, "MVMArray: Index out of bounds");
    }

    real_index = (MVMuint64)index;

    /* Go by type. */
    switch (repr_data->slot_type) {
        case MVM_ARRAY_OBJ:
            if (kind != MVM_reg_obj)
                MVM_exception_throw_adhoc(tc, "MVMArray: atpos expected object register");
            if (real_index >= body->elems) {
                value->o = tc->instance->VMNull;
            }
            else {
                MVMObject *found = body->slots.o[body->start + real_index];
                value->o = found ? found : tc->instance->VMNull;
            }
            break;
        case MVM_ARRAY_STR:
            if (kind != MVM_reg_str)
                MVM_exception_throw_adhoc(tc, "MVMArray: atpos expected string register");
            if (real_index >= body->elems)
                value->s = NULL;
            else
                value->s = body->slots.s[body->start + real_index];
            break;
        case MVM_ARRAY_I64:
            if (kind != MVM_reg_int64)
                MVM_exception_throw_adhoc(tc, "MVMArray: atpos I64 expected int register");
            if (real_index >= body->elems)
                value->i64 = 0;
            else
                value->i64 = (MVMint64)body->slots.i64[body->start + real_index];
            break;
        case MVM_ARRAY_I32:
            if (kind != MVM_reg_int64)
                MVM_exception_throw_adhoc(tc, "MVMArray: atpos I32 expected int register");
            if (real_index >= body->elems)
                value->i64 = 0;
            else
                value->i64 = (MVMint64)body->slots.i32[body->start + real_index];
            break;
        case MVM_ARRAY_I16:
            if (kind != MVM_reg_int64)
                MVM_exception_throw_adhoc(tc, "MVMArray: atpos I16 expected int register");
            if (real_index >= body->elems)
                value->i64 = 0;
            else
                value->i64 = (MVMint64)body->slots.i16[body->start + real_index];
            break;
        case MVM_ARRAY_I8:
            if (kind != MVM_reg_int64)
                MVM_exception_throw_adhoc(tc, "MVMArray: atpos I8 expected int register");
            if (real_index >= body->elems)
                value->i64 = 0;
            else
                value->i64 = (MVMint64)body->slots.i8[body->start + real_index];
            break;
        case MVM_ARRAY_N64:
            if (kind != MVM_reg_num64)
                MVM_exception_throw_adhoc(tc, "MVMArray: atpos expected num register");
            if (real_index >= body->elems)
                value->n64 = 0.0;
            else
                value->n64 = (MVMnum64)body->slots.n64[body->start + real_index];
            break;
        case MVM_ARRAY_N32:
            if (kind != MVM_reg_num64)
                MVM_exception_throw_adhoc(tc, "MVMArray: atpos expected num register");
            if (real_index >= body->elems)
                value->n64 = 0.0;
            else
                value->n64 = (MVMnum64)body->slots.n32[body->start + real_index];
            break;
        case MVM_ARRAY_U64:
            if (kind != MVM_reg_uint64 && kind != MVM_reg_int64)
                MVM_exception_throw_adhoc(tc, "MVMArray: atpos U64 expected int register, got %d instead", kind);
            if (real_index >= body->elems)
                value->u64 = 0;
            else
                value->u64 = (MVMint64)body->slots.u64[body->start + real_index];
            break;
        case MVM_ARRAY_U32:
            if (kind != MVM_reg_uint64 && kind != MVM_reg_int64)
                MVM_exception_throw_adhoc(tc, "MVMArray: atpos U32 expected int register");
            if (real_index >= body->elems)
                value->u64 = 0;
            else
                value->u64 = (MVMint64)body->slots.u32[body->start + real_index];
            break;
        case MVM_ARRAY_U16:
            if (kind != MVM_reg_uint64 && kind != MVM_reg_int64)
                MVM_exception_throw_adhoc(tc, "MVMArray: atpos U16 expected int register");
            if (real_index >= body->elems)
                value->u64 = 0;
            else
                value->u64 = (MVMint64)body->slots.u16[body->start + real_index];
            break;
        case MVM_ARRAY_U8:
            if (kind != MVM_reg_uint64 && kind != MVM_reg_int64)
                MVM_exception_throw_adhoc(tc, "MVMArray: atpos U8 expected int register");
            if (real_index >= body->elems)
                value->u64 = 0;
            else
                value->u64 = (MVMint64)body->slots.u8[body->start + real_index];
            break;
        default:
            MVM_exception_throw_adhoc(tc, "MVMArray: Unhandled slot type, got '%s'", MVM_reg_get_debug_name(tc, repr_data->slot_type));
    }
}

static MVMuint64 zero_slots(MVMThreadContext *tc, MVMArrayBody *body,
        MVMuint64 elems, MVMuint64 ssize, MVMuint8 slot_type) {
    switch (slot_type) {
        case MVM_ARRAY_OBJ:
            memset(&(body->slots.o[elems]), 0, (ssize - elems) * sizeof(MVMObject *));
            break;
        case MVM_ARRAY_STR:
            memset(&(body->slots.s[elems]), 0, (ssize - elems) * sizeof(MVMString *));
            break;
        case MVM_ARRAY_I64:
            memset(&(body->slots.i64[elems]), 0, (ssize - elems) * sizeof(MVMint64));
            break;
        case MVM_ARRAY_I32:
            memset(&(body->slots.i32[elems]), 0, (ssize - elems) * sizeof(MVMint32));
            break;
        case MVM_ARRAY_I16:
            memset(&(body->slots.i16[elems]), 0, (ssize - elems) * sizeof(MVMint16));
            break;
        case MVM_ARRAY_I8:
            memset(&(body->slots.i8[elems]), 0, (ssize - elems) * sizeof(MVMint8));
            break;
        case MVM_ARRAY_N64:
            memset(&(body->slots.n64[elems]), 0, (ssize - elems) * sizeof(MVMnum64));
            break;
        case MVM_ARRAY_N32:
            memset(&(body->slots.n32[elems]), 0, (ssize - elems) * sizeof(MVMnum32));
            break;
        case MVM_ARRAY_U64:
            memset(&(body->slots.u64[elems]), 0, (ssize - elems) * sizeof(MVMuint64));
            break;
        case MVM_ARRAY_U32:
            memset(&(body->slots.u32[elems]), 0, (ssize - elems) * sizeof(MVMuint32));
            break;
        case MVM_ARRAY_U16:
            memset(&(body->slots.u16[elems]), 0, (ssize - elems) * sizeof(MVMuint16));
            break;
        case MVM_ARRAY_U8:
            memset(&(body->slots.u8[elems]), 0, (ssize - elems) * sizeof(MVMuint8));
            break;
        default:
            MVM_exception_throw_adhoc(tc, "MVMArray: Unhandled slot type");
    }
    return elems;
}

static void set_size_internal(MVMThreadContext *tc, MVMArrayBody *body, MVMuint64 n, MVMArrayREPRData *repr_data) {
    MVMuint64   elems = body->elems;
    MVMuint64   start = body->start;
    MVMuint64   ssize = body->ssize;
    void       *slots = body->slots.any;

    if (n == elems)
        return;

    if (start > 0 && n + start > ssize) {
        /* if there aren't enough slots at the end, shift off empty slots
         * from the beginning first */
        if (elems > 0)
            memmove(slots,
                (char *)slots + start * repr_data->elem_size,
                elems * repr_data->elem_size);
        body->start = 0;
        /* fill out any unused slots with NULL pointers or zero values */
        zero_slots(tc, body, elems, start+elems, repr_data->slot_type);
        elems = ssize; /* we'll use this as a point to clear from later */
    }
    else if (n < elems) {
        /* we're downsizing; clear off extra slots */
        zero_slots(tc, body, n+start, start+elems, repr_data->slot_type);
    }

    if (n <= ssize) {
        /* we already have n slots available, we can just return */
        body->elems = n;
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
        ssize = (n + 0x1000) & ~0xfffUL;
    }
    {
        /* Our budget is 2^(
         *     <number of bits in an array index>
         *     - <number of bits to address individual bytes in an array element>
         * ) */
        size_t const elem_addr_size = repr_data->elem_size == 8 ? 4 :
                                      repr_data->elem_size == 4 ? 3 :
                                      repr_data->elem_size == 2 ? 2 :
                                                                  1;
        if (ssize > (1ULL << (CHAR_BIT * sizeof(size_t) - elem_addr_size)))
            MVM_exception_throw_adhoc(tc,
                "Unable to allocate an array of %"PRIu64" elements",
                ssize);
    }

    /* now allocate the new slot buffer */
    slots = (slots)
            ? MVM_realloc(slots, ssize * repr_data->elem_size)
            : MVM_malloc(ssize * repr_data->elem_size);

    /* fill out any unused slots with NULL pointers or zero values */
    body->slots.any = slots;
    zero_slots(tc, body, elems, ssize, repr_data->slot_type);

    body->ssize = ssize;
    /* set elems last so no thread tries to access slots before they are available */
    body->elems = n;
}

void MVM_VMArray_bind_pos(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMint64 index, MVMRegister value, MVMuint16 kind) {
    MVMArrayREPRData *repr_data = (MVMArrayREPRData *)st->REPR_data;
    MVMArrayBody     *body      = (MVMArrayBody *)data;
    MVMuint64        real_index;

    /* Handle negative indexes and resizing if needed. */
    enter_single_user(tc, body);
    if (index < 0) {
        index += body->elems;
        if (index < 0)
            MVM_exception_throw_adhoc(tc, "MVMArray: Index out of bounds");
    }
    else if ((MVMuint64)index >= body->elems)
        set_size_internal(tc, body, (MVMuint64)index + 1, repr_data);

    real_index = (MVMuint64)index;

    /* Go by type. */
    switch (repr_data->slot_type) {
        case MVM_ARRAY_OBJ:
            if (kind != MVM_reg_obj)
                MVM_exception_throw_adhoc(tc, "MVMArray: bindpos expected object register");
            MVM_ASSIGN_REF(tc, &(root->header), body->slots.o[body->start + real_index], value.o);
            break;
        case MVM_ARRAY_STR:
            if (kind != MVM_reg_str)
                MVM_exception_throw_adhoc(tc, "MVMArray: bindpos expected string register");
            MVM_ASSIGN_REF(tc, &(root->header), body->slots.s[body->start + real_index], value.s);
            break;
        case MVM_ARRAY_I64:
            if (kind != MVM_reg_int64)
                MVM_exception_throw_adhoc(tc, "MVMArray: bindpos I64 expected int register");
            body->slots.i64[body->start + real_index] = value.i64;
            break;
        case MVM_ARRAY_I32:
            if (kind != MVM_reg_int64)
                MVM_exception_throw_adhoc(tc, "MVMArray: bindpos I32 expected int register");
            body->slots.i32[body->start + real_index] = (MVMint32)value.i64;
            break;
        case MVM_ARRAY_I16:
            if (kind != MVM_reg_int64)
                MVM_exception_throw_adhoc(tc, "MVMArray: bindpos I16 expected int register");
            body->slots.i16[body->start + real_index] = (MVMint16)value.i64;
            break;
        case MVM_ARRAY_I8:
            if (kind != MVM_reg_int64)
                MVM_exception_throw_adhoc(tc, "MVMArray: bindpos I8 expected int register");
            body->slots.i8[body->start + real_index] = (MVMint8)value.i64;
            break;
        case MVM_ARRAY_N64:
            if (kind != MVM_reg_num64)
                MVM_exception_throw_adhoc(tc, "MVMArray: bindpos expected num register");
            body->slots.n64[body->start + real_index] = value.n64;
            break;
        case MVM_ARRAY_N32:
            if (kind != MVM_reg_num64)
                MVM_exception_throw_adhoc(tc, "MVMArray: bindpos expected num register");
            body->slots.n32[body->start + real_index] = (MVMnum32)value.n64;
            break;
        case MVM_ARRAY_U64:
            if (kind != MVM_reg_uint64 && kind != MVM_reg_int64)
                MVM_exception_throw_adhoc(tc, "MVMArray: bindpos U64 expected int register");
            body->slots.u64[body->start + real_index] = value.i64;
            break;
        case MVM_ARRAY_U32:
            if (kind != MVM_reg_uint64 && kind != MVM_reg_int64)
                MVM_exception_throw_adhoc(tc, "MVMArray: bindpos U32 expected int register");
            body->slots.u32[body->start + real_index] = (MVMuint32)value.i64;
            break;
        case MVM_ARRAY_U16:
            if (kind != MVM_reg_uint64 && kind != MVM_reg_int64)
                MVM_exception_throw_adhoc(tc, "MVMArray: bindpos U16 expected int register");
            body->slots.u16[body->start + real_index] = (MVMuint16)value.i64;
            break;
        case MVM_ARRAY_U8:
            if (kind != MVM_reg_uint64 && kind != MVM_reg_int64)
                MVM_exception_throw_adhoc(tc, "MVMArray: bindpos U8 expected int register");
            body->slots.u8[body->start + real_index] = (MVMuint8)value.i64;
            break;
        default:
            MVM_exception_throw_adhoc(tc, "MVMArray: Unhandled slot type");
    }
    exit_single_user(tc, body);
}

static MVMuint64 elems(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data) {
    MVMArrayBody *body = (MVMArrayBody *)data;
    return body->elems;
}

static void set_elems(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMuint64 count) {
    MVMArrayREPRData *repr_data = (MVMArrayREPRData *)st->REPR_data;
    MVMArrayBody     *body      = (MVMArrayBody *)data;
    enter_single_user(tc, body);
    set_size_internal(tc, body, count, repr_data);
    exit_single_user(tc, body);
}

void MVM_VMArray_push(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMRegister value, MVMuint16 kind) {
    MVMArrayBody     *body      = (MVMArrayBody *)data;
    MVMArrayREPRData *repr_data = (MVMArrayREPRData *)st->REPR_data;
    enter_single_user(tc, body);
    set_size_internal(tc, body, body->elems + 1, repr_data);
    switch (repr_data->slot_type) {
        case MVM_ARRAY_OBJ:
            if (kind != MVM_reg_obj)
                MVM_exception_throw_adhoc(tc, "MVMArray: push expected object register");
            MVM_ASSIGN_REF(tc, &(root->header), body->slots.o[body->start + body->elems - 1], value.o);
            break;
        case MVM_ARRAY_STR:
            if (kind != MVM_reg_str)
                MVM_exception_throw_adhoc(tc, "MVMArray: push expected string register");
            MVM_ASSIGN_REF(tc, &(root->header), body->slots.s[body->start + body->elems - 1], value.s);
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
        case MVM_ARRAY_U64:
            if (kind != MVM_reg_int64)
                MVM_exception_throw_adhoc(tc, "MVMArray: push expected int register");
            body->slots.u64[body->start + body->elems - 1] = (MVMuint64)value.i64;
            break;
        case MVM_ARRAY_U32:
            if (kind != MVM_reg_int64)
                MVM_exception_throw_adhoc(tc, "MVMArray: push expected int register");
            body->slots.u32[body->start + body->elems - 1] = (MVMuint32)value.i64;
            break;
        case MVM_ARRAY_U16:
            if (kind != MVM_reg_int64)
                MVM_exception_throw_adhoc(tc, "MVMArray: push expected int register");
            body->slots.u16[body->start + body->elems - 1] = (MVMuint16)value.i64;
            break;
        case MVM_ARRAY_U8:
            if (kind != MVM_reg_int64)
                MVM_exception_throw_adhoc(tc, "MVMArray: push expected int register");
            body->slots.u8[body->start + body->elems - 1] = (MVMuint8)value.i64;
            break;
        default:
            MVM_exception_throw_adhoc(tc, "MVMArray: Unhandled slot type");
    }
    exit_single_user(tc, body);
}

static void pop(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMRegister *value, MVMuint16 kind) {
    MVMArrayREPRData *repr_data = (MVMArrayREPRData *)st->REPR_data;
    MVMArrayBody     *body      = (MVMArrayBody *)data;
    const MVMuint64 slot        = body->start + body->elems - 1;

    if (body->elems < 1)
        MVM_exception_throw_adhoc(tc,
            "MVMArray: Can't pop from an empty array");

    enter_single_user(tc, body);
    body->elems--;
    switch (repr_data->slot_type) {
        case MVM_ARRAY_OBJ:
            if (kind != MVM_reg_obj)
                MVM_exception_throw_adhoc(tc, "MVMArray: pop expected object register");
            value->o = body->slots.o[slot];
            break;
        case MVM_ARRAY_STR:
            if (kind != MVM_reg_str)
                MVM_exception_throw_adhoc(tc, "MVMArray: pop expected string register");
            value->s = body->slots.s[slot];
            break;
        case MVM_ARRAY_I64:
            if (kind != MVM_reg_int64)
                MVM_exception_throw_adhoc(tc, "MVMArray: pop expected int register");
            value->i64 = (MVMint64)body->slots.i64[slot];
            break;
        case MVM_ARRAY_I32:
            if (kind != MVM_reg_int64)
                MVM_exception_throw_adhoc(tc, "MVMArray: pop expected int register");
            value->i64 = (MVMint64)body->slots.i32[slot];
            break;
        case MVM_ARRAY_I16:
            if (kind != MVM_reg_int64)
                MVM_exception_throw_adhoc(tc, "MVMArray: pop expected int register");
            value->i64 = (MVMint64)body->slots.i16[slot];
            break;
        case MVM_ARRAY_I8:
            if (kind != MVM_reg_int64)
                MVM_exception_throw_adhoc(tc, "MVMArray: pop expected int register");
            value->i64 = (MVMint64)body->slots.i8[slot];
            break;
        case MVM_ARRAY_N64:
            if (kind != MVM_reg_num64)
                MVM_exception_throw_adhoc(tc, "MVMArray: pop expected num register");
            value->n64 = (MVMnum64)body->slots.n64[slot];
            break;
        case MVM_ARRAY_N32:
            if (kind != MVM_reg_num64)
                MVM_exception_throw_adhoc(tc, "MVMArray: pop expected num register");
            value->n64 = (MVMnum64)body->slots.n32[slot];
            break;
        case MVM_ARRAY_U64:
            if (kind != MVM_reg_int64)
                MVM_exception_throw_adhoc(tc, "MVMArray: pop expected int register");
            value->i64 = (MVMint64)body->slots.u64[slot];
            break;
        case MVM_ARRAY_U32:
            if (kind != MVM_reg_int64)
                MVM_exception_throw_adhoc(tc, "MVMArray: pop expected int register");
            value->i64 = (MVMint64)body->slots.u32[slot];
            break;
        case MVM_ARRAY_U16:
            if (kind != MVM_reg_int64)
                MVM_exception_throw_adhoc(tc, "MVMArray: pop expected int register");
            value->i64 = (MVMint64)body->slots.u16[slot];
            break;
        case MVM_ARRAY_U8:
            if (kind != MVM_reg_int64)
                MVM_exception_throw_adhoc(tc, "MVMArray: pop expected int register");
            value->i64 = (MVMint64)body->slots.u8[slot];
            break;
        default:
            MVM_exception_throw_adhoc(tc, "MVMArray: Unhandled slot type");
    }
    zero_slots(tc, body, slot, slot + 1, repr_data->slot_type);
    exit_single_user(tc, body);
}

static void unshift(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMRegister value, MVMuint16 kind) {
    MVMArrayREPRData *repr_data = (MVMArrayREPRData *)st->REPR_data;
    MVMArrayBody     *body      = (MVMArrayBody *)data;

    /* If we don't have room at the beginning of the slots, make some
     * room for unshifting. We make room for a minimum of 8 elements, but
     * for cases where we're just continuously unshifting factor in the
     * body size too - however also apply an upper limit on that as in the
     * push-based growth. */
    enter_single_user(tc, body);
    if (body->start < 1) {
        MVMuint64 elems = body->elems;
        MVMuint64 n = MVM_MIN(MVM_MAX(elems, 8), 8192);

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
            MVM_ASSIGN_REF(tc, &(root->header), body->slots.o[body->start], value.o);
            break;
        case MVM_ARRAY_STR:
            if (kind != MVM_reg_str)
                MVM_exception_throw_adhoc(tc, "MVMArray: unshift expected string register");
            MVM_ASSIGN_REF(tc, &(root->header), body->slots.s[body->start], value.s);
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
        case MVM_ARRAY_U64:
            if (kind != MVM_reg_int64)
                MVM_exception_throw_adhoc(tc, "MVMArray: unshift expected int register");
            body->slots.u64[body->start] = (MVMuint64)value.i64;
            break;
        case MVM_ARRAY_U32:
            if (kind != MVM_reg_int64)
                MVM_exception_throw_adhoc(tc, "MVMArray: unshift expected int register");
            body->slots.u32[body->start] = (MVMuint32)value.i64;
            break;
        case MVM_ARRAY_U16:
            if (kind != MVM_reg_int64)
                MVM_exception_throw_adhoc(tc, "MVMArray: unshift expected int register");
            body->slots.u16[body->start] = (MVMuint16)value.i64;
            break;
        case MVM_ARRAY_U8:
            if (kind != MVM_reg_int64)
                MVM_exception_throw_adhoc(tc, "MVMArray: unshift expected int register");
            body->slots.u8[body->start] = (MVMuint8)value.i64;
            break;
        default:
            MVM_exception_throw_adhoc(tc, "MVMArray: Unhandled slot type");
    }
    body->elems++;
    exit_single_user(tc, body);
}

static void shift(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMRegister *value, MVMuint16 kind) {
    MVMArrayBody     *body      = (MVMArrayBody *)data;
    MVMArrayREPRData *repr_data = (MVMArrayREPRData *)st->REPR_data;

    if (body->elems < 1)
        MVM_exception_throw_adhoc(tc,
            "MVMArray: Can't shift from an empty array");

    enter_single_user(tc, body);
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
        case MVM_ARRAY_U64:
            if (kind != MVM_reg_int64)
                MVM_exception_throw_adhoc(tc, "MVMArray: shift expected int register");
            value->i64 = (MVMint64)body->slots.u64[body->start];
            break;
        case MVM_ARRAY_U32:
            if (kind != MVM_reg_int64)
                MVM_exception_throw_adhoc(tc, "MVMArray: shift expected int register");
            value->i64 = (MVMint64)body->slots.u32[body->start];
            break;
        case MVM_ARRAY_U16:
            if (kind != MVM_reg_int64)
                MVM_exception_throw_adhoc(tc, "MVMArray: shift expected int register");
            value->i64 = (MVMint64)body->slots.u16[body->start];
            break;
        case MVM_ARRAY_U8:
            if (kind != MVM_reg_int64)
                MVM_exception_throw_adhoc(tc, "MVMArray: shift expected int register");
            value->i64 = (MVMint64)body->slots.u8[body->start];
            break;
        default:
            MVM_exception_throw_adhoc(tc, "MVMArray: Unhandled slot type");
    }
    body->start++;
    body->elems--;
    exit_single_user(tc, body);
}

static MVMuint16 slot_type_to_kind(MVMuint8 slot_type) {
    switch (slot_type) {
        case MVM_ARRAY_OBJ:
            return MVM_reg_obj;
            break;
        case MVM_ARRAY_STR:
            return MVM_reg_str;
            break;
        case MVM_ARRAY_I64:
        case MVM_ARRAY_I32:
        case MVM_ARRAY_I16:
        case MVM_ARRAY_I8:
            return MVM_reg_int64;
            break;
        case MVM_ARRAY_N64:
        case MVM_ARRAY_N32:
            return MVM_reg_num64;
            break;
        case MVM_ARRAY_U64:
        case MVM_ARRAY_U32:
        case MVM_ARRAY_U16:
        case MVM_ARRAY_U8:
            return MVM_reg_uint64;
            break;
        default:
            abort(); /* never reached, silence compiler warnings */
    }
}

static void copy_elements(MVMThreadContext *tc, MVMObject *src, MVMObject *dest, MVMint64 s_offset, MVMint64 d_offset, MVMint64 elems) {
    MVMArrayBody     *s_body      = (MVMArrayBody *)OBJECT_BODY(src);
    MVMArrayBody     *d_body      = (MVMArrayBody *)OBJECT_BODY(dest);
    MVMArrayREPRData *s_repr_data = REPR(src)->ID == MVM_REPR_ID_VMArray
                                    ? (MVMArrayREPRData *)STABLE(src)->REPR_data  : NULL;
    MVMArrayREPRData *d_repr_data = (MVMArrayREPRData *)STABLE(dest)->REPR_data;

    if (elems > 0) {
        MVMint64  i;
        MVMuint8 d_needs_barrier = dest->header.flags2 & MVM_CF_SECOND_GEN;
        if (s_repr_data
                && s_repr_data->slot_type == d_repr_data->slot_type
                && s_repr_data->elem_size == d_repr_data->elem_size
                && (d_repr_data->slot_type != MVM_ARRAY_OBJ || !d_needs_barrier)
                && d_repr_data->slot_type  != MVM_ARRAY_STR) {
            /* Optimized for copying from a VMArray with same slot type */
            MVMint64 s_start = s_body->start;
            MVMint64 d_start = d_body->start;
            memcpy( d_body->slots.u8 + (d_start + d_offset) * d_repr_data->elem_size,
                    s_body->slots.u8  + (s_start + s_offset) * s_repr_data->elem_size,
                    d_repr_data->elem_size * elems
            );
        }
        else {
            MVMuint16 target_kind = slot_type_to_kind(d_repr_data->slot_type);
            MVMuint16 source_kind = slot_type_to_kind(s_repr_data->slot_type);
            for (i = 0; i < elems; i++) {
                MVMRegister to_copy;
                REPR(src)->pos_funcs.at_pos(tc, STABLE(src), src, s_body, s_offset + i, &to_copy, source_kind);
                /* actually should coerce between source_kind and target_kind here */
                MVM_VMArray_bind_pos(tc, STABLE(dest), dest, d_body, d_offset + i, to_copy, target_kind);
            }
        }
    }
}

static void aslice(MVMThreadContext *tc, MVMSTable *st, MVMObject *src, void *data, MVMObject *dest, MVMint64 start, MVMint64 end) {
    MVMArrayBody     *s_body      = (MVMArrayBody *)data;
    MVMArrayBody     *d_body      = (MVMArrayBody *)OBJECT_BODY(dest);
    MVMArrayREPRData *d_repr_data = STABLE(dest)->REPR_data;

    MVMint64 total_elems = REPR(src)->elems(tc, st, src, s_body);
    MVMint64 elems;

    start = start < 0 ? total_elems + start : start;
    end   = end   < 0 ? total_elems + end   : end;
    if ( end < start || start < 0 || end < 0 || total_elems <= start || total_elems <= end ) {
        MVM_exception_throw_adhoc(tc, "MVMArray: Slice index out of bounds");
    }

    elems = end - start + 1;
    set_size_internal(tc, d_body, elems, d_repr_data);
    copy_elements(tc, src, dest, start, 0, elems);
}

static void write_buf(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, char *from, MVMint64 offset, MVMuint64 count) {
    MVMArrayREPRData *repr_data = (MVMArrayREPRData *)st->REPR_data;
    MVMArrayBody     *body      = (MVMArrayBody *)data;
    MVMuint64 start = body->start;
    MVMuint64 elems = body->elems;

    /* Throw on invalid slot type */
    if (repr_data->slot_type < MVM_ARRAY_I64) {
        MVM_exception_throw_adhoc(tc, "MVMArray: write_buf requires an integer type");
    }
    /* Throw on negative offset. */
    if (offset < 0) {
        MVM_exception_throw_adhoc(tc, "MVMArray: Index out of bounds");
    }

    /* resize the array if necessary*/
    size_t elem_size = repr_data->elem_size;
    /* No need to account for start, set_size_internal will take care of that */
    if (elems * elem_size < offset * elem_size + count) {
        set_size_internal(tc, body, offset + count, repr_data);
        start = body->start;
    }

    memcpy(body->slots.u8 + (start + offset) * elem_size, from, count);
}

static MVMint64 read_buf(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMint64 offset, MVMuint64 count) {
    MVMArrayREPRData *repr_data = (MVMArrayREPRData *)st->REPR_data;
    MVMArrayBody     *body      = (MVMArrayBody *)data;
    MVMint64 start = body->start;
    MVMint64 result = 0;
    size_t elem_size = repr_data->elem_size;

    /* Throw on invalid slot type */
    if (repr_data->slot_type < MVM_ARRAY_I64) {
        MVM_exception_throw_adhoc(tc, "MVMArray: read_buf requires an integer type");
    }

    if (offset < 0 || (start + body->elems) * elem_size < offset * elem_size + count) {
        MVM_exception_throw_adhoc(tc, "MVMArray: read_buf out of bounds offset %"PRIi64" start %"PRIi64" elems %"PRIu64" count %"PRIu64, offset, start, body->elems, count);
    }

    memcpy(((char*)&result)
#if MVM_BIGENDIAN
	+ (8 - count)
#endif
	, body->slots.u8 + (start + offset) * elem_size, count);
    return result;
}

/* This whole splice optimization can be optimized for the case we have two
 * MVMArray representation objects. */
static void asplice(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMObject *from, MVMint64 offset, MVMuint64 count) {
    MVMArrayREPRData *repr_data   = (MVMArrayREPRData *)st->REPR_data;
    MVMArrayBody     *body        = (MVMArrayBody *)data;

    MVMuint64 elems0 = body->elems;
    MVMuint64 elems1 = REPR(from)->elems(tc, STABLE(from), from, OBJECT_BODY(from));
    MVMint64 start;
    MVMint64 tail;

    /* start from end? */
    if (offset < 0) {
        offset += elems0;

        if (offset < 0)
            MVM_exception_throw_adhoc(tc,
                "MVMArray: Illegal splice offset");
    }

    enter_single_user(tc, body);

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
        if (n <= -(MVMint64)elems0) {
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
    if (count == 0 && elems1 == 0) {
        exit_single_user(tc, body);
        return;
    }

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
    exit_single_user(tc, body);


    /* now copy C<from>'s elements into SELF */
    copy_elements(tc, from, root, 0, offset, elems1);
}

static void at_pos_multidim(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMint64 num_indices, MVMint64 *indices, MVMRegister *result, MVMuint16 kind) {
    if (num_indices != 1)
        MVM_exception_throw_adhoc(tc, "A dynamic array can only be indexed with a single dimension");
    MVM_VMArray_at_pos(tc, st, root, data, indices[0], result, kind);
}

static void bind_pos_multidim(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMint64 num_indices, MVMint64 *indices, MVMRegister value, MVMuint16 kind) {
    if (num_indices != 1)
        MVM_exception_throw_adhoc(tc, "A dynamic array can only be indexed with a single dimension");
    MVM_VMArray_bind_pos(tc, st, root, data, indices[0], value, kind);
}

static void dimensions(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMint64 *num_dimensions, MVMint64 **dimensions) {
    MVMArrayBody *body = (MVMArrayBody *)data;
    *num_dimensions = 1;
    *dimensions = (MVMint64 *) &(body->elems);
}

static void set_dimensions(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMint64 num_dimensions, MVMint64 *dimensions) {
    if (num_dimensions != 1)
        MVM_exception_throw_adhoc(tc, "A dynamic array can only have a single dimension");
    set_elems(tc, st, root, data, dimensions[0]);
}

static MVMStorageSpec get_elem_storage_spec(MVMThreadContext *tc, MVMSTable *st) {
    MVMArrayREPRData *repr_data = (MVMArrayREPRData *)st->REPR_data;
    MVMStorageSpec spec;

    /* initialise storage spec to default values */
    spec.bits            = 0;
    spec.align           = 0;
    spec.is_unsigned     = 0;

    switch (repr_data->slot_type) {
        case MVM_ARRAY_STR:
            spec.inlineable      = MVM_STORAGE_SPEC_INLINED;
            spec.boxed_primitive = MVM_STORAGE_SPEC_BP_STR;
            spec.can_box         = MVM_STORAGE_SPEC_CAN_BOX_STR;
            break;
        case MVM_ARRAY_I64:
        case MVM_ARRAY_I32:
        case MVM_ARRAY_I16:
        case MVM_ARRAY_I8:
            spec.inlineable      = MVM_STORAGE_SPEC_INLINED;
            spec.boxed_primitive = MVM_STORAGE_SPEC_BP_INT;
            spec.can_box         = MVM_STORAGE_SPEC_CAN_BOX_INT;
            break;
        case MVM_ARRAY_N64:
        case MVM_ARRAY_N32:
            spec.inlineable      = MVM_STORAGE_SPEC_INLINED;
            spec.boxed_primitive = MVM_STORAGE_SPEC_BP_NUM;
            spec.can_box         = MVM_STORAGE_SPEC_CAN_BOX_NUM;
            break;
        case MVM_ARRAY_U64:
        case MVM_ARRAY_U32:
        case MVM_ARRAY_U16:
        case MVM_ARRAY_U8:
            spec.inlineable      = MVM_STORAGE_SPEC_INLINED;
            spec.boxed_primitive = MVM_STORAGE_SPEC_BP_UINT64;
            spec.can_box         = MVM_STORAGE_SPEC_CAN_BOX_INT;
            spec.is_unsigned     = 1;
            break;
        default:
            spec.inlineable      = MVM_STORAGE_SPEC_REFERENCE;
            spec.boxed_primitive = MVM_STORAGE_SPEC_BP_NONE;
            spec.can_box         = 0;
            break;
    }
    return spec;
}

static AO_t * pos_as_atomic(MVMThreadContext *tc, MVMSTable *st, MVMObject *root,
                            void *data, MVMint64 index) {
    MVMArrayREPRData *repr_data = (MVMArrayREPRData *)st->REPR_data;
    MVMArrayBody     *body      = (MVMArrayBody *)data;

    /* Handle negative indexes and require in bounds. */
    if (index < 0)
        index += body->elems;
    if (index < 0 || (MVMuint64)index >= body->elems)
        MVM_exception_throw_adhoc(tc, "Index out of bounds in atomic operation on array");

    if (sizeof(AO_t) == 8 && (repr_data->slot_type == MVM_ARRAY_I64 ||
            repr_data->slot_type == MVM_ARRAY_U64))
        return (AO_t *)&(body->slots.i64[body->start + index]);
    if (sizeof(AO_t) == 4 && (repr_data->slot_type == MVM_ARRAY_I32 ||
            repr_data->slot_type == MVM_ARRAY_U32))
        return (AO_t *)&(body->slots.i32[body->start + index]);
    MVM_exception_throw_adhoc(tc,
        "Can only do integer atomic operation on native integer array element of atomic size");
}

static AO_t * pos_as_atomic_multidim(MVMThreadContext *tc, MVMSTable *st, MVMObject *root,
                                     void *data, MVMint64 num_indices, MVMint64 *indices) {
    if (num_indices != 1)
        MVM_exception_throw_adhoc(tc,
            "A dynamic array can only be indexed with a single dimension");
    return pos_as_atomic(tc, st, root, data, indices[0]);
}

/* Compose the representation. */
static void spec_to_repr_data(MVMThreadContext *tc, MVMArrayREPRData *repr_data, const MVMStorageSpec *spec) {
    switch (spec->boxed_primitive) {
        case MVM_STORAGE_SPEC_BP_UINT64:
        case MVM_STORAGE_SPEC_BP_INT:
            if (spec->is_unsigned) {
                switch (spec->bits) {
                    case 64:
                        repr_data->slot_type = MVM_ARRAY_U64;
                        repr_data->elem_size = sizeof(MVMuint64);
                        break;
                    case 32:
                        repr_data->slot_type = MVM_ARRAY_U32;
                        repr_data->elem_size = sizeof(MVMuint32);
                        break;
                    case 16:
                        repr_data->slot_type = MVM_ARRAY_U16;
                        repr_data->elem_size = sizeof(MVMuint16);
                        break;
                    case 8:
                        repr_data->slot_type = MVM_ARRAY_U8;
                        repr_data->elem_size = sizeof(MVMuint8);
                        break;
                    case 4:
                        repr_data->slot_type = MVM_ARRAY_U4;
                        repr_data->elem_size = 0;
                        break;
                    case 2:
                        repr_data->slot_type = MVM_ARRAY_U2;
                        repr_data->elem_size = 0;
                        break;
                    case 1:
                        repr_data->slot_type = MVM_ARRAY_U1;
                        repr_data->elem_size = 0;
                        break;
                    default:
                        MVM_exception_throw_adhoc(tc,
                            "MVMArray: Unsupported uint size");
                }
            }
            else {
                switch (spec->bits) {
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
                    case 4:
                        repr_data->slot_type = MVM_ARRAY_I4;
                        repr_data->elem_size = 0;
                        break;
                    case 2:
                        repr_data->slot_type = MVM_ARRAY_I2;
                        repr_data->elem_size = 0;
                        break;
                    case 1:
                        repr_data->slot_type = MVM_ARRAY_I1;
                        repr_data->elem_size = 0;
                        break;
                    default:
                        MVM_exception_throw_adhoc(tc,
                            "MVMArray: Unsupported int size");
                }
            }
            break;
        case MVM_STORAGE_SPEC_BP_NUM:
            switch (spec->bits) {
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
static void compose(MVMThreadContext *tc, MVMSTable *st, MVMObject *info_hash) {
    MVMStringConsts         str_consts = tc->instance->str_consts;
    MVMArrayREPRData * const repr_data = (MVMArrayREPRData *)st->REPR_data;

    MVMObject *info = MVM_repr_at_key_o(tc, info_hash, str_consts.array);
    if (!MVM_is_null(tc, info)) {
        MVMObject *type = MVM_repr_at_key_o(tc, info, str_consts.type);
        if (!MVM_is_null(tc, type)) {
            const MVMStorageSpec *spec = REPR(type)->get_storage_spec(tc, STABLE(type));
            MVM_ASSIGN_REF(tc, &(st->header), repr_data->elem_type, type);
            spec_to_repr_data(tc, repr_data, spec);
        }
    }
}

/* Set the size of the STable. */
static void deserialize_stable_size(MVMThreadContext *tc, MVMSTable *st, MVMSerializationReader *reader) {
    st->size = sizeof(MVMArray);
}

/* Serializes the REPR data. */
static void serialize_repr_data(MVMThreadContext *tc, MVMSTable *st, MVMSerializationWriter *writer) {
    MVMArrayREPRData *repr_data = (MVMArrayREPRData *)st->REPR_data;
    MVM_serialization_write_ref(tc, writer, repr_data->elem_type);
}

/* Deserializes representation data. */
static void deserialize_repr_data(MVMThreadContext *tc, MVMSTable *st, MVMSerializationReader *reader) {
    MVMArrayREPRData *repr_data = (MVMArrayREPRData *)MVM_malloc(sizeof(MVMArrayREPRData));

    MVMObject *type = MVM_serialization_read_ref(tc, reader);
    MVM_ASSIGN_REF(tc, &(st->header), repr_data->elem_type, type);
    repr_data->slot_type = MVM_ARRAY_OBJ;
    repr_data->elem_size = sizeof(MVMObject *);
    st->REPR_data = repr_data;

    if (type) {
        const MVMStorageSpec *spec;
        MVM_serialization_force_stable(tc, reader, STABLE(type));
        spec = REPR(type)->get_storage_spec(tc, STABLE(type));
        spec_to_repr_data(tc, repr_data, spec);
    }
}

static void deserialize(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMSerializationReader *reader) {
    MVMArrayREPRData *repr_data = (MVMArrayREPRData *) st->REPR_data;
    MVMArrayBody     *body      = (MVMArrayBody *)data;
    MVMuint64 i;

    body->elems = MVM_serialization_read_int(tc, reader);
    body->ssize = body->elems;
    if (body->ssize)
        body->slots.any = MVM_malloc(body->ssize * repr_data->elem_size);

    switch (repr_data->slot_type) {
        case MVM_ARRAY_OBJ:
            for (i = 0; i < body->elems; i++)
                MVM_ASSIGN_REF(tc, &(root->header), body->slots.o[i], MVM_serialization_read_ref(tc, reader));
            break;
        case MVM_ARRAY_STR:
            for (i = 0; i < body->elems; i++)
                MVM_ASSIGN_REF(tc, &(root->header), body->slots.s[i], MVM_serialization_read_str(tc, reader));
            break;
        case MVM_ARRAY_I64:
            for (i = 0; i < body->elems; i++)
                body->slots.i64[i] = MVM_serialization_read_int(tc, reader);
            break;
        case MVM_ARRAY_I32:
            for (i = 0; i < body->elems; i++)
                body->slots.i32[i] = (MVMint32)MVM_serialization_read_int(tc, reader);
            break;
        case MVM_ARRAY_I16:
            for (i = 0; i < body->elems; i++)
                body->slots.i16[i] = (MVMint16)MVM_serialization_read_int(tc, reader);
            break;
        case MVM_ARRAY_I8:
            for (i = 0; i < body->elems; i++)
                body->slots.i8[i] = (MVMint8)MVM_serialization_read_int(tc, reader);
            break;
        case MVM_ARRAY_U64:
            for (i = 0; i < body->elems; i++)
                body->slots.i64[i] = MVM_serialization_read_int(tc, reader);
            break;
        case MVM_ARRAY_U32:
            for (i = 0; i < body->elems; i++)
                body->slots.i32[i] = (MVMuint32)MVM_serialization_read_int(tc, reader);
            break;
        case MVM_ARRAY_U16:
            for (i = 0; i < body->elems; i++)
                body->slots.i16[i] = (MVMuint16)MVM_serialization_read_int(tc, reader);
            break;
        case MVM_ARRAY_U8:
            for (i = 0; i < body->elems; i++)
                body->slots.i8[i] = (MVMuint8)MVM_serialization_read_int(tc, reader);
            break;
        case MVM_ARRAY_N64:
            for (i = 0; i < body->elems; i++)
                body->slots.n64[i] = MVM_serialization_read_num(tc, reader);
            break;
        case MVM_ARRAY_N32:
            for (i = 0; i < body->elems; i++)
                body->slots.n32[i] = (MVMnum32)MVM_serialization_read_num(tc, reader);
            break;
        default:
            MVM_exception_throw_adhoc(tc, "MVMArray: Unhandled slot type");
    }
}

static void serialize(MVMThreadContext *tc, MVMSTable *st, void *data, MVMSerializationWriter *writer) {
    MVMArrayREPRData *repr_data = (MVMArrayREPRData *) st->REPR_data;
    MVMArrayBody     *body      = (MVMArrayBody *)data;
    MVMuint64 i;

    MVM_serialization_write_int(tc, writer, body->elems);
    for (i = 0; i < body->elems; i++) {
        switch (repr_data->slot_type) {
            case MVM_ARRAY_OBJ:
                MVM_serialization_write_ref(tc, writer, body->slots.o[body->start + i]);
                break;
            case MVM_ARRAY_STR:
                MVM_serialization_write_str(tc, writer, body->slots.s[body->start + i]);
                break;
            case MVM_ARRAY_I64:
                MVM_serialization_write_int(tc, writer, (MVMint64)body->slots.i64[body->start + i]);
                break;
            case MVM_ARRAY_I32:
                MVM_serialization_write_int(tc, writer, (MVMint64)body->slots.i32[body->start + i]);
                break;
            case MVM_ARRAY_I16:
                MVM_serialization_write_int(tc, writer, (MVMint64)body->slots.i16[body->start + i]);
                break;
            case MVM_ARRAY_I8:
                MVM_serialization_write_int(tc, writer, (MVMint64)body->slots.i8[body->start + i]);
                break;
            case MVM_ARRAY_U64:
                MVM_serialization_write_int(tc, writer, (MVMint64)body->slots.u64[body->start + i]);
                break;
            case MVM_ARRAY_U32:
                MVM_serialization_write_int(tc, writer, (MVMint64)body->slots.u32[body->start + i]);
                break;
            case MVM_ARRAY_U16:
                MVM_serialization_write_int(tc, writer, (MVMint64)body->slots.u16[body->start + i]);
                break;
            case MVM_ARRAY_U8:
                MVM_serialization_write_int(tc, writer, (MVMint64)body->slots.u8[body->start + i]);
                break;
            case MVM_ARRAY_N64:
                MVM_serialization_write_num(tc, writer, (MVMnum64)body->slots.n64[body->start + i]);
                break;
            case MVM_ARRAY_N32:
                MVM_serialization_write_num(tc, writer, (MVMnum64)body->slots.n32[body->start + i]);
                break;
            default:
                MVM_exception_throw_adhoc(tc, "MVMArray: Unhandled slot type");
        }
    }
}

/* Bytecode specialization for this REPR. */
static void spesh(MVMThreadContext *tc, MVMSTable *st, MVMSpeshGraph *g, MVMSpeshBB *bb, MVMSpeshIns *ins) {
    switch (ins->info->opcode) {
    case MVM_OP_create: {
        if (!(st->mode_flags & MVM_FINALIZE_TYPE)) {
            MVMSpeshOperand target   = ins->operands[0];
            MVMSpeshOperand type     = ins->operands[1];
            MVMSpeshFacts *tgt_facts = MVM_spesh_get_facts(tc, g, target);

            ins->info                = MVM_op_get_op(MVM_OP_sp_fastcreate);
            ins->operands            = MVM_spesh_alloc(tc, g, 3 * sizeof(MVMSpeshOperand));
            ins->operands[0]         = target;
            ins->operands[1].lit_i16 = sizeof(MVMArray);
            ins->operands[2].lit_i16 = MVM_spesh_add_spesh_slot(tc, g, (MVMCollectable *)st);
            MVM_spesh_usages_delete_by_reg(tc, g, type, ins);

            tgt_facts->flags |= MVM_SPESH_FACT_KNOWN_TYPE | MVM_SPESH_FACT_CONCRETE;
            tgt_facts->type = st->WHAT;
        }
        break;
    }
    case MVM_OP_elems: {
            MVMSpeshOperand target   = ins->operands[0];
            MVMSpeshOperand obj      = ins->operands[1];

            MVM_spesh_graph_add_comment(tc, g, ins, "specialized from elems on VMArray");

            ins->info                 = MVM_op_get_op(MVM_OP_sp_get_i64);
            ins->operands             = MVM_spesh_alloc(tc, g, 3 * sizeof(MVMSpeshOperand));
            ins->operands[0]          = target;
            ins->operands[1]          = obj;
            ins->operands[2].lit_i16 = offsetof(MVMArray, body.elems);
    }
    }
}

/* Calculates the non-GC-managed memory we hold on to. */
static MVMuint64 unmanaged_size(MVMThreadContext *tc, MVMSTable *st, void *data) {
    MVMArrayREPRData *repr_data = (MVMArrayREPRData *) st->REPR_data;
    MVMArrayBody     *body      = (MVMArrayBody *)data;
    return body->ssize * repr_data->elem_size;
}

static void describe_refs (MVMThreadContext *tc, MVMHeapSnapshotState *ss, MVMSTable *st, void *data) {
    MVMArrayREPRData *repr_data = (MVMArrayREPRData *) st->REPR_data;
    MVMArrayBody     *body      = (MVMArrayBody *)data;
    MVMuint64         elems     = body->elems;
    MVMuint64         start     = body->start;
    MVMuint64         i         = 0;

    switch (repr_data->slot_type) {
        case MVM_ARRAY_OBJ: {
            MVMObject **slots = body->slots.o;
            slots += start;
            while (i < elems) {
                MVM_profile_heap_add_collectable_rel_idx(tc, ss,
                    (MVMCollectable *)slots[i], i);
                i++;
            }
            break;
        }
        case MVM_ARRAY_STR: {
            MVMString **slots = body->slots.s;
            slots += start;
            while (i < elems) {
                MVM_profile_heap_add_collectable_rel_idx(tc, ss,
                    (MVMCollectable *)slots[i], i);
                i++;
            }
            break;
        }
    }
}

/* Initializes the representation. */
const MVMREPROps * MVMArray_initialize(MVMThreadContext *tc) {
    return &VMArray_this_repr;
}

/* devirtualized versions of bind_pos */

static void vmarray_bind_pos_int64(MVMThreadContext *tc, MVMSTable *st, void *data, MVMint64 index, MVMRegister value) {
    MVMArrayBody     *body      = (MVMArrayBody *)data;
    MVMuint64        real_index;

    /* Handle negative indexes and resizing if needed. */
    enter_single_user(tc, body);
    if (index < 0) {
        index += body->elems;
        if (index < 0)
            MVM_exception_throw_adhoc(tc, "MVMArray: Index out of bounds");
    }
    else if ((MVMuint64)index >= body->elems) {
        MVMArrayREPRData *repr_data = (MVMArrayREPRData *)st->REPR_data;
        set_size_internal(tc, body, (MVMuint64)index + 1, repr_data);
    }

    real_index = (MVMuint64)index;

    body->slots.i64[body->start + real_index] = value.i64;

    exit_single_user(tc, body);
}

/* devirtualized versions of at_pos */

static void vmarray_at_pos_int64(MVMThreadContext *tc, MVMSTable *st, void *data, MVMint64 index, MVMRegister *value) {
    MVMArrayBody     *body      = (MVMArrayBody *)data;
    MVMuint64        real_index;

    /* Handle negative indexes. */
    if (index < 0) {
        index += body->elems;
        if (index < 0)
            MVM_exception_throw_adhoc(tc, "MVMArray: Index out of bounds");
    }

    real_index = (MVMuint64)index;

    if (real_index >= body->elems)
        value->i64 = 0;
    else
        value->i64 = (MVMint64)body->slots.i64[body->start + real_index];
}

/* devirtualization dispatch function for the JIT to use */

void *MVM_VMArray_find_fast_impl_for_jit(MVMThreadContext *tc, MVMSTable *st, MVMint16 op, MVMuint16 kind) {
    MVMArrayREPRData *repr_data = (MVMArrayREPRData *)st->REPR_data;

    switch (op) {
        case MVM_OP_atpos_i:
            if (kind != MVM_reg_int64) {
                return NULL;
            }
            if (repr_data->slot_type == MVM_ARRAY_I64) {
                return vmarray_at_pos_int64;
            }
            break;
        case MVM_OP_atpos_u:
            if (kind != MVM_reg_uint64) {
                return NULL;
            }
            if (repr_data->slot_type == MVM_ARRAY_U64) {
                return vmarray_at_pos_int64;
            }
            break;
        case MVM_OP_bindpos_i:
            if (kind != MVM_reg_int64) {
                return NULL;
            }
            if (repr_data->slot_type == MVM_ARRAY_I64) {
                return vmarray_bind_pos_int64;
            }
            break;
        case MVM_OP_bindpos_u:
            if (kind != MVM_reg_uint64) {
                return NULL;
            }
            if (repr_data->slot_type == MVM_ARRAY_U64) {
                return vmarray_bind_pos_int64;
            }
            break;
        default:
            return NULL;
    }
    return NULL;
}

static const MVMREPROps VMArray_this_repr = {
    type_object_for,
    MVM_gc_allocate_object, /* serialization.c relies on this and the next line */
    NULL, /* initialize */
    copy_to,
    MVM_REPR_DEFAULT_ATTR_FUNCS,
    MVM_REPR_DEFAULT_BOX_FUNCS,
    {
        MVM_VMArray_at_pos,
        MVM_VMArray_bind_pos,
        set_elems,
        MVM_VMArray_push,
        pop,
        unshift,
        shift,
        aslice,
        asplice,
        at_pos_multidim,
        bind_pos_multidim,
        dimensions,
        set_dimensions,
        get_elem_storage_spec,
        pos_as_atomic,
        pos_as_atomic_multidim,
        write_buf,
        read_buf
    },    /* pos_funcs */
    MVM_REPR_DEFAULT_ASS_FUNCS,
    elems,
    get_storage_spec,
    NULL, /* change_type */
    serialize,
    deserialize,
    serialize_repr_data,
    deserialize_repr_data,
    deserialize_stable_size,
    VMArray_gc_mark,
    gc_free,
    NULL, /* gc_cleanup */
    gc_mark_repr_data,
    gc_free_repr_data,
    compose,
    spesh,
    "VMArray", /* name */
    MVM_REPR_ID_VMArray,
    unmanaged_size,
    describe_refs,
};
