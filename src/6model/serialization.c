#include <moar.h>
#include <sha1.h>

#ifndef MAX
    #define MAX(x, y) ((y) > (x) ? (y) : (x))
#endif

/* Whether we deserialize lazily or not. */
#define MVM_SERIALIZATION_LAZY 1

/* Version of the serialization format that we are currently at and lowest
 * version we support. */
#define CURRENT_VERSION 22
#define MIN_VERSION     16

/* Various sizes (in bytes). */
#define HEADER_SIZE                 (4 * 18)
#define DEP_TABLE_ENTRY_SIZE        8
#define STABLES_TABLE_ENTRY_SIZE    12
#define OBJECTS_TABLE_ENTRY_SIZE    8
#define CLOSURES_TABLE_ENTRY_SIZE   24
#define CONTEXTS_TABLE_ENTRY_SIZE   16
#define REPOS_TABLE_ENTRY_SIZE      16

/* Some guesses. */
#define DEFAULT_STABLE_DATA_SIZE        4096
#define STABLES_TABLE_ENTRIES_GUESS     16
#define OBJECT_SIZE_GUESS               8
#define CLOSURES_TABLE_ENTRIES_GUESS    16
#define CONTEXTS_TABLE_ENTRIES_GUESS    4
#define DEFAULT_CONTEXTS_DATA_SIZE      1024
#define DEFAULT_PARAM_INTERNS_DATA_SIZE 128

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
#define REFVAR_SC_REF               13

/* For the packed format, for "small" values of si and idx */
#define OBJECTS_TABLE_ENTRY_SC_MASK     0x7FF
#define OBJECTS_TABLE_ENTRY_SC_IDX_MASK 0x000FFFFF
#define OBJECTS_TABLE_ENTRY_SC_MAX      0x7FE
#define OBJECTS_TABLE_ENTRY_SC_IDX_MAX  0x000FFFFF
#define OBJECTS_TABLE_ENTRY_SC_SHIFT    20
#define OBJECTS_TABLE_ENTRY_SC_OVERFLOW 0x7FF
#define OBJECTS_TABLE_ENTRY_IS_CONCRETE 0x80000000

/* In the main serialization data blobs we have 1 more bit to play with.
   The format is either 32 bits, with a packed value.
   or 32 bits with an overflow flag, 32 bits of ID, and 32 bits of index.
   The packed ID could be in the range 0..4094, the packed index 0..1048575.
   With these ranges, overflow isn't even needed for compiling the setting.
   An alternative format would be 8 bits of ID (so 0..254) and then 32 bits of
   index (0..65535), or 8 bits for an overflow flag, then 32 and 32.
   For this format, it turns out that currently for the setting, 296046 entries
   would pack into 3 bytes, and 59757 would overflow and need 9.
   296046 * 3 + 59757 * 9 == 1425951
   (296046 + 59757) * 4   == 1423212
   Hence that format is not quite as space efficient. */

#define PACKED_SC_IDX_MASK  0x000FFFFF
#define PACKED_SC_MAX       0xFFE
#define PACKED_SC_IDX_MAX   0x000FFFFF
#define PACKED_SC_SHIFT     20
#define PACKED_SC_OVERFLOW  0xFFF

#define STRING_HEAP_LOC_MAX             0x7FFFFFFF
#define STRING_HEAP_LOC_PACKED_MAX      0x00007FFF
#define STRING_HEAP_LOC_PACKED_OVERFLOW 0x00008000
#define STRING_HEAP_LOC_PACKED_LOW_MASK 0x0000FFFF
#define STRING_HEAP_LOC_PACKED_SHIFT    16

#define STABLE_BOOLIFICATION_SPEC_MODE_MASK 0x0F
#define STABLE_HAS_CONTAINER_SPEC           0x10
#define STABLE_HAS_INVOCATION_SPEC          0x20
#define STABLE_HAS_HLL_OWNER                0x40
#define STABLE_HAS_HLL_ROLE                 0x80

#define GROW_TABLE(table, alloc) \
        table = (char *)MVM_recalloc( \
            table, \
            alloc, \
            alloc * 2 \
        ); \
        alloc *= 2;

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

    char* str = (char*) MVM_malloc((size+3)*4/3 + 1);

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
    int n[4] = {-1, -1, -1, -1};

    size_t len = strlen(s);
    if (len % 4) {
        *data_len = 0;
        return NULL;
    }
    data = (unsigned char*) MVM_malloc(len/4*3);
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
         || (n[2] == -1 && n[3] != -1)) {
            MVM_free(data);
            return NULL;
        }

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

/* Writes an int32 into a buffer. */
static void write_int32(char *buffer, size_t offset, MVMint32 value) {
    memcpy(buffer + offset, &value, 4);
#ifdef MVM_BIGENDIAN
    switch_endian(buffer + offset, 4);
#endif
}

/* Writes a uint16 into a buffer. */
static void write_uint16(char *buffer, size_t offset, MVMuint16 value) {
    memcpy(buffer + offset, &value, 2);
#if MVM_BIGENDIAN
    switch_endian(buffer + offset, 2);
#endif
}

/* Writes an double into a buffer. */
static void write_double(char *buffer, size_t offset, double value) {
    memcpy(buffer + offset, &value, 8);
#ifdef MVM_BIGENDIAN
    switch_endian(buffer + offset, 8);
#endif
}

/* Adds an item to the MVMString heap if needed, and returns the index where
 * it may be found. */
static MVMint32 add_string_to_heap(MVMThreadContext *tc, MVMSerializationWriter *writer, MVMString *s) {
    if (s == NULL) {
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
static MVMuint32 get_sc_id(MVMThreadContext *tc, MVMSerializationWriter *writer, MVMSerializationContext *sc) {
    MVMint64 i, num_deps, offset;

    /* Easy if it's in the current SC. */
    if (writer->root.sc == sc)
        return 0;

    /* Otherwise, find it in our dependencies list. */
    num_deps = writer->root.num_dependencies;
    for (i = 0; i < num_deps; i++)
        if (writer->root.dependent_scs[i] == sc)
            return (MVMuint32)i + 1;

    /* Otherwise, need to add it to our dependencies list. Ensure there's
     * space in the dependencies table; grow if not. */
    offset = num_deps * DEP_TABLE_ENTRY_SIZE;
    if (offset + DEP_TABLE_ENTRY_SIZE > writer->dependencies_table_alloc) {
        GROW_TABLE(writer->root.dependencies_table, writer->dependencies_table_alloc);
    }

    /* Add dependency. */
    writer->root.dependent_scs = MVM_realloc(writer->root.dependent_scs, sizeof(MVMSerializationContext *) * (writer->root.num_dependencies + 1));
    writer->root.dependent_scs[writer->root.num_dependencies] = sc;
    write_int32(writer->root.dependencies_table, offset,
        add_string_to_heap(tc, writer, MVM_sc_get_handle(tc, sc)));
    write_int32(writer->root.dependencies_table, offset + 4,
        add_string_to_heap(tc, writer, MVM_sc_get_description(tc, sc)));
    writer->root.num_dependencies++;
    return writer->root.num_dependencies; /* Deliberately index + 1. */
}

#define OBJ_IS_NULL(obj) ((obj) == NULL)

/* Takes an STable. If it's already in an SC, returns information on how
 * to reference it. Otherwise, adds it to the current SC, effectively
 * placing it onto the work list. */
static void get_stable_ref_info(MVMThreadContext *tc, MVMSerializationWriter *writer,
                                MVMSTable *st, MVMuint32 *sc, MVMuint32 *sc_idx) {
    /* Add to this SC if needed. */
    if (MVM_sc_get_stable_sc(tc, st) == NULL) {
        MVM_sc_set_stable_sc(tc, st, writer->root.sc);
        MVM_sc_push_stable(tc, writer->root.sc, st);
    }

    /* Work out SC reference. */
    *sc     = get_sc_id(tc, writer, MVM_sc_get_stable_sc(tc, st));
    *sc_idx = (MVMuint32)MVM_sc_find_stable_idx(tc, MVM_sc_get_stable_sc(tc, st), st);
}

/* Expands current target storage as needed. */
static void expand_storage_if_needed(MVMThreadContext *tc, MVMSerializationWriter *writer, MVMint64 need) {
    if (*(writer->cur_write_offset) + need > *(writer->cur_write_limit)) {
        *(writer->cur_write_limit) *= 2;
        *(writer->cur_write_buffer) = (char *)MVM_realloc(*(writer->cur_write_buffer),
            *(writer->cur_write_limit));
    }
}

/* Writing function for null-terminated char array strings */
void MVM_serialization_write_cstr(MVMThreadContext *tc, MVMSerializationWriter *writer, char *string) {
    size_t len;
    if (string)
        len = strlen(string);
    else
        len = 0;
    if (len) {
        MVM_serialization_write_int(tc, writer, len);
        expand_storage_if_needed(tc, writer, len);
        memcpy(*(writer->cur_write_buffer) + *(writer->cur_write_offset), string, len);
        *(writer->cur_write_offset) += len;
    } else {
        MVM_serialization_write_int(tc, writer, 0);
    }
}

/* Writing function for variable sized integers. Writes out a 64 bit value
   using between 1 and 9 bytes. */
void MVM_serialization_write_int(MVMThreadContext *tc, MVMSerializationWriter *writer, MVMint64 value) {
    MVMuint8 storage_needed;
    char *buffer;
    size_t offset;

    if (value >= -1 && value <= 126) {
        storage_needed = 1;
    } else {
        const MVMint64 abs_val = value < 0 ? -value - 1 : value;

        if (abs_val <= 0x7FF)
            storage_needed = 2;
        else if (abs_val <= 0x000000000007FFFF)
            storage_needed = 3;
        else if (abs_val <= 0x0000000007FFFFFF)
            storage_needed = 4;
        else if (abs_val <= 0x00000007FFFFFFFF)
            storage_needed = 5;
        else if (abs_val <= 0x000007FFFFFFFFFFLL)
            storage_needed = 6;
        else if (abs_val <= 0x0007FFFFFFFFFFFFLL)
            storage_needed = 7;
        else if (abs_val <= 0x07FFFFFFFFFFFFFFLL)
            storage_needed = 8;
        else
            storage_needed = 9;
    }

    expand_storage_if_needed(tc, writer, storage_needed);

    buffer = *(writer->cur_write_buffer);
    offset = *(writer->cur_write_offset);

    if (storage_needed == 1) {
        buffer[offset] = 0x80 | (value + 129);
    } else if (storage_needed == 9) {
        buffer[offset++] = 0x00;
        memcpy(buffer + offset, &value, 8);
#ifdef MVM_BIGENDIAN
        switch_endian(buffer + offset, 8);
#endif
    } else {
        MVMuint8 rest = storage_needed - 1;
        MVMint64 nybble = value >> 8 * rest;
        /* All the other high bits should be the same as the top bit of the
           nybble we keep. Or we have a bug.  */
        assert((nybble >> 3) == 0
               || (nybble >> 3) == ~(MVMuint64)0);
        buffer[offset++] = (rest << 4) | (nybble & 0xF);
#ifdef MVM_BIGENDIAN
        memcpy(buffer + offset, (char *)&value + 8 - rest, rest);
        switch_endian(buffer + offset, rest);
#else
        memcpy(buffer + offset, &value, rest);
#endif
    }

    *(writer->cur_write_offset) += storage_needed;
}

/* Writing function for native numbers. */
void MVM_serialization_write_num(MVMThreadContext *tc, MVMSerializationWriter *writer, MVMnum64 value) {
    expand_storage_if_needed(tc, writer, 8);
    write_double(*(writer->cur_write_buffer), *(writer->cur_write_offset), value);
    *(writer->cur_write_offset) += 8;
}

/* Writing function for native strings. */
void MVM_serialization_write_str(MVMThreadContext *tc, MVMSerializationWriter *writer, MVMString *value) {
    MVMint32 heap_loc = add_string_to_heap(tc, writer, value);

    /* avoid warnings that heap_loc > STRING_HEAP_LOC_MAX is always false */
    if (!(heap_loc >= 0 && heap_loc <= STRING_HEAP_LOC_MAX))
        MVM_exception_throw_adhoc(tc,
                                  "Serialization error: string offset %d can't be serialized",
                                  heap_loc);

    if (heap_loc <= STRING_HEAP_LOC_PACKED_MAX) {
        expand_storage_if_needed(tc, writer, 2);
        write_uint16(*(writer->cur_write_buffer), *(writer->cur_write_offset),
                     heap_loc);
        *(writer->cur_write_offset) += 2;
    } else {
        expand_storage_if_needed(tc, writer, 4);
        write_uint16(*(writer->cur_write_buffer), *(writer->cur_write_offset),
                     (heap_loc >> STRING_HEAP_LOC_PACKED_SHIFT)
                     | STRING_HEAP_LOC_PACKED_OVERFLOW);
        *(writer->cur_write_offset) += 2;
        write_uint16(*(writer->cur_write_buffer), *(writer->cur_write_offset),
                     heap_loc & STRING_HEAP_LOC_PACKED_LOW_MASK);
        *(writer->cur_write_offset) += 2;
    }
}

/* Writes the ID, index pair that identifies an entry in a Serialization
   context. */
static void write_locate_sc_and_index(MVMThreadContext *tc, MVMSerializationWriter *writer, MVMint32 sc_id, MVMint32 idx) {
    if (sc_id <= PACKED_SC_MAX && idx <= PACKED_SC_IDX_MAX) {
        MVMuint32 packed = (sc_id << PACKED_SC_SHIFT) | (idx & PACKED_SC_IDX_MASK);
        MVM_serialization_write_int(tc, writer, packed);
    } else {
        MVMuint32 packed = PACKED_SC_OVERFLOW << PACKED_SC_SHIFT;
        MVM_serialization_write_int(tc, writer, packed);
        MVM_serialization_write_int(tc, writer, sc_id);
        MVM_serialization_write_int(tc, writer, idx);
    }
}

/* Writes an object reference. */
static void write_obj_ref(MVMThreadContext *tc, MVMSerializationWriter *writer, MVMObject *ref) {
    MVMint32 sc_id, idx;

    if (OBJ_IS_NULL(MVM_sc_get_obj_sc(tc, ref))) {
        /* This object doesn't belong to an SC yet, so it must be serialized as part of
         * this compilation unit. Add it to the work list. */
        MVM_sc_set_obj_sc(tc, ref, writer->root.sc);
        MVM_sc_push_object(tc, writer->root.sc, ref);
    }
    sc_id = get_sc_id(tc, writer, MVM_sc_get_obj_sc(tc, ref));
    idx   = (MVMint32)MVM_sc_find_object_idx(tc, MVM_sc_get_obj_sc(tc, ref), ref);
    write_locate_sc_and_index(tc, writer, sc_id, idx);
}

/* Writes an array where each item is a variant reference. */
static void write_array_var(MVMThreadContext *tc, MVMSerializationWriter *writer, MVMObject *arr) {
    MVMint32 elems = (MVMint32)MVM_repr_elems(tc, arr);
    MVMint32 i;

    /* Write out element count. */
    MVM_serialization_write_int(tc, writer, elems);

    /* Write elements. */
    for (i = 0; i < elems; i++)
        MVM_serialization_write_ref(tc, writer, MVM_repr_at_pos_o(tc, arr, i));
}

/* Writes an array where each item is an integer. */
static void write_array_int(MVMThreadContext *tc, MVMSerializationWriter *writer, MVMObject *arr) {
    MVMint32 elems = (MVMint32)MVM_repr_elems(tc, arr);
    MVMint32 i;

    /* Write out element count. */
    MVM_serialization_write_int(tc, writer, elems);

    /* Write elements. */
    for (i = 0; i < elems; i++)
        MVM_serialization_write_int(tc, writer, MVM_repr_at_pos_i(tc, arr, i));
}

/* Writes an array where each item is a MVMString. */
static void write_array_str(MVMThreadContext *tc, MVMSerializationWriter *writer, MVMObject *arr) {
    MVMint32 elems = (MVMint32)MVM_repr_elems(tc, arr);
    MVMint32 i;

    /* Write out element count. */
    MVM_serialization_write_int(tc, writer, elems);

    /* Write elements. */
    for (i = 0; i < elems; i++)
        MVM_serialization_write_str(tc, writer, MVM_repr_at_pos_s(tc, arr, i));
}

/* Writes a hash where each key is a MVMString and each value a variant reference. */
MVMThreadContext *cmp_tc;
static int cmp_strings(const void *s1, const void *s2) {
    return MVM_string_compare(cmp_tc, *(MVMString **)s1, *(MVMString **)s2);
}
static void write_hash_str_var(MVMThreadContext *tc, MVMSerializationWriter *writer, MVMObject *hash) {
    MVMint32 elems = (MVMint32)MVM_repr_elems(tc, hash);
    MVMString **keys = MVM_malloc(sizeof(MVMString *) * elems);
    MVMObject *iter = MVM_iter(tc, hash);
    MVMuint64 i = 0;

    /* Write out element count. */
    MVM_serialization_write_int(tc, writer, elems);

    /* Write elements, as key,value,key,value etc. */
    while (MVM_iter_istrue(tc, (MVMIter *)iter)) {
        MVM_repr_shift_o(tc, iter);
        keys[i++] = MVM_iterkey_s(tc, (MVMIter *)iter);
    }
    cmp_tc = tc;
    qsort(keys, elems, sizeof(MVMString*), cmp_strings);
    for (i = 0; i < elems; i++) {
        MVM_serialization_write_str(tc, writer, keys[i]);
        MVM_serialization_write_ref(tc, writer, MVM_repr_at_key_o(tc, hash, keys[i]));
    }
    MVM_free(keys);
}

/* Writes a reference to a code object in some SC. */
static void write_code_ref(MVMThreadContext *tc, MVMSerializationWriter *writer, MVMObject *code) {
    MVMSerializationContext *sc = MVM_sc_get_obj_sc(tc, code);
    MVMint32  sc_id   = get_sc_id(tc, writer, sc);
    MVMint32  idx     = (MVMint32)MVM_sc_find_code_idx(tc, sc, code);
    write_locate_sc_and_index(tc, writer, sc_id, idx);
}
MVM_NO_RETURN void throw_closure_serialization_error(MVMThreadContext *tc, MVMCode *closure, const char *message) MVM_NO_RETURN_ATTRIBUTE;
MVM_NO_RETURN void throw_closure_serialization_error(MVMThreadContext *tc, MVMCode *closure, const char *message) {
    MVMString *file;
    MVMint32 line;
    MVM_gc_enter_from_allocator(tc); /* opportunity for creating a heap snapshot for debugging */
    MVM_code_location_out(tc, (MVMObject *)closure, &file, &line);
    {
        char *c_name = MVM_string_utf8_encode_C_string(tc,
                (closure->body.sf)->body.name);
        char *c_file = MVM_string_utf8_encode_C_string(tc, file);
        char *waste[] = { c_name, c_file, NULL };
        MVM_exception_throw_adhoc_free(tc, waste,
            "Serialization Error: %s '%s' (%s:%d)",
            message, c_name, c_file, line);
    }
}

/* Given a closure, locate the static code reference it was originally cloned
 * from. */
static MVMObject * closure_to_static_code_ref(MVMThreadContext *tc, MVMObject *closure, MVMint64 fatal) {
    MVMObject *scr = (MVMObject *)(((MVMCode *)closure)->body.sf)->body.static_code;

    if (scr == NULL || MVM_sc_get_obj_sc(tc, scr) == NULL) {
        if (fatal) {
            throw_closure_serialization_error(
                tc,
                (MVMCode *)closure,
                "missing static code ref for closure"
            );
        }
        return NULL;
    }
    return scr;
}

/* Takes an outer context that is potentially to be serialized. Checks if it
 * is of interest, and if so sets it up to be serialized. */
static MVMint32 get_serialized_context_idx(MVMThreadContext *tc, MVMSerializationWriter *writer, MVMFrame *ctx, MVMCode *closure) {
     if (OBJ_IS_NULL(MVM_sc_get_frame_sc(tc, ctx))) {
        /* Make sure we should chase a level down. */
        if (OBJ_IS_NULL(closure_to_static_code_ref(tc, ctx->code_ref, 0))) {
            return 0;
        }
        else {
            if (writer->num_contexts == writer->alloc_contexts) {
                writer->alloc_contexts += 256;
                writer->contexts_list = MVM_realloc(writer->contexts_list,
                    writer->alloc_contexts * sizeof(MVMFrame *));
            }
            writer->contexts_list[writer->num_contexts++] = ctx;
            MVM_sc_set_frame_sc(tc, ctx, writer->root.sc);
            return (MVMint32)writer->num_contexts;
        }
    }
    else {
        MVMint64 i, c;
        if (MVM_sc_get_frame_sc(tc, ctx) != writer->root.sc)
            throw_closure_serialization_error(tc,
                closure,
                "reference to context outside of SC for"
            );
        c = writer->num_contexts;
        for (i = 0; i < c; i++)
            if (writer->contexts_list[i] == ctx)
                return (MVMint32)i + 1;
        throw_closure_serialization_error(
            tc,
            closure,
            "could not locate outer context in current SC for"
        );
    }
}

/* Takes a closure, that is to be serialized. Checks if it has an outer that is
 * of interest, and if so sets it up to be serialized. */
static MVMint32 get_serialized_outer_context_idx(MVMThreadContext *tc, MVMSerializationWriter *writer, MVMObject *closure) {
    if (((MVMCode *)closure)->body.is_compiler_stub)
        return 0;
    if (((MVMCode *)closure)->body.outer == NULL)
        return 0;
    return get_serialized_context_idx(tc, writer, ((MVMCode *)closure)->body.outer, (MVMCode *)closure);
}

/* Takes a closure that needs to be serialized. Makes an entry in the closures
 * table for it. Also adds it to this SC's code refs set and tags it with the
 * current SC. */
static void serialize_closure(MVMThreadContext *tc, MVMSerializationWriter *writer, MVMObject *closure) {
    MVMint32 static_sc_id, static_idx, context_idx;

    /* Locate the static code object. */
    MVMObject *static_code_ref = closure_to_static_code_ref(tc, closure, 1);
    MVMSerializationContext *static_code_sc = MVM_sc_get_obj_sc(tc, static_code_ref);

    /* Ensure there's space in the closures table; grow if not. */
    MVMint32 offset = writer->root.num_closures * CLOSURES_TABLE_ENTRY_SIZE;
    if (offset + CLOSURES_TABLE_ENTRY_SIZE > writer->closures_table_alloc) {
        GROW_TABLE(writer->root.closures_table, writer->closures_table_alloc);
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
    if (((MVMCode *)closure)->body.code_object) {
        MVMObject *code_obj = (MVMObject *)((MVMCode *)closure)->body.code_object;
        write_int32(writer->root.closures_table, offset + 12, 1);
        if (!MVM_sc_get_obj_sc(tc, code_obj)) {
            MVM_sc_set_obj_sc(tc, code_obj, writer->root.sc);
            MVM_sc_push_object(tc, writer->root.sc, code_obj);
        }
        write_int32(writer->root.closures_table, offset + 16,
            get_sc_id(tc, writer, MVM_sc_get_obj_sc(tc, code_obj)));
        write_int32(writer->root.closures_table, offset + 20,
            (MVMint32)MVM_sc_find_object_idx(tc, MVM_sc_get_obj_sc(tc, code_obj), code_obj));
    }
    else {
        write_int32(writer->root.closures_table, offset + 12, 0);
    }

    /* Increment count of closures in the table. */
    writer->root.num_closures++;

    /* Add the closure to this SC, and mark it as as being in it. */
    MVM_repr_push_o(tc, writer->codes_list, closure);
    MVM_sc_set_obj_sc(tc, closure, writer->root.sc);
}

/* Writing function for references to things. */
void MVM_serialization_write_ref(MVMThreadContext *tc, MVMSerializationWriter *writer, MVMObject *ref) {
    /* Work out what kind of thing we have and determine the discriminator. */
    /* Note, we could use 0xFF as the sentinel value, and 0 as a "valid" value.
     */
    MVMuint8 discrim = 0;
    if (ref == NULL) {
        discrim = REFVAR_NULL;
    }
    else if (ref == tc->instance->VMNull) {
        discrim = REFVAR_VM_NULL;
    }
    else if (REPR(ref)->ID == MVM_REPR_ID_MVMMultiCache) {
        discrim = REFVAR_VM_NULL;
    }
    else if (REPR(ref)->ID == MVM_REPR_ID_MVMOSHandle) {
        discrim = REFVAR_VM_NULL;
    }
    else if (REPR(ref)->ID == MVM_REPR_ID_Decoder && IS_CONCRETE(ref)) {
        discrim = REFVAR_VM_NULL;
    }
    else if (STABLE(ref) == STABLE(tc->instance->boot_types.BOOTInt) && IS_CONCRETE(ref)) {
        discrim = REFVAR_VM_INT;
    }
    else if (STABLE(ref) == STABLE(tc->instance->boot_types.BOOTNum) && IS_CONCRETE(ref)) {
        discrim = REFVAR_VM_NUM;
    }
    else if (STABLE(ref) == STABLE(tc->instance->boot_types.BOOTStr) && IS_CONCRETE(ref)) {
        discrim = REFVAR_VM_STR;
    }
    else if (STABLE(ref) == STABLE(tc->instance->boot_types.BOOTArray) && IS_CONCRETE(ref)) {
        discrim = REFVAR_VM_ARR_VAR;
    }
    else if (STABLE(ref) == STABLE(tc->instance->boot_types.BOOTIntArray) && IS_CONCRETE(ref)) {
        discrim = REFVAR_VM_ARR_INT;
    }
    else if (STABLE(ref) == STABLE(tc->instance->boot_types.BOOTStrArray) && IS_CONCRETE(ref)) {
        discrim = REFVAR_VM_ARR_STR;
    }
    else if (STABLE(ref) == STABLE(tc->instance->boot_types.BOOTHash) && IS_CONCRETE(ref)) {
        discrim = REFVAR_VM_HASH_STR_VAR;
    }
    else if (REPR(ref)->ID == MVM_REPR_ID_MVMCode && IS_CONCRETE(ref)) {
        if (MVM_sc_get_obj_sc(tc, ref) && ((MVMCode *)ref)->body.is_static) {
            /* Static code reference. */
            discrim = REFVAR_STATIC_CODEREF;
        }
        else if (MVM_sc_get_obj_sc(tc, ref)) {
            /* Closure, but already seen and serialization already handled. */
            discrim = REFVAR_CLONED_CODEREF;
        }
        else {
            /* Closure but didn't see it yet. Take care of its serialization, which
             * gets it marked with this SC. Then it's just a normal code ref that
             * needs serializing. */
            serialize_closure(tc, writer, ref);
            discrim = REFVAR_CLONED_CODEREF;
        }
    }
    else if (REPR(ref)->ID == MVM_REPR_ID_SCRef && IS_CONCRETE(ref)) {
        discrim = REFVAR_SC_REF;
    }
    else {
        discrim = REFVAR_OBJECT;
    }

    /* Write the discriminator. */
    expand_storage_if_needed(tc, writer, 1);
    *(*(writer->cur_write_buffer) + *(writer->cur_write_offset)) = discrim;
    ++*(writer->cur_write_offset);

    /* Now take appropriate action. */
    switch (discrim) {
        case REFVAR_NULL: break;
        case REFVAR_OBJECT:
            write_obj_ref(tc, writer, ref);
            break;
        case REFVAR_VM_NULL:
            /* Nothing to do for these. */
            break;
        case REFVAR_VM_INT:
            MVM_serialization_write_int(tc, writer, MVM_repr_get_int(tc, ref));
            break;
        case REFVAR_VM_NUM:
            MVM_serialization_write_num(tc, writer, MVM_repr_get_num(tc, ref));
            break;
        case REFVAR_VM_STR:
            MVM_serialization_write_str(tc, writer, MVM_repr_get_str(tc, ref));
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
        case REFVAR_SC_REF: {
            MVMString *handle = MVM_sc_get_handle(tc, (MVMSerializationContext *)ref);
            MVM_serialization_write_str(tc, writer, handle);
            break;
        }
        default:
            MVM_exception_throw_adhoc(tc,
                "Serialization Error: Unimplemented discriminator %d in MVM_serialization_read_ref",
                discrim);
    }
}

/* Writing function for references to STables. */
void MVM_serialization_write_stable_ref(MVMThreadContext *tc, MVMSerializationWriter *writer, MVMSTable *st) {
    MVMuint32 sc_id, idx;
    get_stable_ref_info(tc, writer, st, &sc_id, &idx);
    write_locate_sc_and_index(tc, writer, sc_id, idx);
}

/* Concatenates the various output segments into a single binary MVMString. */
static MVMObject * concatenate_outputs(MVMThreadContext *tc, MVMSerializationWriter *writer, MVMObject *type) {
    char      *output      = NULL;
    char      *output_b64  = NULL;
    MVMuint32  output_size = 0;
    MVMuint32  offset      = 0;
    MVMObject *result;

    /* Calculate total size. */
    output_size += MVM_ALIGN_SECTION(HEADER_SIZE);
    output_size += MVM_ALIGN_SECTION(writer->root.num_dependencies * DEP_TABLE_ENTRY_SIZE);
    output_size += MVM_ALIGN_SECTION(writer->root.num_stables * STABLES_TABLE_ENTRY_SIZE);
    output_size += MVM_ALIGN_SECTION(writer->stables_data_offset);
    output_size += MVM_ALIGN_SECTION(writer->root.num_objects * OBJECTS_TABLE_ENTRY_SIZE);
    output_size += MVM_ALIGN_SECTION(writer->objects_data_offset);
    output_size += MVM_ALIGN_SECTION(writer->root.num_closures * CLOSURES_TABLE_ENTRY_SIZE);
    output_size += MVM_ALIGN_SECTION(writer->root.num_contexts * CONTEXTS_TABLE_ENTRY_SIZE);
    output_size += MVM_ALIGN_SECTION(writer->contexts_data_offset);
    output_size += MVM_ALIGN_SECTION(writer->root.num_repos * REPOS_TABLE_ENTRY_SIZE);
    output_size += MVM_ALIGN_SECTION(writer->param_interns_data_offset);

    /* Allocate a buffer that size. */
    output = (char *)MVM_calloc(1, output_size);

    /* Write version into header. */
    write_int32(output, 0, CURRENT_VERSION);
    offset += MVM_ALIGN_SECTION(HEADER_SIZE);

    /* Put dependencies table in place and set location/rows in header. */
    write_int32(output, 4, offset);
    write_int32(output, 8, writer->root.num_dependencies);
    memcpy(output + offset, writer->root.dependencies_table,
        writer->root.num_dependencies * DEP_TABLE_ENTRY_SIZE);
    offset += MVM_ALIGN_SECTION(writer->root.num_dependencies * DEP_TABLE_ENTRY_SIZE);

    /* Put STables table in place, and set location/rows in header. */
    write_int32(output, 12, offset);
    write_int32(output, 16, writer->root.num_stables);
    memcpy(output + offset, writer->root.stables_table,
        writer->root.num_stables * STABLES_TABLE_ENTRY_SIZE);
    offset += MVM_ALIGN_SECTION(writer->root.num_stables * STABLES_TABLE_ENTRY_SIZE);

    /* Put STables data in place. */
    write_int32(output, 20, offset);
    memcpy(output + offset, writer->root.stables_data,
        writer->stables_data_offset);
    offset += MVM_ALIGN_SECTION(writer->stables_data_offset);

    /* Put objects table in place, and set location/rows in header. */
    write_int32(output, 24, offset);
    write_int32(output, 28, writer->root.num_objects);
    memcpy(output + offset, writer->root.objects_table,
        writer->root.num_objects * OBJECTS_TABLE_ENTRY_SIZE);
    offset += MVM_ALIGN_SECTION(writer->root.num_objects * OBJECTS_TABLE_ENTRY_SIZE);

    /* Put objects data in place. */
    write_int32(output, 32, offset);
    memcpy(output + offset, writer->root.objects_data,
        writer->objects_data_offset);
    offset += MVM_ALIGN_SECTION(writer->objects_data_offset);

    /* Put closures table in place, and set location/rows in header. */
    write_int32(output, 36, offset);
    write_int32(output, 40, writer->root.num_closures);
    memcpy(output + offset, writer->root.closures_table,
        writer->root.num_closures * CLOSURES_TABLE_ENTRY_SIZE);
    offset += MVM_ALIGN_SECTION(writer->root.num_closures * CLOSURES_TABLE_ENTRY_SIZE);

    /* Put contexts table in place, and set location/rows in header. */
    write_int32(output, 44, offset);
    write_int32(output, 48, writer->root.num_contexts);
    memcpy(output + offset, writer->root.contexts_table,
        writer->root.num_contexts * CONTEXTS_TABLE_ENTRY_SIZE);
    offset += MVM_ALIGN_SECTION(writer->root.num_contexts * CONTEXTS_TABLE_ENTRY_SIZE);

    /* Put contexts data in place. */
    write_int32(output, 52, offset);
    memcpy(output + offset, writer->root.contexts_data,
        writer->contexts_data_offset);
    offset += MVM_ALIGN_SECTION(writer->contexts_data_offset);

    /* Put repossessions table in place, and set location/rows in header. */
    write_int32(output, 56, offset);
    write_int32(output, 60, writer->root.num_repos);
    memcpy(output + offset, writer->root.repos_table,
        writer->root.num_repos * REPOS_TABLE_ENTRY_SIZE);
    offset += MVM_ALIGN_SECTION(writer->root.num_repos * REPOS_TABLE_ENTRY_SIZE);

    /* Put parameterized type intern data in place. */
    write_int32(output, 64, offset);
    write_int32(output, 68, writer->root.num_param_interns);
    memcpy(output + offset, writer->root.param_interns_data,
        writer->param_interns_data_offset);
    offset += MVM_ALIGN_SECTION(writer->param_interns_data_offset);

    /* Sanity check. */
    if (offset != output_size)
        MVM_exception_throw_adhoc(tc,
            "Serialization sanity check failed: offset != output_size");

    if (type) { /* nqp::serializetobuffer */
        int i = 0;
        result = REPR(type)->allocate(tc, STABLE(type));
        if (REPR(result)->initialize)
            REPR(result)->initialize(tc, STABLE(result), result, OBJECT_BODY(result));
        REPR(result)->pos_funcs.write_buf(tc, STABLE(result), result, OBJECT_BODY(result), output, 0, output_size);

        return result;
    }

    /* nqp::serialize */

    /* If we are compiling at present, then just stash the output for later
     * incorporation into the bytecode file. */
    if (tc->compiling_scs && MVM_repr_elems(tc, tc->compiling_scs) &&
            MVM_repr_at_pos_o(tc, tc->compiling_scs, 0) == (MVMObject *)writer->root.sc) {
        if (tc->serialized)
            MVM_free(tc->serialized);
        tc->serialized = output;
        tc->serialized_size = output_size;
        tc->serialized_string_heap = writer->root.string_heap;
        return NULL;
    }

    /* Base 64 encode. */
    output_b64 = base64_encode(output, output_size);
    MVM_free(output);
    if (output_b64 == NULL)
        MVM_exception_throw_adhoc(tc,
            "Serialization error: failed to convert to base64");

    /* Make a MVMString containing it. */
    result = (MVMObject *)MVM_string_ascii_decode_nt(tc, tc->instance->VMString, output_b64);
    MVM_free(output_b64);
    return result;
}

/* Serializes the possibly-not-deserialized HOW. */
static void serialize_how_lazy(MVMThreadContext *tc, MVMSerializationWriter *writer, MVMSTable *st) {
    if (st->HOW) {
        write_obj_ref(tc, writer, st->HOW);
    }
    else {
        MVMint32 sc_id = get_sc_id(tc, writer, st->HOW_sc);
        write_locate_sc_and_index(tc, writer, sc_id, st->HOW_idx);
    }
}

/* Adds an entry to the parameterized type intern section. */
static void add_param_intern(MVMThreadContext *tc, MVMSerializationWriter *writer,
                             MVMObject *type, MVMObject *ptype, MVMObject *params) {
    MVMint64 num_params, i;

    /* Save previous write buffer. */
    char      **orig_write_buffer = writer->cur_write_buffer;
    MVMuint32  *orig_write_offset = writer->cur_write_offset;
    MVMuint32  *orig_write_limit  = writer->cur_write_limit;

    /* Switch to intern data buffer. */
    writer->cur_write_buffer = &(writer->root.param_interns_data);
    writer->cur_write_offset = &(writer->param_interns_data_offset);
    writer->cur_write_limit  = &(writer->param_interns_data_alloc);

    /* Parametric type object reference. */
    write_obj_ref(tc, writer, ptype);

    /* Indexes in this SC of type object and STable. */
    expand_storage_if_needed(tc, writer, 12);
    if (MVM_sc_get_obj_sc(tc, type) != writer->root.sc)
        MVM_exception_throw_adhoc(tc,
            "Serialization error: parameterized type to intern not in current SC");
    write_int32(*(writer->cur_write_buffer), *(writer->cur_write_offset),
        MVM_sc_find_object_idx(tc, writer->root.sc, type));
    *(writer->cur_write_offset) += 4;
    if (MVM_sc_get_stable_sc(tc, STABLE(type)) != writer->root.sc)
        MVM_exception_throw_adhoc(tc,
            "Serialization error: STable of parameterized type to intern not in current SC");
    write_int32(*(writer->cur_write_buffer), *(writer->cur_write_offset),
        MVM_sc_find_stable_idx(tc, writer->root.sc, STABLE(type)));
    *(writer->cur_write_offset) += 4;

    /* Write parameter count and parameter object refs. */
    num_params = MVM_repr_elems(tc, params);
    write_int32(*(writer->cur_write_buffer), *(writer->cur_write_offset),
        (MVMint32)num_params);
    *(writer->cur_write_offset) += 4;
    for (i = 0; i < num_params; i++)
        write_obj_ref(tc, writer, MVM_repr_at_pos_o(tc, params, i));

    /* Increment number of parameterization interns. */
    writer->root.num_param_interns++;

    /* Restore original output buffer. */
    writer->cur_write_buffer = orig_write_buffer;
    writer->cur_write_offset = orig_write_offset;
    writer->cur_write_limit  = orig_write_limit;
}

/* This handles the serialization of an STable, and calls off to serialize
 * its representation data also. */

static void serialize_stable(MVMThreadContext *tc, MVMSerializationWriter *writer, MVMSTable *st) {
    MVMint64  i;
    MVMuint8  flags;

    /* Ensure there's space in the STables table; grow if not. */
    MVMint32 offset = writer->root.num_stables * STABLES_TABLE_ENTRY_SIZE;
    if (offset + STABLES_TABLE_ENTRY_SIZE > writer->stables_table_alloc) {
        GROW_TABLE(writer->root.stables_table, writer->stables_table_alloc);
    }

    /* Make STables table entry. */
    write_int32(writer->root.stables_table, offset, add_string_to_heap(tc, writer, tc->instance->repr_list[st->REPR->ID]->name));
    write_int32(writer->root.stables_table, offset + 4, writer->stables_data_offset);

    /* Increment count of stables in the table. */
    writer->root.num_stables++;

    /* Make sure we're going to write to the correct place. */
    writer->cur_write_buffer = &(writer->root.stables_data);
    writer->cur_write_offset = &(writer->stables_data_offset);
    writer->cur_write_limit  = &(writer->stables_data_alloc);

    /* Write HOW, WHAT and WHO. */
    serialize_how_lazy(tc, writer, st);
    write_obj_ref(tc, writer, st->WHAT);
    MVM_serialization_write_ref(tc, writer, st->WHO);

    /* Method cache and v-table. */
    if (!st->method_cache)
        MVM_serialization_finish_deserialize_method_cache(tc, st);
    MVM_serialization_write_ref(tc, writer, st->method_cache);

    /* Type check cache. */
    MVM_serialization_write_int(tc, writer, st->type_check_cache_length);
    for (i = 0; i < st->type_check_cache_length; i++)
        MVM_serialization_write_ref(tc, writer, st->type_check_cache[i]);

    /* Mode flags.
       These are stored as MVMuint16, but currently only the bottom 6 bits are
       used. Whilst we could store these as 2 bytes, we don't actually gain any
       future-proofing from that, because if we start assigning meaning to
       currently unused bits, then we have to bump the serialisation version
       *anyway*. If we didn't, older readers would encounter files with the
       newly used bits, but ignore whatever semantics those bits were meant to
       convey, and hence show buggy behaviour. And if we're bumping the
       serialisation version, then we can increase the storage size.  */
    if (st->mode_flags > 255) {
        MVM_exception_throw_adhoc(tc,
                                  "Serialization error: mode_flags %u out of range and can't be serialized",
                                  st->mode_flags);
    }
    expand_storage_if_needed(tc, writer, 1);
    *(*(writer->cur_write_buffer) + *(writer->cur_write_offset)) = st->mode_flags;
    ++*(writer->cur_write_offset);

    /* Boolification spec. */
    /* As this only needs 4 bits, also use the same byte to store various
       NULL/not-NULL flag bits. */
    if (st->boolification_spec) {
        if (st->boolification_spec->mode >= 0xF) {
            MVM_exception_throw_adhoc(tc,
                                  "Serialization error: boolification spec mode %u out of range and can't be serialized",
                                      st->boolification_spec->mode);
        }
        flags = st->boolification_spec->mode;
    } else {
        flags = 0xF;
    }
    if (st->container_spec != NULL)
        flags |= STABLE_HAS_CONTAINER_SPEC;
    if (st->invocation_spec != NULL)
        flags |= STABLE_HAS_INVOCATION_SPEC;
    if (st->hll_owner != NULL)
        flags |= STABLE_HAS_HLL_OWNER;
    if (st->hll_role != MVM_HLL_ROLE_NONE)
        flags |= STABLE_HAS_HLL_ROLE;

    expand_storage_if_needed(tc, writer, 1);
    *(*(writer->cur_write_buffer) + *(writer->cur_write_offset)) = flags;
    ++*(writer->cur_write_offset);

    if (st->boolification_spec) {
        MVM_serialization_write_ref(tc, writer, st->boolification_spec->method);
    }

    /* Container spec. */
    if (st->container_spec) {
        /* Write container spec name. */
        MVM_serialization_write_str(tc, writer,
            MVM_string_ascii_decode_nt(tc, tc->instance->VMString,
                st->container_spec->name));

        /* Give container spec a chance to serialize any data it wishes. */
        st->container_spec->serialize(tc, st, writer);
    }

    /* Invocation spec. */
    if (st->invocation_spec) {
        MVM_serialization_write_ref(tc, writer, st->invocation_spec->class_handle);
        MVM_serialization_write_str(tc, writer, st->invocation_spec->attr_name);
        MVM_serialization_write_int(tc, writer, st->invocation_spec->hint);
        MVM_serialization_write_ref(tc, writer, st->invocation_spec->invocation_handler);
        MVM_serialization_write_ref(tc, writer, st->invocation_spec->md_class_handle);
        MVM_serialization_write_str(tc, writer, st->invocation_spec->md_cache_attr_name);
        MVM_serialization_write_int(tc, writer, st->invocation_spec->md_cache_hint);
        MVM_serialization_write_str(tc, writer, st->invocation_spec->md_valid_attr_name);
        MVM_serialization_write_int(tc, writer, st->invocation_spec->md_valid_hint);
    }

    /* HLL owner. */
    if (st->hll_owner)
        MVM_serialization_write_str(tc, writer, st->hll_owner->name);

    /* HLL role */
    if (st->hll_role != MVM_HLL_ROLE_NONE) {
        MVM_serialization_write_int(tc, writer, st->hll_role);
    }

    /* If it's a parametric type, save parameterizer. */
    if (st->mode_flags & MVM_PARAMETRIC_TYPE)
        MVM_serialization_write_ref(tc, writer, st->paramet.ric.parameterizer);

    /* If it's a parameterized type, we may also need to make an intern table
     * entry as well as writing out the parameter details. */
    if (st->mode_flags & MVM_PARAMETERIZED_TYPE) {
        MVMint64 i, num_params;

        /* To deserve an entry in the intern table, we need that both the type
         * being parameterized and all of the arguments are from an SC other
         * than the one we're currently serializing. Otherwise, there is no
         * way the parameterized type in question could have been produced by
         * another compilation unit. We keep a counter of things, which should
         * add up to parameters + 1 if we need the intern entry. */
        MVMuint32 internability = 0;

        /* Write a reference to the type being parameterized, and increment the
         * internability if it's from a different SC (easier to check that after,
         * as writing the ref will be sure to mark it as being in this one if it
         * has no SC yet). */
        MVMObject *ptype  = st->paramet.erized.parametric_type;
        MVMObject *params = st->paramet.erized.parameters;
        MVM_serialization_write_ref(tc, writer, ptype);
        if (MVM_sc_get_obj_sc(tc, ptype) != writer->root.sc)
            internability++;

        /* Write the parameters. We write them like an array, but an element at a
         * time so we can check if an intern table entry is needed. */
        num_params = MVM_repr_elems(tc, params);
        /* This typically seems to have values between 1 and 3: */
        MVM_serialization_write_int(tc, writer, num_params);
        for (i = 0; i < num_params; i++) {
            /* Save where we were before writing this parameter. */
            size_t pre_write_mark = *(writer->cur_write_offset);

            /* Write parameter. */
            MVMObject *parameter = MVM_repr_at_pos_o(tc, params, i);
            MVM_serialization_write_ref(tc, writer, parameter);

            /* If what we write was an object reference and it's from another
             * SC, add to the internability count. */
            if (*(*(writer->cur_write_buffer) + pre_write_mark) == REFVAR_OBJECT)
                if (MVM_sc_get_obj_sc(tc, parameter) != writer->root.sc)
                    internability++;
        }

        /* Make intern table entry if needed. */
        if (internability == num_params + 1)
            add_param_intern(tc, writer, st->WHAT, ptype, params);
    }

    MVM_serialization_write_cstr(tc, writer, MVM_6model_get_stable_debug_name(tc, st));
    MVM_serialization_write_int(tc, writer, st->is_mixin_type);

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
    MVMuint32 sc;
    MVMuint32 sc_idx;
    MVMuint32 packed;
    get_stable_ref_info(tc, writer, STABLE(obj), &sc, &sc_idx);

    /* Ensure there's space in the objects table; grow if not. */
    offset = writer->root.num_objects * OBJECTS_TABLE_ENTRY_SIZE;
    if (offset + OBJECTS_TABLE_ENTRY_SIZE > writer->objects_table_alloc) {
        GROW_TABLE(writer->root.objects_table, writer->objects_table_alloc);
    }

    /* Increment count of objects in the table. */
    writer->root.num_objects++;

    /* Make sure we're going to write repr data to the correct place. */
    writer->cur_write_buffer = &(writer->root.objects_data);
    writer->cur_write_offset = &(writer->objects_data_offset);
    writer->cur_write_limit  = &(writer->objects_data_alloc);

    packed = IS_CONCRETE(obj) ? OBJECTS_TABLE_ENTRY_IS_CONCRETE : 0;

    if (sc <= OBJECTS_TABLE_ENTRY_SC_MAX && sc_idx <= OBJECTS_TABLE_ENTRY_SC_IDX_MAX) {
        packed |= (sc << OBJECTS_TABLE_ENTRY_SC_SHIFT) | sc_idx;
    } else {
        packed |= OBJECTS_TABLE_ENTRY_SC_OVERFLOW << OBJECTS_TABLE_ENTRY_SC_SHIFT;

        expand_storage_if_needed(tc, writer, 8);
        write_int32(*(writer->cur_write_buffer), *(writer->cur_write_offset), sc);
        *(writer->cur_write_offset) += 4;
        write_int32(*(writer->cur_write_buffer), *(writer->cur_write_offset), sc_idx);
        *(writer->cur_write_offset) += 4;
    }

    /* Make objects table entry. */
    write_int32(writer->root.objects_table, offset + 0, packed);
    write_int32(writer->root.objects_table, offset + 4, writer->objects_data_offset);

    /* Delegate to its serialization REPR function. */
    if (IS_CONCRETE(obj)) {
        if (REPR(obj)->serialize)
            REPR(obj)->serialize(tc, STABLE(obj), OBJECT_BODY(obj), writer);
        else
            MVM_exception_throw_adhoc(tc,
                "Missing serialize REPR function for REPR %s (%s)", REPR(obj)->name, MVM_6model_get_debug_name(tc, obj));
    }
}

/* This handles the serialization of a context, which means serializing
 * the stuff in its lexpad. */
static void serialize_context(MVMThreadContext *tc, MVMSerializationWriter *writer, MVMFrame *frame) {
    MVMint32 i, offset, static_sc_id, static_idx;

    /* Grab lexpad, which we'll serialize later on. */
    MVMStaticFrame *sf   = frame->static_info;
    MVMLexicalRegistry **lexnames = sf->body.lexical_names_list;

    /* Locate the static code ref this context points to. */
    MVMObject *static_code_ref = closure_to_static_code_ref(tc, frame->code_ref, 1);
    MVMSerializationContext *static_code_sc  = MVM_sc_get_obj_sc(tc, static_code_ref);
    if (OBJ_IS_NULL(static_code_sc))
        MVM_exception_throw_adhoc(tc,
            "Serialization Error: closure outer is a code object not in an SC");
    static_sc_id = get_sc_id(tc, writer, static_code_sc);
    static_idx   = (MVMint32)MVM_sc_find_code_idx(tc, static_code_sc, static_code_ref);

    /* Ensure there's space in the STables table; grow if not. */
    offset = writer->root.num_contexts * CONTEXTS_TABLE_ENTRY_SIZE;
    if (offset + CONTEXTS_TABLE_ENTRY_SIZE > writer->contexts_table_alloc) {
        GROW_TABLE(writer->root.contexts_table, writer->contexts_table_alloc);
    }

    /* Make contexts table entry. */
    write_int32(writer->root.contexts_table, offset, static_sc_id);
    write_int32(writer->root.contexts_table, offset + 4, static_idx);
    write_int32(writer->root.contexts_table, offset + 8, writer->contexts_data_offset);

    /* See if there's any relevant outer context, and if so set it up to
     * be serialized. */
    if (frame->outer)
        write_int32(writer->root.contexts_table, offset + 12,
            get_serialized_context_idx(tc, writer, frame->outer, NULL));
    else
        write_int32(writer->root.contexts_table, offset + 12, 0);

    /* Increment count of stables in the table. */
    writer->root.num_contexts++;

    /* Set up writer. */
    writer->cur_write_buffer = &(writer->root.contexts_data);
    writer->cur_write_offset = &(writer->contexts_data_offset);
    writer->cur_write_limit  = &(writer->contexts_data_alloc);

    /* Serialize lexicals. */
    MVM_serialization_write_int(tc, writer, sf->body.num_lexicals);
    for (i = 0; i < sf->body.num_lexicals; i++) {
        MVM_serialization_write_str(tc, writer, lexnames[i]->key);
        switch (sf->body.lexical_types[i]) {
            case MVM_reg_int8:
            case MVM_reg_int16:
            case MVM_reg_int32:
                MVM_exception_throw_adhoc(tc, "unsupported lexical type %s", MVM_reg_get_debug_name(tc, sf->body.lexical_types[i]));
                break;
            case MVM_reg_int64:
                MVM_serialization_write_int(tc, writer, frame->env[i].i64);
                break;
            case MVM_reg_num32:
                MVM_exception_throw_adhoc(tc, "unsupported lexical type %s", MVM_reg_get_debug_name(tc, sf->body.lexical_types[i]));
                break;
            case MVM_reg_num64:
                MVM_serialization_write_num(tc, writer, frame->env[i].n64);
                break;
            case MVM_reg_str:
                MVM_serialization_write_str(tc, writer, frame->env[i].s);
                break;
            case MVM_reg_obj:
                MVM_serialization_write_ref(tc, writer, frame->env[i].o);
                break;
            default:
                MVM_exception_throw_adhoc(tc, "unsupported lexical type %s", MVM_reg_get_debug_name(tc, sf->body.lexical_types[i]));
                break;
        }
    }
}

/* Goes through the list of repossessions and serializes them all. */
static void serialize_repossessions(MVMThreadContext *tc, MVMSerializationWriter *writer) {
    MVMint64 i;

    /* Obtain list of repossession object indexes and original SCs. */
    MVMObject *rep_indexes = writer->root.sc->body->rep_indexes;
    MVMObject *rep_scs     = writer->root.sc->body->rep_scs;

    /* Allocate table space, provided we've actually something to do. */
    writer->root.num_repos = MVM_repr_elems(tc, rep_indexes);
    if (writer->root.num_repos == 0)
        return;
    writer->root.repos_table = (char *)MVM_malloc(writer->root.num_repos * REPOS_TABLE_ENTRY_SIZE);

    /* Make entries. */
    for (i = 0; i < writer->root.num_repos; i++) {
        MVMint32 offset  = (MVMint32)(i * REPOS_TABLE_ENTRY_SIZE);
        MVMint32 obj_idx = (MVMint32)(MVM_repr_at_pos_i(tc, rep_indexes, i) >> 1);
        MVMint32 is_st   = MVM_repr_at_pos_i(tc, rep_indexes, i) & 1;
        MVMSerializationContext *orig_sc =
            (MVMSerializationContext *)MVM_repr_at_pos_o(tc, rep_scs, i);

        /* Work out original object's SC location. */
        MVMint32 orig_sc_id = get_sc_id(tc, writer, orig_sc);
        MVMint32 orig_idx   = (MVMint32)(is_st
            ? MVM_sc_find_stable_idx(tc, orig_sc, writer->root.sc->body->root_stables[obj_idx])
            : MVM_sc_find_object_idx(tc, orig_sc, writer->root.sc->body->root_objects[obj_idx]));

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
        MVMuint64 objects_todo  = writer->root.sc->body->num_objects;
        MVMuint64 contexts_todo = writer->num_contexts;

        /* Reset todo flag - if we do some work we'll go round again as it
         * may have generated more. */
        work_todo = 0;

        /* Serialize any STables on the todo list. */
        while (writer->stables_list_pos < stables_todo) {
            serialize_stable(tc, writer, writer->root.sc->body->root_stables[writer->stables_list_pos]);
            writer->stables_list_pos++;
            work_todo = 1;
        }

        /* Serialize any objects on the todo list. */
        while (writer->objects_list_pos < objects_todo) {
            serialize_object(tc, writer,
                writer->root.sc->body->root_objects[writer->objects_list_pos]);
            writer->objects_list_pos++;
            work_todo = 1;
        }

        /* Serialize any contexts on the todo list. */
        while (writer->contexts_list_pos < contexts_todo) {
            serialize_context(tc, writer, writer->contexts_list[writer->contexts_list_pos]);
            writer->contexts_list_pos++;
            work_todo = 1;
        }
    } while (work_todo);

    /* Finally, serialize repossessions table (this can't make any more
     * work, so is done as a separate step here at the end). */
    serialize_repossessions(tc, writer);
}

MVMObject * MVM_serialization_serialize(MVMThreadContext *tc, MVMSerializationContext *sc, MVMObject *empty_string_heap, MVMObject *type) {
    MVMSerializationWriter *writer;
    MVMObject *result   = NULL;
    MVMint32   sc_elems = (MVMint32)sc->body->num_objects;
    MVMint64 i = 0;
    MVMint64 seed_strings = MVM_repr_elems(tc, empty_string_heap);

    /* We don't sufficiently root things in here for the GC, so enforce gen2
     * allocation. */
    MVM_gc_allocate_gen2_default_set(tc);

    /* Set up writer with some initial settings. */
    writer                      = MVM_calloc(1, sizeof(MVMSerializationWriter));
    writer->root.version        = CURRENT_VERSION;
    writer->root.sc             = sc;
    writer->codes_list          = sc->body->root_codes;
    writer->root.string_heap    = empty_string_heap;
    writer->root.dependent_scs  = MVM_calloc(1, sizeof(MVMSerializationContext *));
    writer->seen_strings        = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTHash);

    for (i = 0; i < seed_strings; i++) {
        MVM_repr_bind_key_int(tc, writer->seen_strings, MVM_repr_at_pos_s(tc, empty_string_heap, i), i + 1);
    }

    /* Allocate initial memory space for storing serialized tables and data. */
    writer->dependencies_table_alloc = DEP_TABLE_ENTRY_SIZE * 4;
    writer->root.dependencies_table  = (char *)MVM_calloc(1, writer->dependencies_table_alloc);
    writer->stables_table_alloc      = STABLES_TABLE_ENTRY_SIZE * STABLES_TABLE_ENTRIES_GUESS;
    writer->root.stables_table       = (char *)MVM_calloc(1, writer->stables_table_alloc);
    writer->objects_table_alloc      = OBJECTS_TABLE_ENTRY_SIZE * MAX(sc_elems, 1);
    writer->root.objects_table       = (char *)MVM_calloc(1, writer->objects_table_alloc);
    writer->stables_data_alloc       = DEFAULT_STABLE_DATA_SIZE;
    writer->root.stables_data        = (char *)MVM_calloc(1, writer->stables_data_alloc);
    writer->objects_data_alloc       = OBJECT_SIZE_GUESS * MAX(sc_elems, 1);
    writer->root.objects_data        = (char *)MVM_calloc(1, writer->objects_data_alloc);
    writer->closures_table_alloc     = CLOSURES_TABLE_ENTRY_SIZE * CLOSURES_TABLE_ENTRIES_GUESS;
    writer->root.closures_table      = (char *)MVM_calloc(1, writer->closures_table_alloc);
    writer->contexts_table_alloc     = CONTEXTS_TABLE_ENTRY_SIZE * CONTEXTS_TABLE_ENTRIES_GUESS;
    writer->root.contexts_table      = (char *)MVM_calloc(1, writer->contexts_table_alloc);
    writer->contexts_data_alloc      = DEFAULT_CONTEXTS_DATA_SIZE;
    writer->root.contexts_data       = (char *)MVM_calloc(1, writer->contexts_data_alloc);
    writer->param_interns_data_alloc = DEFAULT_PARAM_INTERNS_DATA_SIZE;
    writer->root.param_interns_data  = (char *)MVM_calloc(1, writer->param_interns_data_alloc);

    /* Initialize MVMString heap so first entry is the NULL MVMString. */
    MVM_repr_unshift_s(tc, empty_string_heap, NULL);

    /* Start serializing. */
    serialize(tc, writer);

    /* Build a single result out of the serialized data; note if we're in the
     * compiler pipeline this will return null and stash the output to write
     * to a bytecode file later. */
    result = concatenate_outputs(tc, writer, type);

    /* Clear up afterwards. */
    MVM_free(writer->contexts_list);
    MVM_free(writer->root.dependent_scs);
    MVM_free(writer->root.dependencies_table);
    MVM_free(writer->root.stables_table);
    MVM_free(writer->root.stables_data);
    MVM_free(writer->root.objects_table);
    MVM_free(writer->root.objects_data);
    MVM_free(writer->root.closures_table);
    MVM_free(writer->root.contexts_table);
    MVM_free(writer->root.contexts_data);
    MVM_free(writer->root.param_interns_data);
    MVM_free(writer->root.repos_table);
    MVM_free(writer);

    /* Exit gen2 allocation. */
    MVM_gc_allocate_gen2_default_clear(tc);

    return result;
}


/* ***************************************************************************
 * Deserialization (reading related)
 * ***************************************************************************/

/* Reads an int64 from a buffer. */
static MVMint64 read_int64(const char *buffer, size_t offset) {
    MVMint64 value;
    memcpy(&value, buffer + offset, 8);
#ifdef MVM_BIGENDIAN
    switch_endian(&value, 8);
#endif
    return value;
}

/* Reads an int32 from a buffer. */
static MVMint32 read_int32(const char *buffer, size_t offset) {
    MVMint32 value;
    memcpy(&value, buffer + offset, 4);
#ifdef MVM_BIGENDIAN
    switch_endian(&value, 4);
#endif
    return value;
}

static MVMuint16 read_uint16(const char *buffer, size_t offset) {
    MVMuint16 value;
    memcpy(&value, buffer + offset, 2);
#ifdef MVM_BIGENDIAN
    switch_endian(&value, 2);
#endif
    return value;
}

/* Reads double from a buffer. */
static MVMnum64 read_double(const char *buffer, size_t offset) {
    MVMnum64 value;
    memcpy(&value, buffer + offset, 8);
#ifdef MVM_BIGENDIAN
    switch_endian(&value, 8);
#endif
    return value;
}

/* If deserialization should fail, cleans up before throwing an exception. */
MVM_NO_RETURN static void fail_deserialize(MVMThreadContext *tc, char **waste, MVMSerializationReader *reader,
                             const char *messageFormat, ...) MVM_NO_RETURN_ATTRIBUTE MVM_FORMAT(printf, 4, 5);
MVM_NO_RETURN static void fail_deserialize(MVMThreadContext *tc, char **waste, MVMSerializationReader *reader,
        const char *messageFormat, ...) {
    va_list args;
    if (reader->data_needs_free && reader->data)
        MVM_free(reader->data);
    if (reader->contexts)
        MVM_free(reader->contexts);
    if (reader->root.sc)
        reader->root.sc->body->sr = NULL;
    if (reader->root.dependent_scs)
        MVM_free(reader->root.dependent_scs);
    MVM_free(reader);
    MVM_gc_allocate_gen2_default_clear(tc);
    va_start(args, messageFormat);
    MVM_exception_throw_adhoc_free_va(tc, waste, messageFormat, args);
    va_end(args);
}

/* Reads the item from the string heap at the specified index. */
static MVMString * read_string_from_heap(MVMThreadContext *tc, MVMSerializationReader *reader, MVMuint32 idx) {
    if (reader->root.string_heap) {
        if (idx < MVM_repr_elems(tc, reader->root.string_heap))
            return MVM_repr_at_pos_s(tc, reader->root.string_heap, idx);
        else
            fail_deserialize(tc, NULL, reader,
                "Attempt to read past end of string heap (index %d)", idx);
    }
    else {
        MVMCompUnit *cu = reader->root.string_comp_unit;
        if (idx == 0)
            return NULL;
        idx--;
        if (idx < cu->body.num_strings)
            return MVM_cu_string(tc, cu, idx);
        else
            fail_deserialize(tc, NULL, reader,
                "Attempt to read past end of compilation unit string heap (index %d)", idx);
    }
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
        fail_deserialize(tc, NULL, reader,
            "Invalid dependencies table index encountered (index %d)", sc_id);
    return sc;
}

/* Ensure that we aren't going to read off the end of the buffer. */
MVM_STATIC_INLINE void assert_can_read(MVMThreadContext *tc, MVMSerializationReader *reader, MVMint32 amount) {
    char *read_end = *(reader->cur_read_buffer) + *(reader->cur_read_offset) + amount;
    if (read_end > *(reader->cur_read_end))
        fail_deserialize(tc, NULL, reader,
            "Read past end of serialization data buffer");
    if (*(reader->cur_read_offset) < 0)
        fail_deserialize(tc, NULL, reader,
            "Read before start of serialization data buffer");
}

/* Reading function for native integers. */
MVMint64 MVM_serialization_read_int64(MVMThreadContext *tc, MVMSerializationReader *reader) {
    MVMint64 result;
    assert_can_read(tc, reader, 8);
    result = read_int64(*(reader->cur_read_buffer), *(reader->cur_read_offset));
    *(reader->cur_read_offset) += 8;
    return result;
}

/* Reading function for variable-sized integers, using between 1 and 9 bytes of
 * storage for an int64.
 *
 * The format chosen may not be quite the most space efficient for the values
 * that we store, but the intent it is that close to smallest whilst very
 * efficient to read. In particular, it doesn't require any looping, and
 * has at most two length overrun checks.  */

MVMint64 MVM_serialization_read_int(MVMThreadContext *tc, MVMSerializationReader *reader) {
    MVMint64 result;
    const MVMuint8 *read_at = (MVMuint8 *) *(reader->cur_read_buffer) + *(reader->cur_read_offset);
    MVMuint8 *const read_end = (MVMuint8 *) *(reader->cur_read_end);
    MVMuint8 first;
    MVMuint8 need;

    assert_can_read(tc, reader, 1);

    if (read_at >= read_end)
        fail_deserialize(tc, NULL, reader,
                         "Read past end of serialization data buffer");

    first = *read_at++;

    /* Top bit set means remaining 7 bits are a value between -1 and 126.
       (That turns out to be the most common 7 bit range that we serialize.)  */
    if (first & 0x80) {
        *(reader->cur_read_offset) += 1;
        /* Value we have is 128 to 255. Map it back to the range we need:  */
        return (MVMint64) first - 129;
    }

    /* Otherwise next 3 bits indicate how many more bytes follow. */
    need = first >> 4;
    if (!need) {
        /* Have to read all 8 bytes. Ignore the bottom nybble.
           In future, we may want to use it to also store 15 possible "common"
           values. Not clear if that whould be best as a fixed table, a single
           table sent as part of the serialization blob, or multiple tables for
           different contexts (int32, int64, nativeint, others?)  */
        assert_can_read(tc, reader, 9);
#ifdef MVM_CAN_UNALIGNED_INT64
        *((MVMuint64*)&result) = *((MVMuint64*)read_at);
#else
        memcpy(&result, read_at, 8);
#endif
#ifdef MVM_BIGENDIAN
        switch_endian(&result, 8);
#endif
        *(reader->cur_read_offset) += 9;
        return result;
    }

    if (read_at + need > read_end)
        fail_deserialize(tc, NULL, reader,
                         "Read past end of serialization data buffer");

    /* The bottom nybble of the first byte is the highest byte of the final
       value with any bits set. Right now the top nybble is garbage, but it
       gets flushed away with the sign extension shifting later.  */
    result = (MVMint64)first << 8 * need;

    /* The remaining 1 to 7 lower bytes follow next in the serialization stream.
     */
#ifdef MVM_BIGENDIAN
    {
        MVMuint8 *write_to = (MVMuint8 *)&result + 8 - need;
        memcpy(write_to, read_at, need);
        switch_endian(write_to, need);
    }
#else
#   ifdef MVM_CAN_UNALIGNED_INT64
    /* GCC and Clang both optimize this */
    switch (MVM_EXPECT(need, 2)) {
        case 7:
            ((MVMuint8*)&result)[6] = read_at[6];
        case 6:
            ((MVMuint8*)&result)[5] = read_at[5];
        case 5:
            ((MVMuint8*)&result)[4] = read_at[4];
        case 4:
            ((MVMuint8*)&result)[3] = read_at[3];
        case 3:
            ((MVMuint8*)&result)[2] = read_at[2];
        case 2:
            ((MVMuint8*)&result)[1] = read_at[1];
        case 1:
            ((MVMuint8*)&result)[0] = read_at[0];
            break;
    }
#   else
    memcpy(&result, read_at, need);
#   endif
#endif

    /* Having pieced the (unsigned) value back together, sign extend it:  */
    result = result << (64 - 4 - 8 * need);
    result = result >> (64 - 4 - 8 * need);

    *(reader->cur_read_offset) += need + 1;
    return result;
}

/* Reading function for native numbers. */
MVMnum64 MVM_serialization_read_num(MVMThreadContext *tc, MVMSerializationReader *reader) {
    MVMnum64 result;
    assert_can_read(tc, reader, 8);
    result = read_double(*(reader->cur_read_buffer), *(reader->cur_read_offset));
    *(reader->cur_read_offset) += 8;
    return result;
}

/* Reading function for native strings.
 *
 * BEWARE - logic in this function is partly duplicated in the skip calculations
 * of deserialize_method_cache_lazy(). See the note before
 * MVM_serialization_read_ref(). */
MVMString * MVM_serialization_read_str(MVMThreadContext *tc, MVMSerializationReader *reader) {
    MVMint32 offset;

    assert_can_read(tc, reader, 2);
    offset = read_uint16(*(reader->cur_read_buffer), *(reader->cur_read_offset));
    *(reader->cur_read_offset) += 2;
    if (offset & STRING_HEAP_LOC_PACKED_OVERFLOW) {
        assert_can_read(tc, reader, 2);
        offset ^= STRING_HEAP_LOC_PACKED_OVERFLOW;
        offset <<= STRING_HEAP_LOC_PACKED_SHIFT;
        offset |= read_uint16(*(reader->cur_read_buffer), *(reader->cur_read_offset));
        *(reader->cur_read_offset) += 2;
    }
    return read_string_from_heap(tc, reader, offset);
}

/* Reading function for null-terminated char array strings */
char *MVM_serialization_read_cstr(MVMThreadContext *tc, MVMSerializationReader *reader) {
    MVMint64 len = MVM_serialization_read_int(tc, reader);
    char *strbuf = 0;
    if (len > 0) {
        const MVMuint8 *read_at = (MVMuint8 *) *(reader->cur_read_buffer) + *(reader->cur_read_offset);
        assert_can_read(tc, reader, len);
        strbuf = MVM_malloc(len + 1);
        if (strbuf == 0)
            fail_deserialize(tc, NULL, reader, "Cannot read a c string: malloc failed.");
        memcpy(strbuf, read_at, len);
        strbuf[len] = 0;
        *(reader->cur_read_offset) += len;
    } else if (len < 0) {
        fail_deserialize(tc, NULL, reader, "Cannot read a c string with negative length %"PRIi64".", len);
    }
    return strbuf;
}

/* The SC id,idx pair is used in various ways, but common to them all is to
   look up the SC, then use the index to call some other function. Putting the
   common parts into one function permits the serialized representation to be
   changed, but frustratingly it requires two return values, which is a bit of
   a pain in (real) C. Hence this rather ungainly function.

   BEWARE - logic in this function is partly duplicated in the skip calculations
   of deserialize_method_cache_lazy(). See the note before
   MVM_serialization_read_ref(). */
MVM_STATIC_INLINE MVMSerializationContext * read_locate_sc_and_index(MVMThreadContext *tc, MVMSerializationReader *reader, MVMint32 *idx) {
    MVMint32 sc_id;
    MVMuint32 packed;

    if (reader->root.version >= 19) {
        packed = MVM_serialization_read_int(tc, reader);
    } else {
        assert_can_read(tc, reader, 4);
        packed = read_int32(*(reader->cur_read_buffer), *(reader->cur_read_offset));
        *(reader->cur_read_offset) += 4;
    }

    sc_id = packed >> PACKED_SC_SHIFT;
    if (sc_id != PACKED_SC_OVERFLOW) {
        *idx = packed & PACKED_SC_IDX_MASK;
    } else {
        if (reader->root.version >= 19) {
            sc_id = MVM_serialization_read_int(tc, reader);
            *idx = MVM_serialization_read_int(tc, reader);
        } else {
            assert_can_read(tc, reader, 8);
            sc_id = read_int32(*(reader->cur_read_buffer), *(reader->cur_read_offset));
            *(reader->cur_read_offset) += 4;
            *idx = read_int32(*(reader->cur_read_buffer), *(reader->cur_read_offset));
            *(reader->cur_read_offset) += 4;
        }
    }

    return locate_sc(tc, reader, sc_id);
}

/* Reads in and resolves an object references.
 *
 * BEWARE - logic in this function is partly duplicated in the skip calculations
 * of deserialize_method_cache_lazy(). See the note before
 * MVM_serialization_read_ref(). */
static MVMObject * read_obj_ref(MVMThreadContext *tc, MVMSerializationReader *reader) {
    MVMint32 idx;
    MVMSerializationContext *sc = read_locate_sc_and_index(tc, reader, &idx);
    /* sequence point... */
    return MVM_sc_get_object(tc, sc, idx);
}

/* Reads in an array of variant references. */
static MVMObject * read_array_var(MVMThreadContext *tc, MVMSerializationReader *reader) {
    MVMObject *result = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTArray);
    MVMint32 elems, i;

    /* Read the element count. */
    elems = MVM_serialization_read_int(tc, reader);

    /* Read in the elements. */
    for (i = 0; i < elems; i++)
        MVM_repr_bind_pos_o(tc, result, i, MVM_serialization_read_ref(tc, reader));

    /* Set the SC. */
    MVM_sc_set_obj_sc(tc, result, reader->root.sc);

    return result;
}

/* Reads in an hash with string keys and variant references.
 *
 * BEWARE - logic in this function is partly duplicated in the skip calculations
 * of deserialize_method_cache_lazy(). See the note before
 * MVM_serialization_read_ref(). */
static MVMObject * read_hash_str_var(MVMThreadContext *tc, MVMSerializationReader *reader) {
    MVMObject *result = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTHash);
    MVMint32 elems, i;

    /* Read the element count. */
    if (reader->root.version >= 19) {
        elems = MVM_serialization_read_int(tc, reader);
    } else {
        assert_can_read(tc, reader, 4);
        elems = read_int32(*(reader->cur_read_buffer), *(reader->cur_read_offset));
        *(reader->cur_read_offset) += 4;
    }

    /* Read in the elements. */
    for (i = 0; i < elems; i++) {
        MVMString *key = MVM_serialization_read_str(tc, reader);
        MVM_repr_bind_key_o(tc, result, key, MVM_serialization_read_ref(tc, reader));
    }

    /* Set the SC. */
    MVM_sc_set_obj_sc(tc, result, reader->root.sc);

    return result;
}

/* Reads in an array of integers. */
static MVMObject * read_array_int(MVMThreadContext *tc, MVMSerializationReader *reader) {
    MVMObject *result = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTIntArray);
    MVMint64 elems, i;

    /* Read the element count. */
    elems = MVM_serialization_read_int(tc, reader);

    /* Read in the elements. */
    for (i = 0; i < elems; i++)
        MVM_repr_bind_pos_i(tc, result, i, MVM_serialization_read_int(tc, reader));

    return result;
}

/* Reads in an array of strings. */
static MVMObject * read_array_str(MVMThreadContext *tc, MVMSerializationReader *reader) {
    MVMObject *result = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTStrArray);
    MVMint32 elems, i;

    /* Read the element count. */
    if (reader->root.version >= 19) {
        elems = MVM_serialization_read_int(tc, reader);
    } else {
        assert_can_read(tc, reader, 4);
        elems = read_int32(*(reader->cur_read_buffer), *(reader->cur_read_offset));
        *(reader->cur_read_offset) += 4;
    }

    /* Read in the elements. */
    for (i = 0; i < elems; i++)
        MVM_repr_bind_pos_s(tc, result, i, MVM_serialization_read_str(tc, reader));

    return result;
}

/* Reads in a code reference.
 *
 * BEWARE - logic in this function is partly duplicated in the skip calculations
 * of deserialize_method_cache_lazy(). See the note before
 * MVM_serialization_read_ref(). */
static MVMObject * read_code_ref(MVMThreadContext *tc, MVMSerializationReader *reader) {
    MVMint32 idx;
    MVMSerializationContext *sc = read_locate_sc_and_index(tc, reader, &idx);
    return MVM_sc_get_code(tc, sc, idx);
}

/* Read the reference type discriminator from the buffer. */
MVM_STATIC_INLINE MVMuint8 read_discrim(MVMThreadContext *tc, MVMSerializationReader *reader) {
    assert_can_read(tc, reader, 1);
    return *(*(reader->cur_read_buffer) + *(reader->cur_read_offset));
}

/* Reading function for references.
 *
 * BEWARE - logic in this function is partly duplicated in
 * deserialize_method_cache_lazy(). If you change the format (or sizes) of
 * things read here (including of course, things read down the calltree) you may
 * need to update the corresponding skip count logic in
 * deserialize_method_cache_lazy().
 */

MVMObject * MVM_serialization_read_ref(MVMThreadContext *tc, MVMSerializationReader *reader) {
    MVMObject *result;

    /* Read the discriminator. */
    const int discrim_size = 1;
    const MVMuint8 discrim = read_discrim(tc, reader);
    *(reader->cur_read_offset) += discrim_size;

    /* Decide what to do based on it. */
    switch (discrim) {
        case REFVAR_NULL:
            return NULL;
        case REFVAR_OBJECT:
            return read_obj_ref(tc, reader);
        case REFVAR_VM_NULL:
            return tc->instance->VMNull;
        case REFVAR_VM_INT: {
            MVMint64 value;
            value = MVM_serialization_read_int(tc, reader);
            result = MVM_repr_box_int(tc, tc->instance->boot_types.BOOTInt, value);
            return result;
        }
        case REFVAR_VM_NUM:
            result = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTNum);
            MVM_repr_set_num(tc, result, MVM_serialization_read_num(tc, reader));
            return result;
        case REFVAR_VM_STR:
            result = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTStr);
            MVM_repr_set_str(tc, result, MVM_serialization_read_str(tc, reader));
            return result;
        case REFVAR_VM_ARR_VAR:
            result = read_array_var(tc, reader);
            if (reader->current_object) {
                MVM_repr_push_o(tc, reader->root.sc->body->owned_objects, result);
                MVM_repr_push_o(tc, reader->root.sc->body->owned_objects,
                    reader->current_object);
            }
            return result;
        case REFVAR_VM_ARR_STR:
            return read_array_str(tc, reader);
        case REFVAR_VM_ARR_INT:
            return read_array_int(tc, reader);
        case REFVAR_VM_HASH_STR_VAR:
            result = read_hash_str_var(tc, reader);
            if (reader->current_object) {
                MVM_repr_push_o(tc, reader->root.sc->body->owned_objects, result);
                MVM_repr_push_o(tc, reader->root.sc->body->owned_objects,
                    reader->current_object);
            }
            return result;
        case REFVAR_STATIC_CODEREF:
        case REFVAR_CLONED_CODEREF:
            return read_code_ref(tc, reader);
        case REFVAR_SC_REF:
            return (MVMObject *)MVM_sc_find_by_handle(tc,
                MVM_serialization_read_str(tc, reader));
        default:
            fail_deserialize(tc, NULL, reader,
                "Serialization Error: Unimplemented case of read_ref");
    }
}

/* Reading function for STable references. */
MVMSTable * MVM_serialization_read_stable_ref(MVMThreadContext *tc, MVMSerializationReader *reader) {
    MVMint32 idx;
    MVMSerializationContext *sc = read_locate_sc_and_index(tc, reader, &idx);
    return MVM_sc_get_stable(tc, sc, idx);
}

/* Checks the header looks sane and all of the places it points to make sense.
 * Also dissects the input string into the tables and data segments and populates
 * the reader data structure more fully. */
static void check_and_dissect_input(MVMThreadContext *tc,
        MVMSerializationReader *reader, MVMString *data_str) {
    size_t  data_len;
    size_t  header_size;
    char   *data;
    char   *prov_pos;
    char   *data_end;
    if (data_str) {
        /* Grab data from string. */
        char *data_b64 = (char *)MVM_string_ascii_encode(tc, data_str, NULL, 0);
        data = (char *)base64_decode(data_b64, &data_len);
        MVM_free(data_b64);
        reader->data_needs_free = 1;
    }
    else {
        /* Try to get it from the current compilation unit. */
        data = (char *)(*tc->interp_cu)->body.serialized;
        if (!data)
            fail_deserialize(tc, NULL, reader,
                "Failed to find deserialization data in compilation unit");
        data_len = (*tc->interp_cu)->body.serialized_size;
    }
    prov_pos = data;
    data_end = data + data_len;

    /* Ensure we got the data. */
    if (data == NULL)
        fail_deserialize(tc, NULL, reader,
            "Failed to decode base64-encoded serialization data");
    reader->data = data;

    /* Ensure that we have enough space to read a version number and check it. */
    if (data_len < 4)
        fail_deserialize(tc, NULL, reader,
            "Serialized data too short to read a version number (< 4 bytes)");
    reader->root.version = read_int32(data, 0);
    if (reader->root.version < MIN_VERSION || reader->root.version > CURRENT_VERSION)
        fail_deserialize(tc, NULL, reader,
            "Unsupported serialization format version %d (current version is %d)",
            reader->root.version, CURRENT_VERSION);

    /* Pick header size by version. */
    /* See blame history for the next line if you change the header size:  */
    header_size = HEADER_SIZE;

    /* Ensure that the data is at least as long as the header is expected to be. */
    if (data_len < header_size)
        fail_deserialize(tc, NULL, reader,
            "Serialized data shorter than header (< %"MVM_PRSz" bytes)", header_size);
    prov_pos += header_size;

    /* Get size and location of dependencies table. */
    reader->root.dependencies_table = data + read_int32(data, 4);
    reader->root.num_dependencies   = read_int32(data, 8);
    if (reader->root.dependencies_table < prov_pos)
        fail_deserialize(tc, NULL, reader,
            "Corruption detected (dependencies table starts before header ends)");
    prov_pos = reader->root.dependencies_table + reader->root.num_dependencies * DEP_TABLE_ENTRY_SIZE;
    if (prov_pos > data_end)
        fail_deserialize(tc, NULL, reader,
            "Corruption detected (dependencies table overruns end of data)");

    /* Get size and location of STables table. */
    reader->root.stables_table = data + read_int32(data, 12);
    reader->root.num_stables   = read_int32(data, 16);
    if (reader->root.stables_table < prov_pos)
        fail_deserialize(tc, NULL, reader,
            "Corruption detected (STables table starts before dependencies table ends)");
    prov_pos = reader->root.stables_table + reader->root.num_stables * STABLES_TABLE_ENTRY_SIZE;
    if (prov_pos > data_end)
        fail_deserialize(tc, NULL, reader,
            "Corruption detected (STables table overruns end of data)");

    /* Get location of STables data. */
    reader->root.stables_data = data + read_int32(data, 20);
    if (reader->root.stables_data < prov_pos)
        fail_deserialize(tc, NULL, reader,
            "Corruption detected (STables data starts before STables table ends)");
    prov_pos = reader->root.stables_data;
    if (prov_pos > data_end)
        fail_deserialize(tc, NULL, reader,
            "Corruption detected (STables data starts after end of data)");

    /* Get size and location of objects table. */
    reader->root.objects_table = data + read_int32(data, 24);
    reader->root.num_objects   = read_int32(data, 28);
    if (reader->root.objects_table < prov_pos)
        fail_deserialize(tc, NULL, reader,
            "Corruption detected (objects table starts before STables data ends)");
    prov_pos = reader->root.objects_table + reader->root.num_objects * OBJECTS_TABLE_ENTRY_SIZE;
    if (prov_pos > data_end)
        fail_deserialize(tc, NULL, reader,
            "Corruption detected (objects table overruns end of data)");

    /* Get location of objects data. */
    reader->root.objects_data = data + read_int32(data, 32);
    if (reader->root.objects_data < prov_pos)
        fail_deserialize(tc, NULL, reader,
            "Corruption detected (objects data starts before objects table ends)");
    prov_pos = reader->root.objects_data;
    if (prov_pos > data_end)
        fail_deserialize(tc, NULL, reader,
            "Corruption detected (objects data starts after end of data)");

    /* Get size and location of closures table. */
    reader->root.closures_table = data + read_int32(data, 36);
    reader->root.num_closures   = read_int32(data, 40);
    if (reader->root.closures_table < prov_pos)
        fail_deserialize(tc, NULL, reader,
            "Corruption detected (Closures table starts before objects data ends)");
    prov_pos = reader->root.closures_table + reader->root.num_closures * CLOSURES_TABLE_ENTRY_SIZE;
    if (prov_pos > data_end)
        fail_deserialize(tc, NULL, reader,
            "Corruption detected (Closures table overruns end of data)");

    /* Get size and location of contexts table. */
    reader->root.contexts_table = data + read_int32(data, 44);
    reader->root.num_contexts   = read_int32(data, 48);
    if (reader->root.contexts_table < prov_pos)
        fail_deserialize(tc, NULL, reader,
            "Corruption detected (contexts table starts before closures table ends)");
    prov_pos = reader->root.contexts_table + reader->root.num_contexts * CONTEXTS_TABLE_ENTRY_SIZE;
    if (prov_pos > data_end)
        fail_deserialize(tc, NULL, reader,
            "Corruption detected (contexts table overruns end of data)");

    /* Get location of contexts data. */
    reader->root.contexts_data = data + read_int32(data, 52);
    if (reader->root.contexts_data < prov_pos)
        fail_deserialize(tc, NULL, reader,
            "Corruption detected (contexts data starts before contexts table ends)");
    prov_pos = reader->root.contexts_data;
    if (prov_pos > data_end)
        fail_deserialize(tc, NULL, reader,
            "Corruption detected (contexts data starts after end of data)");

    /* Get size and location of repossessions table. */
    reader->root.repos_table = data + read_int32(data, 56);
    reader->root.num_repos   = read_int32(data, 60);
    if (reader->root.repos_table < prov_pos)
        fail_deserialize(tc, NULL, reader,
            "Corruption detected (repossessions table starts before contexts data ends)");
    prov_pos = reader->root.repos_table + reader->root.num_repos * REPOS_TABLE_ENTRY_SIZE;
    if (prov_pos > data_end)
        fail_deserialize(tc, NULL, reader,
            "Corruption detected (repossessions table overruns end of data)");

    /* Get location and number of entries in the interns data section. */
    reader->root.param_interns_data = data + read_int32(data, 64);
    reader->root.num_param_interns  = read_int32(data, 68);
    if (reader->root.param_interns_data < prov_pos)
        fail_deserialize(tc, NULL, reader,
            "Corruption detected (parameterization interns data starts before repossessions table ends)");
    prov_pos = reader->root.param_interns_data;
    if (prov_pos > data_end)
        fail_deserialize(tc, NULL, reader,
            "Corruption detected (parameterization interns data overruns end of data)");

    /* Set reading limits for data chunks. */
    reader->stables_data_end       = reader->root.objects_table;
    reader->objects_data_end       = reader->root.closures_table;
    reader->contexts_data_end      = reader->root.repos_table;
    reader->param_interns_data_end = data_end;
}

/* Goes through the dependencies table and resolves the dependencies that it
 * contains to SerializationContexts. */
static void resolve_dependencies(MVMThreadContext *tc, MVMSerializationReader *reader) {
    char      *table_pos = reader->root.dependencies_table;
    MVMuint32  num_deps  = reader->root.num_dependencies;
    MVMuint32  i;
    reader->root.dependent_scs = MVM_malloc(MAX(num_deps, 1) * sizeof(MVMSerializationContext *));
    for (i = 0; i < num_deps; i++) {
        MVMString *handle = read_string_from_heap(tc, reader, read_int32(table_pos, 0));
        MVMSerializationContext *sc;
        sc = MVM_sc_find_by_handle(tc, handle);
        if (sc == NULL) {
            MVMString *desc = read_string_from_heap(tc, reader, read_int32(table_pos, 4));
            char *cname = MVM_string_ascii_encode(tc, desc, NULL, 0);
            char *cdesc = NULL;
            char *waste[] = { cname, NULL, NULL };
            if (reader->root.sc->body->description) {
                cdesc = MVM_string_ascii_encode(tc, reader->root.sc->body->description, NULL, 0);
                waste[1] = cdesc;
            }
            else {
                cdesc = "<unknown>";
            }
            if (!desc) desc = handle;
            fail_deserialize(tc, waste, reader,
                "Missing or wrong version of dependency '%s' (from '%s')",
                cname,
                cdesc);
        }
        reader->root.dependent_scs[i] = sc;
        table_pos += 8;
    }
}

/* Allocates and STables that we need to deserialize, associating it with its
 * REPR and getting its allocation size set up. */
static void stub_stable(MVMThreadContext *tc, MVMSerializationReader *reader, MVMuint32 i) {
    /* Save last read positions. */
    MVMint32   orig_stables_data_offset = reader->stables_data_offset;
    char     **orig_read_buffer         = reader->cur_read_buffer;
    MVMint32  *orig_read_offset         = reader->cur_read_offset;
    char     **orig_read_end            = reader->cur_read_end;
    char      *orig_read_buffer_val     = reader->cur_read_buffer ? *(reader->cur_read_buffer) : NULL;
    MVMint32   orig_read_offset_val     = reader->cur_read_offset ? *(reader->cur_read_offset) : 0;
    char      *orig_read_end_val        = reader->cur_read_end    ? *(reader->cur_read_end)    : NULL;

    /* Calculate location of STable's table row. */
    char *st_table_row = reader->root.stables_table + i * STABLES_TABLE_ENTRY_SIZE;

    /* Check we don't already have the STable (due to repossession). */
    MVMSTable *st = MVM_sc_try_get_stable(tc, reader->root.sc, i);
    if (!st) {
        /* Read in and look up representation. */
        const MVMREPROps *repr = MVM_repr_get_by_name(tc,
            read_string_from_heap(tc, reader, read_int32(st_table_row, 0)));

        /* Allocate and store stub STable. */
        st = MVM_gc_allocate_stable(tc, repr, NULL);
        MVM_sc_set_stable(tc, reader->root.sc, i, st);
    }

    /* Set the STable's SC. */
    MVM_sc_set_stable_sc(tc, st, reader->root.sc);

    /* Set STable read position, and set current read buffer to the
     * location of the REPR data. */
    reader->stables_data_offset = read_int32(st_table_row, 8);
    reader->cur_read_buffer     = &(reader->root.stables_data);
    reader->cur_read_offset     = &(reader->stables_data_offset);
    reader->cur_read_end        = &(reader->stables_data_end);

    if (st->REPR->deserialize_stable_size)
        st->REPR->deserialize_stable_size(tc, st, reader);
    else
        fail_deserialize(tc, NULL, reader, "Missing deserialize_stable_size");
    if (st->size == 0)
        fail_deserialize(tc, NULL, reader, "STable with size zero after deserialization");

    /* Restore original read positions. */
    reader->stables_data_offset = orig_stables_data_offset;
    reader->cur_read_buffer     = orig_read_buffer;
    reader->cur_read_offset     = orig_read_offset;
    reader->cur_read_end        = orig_read_end;
    if (reader->cur_read_buffer) {
        *(reader->cur_read_buffer)  = orig_read_buffer_val;
        *(reader->cur_read_offset)  = orig_read_offset_val;
        *(reader->cur_read_end)     = orig_read_end_val;
    }
}

/* This is slightly misnamed because it doesn't read objects_data_offset.
   However, we never need that at the same time as we need the other data, so it
   makes sense not to over generalise this code. */
static MVMSTable *read_object_table_entry(MVMThreadContext *tc, MVMSerializationReader *reader, MVMuint32 i, MVMint32 *concrete) {
    MVMint32 si;        /* The SC in the dependencies table, + 1 */
    MVMint32 si_idx;    /* The index in that SC */
    /* Calculate location of object's table row. */
    const char *const obj_table_row = reader->root.objects_table + i * OBJECTS_TABLE_ENTRY_SIZE;
    const MVMuint32 packed = read_int32(obj_table_row, 0);

    if (concrete)
        *concrete = packed & OBJECTS_TABLE_ENTRY_IS_CONCRETE;

    si = (packed >> OBJECTS_TABLE_ENTRY_SC_SHIFT) & OBJECTS_TABLE_ENTRY_SC_MASK;
    if (si == OBJECTS_TABLE_ENTRY_SC_OVERFLOW) {
        const char *const overflow_data
            = reader->root.objects_data + read_int32(obj_table_row, 4) - 8;
        si = read_int32(overflow_data, 0);
        si_idx = read_int32(overflow_data, 4);
    } else {
        si_idx = packed & OBJECTS_TABLE_ENTRY_SC_IDX_MASK;
    }

    /* Resolve the STable. */
    return MVM_sc_get_stable(tc, locate_sc(tc, reader, si), si_idx);
}

/* Stubs an object we need to deserialize, setting their REPR and type object
 * flag. */
static void stub_object(MVMThreadContext *tc, MVMSerializationReader *reader, MVMuint32 i) {
    MVMint32 concrete;
    MVMSTable *st = read_object_table_entry(tc, reader, i, &concrete);

    /* Allocate and store stub object, unless it's already there due to a
     * repossession. */
    MVMObject *obj = MVM_sc_try_get_object(tc, reader->root.sc, i);
    if (!obj) {
        if (concrete)
            obj = st->REPR->allocate(tc, st);
        else
            obj = MVM_gc_allocate_type_object(tc, st);
        MVM_sc_set_object(tc, reader->root.sc, i, obj);
    }

    /* Set the object's SC. */
    MVM_sc_set_obj_sc(tc, obj, reader->root.sc);
}

/* Deserializes a context. */
static void deserialize_context(MVMThreadContext *tc, MVMSerializationReader *reader, MVMint32 row) {
    MVMStaticFrame *sf;
    MVMFrame       *f;
    MVMint64        i, syms;

    /* Save last read positions. */
    MVMint32   orig_contexts_data_offset = reader->stables_data_offset;
    char     **orig_read_buffer          = reader->cur_read_buffer;
    MVMint32  *orig_read_offset          = reader->cur_read_offset;
    char     **orig_read_end             = reader->cur_read_end;

    /* Calculate location of context's table row. */
    char *table_row = reader->root.contexts_table + row * CONTEXTS_TABLE_ENTRY_SIZE;

    /* Resolve the reference to the static code object this context is for. */
    MVMuint32  static_sc_id = read_int32(table_row, 0);
    MVMuint32  static_idx   = read_int32(table_row, 4);
    MVMuint32  outer_idx    = read_int32(table_row, 12);
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
    if (reader->root.version >= 19) {
        syms = MVM_serialization_read_int(tc, reader);
    } else {
        syms = MVM_serialization_read_int64(tc, reader);
    }

    for (i = 0; i < syms; i++) {
        MVMString   *sym = MVM_serialization_read_str(tc, reader);
        MVMRegister *lex = MVM_frame_lexical(tc, f, sym);
        switch (MVM_frame_lexical_primspec(tc, f, sym)) {
            case MVM_STORAGE_SPEC_BP_INT:
                if (reader->root.version >= 19) {
                    lex->i64 = MVM_serialization_read_int(tc, reader);
                } else {
                    lex->i64 = MVM_serialization_read_int64(tc, reader);
                }

                break;
            case MVM_STORAGE_SPEC_BP_NUM:
                lex->n64 = MVM_serialization_read_num(tc, reader);
                break;
            case MVM_STORAGE_SPEC_BP_STR:
                MVM_ASSIGN_REF(tc, &(f->header), lex->s,
                    MVM_serialization_read_str(tc, reader));
                break;
            default:
                MVM_ASSIGN_REF(tc, &(f->header), lex->o,
                    MVM_serialization_read_ref(tc, reader));
        }
    }

    /* Put context into contexts array (will be attached in a later pass). */
    reader->contexts[row] = f;

    /* Restore original read positions. */
    reader->contexts_data_offset = orig_contexts_data_offset;
    reader->cur_read_buffer      = orig_read_buffer;
    reader->cur_read_offset      = orig_read_offset;
    reader->cur_read_end         = orig_read_end;

    /* If we have an outer context... */
    if (outer_idx) {
        /* Deserialize it if we don't already have it. */
        if (!reader->contexts[outer_idx - 1])
            deserialize_context(tc, reader, outer_idx - 1);

        /* Attach it. */
        MVM_ASSIGN_REF(tc, &(f->header), f->outer, reader->contexts[outer_idx - 1]);
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
    MVMuint32  context_idx  = read_int32(table_row, 8);
    MVMObject *static_code  = MVM_sc_get_code(tc,
        locate_sc(tc, reader, static_sc_id), static_idx);

    /* Clone it and add it to the SC's code refs list. */
    MVMObject *closure = MVM_repr_clone(tc, static_code);
    MVM_repr_bind_pos_o(tc, reader->codes_list,
        reader->num_static_codes + i, closure);

    /* Tag it as being in this SC. */
    MVM_sc_set_obj_sc(tc, closure, reader->root.sc);

    /* See if there's a code object we need to attach. */
    if (read_int32(table_row, 12)) {
        MVMObject *obj = MVM_sc_get_object(tc,
            locate_sc(tc, reader, read_int32(table_row, 16)),
            read_int32(table_row, 20));
        MVM_ASSIGN_REF(tc, &(closure->header), ((MVMCode *)closure)->body.code_object, obj);
    }

    /* If we have an outer context... */
    if (context_idx) {
        /* Deserialize it if we don't already have it. */
        if (!reader->contexts[context_idx - 1])
            deserialize_context(tc, reader, context_idx - 1);

        /* Attach it. */
        MVM_ASSIGN_REF(tc, &(closure->header), ((MVMCode *)closure)->body.outer,
            reader->contexts[context_idx - 1]);
    }
}

/* Reads in what we need to lazily deserialize ->HOW later. */
static void deserialize_how_lazy(MVMThreadContext *tc, MVMSTable *st, MVMSerializationReader *reader) {
    MVMSerializationContext *sc = read_locate_sc_and_index(tc, reader, (MVMint32 *) &st->HOW_idx);

    MVM_ASSIGN_REF(tc, &(st->header), st->HOW_sc, sc);
}

/* calculate needed bytes for int, it is a simple version of MVM_serialization_read_int. */
static MVMuint8 calculate_int_bytes(MVMThreadContext *tc, MVMSerializationReader *reader) {
    const MVMuint8 *read_at = (MVMuint8 *) *(reader->cur_read_buffer) + *(reader->cur_read_offset);
    MVMuint8 *const read_end = (MVMuint8 *) *(reader->cur_read_end);
    MVMuint8 first;
    MVMuint8 need;

    if (read_at >= read_end)
        fail_deserialize(tc, NULL, reader,
                         "Read past end of serialization data buffer");

    first = *read_at++;

    /* Top bit set means remaining 7 bits are a value between -1 and 126.
       (That turns out to be the most common 7 bit range that we serialize.)  */
    if (first & 0x80) {
        return 1;
    }

    /* Otherwise next 3 bits indicate how many more bytes follow. */
    need = first >> 4;
    if (!need) {
      return 9;
    }

    if (read_at + need > read_end)
        fail_deserialize(tc, NULL, reader,
                         "Read past end of serialization data buffer");

    return need + 1;
}

/* Stashes what we need to deserialize the method cache lazily later, and then
 * skips over it.
 *
 * This function is cruel and unforgiving if you change other parts of the
 * serialization format, but don't remember (or realise) that you need to update
 * its idea of sizes. Its "failure" mode is silent, and everything still passes
 * tests. Only if you benchmark do you realise that everything takes longer,
 * because the lazy paths are now no longer taken. */
static void deserialize_method_cache_lazy(MVMThreadContext *tc, MVMSTable *st, MVMSerializationReader *reader) {
    /* Peek ahead at the discriminator. */
    const int discrim_size = 1;
    const MVMuint8 discrim = read_discrim(tc, reader);

    /* We only know how to lazily handle a hash of code refs or code objects;
     * for anything else, don't do it lazily. */
    if (discrim == REFVAR_VM_HASH_STR_VAR) {
        MVMint32 elems, i, valid;

        /* Save the offset, then skip past discriminator. */
        MVMint32 before = *(reader->cur_read_offset);
        *(reader->cur_read_offset) += discrim_size;

        /* Check the elements are as expected. */
        if (reader->root.version >= 19) {
            elems = MVM_serialization_read_int(tc, reader);
        } else {
            assert_can_read(tc, reader, 4);
            elems = read_int32(*(reader->cur_read_buffer), *(reader->cur_read_offset));
            *(reader->cur_read_offset) += 4;
        }
        valid = 1;
        for (i = 0; i < elems; i++) {
            MVMuint32 packed;
            MVMuint8 inner_discrim;
            MVMint32 offset;
            /* Skip string. */
            assert_can_read(tc, reader, 2);
            offset = read_uint16(*(reader->cur_read_buffer), *(reader->cur_read_offset));
            *(reader->cur_read_offset) += 2;
            if (offset & STRING_HEAP_LOC_PACKED_OVERFLOW) {
                assert_can_read(tc, reader, 2);
                *(reader->cur_read_offset) += 2;
            }

            /* Ensure we've a coderef or code object. */
            assert_can_read(tc, reader, discrim_size);
            inner_discrim = read_discrim(tc, reader);
            *(reader->cur_read_offset) += discrim_size;
            switch (inner_discrim) {
            case REFVAR_OBJECT:
            case REFVAR_STATIC_CODEREF:
            case REFVAR_CLONED_CODEREF:
                if (reader->root.version >= 19) {
                    packed = MVM_serialization_read_int(tc, reader);
                } else {
                    assert_can_read(tc, reader, 4);
                    packed = read_int32(*(reader->cur_read_buffer),
                                        *(reader->cur_read_offset) );
                }

                if(packed == (PACKED_SC_OVERFLOW << PACKED_SC_SHIFT)) {
                    if (reader->root.version >= 19) {
                        *(reader->cur_read_offset) += calculate_int_bytes(tc, reader); /* for sc_id */
                        *(reader->cur_read_offset) += calculate_int_bytes(tc, reader); /* for idx */
                    } else {
                        assert_can_read(tc, reader, 12);
                        *(reader->cur_read_offset) += 12;
                    }
                } else {
                    if (reader->root.version >= 19) {

                    } else {
                        *(reader->cur_read_offset) += 4;
                    }
                }
                break;
            case REFVAR_NULL:
            case REFVAR_VM_NULL:
            case REFVAR_VM_INT:
            case REFVAR_VM_NUM:
            case REFVAR_VM_STR:
            case REFVAR_VM_ARR_VAR:
            case REFVAR_VM_ARR_STR:
            case REFVAR_VM_ARR_INT:
            case REFVAR_VM_HASH_STR_VAR:
                valid = 0;
                *(reader->cur_read_offset) = before;
                break;
            default:
                MVM_exception_throw_adhoc(tc,
                                          "Serialization Error: Unimplemented discriminator %d in inner loop in deserialize_method_cache_lazy",
                inner_discrim);
            }
            if (!valid)
                break;
        }

        /* If all was valid then just stash what we need for later. */
        if (valid) {
            st->method_cache = NULL;
            MVM_ASSIGN_REF(tc, &(st->header), st->method_cache_sc, reader->root.sc);
            st->method_cache_offset = before;
            return;
        }
    } else {
        switch (discrim) {
        case REFVAR_OBJECT:
        case REFVAR_STATIC_CODEREF:
        case REFVAR_CLONED_CODEREF:
        case REFVAR_NULL:
        case REFVAR_VM_NULL:
        case REFVAR_VM_INT:
        case REFVAR_VM_NUM:
        case REFVAR_VM_STR:
        case REFVAR_VM_ARR_VAR:
        case REFVAR_VM_ARR_STR:
        case REFVAR_VM_ARR_INT:
        case REFVAR_VM_HASH_STR_VAR:
            break;
        default:
            MVM_exception_throw_adhoc(tc,
                                      "Serialization Error: Unimplemented discriminator %d in deserialize_method_cache_lazy",
                                      discrim);
        }
    }

    /* If we get here, fall back to eager deserialization. */
    MVM_ASSIGN_REF(tc, &(st->header), st->method_cache,
        MVM_serialization_read_ref(tc, reader));
}

/* Deserializes a single STable, along with its REPR data. */
static void deserialize_stable(MVMThreadContext *tc, MVMSerializationReader *reader, MVMint32 i, MVMSTable *st) {
    /* Save last read positions. */
    MVMint32   orig_stables_data_offset = reader->stables_data_offset;
    char     **orig_read_buffer         = reader->cur_read_buffer;
    MVMint32  *orig_read_offset         = reader->cur_read_offset;
    char     **orig_read_end            = reader->cur_read_end;
    char      *orig_read_buffer_val     = reader->cur_read_buffer ? *(reader->cur_read_buffer) : NULL;
    MVMint32   orig_read_offset_val     = reader->cur_read_offset ? *(reader->cur_read_offset) : 0;
    char      *orig_read_end_val        = reader->cur_read_end    ? *(reader->cur_read_end)    : NULL;

    /* Calculate location of STable's table row. */
    char *st_table_row = reader->root.stables_table + i * STABLES_TABLE_ENTRY_SIZE;
    MVMuint8 flags;
    MVMuint8 mode;

    /* Set STable read position, and set current read buffer to the correct thing. */
    reader->stables_data_offset = read_int32(st_table_row, 4);
    reader->cur_read_buffer     = &(reader->root.stables_data);
    reader->cur_read_offset     = &(reader->stables_data_offset);
    reader->cur_read_end        = &(reader->stables_data_end);

    /* If the STable is being repossessed, clean up its existing data before we
     * write over it. */
    if (st->being_repossessed) {
        if (st->REPR->gc_free_repr_data)
            st->REPR->gc_free_repr_data(tc, st);
        MVM_free(st->type_check_cache);
        st->type_check_cache = NULL;
        MVM_free(st->boolification_spec);
        st->boolification_spec = NULL;
        MVM_free(st->invocation_spec);
        st->invocation_spec = NULL;
        MVM_free(st->debug_name);
        st->debug_name = NULL;
        st->being_repossessed = 0;
    }

    /* Read the HOW, WHAT and WHO. */
    deserialize_how_lazy(tc, st, reader);
    MVM_ASSIGN_REF(tc, &(st->header), st->WHAT, read_obj_ref(tc, reader));
    MVM_ASSIGN_REF(tc, &(st->header), st->WHO, MVM_serialization_read_ref(tc, reader));

    /* Method cache. */
    deserialize_method_cache_lazy(tc, st, reader);

    /* Type check cache. */
    st->type_check_cache_length = MVM_serialization_read_int(tc, reader);
    if (st->type_check_cache_length > 0) {
        st->type_check_cache = (MVMObject **)MVM_malloc(st->type_check_cache_length * sizeof(MVMObject *));
        for (i = 0; i < st->type_check_cache_length; i++)
            MVM_ASSIGN_REF(tc, &(st->header), st->type_check_cache[i], MVM_serialization_read_ref(tc, reader));
    }

    /* Mode flags. */
    assert_can_read(tc, reader, 1);
    st->mode_flags = *(*(reader->cur_read_buffer) + *(reader->cur_read_offset));
    *(reader->cur_read_offset) += 1;
    if (st->mode_flags & MVM_PARAMETRIC_TYPE && st->mode_flags & MVM_PARAMETERIZED_TYPE)
        fail_deserialize(tc, NULL, reader,
            "STable mode flags cannot indicate both parametric and parameterized");

    /* Boolification spec. */
    assert_can_read(tc, reader, 1);
    flags = *(*(reader->cur_read_buffer) + *(reader->cur_read_offset));
    *(reader->cur_read_offset) += 1;
    mode = flags & 0xF;
    if (mode != 0xF) {
        st->boolification_spec = (MVMBoolificationSpec *)MVM_malloc(sizeof(MVMBoolificationSpec));
        st->boolification_spec->mode = mode;
        MVM_ASSIGN_REF(tc, &(st->header), st->boolification_spec->method, MVM_serialization_read_ref(tc, reader));
    }

    /* Container spec. */
    if (flags & STABLE_HAS_CONTAINER_SPEC) {
        MVMString *name = MVM_serialization_read_str(tc, reader);
        const MVMContainerConfigurer *cc = MVM_6model_get_container_config(tc, name);
        if (!cc) {
            char *cname = MVM_string_ascii_encode(tc, name, NULL, 0);
            char *waste[] = { cname, NULL };
            fail_deserialize(tc, waste, reader, "Could not look up the container config for '%s'",
                cname);
        }
        cc->set_container_spec(tc, st);
        st->container_spec->deserialize(tc, st, reader);
    }

    /* Invocation spec. */
    if (flags & STABLE_HAS_INVOCATION_SPEC) {
        st->invocation_spec = (MVMInvocationSpec *)MVM_calloc(1, sizeof(MVMInvocationSpec));
        MVM_ASSIGN_REF(tc, &(st->header), st->invocation_spec->class_handle, MVM_serialization_read_ref(tc, reader));
        MVM_ASSIGN_REF(tc, &(st->header), st->invocation_spec->attr_name, MVM_serialization_read_str(tc, reader));
        st->invocation_spec->hint = MVM_serialization_read_int(tc, reader);
        MVM_ASSIGN_REF(tc, &(st->header), st->invocation_spec->invocation_handler, MVM_serialization_read_ref(tc, reader));
        MVM_ASSIGN_REF(tc, &(st->header), st->invocation_spec->md_class_handle, MVM_serialization_read_ref(tc, reader));
        MVM_ASSIGN_REF(tc, &(st->header), st->invocation_spec->md_cache_attr_name, MVM_serialization_read_str(tc, reader));
        st->invocation_spec->md_cache_hint = MVM_serialization_read_int(tc, reader);
        MVM_ASSIGN_REF(tc, &(st->header), st->invocation_spec->md_valid_attr_name, MVM_serialization_read_str(tc, reader));
        st->invocation_spec->md_valid_hint = MVM_serialization_read_int(tc, reader);
    }

    /* HLL owner. */
    if (flags & STABLE_HAS_HLL_OWNER) {
        st->hll_owner = MVM_hll_get_config_for(tc, MVM_serialization_read_str(tc, reader));
    }

    /* HLL role. */
    if (flags & STABLE_HAS_HLL_ROLE) {
        st->hll_role = MVM_serialization_read_int(tc, reader);
    }

    /* If it's a parametric type... */
    if (st->mode_flags & MVM_PARAMETRIC_TYPE) {
        /* Create empty lookup table, unless we were beat to it. */
        if (!st->paramet.ric.lookup) {
            MVMObject *lookup = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTArray);
            MVM_ASSIGN_REF(tc, &(st->header), st->paramet.ric.lookup, lookup);
        }

        /* Deserialize parameterizer. */
        MVM_ASSIGN_REF(tc, &(st->header), st->paramet.ric.parameterizer,
                       MVM_serialization_read_ref(tc, reader));
    }

    /* If it's a parameterized type... */
    if (st->mode_flags & MVM_PARAMETERIZED_TYPE) {
        MVMObject *lookup;

        /* Deserialize parametric type and parameters. */
        MVMObject *ptype  = MVM_serialization_read_ref(tc, reader);
        MVMObject *params = read_array_var(tc, reader);

        /* Attach them to the STable. */
        MVM_ASSIGN_REF(tc, &(st->header), st->paramet.erized.parametric_type, ptype);
        MVM_ASSIGN_REF(tc, &(st->header), st->paramet.erized.parameters, params);

        /* Add a mapping into the lookup list of the parameteric type. */
        lookup = STABLE(ptype)->paramet.ric.lookup;
        if (!lookup) {
            lookup = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTArray);
            MVM_ASSIGN_REF(tc, &(STABLE(ptype)->header), STABLE(ptype)->paramet.ric.lookup, lookup);
        }
        MVM_repr_push_o(tc, lookup, params);
        MVM_repr_push_o(tc, lookup, st->WHAT);
    }

    if (reader->root.version >= 18) {
        st->debug_name = MVM_serialization_read_cstr(tc, reader);
    } else {
        st->debug_name = NULL;
    }
    if (reader->root.version >= 21) {
        st->is_mixin_type = MVM_serialization_read_int(tc, reader);
    }

    /* If the REPR has a function to deserialize representation data, call it. */
    if (st->REPR->deserialize_repr_data)
        st->REPR->deserialize_repr_data(tc, st, reader);

    /* Restore original read positions. */
    reader->stables_data_offset = orig_stables_data_offset;
    reader->cur_read_buffer     = orig_read_buffer;
    reader->cur_read_offset     = orig_read_offset;
    reader->cur_read_end        = orig_read_end;
    if (reader->cur_read_buffer) {
        *(reader->cur_read_buffer)  = orig_read_buffer_val;
        *(reader->cur_read_offset)  = orig_read_offset_val;
        *(reader->cur_read_end)     = orig_read_end_val;
    }
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
        reader->current_object = obj;
        reader->objects_data_offset = read_int32(obj_table_row, 4);
        if (REPR(obj)->deserialize)
            REPR(obj)->deserialize(tc, STABLE(obj), obj, OBJECT_BODY(obj), reader);
        else
            fail_deserialize(tc, NULL, reader, "Missing deserialize REPR function for %s (%s)",
                REPR(obj)->name, MVM_6model_get_debug_name(tc, obj));
        reader->current_object = NULL;
    }
}

/* Worklist manipulation functions. */
static void worklist_add_index(MVMThreadContext *tc, MVMDeserializeWorklist *wl, MVMuint32 index) {
    if (wl->num_indexes == wl->alloc_indexes) {
        if (wl->alloc_indexes)
            wl->alloc_indexes *= 2;
        else
            wl->alloc_indexes = 128;
        wl->indexes = MVM_realloc(wl->indexes, wl->alloc_indexes * sizeof(MVMuint32));
    }
    wl->indexes[wl->num_indexes] = index;
    wl->num_indexes++;
}
static MVMuint32 worklist_has_work(MVMThreadContext *tc, MVMDeserializeWorklist *wl) {
    return wl->num_indexes > 0;
}
static MVMuint32 worklist_take_index(MVMThreadContext *tc, MVMDeserializeWorklist *wl) {
    wl->num_indexes--;
    return wl->indexes[wl->num_indexes];
}

/* Evaluates work lists until they are all empty. */
static void work_loop(MVMThreadContext *tc, MVMSerializationReader *sr) {
    MVMuint32 worked = 1;

    while (worked) {
        worked = 0;

        while (worklist_has_work(tc, &(sr->wl_stables))) {
            MVMuint32 index = worklist_take_index(tc, &(sr->wl_stables));
            deserialize_stable(tc, sr, index,
                sr->root.sc->body->root_stables[index]);
            worked = 1;
        }

        while (worklist_has_work(tc, &(sr->wl_objects)) &&
               !worklist_has_work(tc, &(sr->wl_stables))) {
            MVMuint32 index = worklist_take_index(tc, &(sr->wl_objects));
            deserialize_object(tc, sr, index,
                sr->root.sc->body->root_objects[index]);
            worked = 1;
        }
    }
}

/* Demands that we finish deserializing an object. */
MVMObject * MVM_serialization_demand_object(MVMThreadContext *tc, MVMSerializationContext *sc, MVMint64 idx) {
    /* Obtain lock and check we didn't lose a race to deserialize this
     * object. */
    MVMSerializationReader *sr = sc->body->sr;
    MVMROOT(tc, sc, {
        MVM_reentrantmutex_lock(tc, (MVMReentrantMutex *)sc->body->mutex);
    });
    if (sc->body->root_objects[idx]) {
        MVM_reentrantmutex_unlock(tc, (MVMReentrantMutex *)sc->body->mutex);
        return sc->body->root_objects[idx];
    }

    /* Flag that we're working on some deserialization (and so will run the
     * loop). */
    sr->working++;
    MVM_gc_allocate_gen2_default_set(tc);

    /* Stub the object. */
    stub_object(tc, sr, idx);

    /* Add to worklist and process as needed. */
    worklist_add_index(tc, &(sr->wl_objects), idx);
    if (sr->working == 1)
        work_loop(tc, sr);

    /* Clear up. */
    MVM_gc_allocate_gen2_default_clear(tc);
    sr->working--;
    MVM_reentrantmutex_unlock(tc, (MVMReentrantMutex *)sc->body->mutex);

    /* Return the (perhaps just stubbed) object. */
    return sc->body->root_objects[idx];
}

/* Demands that we finish deserializing an STable. */
MVMSTable * MVM_serialization_demand_stable(MVMThreadContext *tc, MVMSerializationContext *sc, MVMint64 idx) {
    /* Obtain lock and ensure we didn't lose a race to deserialize this
     * STable. */
    MVMSerializationReader *sr = sc->body->sr;
    MVMROOT(tc, sc, {
        MVM_reentrantmutex_lock(tc, (MVMReentrantMutex *)sc->body->mutex);
    });
    if (sc->body->root_stables[idx]) {
        MVM_reentrantmutex_unlock(tc, (MVMReentrantMutex *)sc->body->mutex);
        return sc->body->root_stables[idx];
    }

    /* Flag that we're working on some deserialization (and so will run the
     * loop). */
    sr->working++;
    MVM_gc_allocate_gen2_default_set(tc);

    /* Stub the STable. */
    stub_stable(tc, sr, idx);

    /* Add to worklist and process as needed. */
    worklist_add_index(tc, &(sr->wl_stables), idx);
    if (sr->working == 1)
        work_loop(tc, sr);

    /* Clear up. */
    MVM_gc_allocate_gen2_default_clear(tc);
    sr->working--;
    MVM_reentrantmutex_unlock(tc, (MVMReentrantMutex *)sc->body->mutex);

    /* Return the (perhaps just stubbed) STable. */
    return sc->body->root_stables[idx];
}

/* Demands that we finish deserializing a coderef. */
MVMObject * MVM_serialization_demand_code(MVMThreadContext *tc, MVMSerializationContext *sc, MVMint64 idx) {
    /* Obtain lock and ensure we didn't lose a race to deserialize this
     * code object. */
    MVMSerializationReader *sr = sc->body->sr;
    MVMROOT(tc, sc, {
        MVM_reentrantmutex_lock(tc, (MVMReentrantMutex *)sc->body->mutex);
    });
    if (!MVM_is_null(tc, MVM_repr_at_pos_o(tc, sr->codes_list, idx))) {
        MVM_reentrantmutex_unlock(tc, (MVMReentrantMutex *)sc->body->mutex);
        return MVM_repr_at_pos_o(tc, sr->codes_list, idx);
    }

    /* Flag that we're working on some deserialization (and so will run the
     * loop). */
    sr->working++;
    MVM_gc_allocate_gen2_default_set(tc);

    /* Deserialize the code object. */
    deserialize_closure(tc, sr, idx - sr->num_static_codes);

    /* Add to worklist and process as needed. */
    if (sr->working == 1)
        work_loop(tc, sr);

    /* Clear up. */
    MVM_gc_allocate_gen2_default_clear(tc);
    sr->working--;
    MVM_reentrantmutex_unlock(tc, (MVMReentrantMutex *)sc->body->mutex);

    /* Return the (perhaps just stubbed) STable. */
    return MVM_repr_at_pos_o(tc, sr->codes_list, idx);
}

/* Forces us to complete deserialization of a particular STable before work
 * can go on. */
void MVM_serialization_force_stable(MVMThreadContext *tc, MVMSerializationReader *sr, MVMSTable *st) {
    /* We'll always have the WHAT if we finished deserializing. */
    if (!st->WHAT) {
        /* Not finished. Try to find the index. */
        MVMDeserializeWorklist *wl = &(sr->wl_stables);
        MVMint32  found = 0;
        MVMuint32 i;
        for (i = 0; i < wl->num_indexes; i++) {
            MVMuint32 index = wl->indexes[i];
            if (!found) {
                if (sr->root.sc->body->root_stables[index] == st) {
                    /* Found it; finish deserialize. */
                    deserialize_stable(tc, sr, index,
                        sr->root.sc->body->root_stables[index]);
                    found = 1;
                }
            }
            else {
                /* After the found index; steal from list. */
                wl->indexes[i - 1] = index;
            }
        }
        if (found)
            wl->num_indexes--;
    }
}

/* Finishes deserializing the method cache. */
void MVM_serialization_finish_deserialize_method_cache(MVMThreadContext *tc, MVMSTable *st) {
    MVMSerializationContext *sc = st->method_cache_sc;
    if (sc && sc->body->sr) {
        /* Acquire mutex and ensure we didn't lose a race to do this. */
        MVMSerializationReader *sr = sc->body->sr;
        MVMROOT2(tc, st, sc, {
            MVM_reentrantmutex_lock(tc, (MVMReentrantMutex *)sc->body->mutex);
        });
        if (st->method_cache_sc) {
            MVMObject *cache;

            /* Set reader's position. */
            sr->stables_data_offset    = st->method_cache_offset;
            sr->cur_read_buffer        = &(sr->root.stables_data);
            sr->cur_read_offset        = &(sr->stables_data_offset);
            sr->cur_read_end           = &(sr->stables_data_end);

            /* Flag that we're working on some deserialization (and so will run the
            * loop). */
            sr->working++;
            MVM_gc_allocate_gen2_default_set(tc);

            /* Deserialize what we need. */
            cache = MVM_serialization_read_ref(tc, sr);
            if (sr->working == 1)
                work_loop(tc, sr);
            MVM_ASSIGN_REF(tc, &(st->header), st->method_cache, cache);

            /* Clear up. */
            MVM_gc_allocate_gen2_default_clear(tc);
            sr->working--;
            st->method_cache_sc = NULL;
        }
        MVM_reentrantmutex_unlock(tc, (MVMReentrantMutex *)sc->body->mutex);
    }
}

/* Repossess an object or STable. Ignores those not matching the specified
 * type (where 0 = object, 1 = STable). */
static void repossess(MVMThreadContext *tc, MVMSerializationReader *reader, MVMint64 i,
                      MVMObject *repo_conflicts, MVMint32 type) {
    MVMuint32 slot;

    /* Calculate location of table row. */
    char *table_row = reader->root.repos_table + i * REPOS_TABLE_ENTRY_SIZE;

    /* Do appropriate type of repossession, provided it matches the type of
     * thing we're current repossessing. */
    MVMint32 repo_type = read_int32(table_row, 0);
    if (repo_type != type)
        return;
    if (repo_type == 0) {
        MVMSTable *updated_st;

        /* Get object to repossess. */
        MVMSerializationContext *orig_sc = locate_sc(tc, reader, read_int32(table_row, 8));
        MVMObject *orig_obj = MVM_sc_get_object(tc, orig_sc, read_int32(table_row, 12));

        /* If we have a reposession conflict, make a copy of the original object
         * and reference it from the conflicts list. Push the original (about to
         * be overwritten) object reference too. */
        if (MVM_sc_get_obj_sc(tc, orig_obj) != orig_sc) {
            MVMROOT(tc, orig_obj, {
                MVMObject *backup = NULL;
                MVMROOT(tc, backup, {
                    if (IS_CONCRETE(orig_obj)) {
                        backup = REPR(orig_obj)->allocate(tc, STABLE(orig_obj));
                        REPR(orig_obj)->copy_to(tc, STABLE(orig_obj), OBJECT_BODY(orig_obj), backup, OBJECT_BODY(backup));
                    }
                    else
                        backup = MVM_gc_allocate_type_object(tc, STABLE(orig_obj));
                });

                MVM_SC_WB_OBJ(tc, backup);
                MVM_repr_push_o(tc, repo_conflicts, backup);
                MVM_repr_push_o(tc, repo_conflicts, orig_obj);
            });
        }

        /* Put it into objects root set at the apporpriate slot. */
        slot = read_int32(table_row, 4);
        MVM_sc_set_object(tc, reader->root.sc, slot, orig_obj);
        MVM_sc_set_obj_sc(tc, orig_obj, reader->root.sc);
        MVM_sc_set_idx_in_sc(&(orig_obj->header), slot);

        /* Clear it up, since we'll re-allocate all the bits inside
         * it on deserialization. */
        if (REPR(orig_obj)->gc_free) {
            REPR(orig_obj)->gc_free(tc, orig_obj);
            /* Ensure the object is clean in case the deserialization never happens */
            memset(OBJECT_BODY(orig_obj), 0, orig_obj->header.size - sizeof(MVMObject));
        }

        /* The object's STable may have changed as a result of the
         * repossession (perhaps due to mixing in to it), so put the
         * STable it should now have in place. */
        updated_st = read_object_table_entry(tc, reader, slot, NULL);
        if (updated_st != orig_obj->st)
            REPR(orig_obj)->change_type(tc, orig_obj, updated_st->WHAT);

        /* Put this on the list of things we should deserialize right away. */
        worklist_add_index(tc, &(reader->wl_objects), slot);
    }
    else if (repo_type == 1) {
        /* Get STable to repossess. */
        MVMSerializationContext *orig_sc = locate_sc(tc, reader, read_int32(table_row, 8));
        MVMSTable *orig_st = MVM_sc_get_stable(tc, orig_sc, read_int32(table_row, 12));

        /* Make sure we don't have a reposession conflict. */
        if (MVM_sc_get_stable_sc(tc, orig_st) != orig_sc)
            fail_deserialize(tc, NULL, reader,
                "STable conflict detected during deserialization.\n"
                "(Probable attempt to load a mutated module or modules that cannot be loaded together).");

        /* Put it into STables root set at the apporpriate slot. */
        slot = read_int32(table_row, 4);
        MVM_sc_set_stable(tc, reader->root.sc, slot, orig_st);
        MVM_sc_set_stable_sc(tc, orig_st, reader->root.sc);
        MVM_sc_set_idx_in_sc(&(orig_st->header), slot);

        /* Flag as being repossessed, so we can clear up memory at the point
         * we replaced the STable data with the updated data. */
        orig_st->being_repossessed = 1;

        /* Put this on the list of things we should deserialize right away. */
        worklist_add_index(tc, &(reader->wl_stables), slot);
    }
    else {
        fail_deserialize(tc, NULL, reader, "Unknown repossession type");
    }
}

/* This goes through the entries in the parameterized types interning section,
 * if any. For each, if we already deserialized the parameterization from a
 * different compilation unit or created it in something we already compiled,
 * we just use that existing parameterization. */
static void resolve_param_interns(MVMThreadContext *tc, MVMSerializationReader *reader) {
    MVMint32 iidx;

    /* Switch to reading the parameterization segment. */
    reader->cur_read_buffer = &(reader->root.param_interns_data);
    reader->cur_read_offset = &(reader->param_interns_data_offset);
    reader->cur_read_end    = &(reader->param_interns_data_end);

    /* Go over all the interns we have. */
    for (iidx = 0; iidx < reader->root.num_param_interns; iidx++) {
        MVMObject *params, *matching;
        MVMint32   num_params, i;

        /* Resolve the parametric type. */
        MVMObject *ptype = read_obj_ref(tc, reader);

        /* Read indexes where type object and STable will get placed if a
         * matching intern is found. */
        MVMint32 type_idx = read_int32(*(reader->cur_read_buffer), *(reader->cur_read_offset));
        MVMint32 st_idx   = read_int32(*(reader->cur_read_buffer), *(reader->cur_read_offset) + 4);
        *(reader->cur_read_offset) += 8;

        /* Read parameters and push into array. */
        num_params = read_int32(*(reader->cur_read_buffer), *(reader->cur_read_offset));
        *(reader->cur_read_offset) += 4;
        params = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTArray);
        for (i = 0; i < num_params; i++)
            MVM_repr_push_o(tc, params, read_obj_ref(tc, reader));

        /* Try to find a matching parameterization. */
        matching = MVM_6model_parametric_try_find_parameterization(tc, STABLE(ptype), params);
        if (matching) {
            MVM_sc_set_object_no_update(tc, reader->root.sc, type_idx, matching);
            MVM_sc_set_stable(tc, reader->root.sc, st_idx, STABLE(matching));
        }
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
    MVMSerializationReader *reader = MVM_calloc(1, sizeof(MVMSerializationReader));
    reader->root.sc          = sc;

    /* If we've been given a NULL string heap, use that of the current
     * compilation unit. */
    if (MVM_is_null(tc, string_heap))
        reader->root.string_comp_unit = *(tc->interp_cu);
    else
        reader->root.string_heap = string_heap;

    /* Store reader inside serialization context; it'll need it for lazy
     * deserialization. */
    sc->body->sr = reader;

    /* Put code root list into SC. We'll end up mutating it, but that's
     * probably fine. */
    MVM_sc_set_code_list(tc, sc, codes_static);
    reader->codes_list = codes_static;
    scodes = (MVMint32)MVM_repr_elems(tc, codes_static);
    reader->num_static_codes = scodes;

    /* Mark all the static code refs we've been provided with as static. */
    for (i = 0; i < scodes; i++) {
        MVMObject *scr = MVM_repr_at_pos_o(tc, reader->codes_list, i);
        ((MVMCode *)scr)->body.is_static = 1;
        MVM_sc_set_obj_sc(tc, scr, sc);
    }

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

    /* Size objects, STables, and contexts arrays. */
    if (sc->body->root_objects)
        MVM_free(sc->body->root_objects);
    if (sc->body->root_stables)
        MVM_free(sc->body->root_stables);
    sc->body->root_objects  = MVM_calloc(reader->root.num_objects, sizeof(MVMObject *));
    sc->body->num_objects   = reader->root.num_objects;
    sc->body->alloc_objects = reader->root.num_objects;
    sc->body->root_stables  = MVM_calloc(reader->root.num_stables, sizeof(MVMSTable *));
    sc->body->num_stables   = reader->root.num_stables;
    sc->body->alloc_stables = reader->root.num_stables;
    reader->contexts        = MVM_calloc(reader->root.num_contexts, sizeof(MVMFrame *));

    /* Increase size of code refs list to include closures we'll later
     * deserialize. */
    REPR(codes_static)->pos_funcs.set_elems(tc, STABLE(codes_static),
        codes_static, OBJECT_BODY(codes_static),
        scodes + reader->root.num_closures);

    /* Handle any type parameterization interning, meaning we should not
     * deserialize our own versions of things. */
    resolve_param_interns(tc, reader);

    /* If we're repossessing STables and objects from other SCs, then first
      * get those raw objects into our root set. Note we do all the STables,
      * then all the objects, since the objects may, post-repossession, refer
      * to a repossessed STable. */
     for (i = 0; i < reader->root.num_repos; i++)
        repossess(tc, reader, i, repo_conflicts, 1);
     for (i = 0; i < reader->root.num_repos; i++)
        repossess(tc, reader, i, repo_conflicts, 0);

    /* Enter the work loop to deal with the things we immediately need to
     * handle in order to complete repossession object deserialization. */
    reader->working = 1;
    work_loop(tc, reader);
    reader->working = 0;

    /* Clear serialized data reference in CU. */
    if ((*tc->interp_cu)->body.serialized) {
        (*tc->interp_cu)->body.serialized = NULL;
        (*tc->interp_cu)->body.serialized_size = 0;
    }

    /* If lazy deserialization is disabled, deserialize everything. */
#if !MVM_SERIALIZATION_LAZY
    for (i = 0; i < sc->body->num_objects; i++)
        MVM_serialization_demand_object(tc, sc, i);
    for (i = 0; i < sc->body->num_stables; i++)
        MVM_serialization_demand_stable(tc, sc, i);
#endif

    /* Restore normal GC allocation. */
    MVM_gc_allocate_gen2_default_clear(tc);
}

/*

=item sha1

Computes the SHA-1 hash of string.

=cut

*/
MVMString * MVM_sha1(MVMThreadContext *tc, MVMString *str) {
    /* Grab the string as UTF8 bytes. */
    MVMuint64 output_size;
    char *utf8_string = MVM_string_utf8_encode(tc, str, &output_size, 0);

    /* Compute its SHA-1 and encode it. */
    SHA1Context      context;
    char          output[80];
    SHA1Init(&context);
    SHA1Update(&context, (unsigned char*) utf8_string, (size_t) output_size);
    SHA1Final(&context, output);

    /* Free the C-MVMString and put result into a new MVMString. */
    MVM_free(utf8_string);
    return MVM_string_ascii_decode(tc, tc->instance->VMString, output, 40);
}
