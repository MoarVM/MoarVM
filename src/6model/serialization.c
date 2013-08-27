#include <moarvm.h>
#include <sha1.h>

#ifndef MAX
    #define MAX(x, y) ((y) > (x) ? (y) : (x))
#endif

/* Version of the serialization format that we are currently at and lowest
 * version we support. */
#define CURRENT_VERSION 6
#define MIN_VERSION     5

/* Various sizes (in bytes). */
#define HEADER_SIZE                 4 * 16
#define DEP_TABLE_ENTRY_SIZE        8
#define STABLES_TABLE_ENTRY_SIZE    12
#define OBJECTS_TABLE_ENTRY_SIZE    16
#define CLOSURES_TABLE_ENTRY_SIZE   24
#define CONTEXTS_TABLE_ENTRY_SIZE   16
#define REPOS_TABLE_ENTRY_SIZE      16

/* Some guesses. */
#define DEFAULT_STABLE_DATA_SIZE     4096
#define STABLES_TABLE_ENTRIES_GUESS  16
#define OBJECT_SIZE_GUESS            8
#define CLOSURES_TABLE_ENTRIES_GUESS 16
#define CONTEXTS_TABLE_ENTRIES_GUESS 4
#define DEFAULT_CONTEXTS_DATA_SIZE   1024

/* Possible reference types we can serialize. */
#define REFVAR_NULL                 1
#define REFVAR_OBJECT               2
#define REFVAR_VM_NULL              3
#define REFVAR_VM_INT               4
#define REFVAR_VM_NUM               5
#define REFVAR_VM_STR               6
#define REFVAR_VM_ARR_VAR           7
#define REFVAR_VM_ARR_STR           8
#define REFVAR_VM_ARR_INT           9
#define REFVAR_VM_HASH_STR_VAR      10
#define REFVAR_STATIC_CODEREF       11
#define REFVAR_CLONED_CODEREF       12

/* Endian translation (file format is little endian, so on big endian we need
 * to twiddle. */
#ifdef MVM_BIGENDIAN
static void switch_endian(char *bytes, size_t size)
{
    size_t low  = 0;
    size_t high = size - 1;
    while (high > low) {
        char tmp    = bytes[low];
        bytes[low]  = bytes[high];
        bytes[high] = tmp;
        low++;
        high--;
    }
}
#endif

/* Base64 encoding */
static char * base64_encode(const void *buf, size_t size)
{
    static const char base64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    char* str = (char*) malloc((size+3)*4/3 + 1);

    char* p = str;
    const unsigned char* q = (const unsigned char*) buf;
    size_t i = 0;

    while (i < size) {
        int c = q[i++];
        c *= 256;
        if (i < size)
            c += q[i];
        i++;

        c *= 256;
        if (i < size)
            c += q[i];
        i++;

        *p++ = base64[(c & 0x00fc0000) >> 18];
        *p++ = base64[(c & 0x0003f000) >> 12];

        if (i > size + 1)
            *p++ = '=';
        else
            *p++ = base64[(c & 0x00000fc0) >> 6];

        if (i > size)
            *p++ = '=';
        else
            *p++ = base64[c & 0x0000003f];
    }

    *p = 0;

    return str;
}

/* Base64 decoding */
static int POS(char c)
{
    /* XXX TODO: investigate whether enumerating all 256 cases of
     * this in a switch/case can help the compiler turn it into a
     * jump table instead of a bunch of comparisons (if it doesn't
     * already, of course!)... */
    if (c>='A' && c<='Z') return c - 'A';
    if (c>='a' && c<='z') return c - 'a' + 26;
    if (c>='0' && c<='9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    if (c == '=') return -1;
    return -2;
}
static void * base64_decode(const char *s, size_t *data_len)
{
    const char *p;
    unsigned char *q, *data;
    /* XXX TODO: investigate whether putting these n[4] into 4
     * separate locals helps the compiler optimize them better.. */
    int n[4];

    size_t len = strlen(s);
    if (len % 4)
        return NULL;
    data = (unsigned char*) malloc(len/4*3);
    q = (unsigned char*) data;

    for (p = s; *p; ) {
        n[0] = POS(*p++);
        n[1] = POS(*p++);
        n[2] = POS(*p++);
        n[3] = POS(*p++);

        /* XXX TODO: investigate jump table possibility here too,
         * or at least collapse some of the branches... */
        if (n[0] == -2
         || n[1] == -2
         || n[2] == -2
         || n[3] == -2
         || n[0] == -1
         || n[1] == -1
         || (n[2] == -1 && n[3] != -1))
            return NULL;

        q[0] = (n[0] << 2) + (n[1] >> 4);
        if (n[2] != -1)
            q[1] = ((n[1] & 15) << 4) + (n[2] >> 2);
        if (n[3] != -1)
            q[2] = ((n[2] & 3) << 6) + n[3];
        q += 3;
    }

    *data_len = q-data - (n[2]==-1) - (n[3]==-1);

    return data;
}


/* ***************************************************************************
 * Serialization (writing related)
 * ***************************************************************************/

/* Writes an int64 into a buffer. */
static void write_int64(char *buffer, size_t offset, MVMint64 value) {
    memcpy(buffer + offset, &value, 8);
#if MVM_BIGENDIAN
    switch_endian(buffer + offset, 8);
#endif
}

/* Writes an int32 into a buffer. */
static void write_int32(char *buffer, size_t offset, MVMint32 value) {
    memcpy(buffer + offset, &value, 4);
#if MVM_BIGENDIAN
    switch_endian(buffer + offset, 4);
#endif
}

/* Writes an int16 into a buffer. */
static void write_int16(char *buffer, size_t offset, MVMint16 value) {
    memcpy(buffer + offset, &value, 2);
#if MVM_BIGENDIAN
    switch_endian(buffer + offset, 2);
#endif
}

/* Writes an double into a buffer. */
static void write_double(char *buffer, size_t offset, double value) {
    memcpy(buffer + offset, &value, 8);
#if MVM_BIGENDIAN
    switch_endian(buffer + offset, 8);
#endif
}

#define STRING_IS_NULL(s) ((s) == NULL)

/* Adds an item to the MVMString heap if needed, and returns the index where
 * it may be found. */
static MVMint32 add_string_to_heap(MVMThreadContext *tc, MVMSerializationWriter *writer, MVMString *s) {
    if (STRING_IS_NULL(s)) {
        /* We ensured that the first entry in the heap represents the null MVMString,
         * so can just hand back 0 here. */
        return 0;
    }
    else if (MVM_repr_exists_key(tc, writer->seen_strings, s)) {
        return (MVMint32)MVM_repr_at_key_int(tc, writer->seen_strings, s);
    }
    else {
        MVMint64 next_idx = MVM_repr_elems(tc, writer->root.string_heap);
        MVM_repr_bind_pos_s(tc, writer->root.string_heap, next_idx, s);
        MVM_repr_bind_key_int(tc, writer->seen_strings, s, next_idx);
        return (MVMint32)next_idx;
    }
}

/* Gets the ID of a serialization context. Returns 0 if it's the current
 * one, or its dependency table offset (base-1) otherwise. Note that if
 * it is not yet in the dependency table, it will be added. */
static MVMint32 get_sc_id(MVMThreadContext *tc, MVMSerializationWriter *writer, MVMSerializationContext *sc) {
    MVMint64 i, num_deps, offset;

    /* Easy if it's in the current SC. */
    if (writer->root.sc == sc)
        return 0;

    /* Otherwise, find it in our dependencies list. */
    num_deps = writer->root.num_dependencies;
    for (i = 0; i < num_deps; i++)
        if (writer->root.dependent_scs[i] == sc)
            return (MVMint32)i + 1;

    /* Otherwise, need to add it to our dependencies list. Ensure there's
     * space in the dependencies table; grow if not. */
    offset = num_deps * DEP_TABLE_ENTRY_SIZE;
    if (offset + DEP_TABLE_ENTRY_SIZE > writer->dependencies_table_alloc) {
        writer->dependencies_table_alloc *= 2;
        writer->root.dependencies_table = (char *)realloc(writer->root.dependencies_table, writer->dependencies_table_alloc);
    }

    /* Add dependency. */
    /* XXX MVM_repr_push_o(tc, writer->root.dependent_scs, sc); */
    write_int32(writer->root.dependencies_table, offset,
        add_string_to_heap(tc, writer, MVM_sc_get_handle(tc, sc)));
    write_int32(writer->root.dependencies_table, offset + 4,
        add_string_to_heap(tc, writer, MVM_sc_get_description(tc, sc)));
    writer->root.num_dependencies++;
    return writer->root.num_dependencies; /* Deliberately index + 1. */
}

#define OBJ_IS_NULL(obj) ((obj) == NULL)
/* cheater */
#define STABLE_STRUCT(st) (&(st)->header)

/* Takes an STable. If it's already in an SC, returns information on how
 * to reference it. Otherwise, adds it to the current SC, effectively
 * placing it onto the work list. */
static void get_stable_ref_info(MVMThreadContext *tc, MVMSerializationWriter *writer,
                                MVMSTable *st, MVMint32 *sc, MVMint32 *sc_idx) {
    /* Add to this SC if needed. */
    if (st->header.sc == NULL) {
        MVM_ASSIGN_REF(tc, st, st->header.sc, writer->root.sc);
        MVM_sc_push_stable(tc, writer->root.sc, st);
    }

    /* Work out SC reference. */
    *sc     = get_sc_id(tc, writer, st->header.sc);
    *sc_idx = (MVMint32)MVM_sc_find_stable_idx(tc, st->header.sc, st);
}

/* Expands current target storage as needed. */
static void expand_storage_if_needed(MVMThreadContext *tc, MVMSerializationWriter *writer, MVMint64 need) {
    if (*(writer->cur_write_offset) + need > *(writer->cur_write_limit)) {
        *(writer->cur_write_limit) *= 2;
        *(writer->cur_write_buffer) = (char *)realloc(*(writer->cur_write_buffer),
            *(writer->cur_write_limit));
    }
}

/* Writing function for native integers. */
static void write_int_func(MVMThreadContext *tc, MVMSerializationWriter *writer, MVMint64 value) {
    expand_storage_if_needed(tc, writer, 8);
    write_int64(*(writer->cur_write_buffer), *(writer->cur_write_offset), value);
    *(writer->cur_write_offset) += 8;
}

/* Writing function for native numbers. */
static void write_num_func(MVMThreadContext *tc, MVMSerializationWriter *writer, MVMnum64 value) {
    expand_storage_if_needed(tc, writer, 8);
    write_double(*(writer->cur_write_buffer), *(writer->cur_write_offset), value);
    *(writer->cur_write_offset) += 8;
}

/* Writing function for native strings. */
static void write_str_func(MVMThreadContext *tc, MVMSerializationWriter *writer, MVMString *value) {
    MVMint32 heap_loc = add_string_to_heap(tc, writer, value);
    expand_storage_if_needed(tc, writer, 4);
    write_int32(*(writer->cur_write_buffer), *(writer->cur_write_offset), heap_loc);
    *(writer->cur_write_offset) += 4;
}

#define SC_OBJ(obj) ((obj)->header.sc)

/* Writes an object reference. */
static void write_obj_ref(MVMThreadContext *tc, MVMSerializationWriter *writer, MVMObject *ref) {
    MVMint32 sc_id, idx;

    if (OBJ_IS_NULL(SC_OBJ(ref))) {
        /* This object doesn't belong to an SC yet, so it must be serialized as part of
         * this compilation unit. Add it to the work list. */
        SC_OBJ(ref) = writer->root.sc;
        MVM_repr_push_o(tc, writer->objects_list, ref);
    }
    sc_id = get_sc_id(tc, writer, SC_OBJ(ref));
    idx   = (MVMint32)MVM_sc_find_object_idx(tc, SC_OBJ(ref), ref);

    expand_storage_if_needed(tc, writer, 8);
    write_int32(*(writer->cur_write_buffer), *(writer->cur_write_offset), sc_id);
    *(writer->cur_write_offset) += 4;
    write_int32(*(writer->cur_write_buffer), *(writer->cur_write_offset), idx);
    *(writer->cur_write_offset) += 4;
}

/* Writes an array where each item is a variant reference. */
void write_ref_func(MVMThreadContext *tc, MVMSerializationWriter *writer, MVMObject *ref);
static void write_array_var(MVMThreadContext *tc, MVMSerializationWriter *writer, MVMObject *arr) {
    MVMint32 elems = (MVMint32)MVM_repr_elems(tc, arr);
    MVMint32 i;

    /* Write out element count. */
    expand_storage_if_needed(tc, writer, 4);
    write_int32(*(writer->cur_write_buffer), *(writer->cur_write_offset), elems);
    *(writer->cur_write_offset) += 4;

    /* Write elements. */
    for (i = 0; i < elems; i++)
        write_ref_func(tc, writer, MVM_repr_at_pos_o(tc, arr, i));
}

/* Writes an array where each item is an integer. */
static void write_array_int(MVMThreadContext *tc, MVMSerializationWriter *writer, MVMObject *arr) {
    MVMint32 elems = (MVMint32)MVM_repr_elems(tc, arr);
    MVMint32 i;

    /* Write out element count. */
    expand_storage_if_needed(tc, writer, 4);
    write_int32(*(writer->cur_write_buffer), *(writer->cur_write_offset), elems);
    *(writer->cur_write_offset) += 4;

    /* Write elements. */
    for (i = 0; i < elems; i++)
        write_int_func(tc, writer, MVM_repr_at_pos_i(tc, arr, i));
}

/* Writes an array where each item is a MVMString. */
static void write_array_str(MVMThreadContext *tc, MVMSerializationWriter *writer, MVMObject *arr) {
    MVMint32 elems = (MVMint32)MVM_repr_elems(tc, arr);
    MVMint32 i;

    /* Write out element count. */
    expand_storage_if_needed(tc, writer, 4);
    write_int32(*(writer->cur_write_buffer), *(writer->cur_write_offset), elems);
    *(writer->cur_write_offset) += 4;

    /* Write elements. */
    for (i = 0; i < elems; i++)
        write_str_func(tc, writer, MVM_repr_at_pos_s(tc, arr, i));
}

/* Writes a hash where each key is a MVMString and each value a variant reference. */
void write_ref_func(MVMThreadContext *tc, MVMSerializationWriter *writer, MVMObject *ref);
static void write_hash_str_var(MVMThreadContext *tc, MVMSerializationWriter *writer, MVMObject *hash) {
    MVMObject *iter = MVM_iter(tc, hash);
    MVMint32 elems = (MVMint32)MVM_repr_elems(tc, hash);

    /* Write out element count. */
    expand_storage_if_needed(tc, writer, 4);
    write_int32(*(writer->cur_write_buffer), *(writer->cur_write_offset), elems);
    *(writer->cur_write_offset) += 4;

    /* Write elements, as key,value,key,value etc. */
    while (MVM_iter_istrue(tc, (MVMIter *)iter)) {
        write_str_func(tc, writer, MVM_iterkey_s(tc, (MVMIter *)iter));
        write_ref_func(tc, writer, MVM_iterval(tc, (MVMIter *)iter));
    }
}

/* Writes a reference to a code object in some SC. */
static void write_code_ref(MVMThreadContext *tc, MVMSerializationWriter *writer, MVMObject *code) {
    MVMSerializationContext *sc = code->header.sc;
    MVMint32  sc_id   = get_sc_id(tc, writer, sc);
    MVMint32  idx     = (MVMint32)MVM_sc_find_code_idx(tc, sc, code);
    expand_storage_if_needed(tc, writer, 8);
    write_int32(*(writer->cur_write_buffer), *(writer->cur_write_offset), sc_id);
    *(writer->cur_write_offset) += 4;
    write_int32(*(writer->cur_write_buffer), *(writer->cur_write_offset), idx);
    *(writer->cur_write_offset) += 4;
}

/* Given a closure, locate the static code reference it was originally cloned
 * from. */
static MVMObject * closure_to_static_code_ref(MVMThreadContext *tc, MVMObject *closure, MVMint64 fatal) {
    MVMObject *scr = (MVMObject *)(((MVMCode *)closure)->body.sf)->body.static_code;

    if (scr == NULL || scr->header.sc == NULL) {
        if (fatal)
            MVM_exception_throw_adhoc(tc,
                "Serialization Error: missing static code ref for closure");
        return NULL;
    }
    return scr;
}

/* Takes an outer context that is potentially to be serialized. Checks if it
 * is of interest, and if so sets it up to be serialized. */
static MVMint32 get_serialized_context_idx(MVMThreadContext *tc, MVMSerializationWriter *writer, MVMObject *ctx) {
    if (OBJ_IS_NULL(ctx->header.sc)) {
        /* Make sure we should chase a level down. */
        if (OBJ_IS_NULL(closure_to_static_code_ref(tc, ((MVMObject *)ctx)->body.code_object, 0))) {
            return 0;
        }
        else {
            MVM_repr_push_o(tc, writer->contexts_list, ctx);
            MVM_ASSIGN_REF(tc, ctx, ctx.header->sc, writer->root.sc);
            return (MVMint32)MVM_repr_elems(tc, writer->contexts_list);
        }
    }
    else {
        MVMint64 i, c;
        if (ctx->header.sc != writer->root.sc)
            MVM_exception_throw_adhoc(tc,
                "Serialization Error: reference to context outside of SC");
        c = MVM_repr_elems(tc, writer->contexts_list);
        for (i = 0; i < c; i++)
            if (MVM_repr_at_pos_o(tc, writer->contexts_list, i) == ctx)
                return (MVMint32)i + 1;
        MVM_exception_throw_adhoc(tc,
            "Serialization Error: could not locate outer context in current SC");
    }
}

/* Takes a closure, that is to be serialized. Checks if it has an outer that is
 * of interest, and if so sets it up to be serialized. */
static MVMint32 get_serialized_outer_context_idx(MVMThreadContext *tc, MVMSerializationWriter *writer, MVMObject *closure) {
    if (((MVMCode *)closure)->body.is_compiler_stub)
        return 0;
    if (((MVMCode *)closure)->body.outer == NULL)
        return 0;
    return get_serialized_context_idx(tc, writer, ((MVMCode *)closure)->body.outer);
}

/* Takes a closure that needs to be serialized. Makes an entry in the closures
 * table for it. Also adds it to this SC's code refs set and tags it with the
 * current SC. */
static void serialize_closure(MVMThreadContext *tc, MVMSerializationWriter *writer, MVMObject *closure) {
    MVMint32 static_sc_id, static_idx, context_idx;

    /* Locate the static code object. */
    MVMObject *static_code_ref = closure_to_static_code_ref(tc, closure, 1);
    MVMSerializationContext *static_code_sc = static_code_ref->header.sc;

    /* Ensure there's space in the closures table; grow if not. */
    MVMint32 offset = writer->root.num_closures * CLOSURES_TABLE_ENTRY_SIZE;
    if (offset + CLOSURES_TABLE_ENTRY_SIZE > writer->closures_table_alloc) {
        writer->closures_table_alloc *= 2;
        writer->root.closures_table = (char *)realloc(writer->root.closures_table, writer->closures_table_alloc);
    }

    /* Get the index of the context (which will add it to the todo list if
     * needed). */
    context_idx = get_serialized_outer_context_idx(tc, writer, closure);

    /* Add an entry to the closures table. */
    static_sc_id = get_sc_id(tc, writer, static_code_sc);
    static_idx   = (MVMint32)MVM_sc_find_code_idx(tc, static_code_sc, static_code_ref);
    write_int32(writer->root.closures_table, offset, static_sc_id);
    write_int32(writer->root.closures_table, offset + 4, static_idx);
    write_int32(writer->root.closures_table, offset + 8, context_idx);

    /* Check if it has a static code object. */
    if (!OBJ_IS_NULL(PARROT_SUB(closure)->multi_signature)) {
        MVMObject *code_obj = PARROT_SUB(closure)->multi_signature;
        write_int32(writer->root.closures_table, offset + 12, 1);
        if (OBJ_IS_NULL(SC_OBJ(code_obj))) {
            SC_OBJ(code_obj) = writer->root.sc;
            MVM_repr_push_o(tc, writer->objects_list, code_obj);
        }
        write_int32(writer->root.closures_table, offset + 16,
            get_sc_id(tc, writer, SC_OBJ(code_obj)));
        write_int32(writer->root.closures_table, offset + 20,
            (MVMint32)MVM_sc_find_object_idx(tc, SC_OBJ(code_obj), code_obj));
    }
    else {
        write_int32(writer->root.closures_table, offset + 12, 0);
    }

    /* Increment count of closures in the table. */
    writer->root.num_closures++;

    /* Add the closure to this SC, and mark it as as being in it. */
    MVM_repr_push_o(tc, writer->codes_list, closure);
    closure->header.sc = writer->root.sc;
}

/* Writing function for references to things. */
void write_ref_func(MVMThreadContext *tc, MVMSerializationWriter *writer, MVMObject *ref) {
    /* Work out what kind of thing we have and determine the discriminator. */
    MVMint16 discrim = 0;
    if (ref == NULL) {
        discrim = REFVAR_NULL;
    }
    else if (OBJ_IS_NULL(ref) || ref->vtable->base_type == enum_class_Undef) {
        discrim = REFVAR_VM_NULL;
    }
    else if (ref->vtable->base_type == enum_class_Pointer) {
        /* This is really being used to hang caches off in Rakudo, at least. So
         * we just drop it. */
        discrim = REFVAR_VM_NULL;
    }
    else if (ref->vtable->base_type == ctmthunk_id) {
        /* Another example of a generated cache/thunk that we should not serialize. */
        discrim = REFVAR_VM_NULL;
    }
    else if (ref->vtable->base_type == enum_class_FileHandle) {
        /* Can't serialize handles. */
        discrim = REFVAR_VM_NULL;
    }
    else if (ref->vtable->base_type == enum_class_CallContext) {
        /* XXX This is a hack for Rakudo's sake; it keeps a CallContext around in
         * the lexpad, for no really good reason. */
        discrim = REFVAR_VM_NULL;
    }
    else if (ref->vtable->base_type == smo_id) {
        discrim = REFVAR_OBJECT;
    }
    else if (ref->vtable->base_type == enum_class_Integer) {
        discrim = REFVAR_VM_INT;
    }
    else if (ref->vtable->base_type == enum_class_Float) {
        discrim = REFVAR_VM_NUM;
    }
    else if (ref->vtable->base_type == enum_class_String) {
        discrim = REFVAR_VM_STR;
    }
    else if (ref->vtable->base_type == enum_class_ResizablePMCArray) {
        discrim = REFVAR_VM_ARR_VAR;
    }
    else if (ref->vtable->base_type == ownedrpa_id) {
        discrim = REFVAR_VM_ARR_VAR;
    }
    else if (ref->vtable->base_type == qrpa_id) {
        discrim = REFVAR_VM_ARR_VAR;
    }
    else if (ref->vtable->base_type == enum_class_ResizableIntegerArray) {
        discrim = REFVAR_VM_ARR_INT;
    }
    else if (ref->vtable->base_type == enum_class_ResizableStringArray) {
        discrim = REFVAR_VM_ARR_STR;
    }
    else if (ref->vtable->base_type == enum_class_Hash) {
        discrim = REFVAR_VM_HASH_STR_VAR;
    }
    else if (ref->vtable->base_type == ownedhash_id) {
        discrim = REFVAR_VM_HASH_STR_VAR;
    }
    else if (ref->vtable->base_type == enum_class_Sub || ref->vtable->base_type == enum_class_Coroutine) {
        MVMObject *code_sc = Parrot_pmc_getprop(tc, ref, Parrot_str_new_constant(tc, "SC"));
        MVMObject *static_cr = Parrot_pmc_getprop(tc, ref, Parrot_str_new_constant(tc, "STATIC_CODE_REF"));
        if (!OBJ_IS_NULL(code_sc) && !OBJ_IS_NULL(static_cr)) {
            /* Static code reference. */
            discrim = REFVAR_STATIC_CODEREF;
        }
        else if (!OBJ_IS_NULL(code_sc)) {
            /* Closure, but already seen and serialization already handled. */
            discrim = REFVAR_CLONED_CODEREF;
        }
        else {
            /* Closure but didn't see it yet. Take care of it serialization, which
             * gets it marked with this SC. Then it's just a normal code ref that
             * needs serializing. */
            serialize_closure(tc, writer, ref);
            discrim = REFVAR_CLONED_CODEREF;
        }
    }
    else if (ref->vtable->base_type == enum_class_Object) {
        MVM_exception_throw_adhoc(tc,
            "Serialization Error: Parrot object type '%Ss' passed to write_ref",
            VTABLE_name(tc, VTABLE_get_class(tc, ref)));
    }
    else {
        MVM_exception_throw_adhoc(tc,
            "Serialization Error: Unimplemented object type '%Ss' passed to write_ref",
            ref->vtable->whoami);
    }

    /* Write the discriminator. */
    expand_storage_if_needed(tc, writer, 2);
    write_int16(*(writer->cur_write_buffer), *(writer->cur_write_offset), discrim);
    *(writer->cur_write_offset) += 2;

    /* Now take appropriate action. */
    switch (discrim) {
        case REFVAR_NULL: goto REFVAR_VM_NULL;
        case REFVAR_OBJECT:
            write_obj_ref(tc, writer, ref);
            break;
        case REFVAR_VM_NULL:
            /* Nothing to do for these. */
            break;
        case REFVAR_VM_INT:
            write_int_func(tc, writer, VTABLE_get_integer(tc, ref));
            break;
        case REFVAR_VM_NUM:
            write_num_func(tc, writer, VTABLE_get_number(tc, ref));
            break;
        case REFVAR_VM_STR:
            write_str_func(tc, writer, VTABLE_get_string(tc, ref));
            break;
        case REFVAR_VM_ARR_VAR:
            write_array_var(tc, writer, ref);
            break;
        case REFVAR_VM_ARR_STR:
            write_array_str(tc, writer, ref);
            break;
        case REFVAR_VM_ARR_INT:
            write_array_int(tc, writer, ref);
            break;
        case REFVAR_VM_HASH_STR_VAR:
            write_hash_str_var(tc, writer, ref);
            break;
        case REFVAR_STATIC_CODEREF:
        case REFVAR_CLONED_CODEREF:
            write_code_ref(tc, writer, ref);
            break;
        default:
            MVM_exception_throw_adhoc(tc,
                "Serialization Error: Unimplemented object type '%Ss' passed to write_ref",
                ref->vtable->whoami);
    }
}

/* Writing function for references to STables. */
static void write_stable_ref_func(MVMThreadContext *tc, MVMSerializationWriter *writer, MVMSTable *st) {
    MVMint32 sc_id, idx;
    get_stable_ref_info(tc, writer, st->stable_pmc, &sc_id, &idx);
    expand_storage_if_needed(tc, writer, 8);
    write_int32(*(writer->cur_write_buffer), *(writer->cur_write_offset), sc_id);
    *(writer->cur_write_offset) += 4;
    write_int32(*(writer->cur_write_buffer), *(writer->cur_write_offset), idx);
    *(writer->cur_write_offset) += 4;
}

/* Concatenates the various output segments into a single binary MVMString. */
static MVMString * concatenate_outputs(MVMThreadContext *tc, MVMSerializationWriter *writer) {
    char        *output      = NULL;
    char        *output_b64  = NULL;
    MVMint32  output_size = 0;
    MVMint32  offset      = 0;
    MVMint32  version     = writer->root.version;

    /* Calculate total size. */
    output_size += HEADER_SIZE;
    output_size += writer->root.num_dependencies * DEP_TABLE_ENTRY_SIZE;
    output_size += writer->root.num_stables * STABLES_TABLE_ENTRY_SIZE;
    output_size += writer->stables_data_offset;
    output_size += writer->root.num_objects * OBJECTS_TABLE_ENTRY_SIZE;
    output_size += writer->objects_data_offset;
    output_size += writer->root.num_closures * CLOSURES_TABLE_ENTRY_SIZE;
    output_size += writer->root.num_contexts * CONTEXTS_TABLE_ENTRY_SIZE;
    output_size += writer->contexts_data_offset;
    output_size += writer->root.num_repos * REPOS_TABLE_ENTRY_SIZE;

    /* Allocate a buffer that size. */
    output = (char *)mem_sys_allocate(output_size);

    /* Write version into header. */
    write_int32(output, 0, CURRENT_VERSION);
    offset += HEADER_SIZE;

    /* Put dependencies table in place and set location/rows in header. */
    write_int32(output, 4, offset);
    write_int32(output, 8, writer->root.num_dependencies);
    memcpy(output + offset, writer->root.dependencies_table,
        writer->root.num_dependencies * DEP_TABLE_ENTRY_SIZE);
    offset += writer->root.num_dependencies * DEP_TABLE_ENTRY_SIZE;

    /* Put STables table in place, and set location/rows in header. */
    write_int32(output, 12, offset);
    write_int32(output, 16, writer->root.num_stables);
    memcpy(output + offset, writer->root.stables_table,
        writer->root.num_stables * STABLES_TABLE_ENTRY_SIZE);
    offset += writer->root.num_stables * STABLES_TABLE_ENTRY_SIZE;

    /* Put STables data in place. */
    write_int32(output, 20, offset);
    memcpy(output + offset, writer->root.stables_data,
        writer->stables_data_offset);
    offset += writer->stables_data_offset;

    /* Put objects table in place, and set location/rows in header. */
    write_int32(output, 24, offset);
    write_int32(output, 28, writer->root.num_objects);
    memcpy(output + offset, writer->root.objects_table,
        writer->root.num_objects * OBJECTS_TABLE_ENTRY_SIZE);
    offset += writer->root.num_objects * OBJECTS_TABLE_ENTRY_SIZE;

    /* Put objects data in place. */
    write_int32(output, 32, offset);
    memcpy(output + offset, writer->root.objects_data,
        writer->objects_data_offset);
    offset += writer->objects_data_offset;

    /* Put closures table in place, and set location/rows in header. */
    write_int32(output, 36, offset);
    write_int32(output, 40, writer->root.num_closures);
    memcpy(output + offset, writer->root.closures_table,
        writer->root.num_closures * CLOSURES_TABLE_ENTRY_SIZE);
    offset += writer->root.num_closures * CLOSURES_TABLE_ENTRY_SIZE;

    /* Put contexts table in place, and set location/rows in header. */
    write_int32(output, 44, offset);
    write_int32(output, 48, writer->root.num_contexts);
    memcpy(output + offset, writer->root.contexts_table,
        writer->root.num_contexts * CONTEXTS_TABLE_ENTRY_SIZE);
    offset += writer->root.num_contexts * CONTEXTS_TABLE_ENTRY_SIZE;

    /* Put contexts data in place. */
    write_int32(output, 52, offset);
    memcpy(output + offset, writer->root.contexts_data,
        writer->contexts_data_offset);
    offset += writer->contexts_data_offset;

    /* Put repossessions table in place, and set location/rows in header. */
    write_int32(output, 56, offset);
    write_int32(output, 60, writer->root.num_repos);
    memcpy(output + offset, writer->root.repos_table,
        writer->root.num_repos * REPOS_TABLE_ENTRY_SIZE);
    offset += writer->root.num_repos * REPOS_TABLE_ENTRY_SIZE;

    /* Sanity check. */
    if (offset != output_size)
        MVM_exception_throw_adhoc(tc,
            "Serialization sanity check failed: offset != output_size");

    /* Base 64 encode. */
    output_b64 = base64_encode(output, output_size);
    free(output);
    if (output_b64 == NULL)
        MVM_exception_throw_adhoc(tc,
            "Serialization error: failed to convert to base64");

    /* Make a Parrot binary MVMString containing it (external flag means use
     * this buffer, not copy it to a new one). */
    return Parrot_str_new_init(tc, output_b64, strlen(output_b64),
        Parrot_binary_encoding_ptr, PObj_external_FLAG);
}

/* This handles the serialization of an STable, and calls off to serialize
 * its representation data also. */
static void serialize_stable(MVMThreadContext *tc, MVMSerializationWriter *writer, MVMSTable *st) {
    MVMint32 version = writer->root.version;
    MVMint64  i;

    /* Ensure there's space in the STables table; grow if not. */
    MVMint32 offset = writer->root.num_stables * STABLES_TABLE_ENTRY_SIZE;
    if (offset + STABLES_TABLE_ENTRY_SIZE > writer->stables_table_alloc) {
        writer->stables_table_alloc *= 2;
        writer->root.stables_table = (char *)realloc(writer->root.stables_table, writer->stables_table_alloc);
    }

    /* Make STables table entry. */
    write_int32(writer->root.stables_table, offset, add_string_to_heap(tc, writer, st->REPR->name));
    write_int32(writer->root.stables_table, offset + 4, writer->stables_data_offset);

    /* Increment count of stables in the table. */
    writer->root.num_stables++;

    /* Make sure we're going to write to the correct place. */
    writer->cur_write_buffer = &(writer->root.stables_data);
    writer->cur_write_offset = &(writer->stables_data_offset);
    writer->cur_write_limit  = &(writer->stables_data_alloc);

    /* Write HOW, WHAT and WHO. */
    write_obj_ref(tc, writer, st->HOW);
    write_obj_ref(tc, writer, st->WHAT);
    write_ref_func(tc, writer, st->WHO);

    /* Method cache and v-table. */
    write_ref_func(tc, writer, st->method_cache);
    write_int_func(tc, writer, st->vtable_length);
    for (i = 0; i < st->vtable_length; i++)
        write_ref_func(tc, writer, st->vtable[i]);

    /* Type check cache. */
    write_int_func(tc, writer, st->type_check_cache_length);
    for (i = 0; i < st->type_check_cache_length; i++)
        write_ref_func(tc, writer, st->type_check_cache[i]);

    /* Mode flags. */
    write_int_func(tc, writer, st->mode_flags);

    /* Boolification spec. */
    write_int_func(tc, writer, st->boolification_spec != NULL);
    if (st->boolification_spec) {
        write_int_func(tc, writer, st->boolification_spec->mode);
        write_ref_func(tc, writer, st->boolification_spec->method);
    }

    /* Container spec. */
    write_int_func(tc, writer, st->container_spec != NULL);
    if (st->container_spec) {
        /* Write container spec name. */
        write_str_func(tc, writer, st->container_spec->name);

        /* Give container spec a chance to serialize any data it wishes. */
        st->container_spec->serialize(tc, st, writer);
    }

    /* Invocation spec. */
    if (writer->root.version >= 5) {
        write_int_func(tc, writer, st->invocation_spec != NULL);
        if (st->invocation_spec) {
            write_ref_func(tc, writer, st->invocation_spec->class_handle);
            write_str_func(tc, writer, st->invocation_spec->attr_name);
            write_int_func(tc, writer, st->invocation_spec->hint);
            write_ref_func(tc, writer, st->invocation_spec->invocation_handler);
        }
    }

    /* Store offset we save REPR data at. */
    write_int32(writer->root.stables_table, offset + 8, writer->stables_data_offset);

    /* If the REPR has a function to serialize representation data, call it. */
    if (st->REPR->serialize_repr_data)
        st->REPR->serialize_repr_data(tc, st, writer);
}

/* This handles the serialization of an object, which largely involves a
 * delegation to its representation. */
static void serialize_object(MVMThreadContext *tc, MVMSerializationWriter *writer, MVMObject *obj) {
    MVMint32 offset;

    /* Get index of SC that holds the STable and its index. */
    MVMint32 sc;
    MVMint32 sc_idx;
    if (obj->vtable->base_type != smo_id)
        MVM_exception_throw_adhoc(tc,
            "Non-6model object in object serialization list");
    get_stable_ref_info(tc, writer, STABLE_PMC(obj), &sc, &sc_idx);

    /* Ensure there's space in the objects table; grow if not. */
    offset = writer->root.num_objects * OBJECTS_TABLE_ENTRY_SIZE;
    if (offset + OBJECTS_TABLE_ENTRY_SIZE > writer->objects_table_alloc) {
        writer->objects_table_alloc *= 2;
        writer->root.objects_table = (char *)realloc(writer->root.objects_table, writer->objects_table_alloc);
    }

    /* Make objects table entry. */
    write_int32(writer->root.objects_table, offset, sc);
    write_int32(writer->root.objects_table, offset + 4, sc_idx);
    write_int32(writer->root.objects_table, offset + 8, writer->objects_data_offset);
    write_int32(writer->root.objects_table, offset + 12, IS_CONCRETE(obj) ? 1 : 0);

    /* Increment count of objects in the table. */
    writer->root.num_objects++;

    /* Make sure we're going to write to the correct place. */
    writer->cur_write_buffer = &(writer->root.objects_data);
    writer->cur_write_offset = &(writer->objects_data_offset);
    writer->cur_write_limit  = &(writer->objects_data_alloc);

    /* Delegate to its serialization REPR function. */
    if (IS_CONCRETE(obj)) {
        if (REPR(obj)->serialize)
            REPR(obj)->serialize(tc, STABLE(obj), OBJECT_BODY(obj), writer);
        else
            MVM_exception_throw_adhoc(tc,
                "Missing serialize REPR function");
    }
}

/* This handles the serialization of a context, which means serializing
 * the stuff in its lexpad. */
static void serialize_context(MVMThreadContext *tc, MVMSerializationWriter *writer, MVMObject *ctx) {
    MVMint32 i, offset, static_sc_id, static_idx;

    /* Grab lexpad, which we'll serialize later on. */
    MVMObject *lexpad    = PARROT_CALLCONTEXT(ctx)->lex_pad;
    MVMObject *lexinfo   = PARROT_SUB(PARROT_CALLCONTEXT(ctx)->current_sub)->lex_info;
    MVMObject *lexiter   = MVM_iter(tc, lexpad);

    /* Locate the static code ref this context points to. */
    MVMObject *static_code_ref = closure_to_static_code_ref(tc, PARROT_CALLCONTEXT(ctx)->current_sub, 1);
    MVMObject *static_code_sc  = Parrot_pmc_getprop(tc, static_code_ref, Parrot_str_new_constant(tc, "SC"));
    if (OBJ_IS_NULL(static_code_sc))
        MVM_exception_throw_adhoc(tc,
            "Serialization Error: closure outer is a code object not in an SC");
    static_sc_id = get_sc_id(tc, writer, static_code_sc);
    static_idx   = (MVMint32)MVM_sc_find_code_idx(tc, static_code_sc, static_code_ref);

    /* Ensure there's space in the STables table; grow if not. */
    offset = writer->root.num_contexts * CONTEXTS_TABLE_ENTRY_SIZE;
    if (offset + CONTEXTS_TABLE_ENTRY_SIZE > writer->contexts_table_alloc) {
        writer->contexts_table_alloc *= 2;
        writer->root.contexts_table = (char *)realloc(writer->root.contexts_table, writer->contexts_table_alloc);
    }

    /* Make contexts table entry. */
    write_int32(writer->root.contexts_table, offset, static_sc_id);
    write_int32(writer->root.contexts_table, offset + 4, static_idx);
    write_int32(writer->root.contexts_table, offset + 8, writer->contexts_data_offset);

    /* See if there's any relevant outer context, and if so set it up to
     * be serialized. */
    if (!OBJ_IS_NULL(PARROT_CALLCONTEXT(ctx)->outer_ctx))
        write_int32(writer->root.contexts_table, offset + 12,
            get_serialized_context_idx(tc, writer, PARROT_CALLCONTEXT(ctx)->outer_ctx));
    else
        write_int32(writer->root.contexts_table, offset + 12, 0);

    /* Increment count of stables in the table. */
    writer->root.num_contexts++;

    /* Set up writer. */
    writer->cur_write_buffer = &(writer->root.contexts_data);
    writer->cur_write_offset = &(writer->contexts_data_offset);
    writer->cur_write_limit  = &(writer->contexts_data_alloc);

    /* Serialize lexicals. */
    writer->write_int(tc, writer, MVM_repr_elems(tc, lexpad));
    while (VTABLE_get_bool(tc, lexiter)) {
        MVMString *sym = VTABLE_shift_string(tc, lexiter);
        writer->write_str(tc, writer, sym);
        switch (MVM_repr_at_key_int(tc, lexinfo, sym) & 3) {
            case REGNO_INT:
                writer->write_int(tc, writer,
                    MVM_repr_at_key_int(tc, lexpad, sym));
                break;
            case REGNO_NUM:
                writer->write_num(tc, writer,
                    VTABLE_get_number_keyed_str(tc, lexpad, sym));
                break;
            case REGNO_STR:
                writer->write_str(tc, writer,
                    VTABLE_get_string_keyed_str(tc, lexpad, sym));
                break;
            default:
                writer->write_ref(tc, writer,
                    VTABLE_get_pmc_keyed_str(tc, lexpad, sym));
        }
    }
}

/* Goes through the list of repossessions and serializes them all. */
static void serialize_repossessions(MVMThreadContext *tc, MVMSerializationWriter *writer) {
    MVMint64 i;

    /* Obtain list of repossession object indexes and original SCs. */
    MVMObject *rep_indexes, *rep_scs;
    GETATTR_SerializationContext_rep_indexes(tc, writer->root.sc, rep_indexes);
    GETATTR_SerializationContext_rep_scs(tc, writer->root.sc, rep_scs);

    /* Allocate table space, provided we've actually something to do. */
    writer->root.num_repos = (MVMint32)MVM_repr_elems(tc, rep_indexes);
    if (writer->root.num_repos == 0)
        return;
    writer->root.repos_table = (char *)mem_sys_allocate(writer->root.num_repos * REPOS_TABLE_ENTRY_SIZE);

    /* Make entries. */
    for (i = 0; i < writer->root.num_repos; i++) {
        MVMint32 offset  = (MVMint32)(i * REPOS_TABLE_ENTRY_SIZE);
        MVMint32 obj_idx = (MVMint32)(MVM_repr_at_pos_i(tc, rep_indexes, i) >> 1);
        MVMint32 is_st   = MVM_repr_at_pos_i(tc, rep_indexes, i) & 1;

        /* Work out original object's SC location. */
        MVMObject *orig_sc    = MVM_repr_at_pos_o(tc, rep_scs, i);
        MVMint32   orig_sc_id = get_sc_id(tc, writer, orig_sc);
        MVMint32   orig_idx   = (MVMint32)(is_st ?
            MVM_sc_find_stable_idx(tc, orig_sc, writer->stables_list[obj_idx]) :
            MVM_sc_find_object_idx(tc, orig_sc, MVM_repr_at_pos_o(tc, writer->objects_list, obj_idx)));

        /* Write table row. */
        write_int32(writer->root.repos_table, offset, is_st);
        write_int32(writer->root.repos_table, offset + 4, obj_idx);
        write_int32(writer->root.repos_table, offset + 8, orig_sc_id);
        write_int32(writer->root.repos_table, offset + 12, orig_idx);
    }
}

static void serialize(MVMThreadContext *tc, MVMSerializationWriter *writer) {
    MVMuint32 work_todo;
    do {
        /* Current work list sizes. */
        MVMuint64 stables_todo  = writer->root.sc->body->num_stables;
        MVMuint64 objects_todo  = MVM_repr_elems(tc, writer->objects_list);
        MVMuint64 contexts_todo = MVM_repr_elems(tc, writer->contexts_list);

        /* Reset todo flag - if we do some work we'll go round again as it
         * may have generated more. */
        work_todo = 0;

        /* Serialize any STables on the todo list. */
        while (writer->stables_list_pos < stables_todo) {
            serialize_stable(tc, writer, writer->stables_list[writer->stables_list_pos]);
            writer->stables_list_pos++;
            work_todo = 1;
        }

        /* Serialize any objects on the todo list. */
        while (writer->objects_list_pos < objects_todo) {
            serialize_object(tc, writer, MVM_repr_at_pos_o(tc,
                writer->objects_list, writer->objects_list_pos));
            writer->objects_list_pos++;
            work_todo = 1;
        }

        /* Serialize any contexts on the todo list. */
        while (writer->contexts_list_pos < contexts_todo) {
            serialize_context(tc, writer, MVM_repr_at_pos_o(tc,
                writer->contexts_list, writer->contexts_list_pos));
            writer->contexts_list_pos++;
            work_todo = 1;
        }
    } while (work_todo);

    /* Finally, serialize repossessions table (this can't make any more
     * work, so is done as a separate step here at the end). */
    serialize_repossessions(tc, writer);
}

MVMString * MVM_serialization_serialize(MVMThreadContext *tc, MVMSerializationContext *sc, MVMObject *obj) {
    MVMObject *objects  = NULL;
    MVMObject *codes    = NULL;
    MVMString *result   = NULL;
    MVMint32   sc_elems = (MVMint32)MVM_repr_elems(tc, (MVMObject *)sc);
    MVMint32   version  = CURRENT_VERSION;

    /* Set up writer with some initial settings. */
    MVMSerializationWriter *writer = (MVMSerializationWriter *)calloc(1, sizeof (MVMSerializationWriter));
    GETATTR_SerializationContext_root_objects(tc, sc, objects);
    GETATTR_SerializationContext_root_codes(tc, sc, codes);
    writer->root.version        = CURRENT_VERSION;
    writer->root.sc             = sc;
    writer->stables_list        = sc->body->root_stables;
    writer->objects_list        = objects;
    writer->codes_list          = codes;
    writer->contexts_list       = Parrot_pmc_new(tc, enum_class_ResizablePMCArray);
    writer->root.string_heap    = empty_string_heap;
    writer->root.dependent_scs  = Parrot_pmc_new(tc, enum_class_ResizablePMCArray);
    writer->seen_strings        = Parrot_pmc_new(tc, enum_class_Hash);

    /* Allocate initial memory space for storing serialized tables and data. */
    writer->dependencies_table_alloc = DEP_TABLE_ENTRY_SIZE * 4;
    writer->root.dependencies_table  = (char *)mem_sys_allocate(writer->dependencies_table_alloc);
    writer->stables_table_alloc      = STABLES_TABLE_ENTRY_SIZE * STABLES_TABLE_ENTRIES_GUESS;
    writer->root.stables_table       = (char *)mem_sys_allocate(writer->stables_table_alloc);
    writer->objects_table_alloc      = OBJECTS_TABLE_ENTRY_SIZE * MAX(sc_elems, 1);
    writer->root.objects_table       = (char *)mem_sys_allocate(writer->objects_table_alloc);
    writer->stables_data_alloc       = DEFAULT_STABLE_DATA_SIZE;
    writer->root.stables_data        = (char *)mem_sys_allocate(writer->stables_data_alloc);
    writer->objects_data_alloc       = OBJECT_SIZE_GUESS * MAX(sc_elems, 1);
    writer->root.objects_data        = (char *)mem_sys_allocate(writer->objects_data_alloc);
    writer->closures_table_alloc     = CLOSURES_TABLE_ENTRY_SIZE * CLOSURES_TABLE_ENTRIES_GUESS;
    writer->root.closures_table      = (char *)mem_sys_allocate(writer->closures_table_alloc);
    writer->contexts_table_alloc     = CONTEXTS_TABLE_ENTRY_SIZE * CONTEXTS_TABLE_ENTRIES_GUESS;
    writer->root.contexts_table      = (char *)mem_sys_allocate(writer->contexts_table_alloc);
    writer->contexts_data_alloc      = DEFAULT_CONTEXTS_DATA_SIZE;
    writer->root.contexts_data       = (char *)mem_sys_allocate(writer->contexts_data_alloc);

    /* Populate write functions table. */
    writer->write_int        = write_int_func;
    writer->write_num        = write_num_func;
    writer->write_str        = write_str_func;
    writer->write_ref        = write_ref_func;
    writer->write_stable_ref = write_stable_ref_func;

    /* Disable GC at this stage; the stuff hanging off writer isn't anchored. */
     Parrot_block_GC_mark(tc);

    /* Other init. */
    smo_id = Parrot_pmc_get_type_str(tc, Parrot_str_new(tc, "SixModelObject", 0));
    nqp_lexpad_id = Parrot_pmc_get_type_str(tc, Parrot_str_new(tc, "NQPLexInfo", 0));
    perl6_lexpad_id = Parrot_pmc_get_type_str(tc, Parrot_str_new(tc, "Perl6LexInfo", 0));
    ctmthunk_id = Parrot_pmc_get_type_str(tc, Parrot_str_new(tc, "CTMThunk", 0));
    ownedhash_id = Parrot_pmc_get_type_str(tc, Parrot_str_new(tc, "OwnedHash", 0));
    ownedrpa_id = Parrot_pmc_get_type_str(tc, Parrot_str_new(tc, "OwnedResizablePMCArray", 0));
    qrpa_id = Parrot_pmc_get_type_str(tc, Parrot_str_new(tc, "QRPA", 0));

    /* Initialize MVMString heap so first entry is the NULL MVMString. */
    VTABLE_push_string(tc, empty_string_heap, NULL);

    /* Start serializing. */
    serialize(tc, writer);

    /* Build a single result MVMString out of the serialized data. */
    result = concatenate_outputs(tc, writer);

    /* Re-enable GC. */
    Parrot_unblock_GC_mark(tc);

    /* Clear up afterwards. */
    free(writer->root.dependencies_table);
    free(writer->root.stables_table);
    free(writer->root.stables_data);
    free(writer->root.objects_table);
    free(writer->root.objects_data);
    free(writer);

    return result;
}


/* ***************************************************************************
 * Deserialization (reading related)
 * ***************************************************************************/

/* Reads an int64 from a buffer. */
static MVMint64 read_int64(char *buffer, size_t offset) {
    MVMint64 value;
#ifdef MVM_BIGENDIAN
    switch_endian(buffer + offset, 8);
#endif
    memcpy(&value, buffer + offset, 8);
    return value;
}

/* Reads an int32 from a buffer. */
static MVMint32 read_int32(char *buffer, size_t offset) {
    MVMint32 value;
#ifdef MVM_BIGENDIAN
    switch_endian(buffer + offset, 4);
#endif
    memcpy(&value, buffer + offset, 4);
    return value;
}

/* Reads an int16 from a buffer. */
static MVMint16 read_int16(char *buffer, size_t offset) {
    MVMint16 value;
#ifdef MVM_BIGENDIAN
    switch_endian(buffer + offset, 2);
#endif
    memcpy(&value, buffer + offset, 2);
    return value;
}

/* Reads double from a buffer. */
static MVMnum64 read_double(char *buffer, size_t offset) {
    MVMnum64 value;
#ifdef MVM_BIGENDIAN
    switch_endian(buffer + offset, 8);
#endif
    memcpy(&value, buffer + offset, 8);
    return value;
}

/* If deserialization should fail, cleans up before throwing an exception. */
MVM_NO_RETURN
static void fail_deserialize(MVMThreadContext *tc, MVMSerializationReader *reader,
                             const char *messageFormat, ...) MVM_NO_RETURN_GCC;
MVM_NO_RETURN
static void fail_deserialize(MVMThreadContext *tc, MVMSerializationReader *reader,
        const char *messageFormat, ...) {
    va_list args;
    if (reader->data)
        free(reader->data);
    if (reader->contexts)
        free(reader->contexts);
    free(reader);
    MVM_gc_allocate_gen2_default_clear(tc);
    va_start(args, messageFormat);
    MVM_exception_throw_adhoc_va(tc, messageFormat, args);
    va_end(args);
}

/* Reads the item from the string heap at the specified index. */
static MVMString * read_string_from_heap(MVMThreadContext *tc, MVMSerializationReader *reader, MVMint32 idx) {
    if (idx < MVM_repr_elems(tc, reader->root.string_heap))
        return MVM_repr_at_pos_s(tc, reader->root.string_heap, idx);
    else
        fail_deserialize(tc, reader,
            "Attempt to read past end of string heap (index %d)", idx);
}

/* Locates a serialization context; 0 is the current one, otherwise see the
 * dependencies table. */
static MVMSerializationContext * locate_sc(MVMThreadContext *tc, MVMSerializationReader *reader, MVMint32 sc_id) {
    MVMSerializationContext *sc;
    if (sc_id == 0)
        sc = reader->root.sc;
    else if (sc_id > 0 && sc_id - 1 < reader->root.num_dependencies)
        sc = reader->root.dependent_scs[sc_id - 1];
    else
        fail_deserialize(tc, reader,
            "Invalid dependencies table index encountered (index %d)", sc_id);
    return sc;
}

/* Looks up an STable. */
static MVMSTable * lookup_stable(MVMThreadContext *tc, MVMSerializationReader *reader, MVMint32 sc_id, MVMint32 idx) {
    return MVM_sc_get_stable(tc, locate_sc(tc, reader, sc_id), idx);
}

/* Ensure that we aren't going to read off the end of the buffer. */
static void assert_can_read(MVMThreadContext *tc, MVMSerializationReader *reader, MVMint32 amount) {
    char *read_end = *(reader->cur_read_buffer) + *(reader->cur_read_offset) + amount;
    if (read_end > *(reader->cur_read_end))
        fail_deserialize(tc, reader,
            "Read past end of serialization data buffer");
}

/* Reading function for native integers. */
static MVMint64 read_int_func(MVMThreadContext *tc, MVMSerializationReader *reader) {
    MVMint64 result;
    assert_can_read(tc, reader, 8);
    result = read_int64(*(reader->cur_read_buffer), *(reader->cur_read_offset));
    *(reader->cur_read_offset) += 8;
    return result;
}

/* Reading function for native 32-bit integers. */
static MVMint32 read_int32_func(MVMThreadContext *tc, MVMSerializationReader *reader) {
    MVMint32 result;
    assert_can_read(tc, reader, 4);
    result = read_int32(*(reader->cur_read_buffer), *(reader->cur_read_offset));
    *(reader->cur_read_offset) += 4;
    return result;
}

/* Reading function for native 16-bit integers. */
static MVMint16 read_int16_func(MVMThreadContext *tc, MVMSerializationReader *reader) {
    MVMint16 result;
    assert_can_read(tc, reader, 2);
    result = read_int16(*(reader->cur_read_buffer), *(reader->cur_read_offset));
    *(reader->cur_read_offset) += 2;
    return result;
}

/* Reading function for native numbers. */
static MVMnum64 read_num_func(MVMThreadContext *tc, MVMSerializationReader *reader) {
    MVMnum64 result;
    assert_can_read(tc, reader, 8);
    result = read_double(*(reader->cur_read_buffer), *(reader->cur_read_offset));
    *(reader->cur_read_offset) += 8;
    return result;
}

/* Reading function for native strings. */
static MVMString * read_str_func(MVMThreadContext *tc, MVMSerializationReader *reader) {
    MVMString *result;
    assert_can_read(tc, reader, 4);
    result = read_string_from_heap(tc, reader,
        read_int32(*(reader->cur_read_buffer), *(reader->cur_read_offset)));
    *(reader->cur_read_offset) += 4;
    return result;
}

/* Reads in and resolves an object references. */
static MVMObject * read_obj_ref(MVMThreadContext *tc, MVMSerializationReader *reader) {
    MVMint32 sc_id, idx;

    assert_can_read(tc, reader, 8);
    sc_id = read_int32(*(reader->cur_read_buffer), *(reader->cur_read_offset));
    *(reader->cur_read_offset) += 4;
    idx = read_int32(*(reader->cur_read_buffer), *(reader->cur_read_offset));
    *(reader->cur_read_offset) += 4;

    return MVM_sc_get_object(tc, locate_sc(tc, reader, sc_id), idx);
}

/* Forward-declare read_ref_func. */
MVMObject * read_ref_func(MVMThreadContext *tc, MVMSerializationReader *reader);

/* Reads in an array of variant references. */
static MVMObject * read_array_var(MVMThreadContext *tc, MVMSerializationReader *reader) {
    MVMObject *result = MVM_repr_alloc_init(tc, tc->instance->boot_types->BOOTArray);
    MVMint32 elems, i;

    /* Read the element count. */
    assert_can_read(tc, reader, 4);
    elems = read_int32(*(reader->cur_read_buffer), *(reader->cur_read_offset));
    *(reader->cur_read_offset) += 4;

    /* Read in the elements. */
    for (i = 0; i < elems; i++)
        MVM_repr_bind_pos_o(tc, result, i, read_ref_func(tc, reader));

    return result;
}

/* Reads in an hash with string keys and variant references. */
static MVMObject * read_hash_str_var(MVMThreadContext *tc, MVMSerializationReader *reader) {
    MVMObject *result = MVM_repr_alloc_init(tc, tc->instance->boot_types->BOOTHash);
    MVMint32 elems, i;

    /* Read the element count. */
    assert_can_read(tc, reader, 4);
    elems = read_int32(*(reader->cur_read_buffer), *(reader->cur_read_offset));
    *(reader->cur_read_offset) += 4;

    /* Read in the elements. */
    for (i = 0; i < elems; i++) {
        MVMString *key = read_str_func(tc, reader);
        MVM_repr_bind_key_boxed(tc, result, key, read_ref_func(tc, reader));
    }

    /* Set the SC. */
    MVM_sc_set_obj_sc(tc, result, reader->root.sc);

    return result;
}

/* Reads in a code reference. */
static MVMObject * read_code_ref(MVMThreadContext *tc, MVMSerializationReader *reader) {
    MVMint32 sc_id, idx;

    assert_can_read(tc, reader, 8);
    sc_id = read_int32(*(reader->cur_read_buffer), *(reader->cur_read_offset));
    *(reader->cur_read_offset) += 4;
    idx = read_int32(*(reader->cur_read_buffer), *(reader->cur_read_offset));
    *(reader->cur_read_offset) += 4;

    return MVM_sc_get_code(tc, locate_sc(tc, reader, sc_id), idx);
}

/* Reading function for references. */
MVMObject * read_ref_func(MVMThreadContext *tc, MVMSerializationReader *reader) {
    MVMObject *result;

    /* Read the discriminator. */
    short discrim;
    assert_can_read(tc, reader, 2);
    discrim = read_int16(*(reader->cur_read_buffer), *(reader->cur_read_offset));
    *(reader->cur_read_offset) += 2;

    /* Decide what to do based on it. */
    switch (discrim) {
        case REFVAR_NULL:
            return NULL;
        case REFVAR_OBJECT:
            return read_obj_ref(tc, reader);
        case REFVAR_VM_NULL:
            return NULL;
        case REFVAR_VM_INT:
            result = MVM_repr_alloc_init(tc, tc->instance->boot_types->BOOTInt);
            MVM_repr_set_int(tc, result, read_int_func(tc, reader));
            return result;
        case REFVAR_VM_NUM:
            result = MVM_repr_alloc_init(tc, tc->instance->boot_types->BOOTNum);
            MVM_repr_set_num(tc, result, read_num_func(tc, reader));
            return result;
        case REFVAR_VM_STR:
            result = MVM_repr_alloc_init(tc, tc->instance->boot_types->BOOTStr);
            MVM_repr_set_str(tc, result, read_str_func(tc, reader));
            return result;
        case REFVAR_VM_ARR_VAR:
            return read_array_var(tc, reader);
        case REFVAR_VM_HASH_STR_VAR:
            return read_hash_str_var(tc, reader);
        case REFVAR_STATIC_CODEREF:
        case REFVAR_CLONED_CODEREF:
            return read_code_ref(tc, reader);
        default:
            fail_deserialize(tc, reader,
                "Serialization Error: Unimplemented case of read_ref");
    }
}

/* Reading function for STable references. */
static MVMSTable * read_stable_ref_func(MVMThreadContext *tc, MVMSerializationReader *reader) {
    MVMint32 sc_id, idx;

    assert_can_read(tc, reader, 8);
    sc_id = read_int32(*(reader->cur_read_buffer), *(reader->cur_read_offset));
    *(reader->cur_read_offset) += 4;
    idx = read_int32(*(reader->cur_read_buffer), *(reader->cur_read_offset));
    *(reader->cur_read_offset) += 4;

    return lookup_stable(tc, reader, sc_id, idx);
}

/* Checks the header looks sane and all of the places it points to make sense.
 * Also dissects the input string into the tables and data segments and populates
 * the reader data structure more fully. */
static void check_and_dissect_input(MVMThreadContext *tc,
        MVMSerializationReader *reader, MVMString *data_str) {
    /* Grab data from string. */
    size_t  data_len;
    /* XXX TODO: create an internals-only interface so a string can
     * be decoded into an existing buffer if it's big enough... then
     * cache that buffer on threadcontext to avoid one of the
     * allocations when decoding base64. */
    char   *data_b64 = (char *)MVM_string_ascii_encode(tc, data_str, NULL);
    /* XXX TODO: extend base64_decode to take a pointer to a pointer
     * to a destination buffer, and to decode to it if the buffer is
     * big enough... then cache this buffer on the threadcontext to
     * get rid of the other mallocation... */
    char   *data     = (char *)base64_decode(data_b64, &data_len);
    char   *prov_pos = data;
    char   *data_end = data + data_len;
    free(data_b64);

    /* Ensure we got the data. */
    if (data == NULL)
        fail_deserialize(tc, reader,
            "Failed to decode base64-encoded serialization data");
    reader->data = data;

    /* Ensure that we have enough space to read a version number and check it. */
    if (data_len < 4)
        fail_deserialize(tc, reader,
            "Serialized data too short to read a version number (< 4 bytes)");
    reader->root.version = read_int32(data, 0);
    if (reader->root.version < MIN_VERSION || reader->root.version > CURRENT_VERSION)
        fail_deserialize(tc, reader,
            "Unsupported serialization format version %d (current version is %d)",
            reader->root.version, CURRENT_VERSION);

    /* Ensure that the data is at least as long as the header is expected to be. */
    if (data_len < HEADER_SIZE)
        fail_deserialize(tc, reader,
            "Serialized data shorter than header (< %d bytes)", HEADER_SIZE);
    prov_pos += HEADER_SIZE;

    /* Get size and location of dependencies table. */
    reader->root.dependencies_table = data + read_int32(data, 4);
    reader->root.num_dependencies   = read_int32(data, 8);
    if (reader->root.dependencies_table < prov_pos)
        fail_deserialize(tc, reader,
            "Corruption detected (dependencies table starts before header ends)");
    prov_pos = reader->root.dependencies_table + reader->root.num_dependencies * DEP_TABLE_ENTRY_SIZE;
    if (prov_pos > data_end)
        fail_deserialize(tc, reader,
            "Corruption detected (dependencies table overruns end of data)");

    /* Get size and location of STables table. */
    reader->root.stables_table = data + read_int32(data, 12);
    reader->root.num_stables   = read_int32(data, 16);
    if (reader->root.stables_table < prov_pos)
        fail_deserialize(tc, reader,
            "Corruption detected (STables table starts before dependencies table ends)");
    prov_pos = reader->root.stables_table + reader->root.num_stables * STABLES_TABLE_ENTRY_SIZE;
    if (prov_pos > data_end)
        fail_deserialize(tc, reader,
            "Corruption detected (STables table overruns end of data)");

    /* Get location of STables data. */
    reader->root.stables_data = data + read_int32(data, 20);
    if (reader->root.stables_data < prov_pos)
        fail_deserialize(tc, reader,
            "Corruption detected (STables data starts before STables table ends)");
    prov_pos = reader->root.stables_data;
    if (prov_pos > data_end)
        fail_deserialize(tc, reader,
            "Corruption detected (STables data starts after end of data)");

    /* Get size and location of objects table. */
    reader->root.objects_table = data + read_int32(data, 24);
    reader->root.num_objects   = read_int32(data, 28);
    if (reader->root.objects_table < prov_pos)
        fail_deserialize(tc, reader,
            "Corruption detected (objects table starts before STables data ends)");
    prov_pos = reader->root.objects_table + reader->root.num_objects * OBJECTS_TABLE_ENTRY_SIZE;
    if (prov_pos > data_end)
        fail_deserialize(tc, reader,
            "Corruption detected (objects table overruns end of data)");

    /* Get location of objects data. */
    reader->root.objects_data = data + read_int32(data, 32);
    if (reader->root.objects_data < prov_pos)
        fail_deserialize(tc, reader,
            "Corruption detected (objects data starts before objects table ends)");
    prov_pos = reader->root.objects_data;
    if (prov_pos > data_end)
        fail_deserialize(tc, reader,
            "Corruption detected (objects data starts after end of data)");

    /* Get size and location of closures table. */
    reader->root.closures_table = data + read_int32(data, 36);
    reader->root.num_closures   = read_int32(data, 40);
    if (reader->root.closures_table < prov_pos)
        fail_deserialize(tc, reader,
            "Corruption detected (Closures table starts before objects data ends)");
    prov_pos = reader->root.closures_table + reader->root.num_closures * CLOSURES_TABLE_ENTRY_SIZE;
    if (prov_pos > data_end)
        fail_deserialize(tc, reader,
            "Corruption detected (Closures table overruns end of data)");

    /* Get size and location of contexts table. */
    reader->root.contexts_table = data + read_int32(data, 44);
    reader->root.num_contexts   = read_int32(data, 48);
    if (reader->root.contexts_table < prov_pos)
        fail_deserialize(tc, reader,
            "Corruption detected (contexts table starts before closures table ends)");
    prov_pos = reader->root.contexts_table + reader->root.num_contexts * CONTEXTS_TABLE_ENTRY_SIZE;
    if (prov_pos > data_end)
        fail_deserialize(tc, reader,
            "Corruption detected (contexts table overruns end of data)");

    /* Get location of contexts data. */
    reader->root.contexts_data = data + read_int32(data, 52);
    if (reader->root.contexts_data < prov_pos)
        fail_deserialize(tc, reader,
            "Corruption detected (contexts data starts before contexts table ends)");
    prov_pos = reader->root.contexts_data;
    if (prov_pos > data_end)
        fail_deserialize(tc, reader,
            "Corruption detected (contexts data starts after end of data)");

    /* Get size and location of repossessions table. */
    reader->root.repos_table = data + read_int32(data, 56);
    reader->root.num_repos   = read_int32(data, 60);
    if (reader->root.repos_table < prov_pos)
        fail_deserialize(tc, reader,
            "Corruption detected (repossessions table starts before contexts data ends)");
    prov_pos = reader->root.repos_table + reader->root.num_repos * REPOS_TABLE_ENTRY_SIZE;
    if (prov_pos > data_end)
        fail_deserialize(tc, reader,
            "Corruption detected (repossessions table overruns end of data)");

    /* Set reading limits for data chunks. */
    reader->stables_data_end = reader->root.objects_table;
    reader->objects_data_end = reader->root.closures_table;
    reader->contexts_data_end = reader->root.repos_table;
}

/* Goes through the dependencies table and resolves the dependencies that it
 * contains to SerializationContexts. */
static void resolve_dependencies(MVMThreadContext *tc, MVMSerializationReader *reader) {
    char      *table_pos = reader->root.dependencies_table;
    MVMuint32  num_deps  = reader->root.num_dependencies;
    MVMuint32  i;
    reader->root.dependent_scs = malloc(MAX(num_deps, 1) * sizeof(MVMSerializationContext *));
    for (i = 0; i < num_deps; i++) {
        MVMString *handle = read_string_from_heap(tc, reader, read_int32(table_pos, 0));
        MVMSerializationContext *sc;
        sc = MVM_sc_find_by_handle(tc, handle);
        if (sc == NULL) {
            MVMString *desc = read_string_from_heap(tc, reader, read_int32(table_pos, 4));
            if (!desc) desc = handle;
            fail_deserialize(tc, reader,
                "Missing or wrong version of dependency '%s'",
                MVM_string_ascii_encode(tc, desc, NULL));
        }
        reader->root.dependent_scs[i] = sc;
        table_pos += 8;
    }
}

/* Allocates STables that we need to deserialize, associating each one
 * with its REPR. */
static void stub_stables(MVMThreadContext *tc, MVMSerializationReader *reader) {
    MVMuint32  num_sts  = reader->root.num_stables;
    MVMuint32  i;
    for (i = 0; i < num_sts; i++) {
        /* Calculate location of STable's table row. */
        char *st_table_row = reader->root.stables_table + i * STABLES_TABLE_ENTRY_SIZE;

        /* Read in and look up representation. */
        MVMREPROps *repr = MVM_repr_get_by_name(tc,
            read_string_from_heap(tc, reader, read_int32(st_table_row, 0)));

        /* Allocate and store stub STable. */
        MVMSTable *st = MVM_gc_allocate_stable(tc, repr, NULL);
        MVM_sc_set_stable(tc, reader->root.sc, i, st);

        /* Set the STable's SC. */
        MVM_sc_set_stable_sc(tc, st, reader->root.sc);
    }
}

/* Goes through all STables and gets them to calculate their sizes, setting
 * the STable size field. */
static void set_stable_sizes(MVMThreadContext *tc, MVMSerializationReader *reader) {
    MVMuint32  num_sts  = reader->root.num_stables;
    MVMuint32  i;
    for (i = 0; i < num_sts; i++) {
        /* Grab STable. */
        MVMSTable *st = MVM_sc_get_stable(tc, reader->root.sc, i);

        /* Set STable read position, and set current read buffer to the
         * location of the REPR data. */
        char *st_table_row = reader->root.stables_table + i * STABLES_TABLE_ENTRY_SIZE;
        reader->stables_data_offset = read_int32(st_table_row, 8);

        reader->cur_read_buffer     = &(reader->root.stables_data);
        reader->cur_read_offset     = &(reader->stables_data_offset);
        reader->cur_read_end        = &(reader->stables_data_end);

        if (st->REPR->deserialize_stable_size)
            st->REPR->deserialize_stable_size(tc, st, reader);
        else
            fail_deserialize(tc, reader, "Missing deserialize_stable_size");
        if (st->size == 0)
            fail_deserialize(tc, reader, "STable with size zero after deserialization");
    }
}

/* Stubs all the objects we need to deserialize, setting their REPR and type
 * object flag. */
static void stub_objects(MVMThreadContext *tc, MVMSerializationReader *reader) {
    MVMuint32  num_objs = reader->root.num_objects;
    MVMuint32  i;
    for (i = 0; i < num_objs; i++) {
        /* Calculate location of object's table row. */
        char *obj_table_row = reader->root.objects_table + i * OBJECTS_TABLE_ENTRY_SIZE;

        /* Resolve the STable. */
        MVMSTable *st = lookup_stable(tc, reader,
            read_int32(obj_table_row, 0),   /* The SC in the dependencies table, + 1 */
            read_int32(obj_table_row, 4));  /* The index in that SC */

        /* Allocate and store stub object. */
        MVMObject *obj;
        if ((read_int32(obj_table_row, 12) & 1))
            obj = st->REPR->allocate(tc, st);
        else
            obj = MVM_gc_allocate_type_object(tc, st);
        MVM_sc_set_object(tc, reader->root.sc, i, obj);

        /* Set the object's SC. */
        MVM_sc_set_obj_sc(tc, obj, reader->root.sc);
    }
}

/* Deserializes a closure, though without attaching outer (that comes in a
 * later step). */
static void deserialize_closure(MVMThreadContext *tc, MVMSerializationReader *reader, MVMint32 i) {
    /* Calculate location of closure's table row. */
    char *table_row = reader->root.closures_table + i * CLOSURES_TABLE_ENTRY_SIZE;

    /* Resolve the reference to the static code object. */
    MVMuint32  static_sc_id = read_int32(table_row, 0);
    MVMuint32  static_idx   = read_int32(table_row, 4);
    MVMObject *static_code  = MVM_sc_get_code(tc,
        locate_sc(tc, reader, static_sc_id), static_idx);

    /* Clone it and add it to the SC's code refs list. */
    MVMObject *closure = MVM_repr_clone(tc, static_code);
    MVM_repr_push_o(tc, reader->codes_list, closure);

    /* Tag it as being in this SC. */
    MVM_sc_set_obj_sc(tc, closure, reader->root.sc);

    /* See if there's a code object we need to attach. */
    if (read_int32(table_row, 12)) {
        MVMObject *obj = MVM_sc_get_object(tc,
            locate_sc(tc, reader, read_int32(table_row, 16)),
            read_int32(table_row, 20));
        MVM_ASSIGN_REF(tc, closure, ((MVMCode *)closure)->body.code_object, obj);
    }
}

/* Deserializes a context. */
static void deserialize_context(MVMThreadContext *tc, MVMSerializationReader *reader, MVMint32 row) {
    MVMStaticFrame *sf;
    MVMFrame       *f;
    MVMint64        i, syms;

    /* Calculate location of context's table row. */
    char *table_row = reader->root.contexts_table + row * CONTEXTS_TABLE_ENTRY_SIZE;

    /* Resolve the reference to the static code object this context is for. */
    MVMuint32  static_sc_id = read_int32(table_row, 0);
    MVMuint32  static_idx   = read_int32(table_row, 4);
    MVMObject *static_code  = MVM_sc_get_code(tc,
        locate_sc(tc, reader, static_sc_id), static_idx);

    /* Create context. */
    sf = ((MVMCode *)static_code)->body.sf;
    f  = MVM_frame_create_context_only(tc, sf, static_code);

    /* Set context data read position, and set current read buffer to the correct thing. */
    reader->contexts_data_offset = read_int32(table_row, 8);
    reader->cur_read_buffer      = &(reader->root.contexts_data);
    reader->cur_read_offset      = &(reader->contexts_data_offset);
    reader->cur_read_end         = &(reader->contexts_data_end);

    /* Deserialize lexicals. */
    syms = reader->read_int(tc, reader);
    for (i = 0; i < syms; i++) {
        MVMString   *sym = reader->read_str(tc, reader);
        MVMRegister *lex = MVM_frame_lexical(tc, f, sym);
        switch (MVM_frame_lexical_primspec(tc, f, sym)) {
            case MVM_STORAGE_SPEC_BP_INT:
                lex->i64 = reader->read_int(tc, reader);
                break;
            case MVM_STORAGE_SPEC_BP_NUM:
                lex->n64 = reader->read_num(tc, reader);
                break;
            case MVM_STORAGE_SPEC_BP_STR:
                lex->s = reader->read_str(tc, reader);
                break;
            default:
                lex->o = reader->read_ref(tc, reader);
        }
    }

    /* Put context into contexts array (will be attached in a later pass). */
    reader->contexts[row] = f;
}

/* Attaches a closure's outer pointer. */
static void attach_closure_outer(MVMThreadContext *tc, MVMSerializationReader *reader, MVMint32 i, MVMObject *closure) {
    char      *row = reader->root.closures_table + i * CLOSURES_TABLE_ENTRY_SIZE;
    MVMuint32  idx = read_int32(row, 8);
    if (idx)
        ((MVMCode *)closure)->body.outer = MVM_frame_inc_ref(tc, reader->contexts[idx - 1]);
}

/* Attaches a context's outer pointer. */
static void attach_context_outer(MVMThreadContext *tc, MVMSerializationReader *reader, MVMint32 i, MVMFrame *context) {
    char      *row = reader->root.contexts_table + i * CONTEXTS_TABLE_ENTRY_SIZE;
    MVMuint32  idx = read_int32(row, 12);
    if (idx)
        context->outer = MVM_frame_inc_ref(tc, reader->contexts[idx - 1]);
}

/* Deserializes a single STable, along with its REPR data. */
static void deserialize_stable(MVMThreadContext *tc, MVMSerializationReader *reader, MVMint32 i, MVMSTable *st) {
    /* Calculate location of STable's table row. */
    char *st_table_row = reader->root.stables_table + i * STABLES_TABLE_ENTRY_SIZE;

    /* Set STable read position, and set current read buffer to the correct thing. */
    reader->stables_data_offset = read_int32(st_table_row, 4);
    reader->cur_read_buffer     = &(reader->root.stables_data);
    reader->cur_read_offset     = &(reader->stables_data_offset);
    reader->cur_read_end        = &(reader->stables_data_end);

    /* Read the HOW, WHAT and WHO. */
    MVM_ASSIGN_REF(tc, st, st->HOW, read_obj_ref(tc, reader));
    MVM_ASSIGN_REF(tc, st, st->WHAT, read_obj_ref(tc, reader));
    MVM_ASSIGN_REF(tc, st, st->WHO, read_ref_func(tc, reader));

    /* Method cache and v-table. */
    MVM_ASSIGN_REF(tc, st, st->method_cache, read_ref_func(tc, reader));
    st->vtable_length = read_int_func(tc, reader);
    if (st->vtable_length > 0)
        st->vtable = (MVMObject **)malloc(st->vtable_length * sizeof(MVMObject *));
    for (i = 0; i < st->vtable_length; i++)
        MVM_ASSIGN_REF(tc, st, st->vtable[i], read_ref_func(tc, reader));

    /* Type check cache. */
    st->type_check_cache_length = read_int_func(tc, reader);
    if (st->type_check_cache_length > 0) {
        st->type_check_cache = (MVMObject **)malloc(st->type_check_cache_length * sizeof(MVMObject *));
        for (i = 0; i < st->type_check_cache_length; i++)
            MVM_ASSIGN_REF(tc, st, st->type_check_cache[i], read_ref_func(tc, reader));
    }

    /* Mode flags. */
    st->mode_flags = read_int_func(tc, reader);

    /* Boolification spec. */
    if (read_int_func(tc, reader)) {
        st->boolification_spec = (MVMBoolificationSpec *)malloc(sizeof(MVMBoolificationSpec));
        st->boolification_spec->mode = read_int_func(tc, reader);
        MVM_ASSIGN_REF(tc, st, st->boolification_spec->method, read_ref_func(tc, reader));
    }

    /* Container spec. */
    if (read_int_func(tc, reader)) {
        fail_deserialize(tc, reader, "Container spec deserialization NYI");
    }

    /* Invocation spec. */
    if (read_int_func(tc, reader)) {
        fail_deserialize(tc, reader, "Invocation spec deserialization NYI");
    }

    /* If the REPR has a function to deserialize representation data, call it. */
    if (st->REPR->deserialize_repr_data)
        st->REPR->deserialize_repr_data(tc, st, reader);
}

/* Deserializes a single object. */
static void deserialize_object(MVMThreadContext *tc, MVMSerializationReader *reader, MVMint32 i, MVMObject *obj) {
    /* We've no more to do for type objects. */
    if (IS_CONCRETE(obj)) {
        /* Calculate location of object's table row. */
        char *obj_table_row = reader->root.objects_table + i * OBJECTS_TABLE_ENTRY_SIZE;

        /* Set current read buffer to the correct thing. */
        reader->cur_read_buffer = &(reader->root.objects_data);
        reader->cur_read_offset = &(reader->objects_data_offset);
        reader->cur_read_end    = &(reader->objects_data_end);

        /* Delegate to its deserialization REPR function. */
        reader->objects_data_offset = read_int32(obj_table_row, 8);
        if (REPR(obj)->deserialize)
            REPR(obj)->deserialize(tc, STABLE(obj), obj, OBJECT_BODY(obj), reader);
        else
            fail_deserialize(tc, reader, "Missing deserialize REPR function for %s",
                MVM_string_ascii_encode(tc, REPR(obj)->name, NULL));
    }
}

/* Takes serialized data, an empty SerializationContext to deserialize it into,
 * a strings heap and the set of static code refs for the compilation unit.
 * Deserializes the data into the required objects and STables. */
void MVM_serialization_deserialize(MVMThreadContext *tc, MVMSerializationContext *sc,
        MVMObject *string_heap, MVMObject *codes_static,
        MVMObject *repo_conflicts, MVMString *data) {
    MVMint32 scodes, i;

    /* Allocate and set up reader. */
    MVMSerializationReader *reader = calloc(1, sizeof(MVMSerializationReader));
    reader->root.sc          = sc;
    reader->root.string_heap = string_heap;

    /* Put reader functions in place. */
    reader->read_int        = read_int_func;
    reader->read_int32      = read_int32_func;
    reader->read_int16      = read_int16_func;
    reader->read_num        = read_num_func;
    reader->read_str        = read_str_func;
    reader->read_ref        = read_ref_func;
    reader->read_stable_ref = read_stable_ref_func;

    /* Put code root list into SC. We'll end up mutating it, but that's
     * probably fine. */
    MVM_sc_set_code_list(tc, sc, codes_static);
    reader->codes_list = codes_static;
    scodes = (MVMint32)MVM_repr_elems(tc, codes_static);

    /* TODO: Mark all the static code refs we've been provided with as static. */

    /* During deserialization, we allocate directly in generation 2. This
     * is because these objects are almost certainly going to be long lived,
     * but also because if we know that we won't end up moving the objects
     * we are working on during a deserialization run, it's a bunch easier
     * to have those partially constructed objects floating around. */
    MVM_gc_allocate_gen2_default_set(tc);

    /* Read header and dissect the data into its parts. */
    check_and_dissect_input(tc, reader, data);

    /* Resolve the SCs in the dependencies table. */
    resolve_dependencies(tc, reader);

    /* TODO: repossessing objects and STables. */

    /* Stub allocate all STables, and then have the appropriate REPRs do
     * the required size calculations. */
    stub_stables(tc, reader);
    set_stable_sizes(tc, reader);

    /* Stub allocate all objects. */
    stub_objects(tc, reader);

    /* Deserialize closures, deserialize contexts, then attach outers. */
    reader->contexts = malloc(reader->root.num_contexts * sizeof(MVMFrame *));
    for (i = 0; i < reader->root.num_closures; i++)
        deserialize_closure(tc, reader, i);
    for (i = 0; i < reader->root.num_contexts; i++)
        deserialize_context(tc, reader, i);
    for (i = 0; i < reader->root.num_closures; i++)
        attach_closure_outer(tc, reader, i,
            MVM_sc_get_code(tc, reader->root.sc, scodes + i));
    for (i = 0; i < reader->root.num_contexts; i++)
        attach_context_outer(tc, reader, i, reader->contexts[i]);

    /* Fully deserialize STables, along with their representation data. */
    for (i = 0; i < reader->root.num_stables; i++)
        deserialize_stable(tc, reader, i, MVM_sc_get_stable(tc, sc, i));

    /* Finish deserializing objects. */
    for (i = 0; i < reader->root.num_objects; i++)
        deserialize_object(tc, reader, i, MVM_sc_get_object(tc, sc, i));

    /* Clear up afterwards. */
    if (reader->data)
        free(reader->data);
    if (reader->contexts)
        free(reader->contexts);
    free(reader);

    /* Restore normal GC allocation. */
    MVM_gc_allocate_gen2_default_clear(tc);
}

/*

=item sha1

Computes the SHA-1 hash of string.

=cut

*/
MVMString * MVM_sha1(MVMThreadContext *tc, MVMString *str) {
    /* Grab the string as a C string. */
    char *cstr = MVM_string_utf8_encode_C_string(tc, str);

    /* Compute its SHA-1 and encode it. */
    SHA1_CTX      context;
    unsigned char digest[20];
    char          output[80];
    SHA1_Init(&context);
    SHA1_Update(&context, (unsigned char*)cstr, strlen(cstr));
    SHA1_Final(&context, digest);
    SHA1_DigestToHex(digest, output);

    /* Free the C-MVMString and put result into a new MVMString. */
    free(cstr);
    return MVM_decode_C_buffer_to_string(tc, tc->instance->VMString, output, 40, MVM_encoding_type_ascii);
}
