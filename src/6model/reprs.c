#include "moar.h"
#include "gcc_diag.h"

/* Default REPR function handlers. */
GCC_DIAG_OFF(return-type)
MVMuint64 MVM_REPR_DEFAULT_ELEMS(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data) {
    MVM_exception_throw_adhoc(tc,
        "This representation (%s) does not support elems (for type %s)",
        st->REPR->name, MVM_6model_get_stable_debug_name(tc, st));
}
GCC_DIAG_ON(return-type)
MVM_NO_RETURN static void die_no_attrs(MVMThreadContext *tc, const char *repr_name, const char *debug_name) MVM_NO_RETURN_ATTRIBUTE;
static void die_no_attrs(MVMThreadContext *tc, const char *repr_name, const char *debug_name) {
    MVM_exception_throw_adhoc(tc,
        "This representation (%s) does not support attribute storage (for type %s)", repr_name, debug_name);
}
void MVM_REPR_DEFAULT_GET_ATTRIBUTE(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMObject *class_handle, MVMString *name, MVMint64 hint, MVMRegister *result, MVMuint16 kind) {
    die_no_attrs(tc, st->REPR->name, MVM_6model_get_stable_debug_name(tc, st));
}
void MVM_REPR_DEFAULT_BIND_ATTRIBUTE(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMObject *class_handle, MVMString *name, MVMint64 hint, MVMRegister value, MVMuint16 kind) {
    die_no_attrs(tc, st->REPR->name, MVM_6model_get_stable_debug_name(tc, st));
}
GCC_DIAG_OFF(return-type)
MVMint64 MVM_REPR_DEFAULT_IS_ATTRIBUTE_INITIALIZED(MVMThreadContext *tc, MVMSTable *st, void *data, MVMObject *class_handle, MVMString *name, MVMint64 hint) {
    die_no_attrs(tc, st->REPR->name, MVM_6model_get_stable_debug_name(tc, st));
}
AO_t * MVM_REPR_DEFAULT_ATTRIBUTE_AS_ATOMIC(MVMThreadContext *tc, MVMSTable *st, void *data, MVMObject *class_handle, MVMString *name, MVMuint16 kind) {
    die_no_attrs(tc, st->REPR->name, MVM_6model_get_stable_debug_name(tc, st));
}
GCC_DIAG_ON(return-type)
MVMint64 MVM_REPR_DEFAULT_HINT_FOR(MVMThreadContext *tc, MVMSTable *st, MVMObject *class_handle, MVMString *name) {
    return MVM_NO_HINT;
}
void MVM_REPR_DEFAULT_SET_INT(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMint64 value) {
    MVM_exception_throw_adhoc(tc,
        "This representation (%s) cannot box a native int (for type %s)", st->REPR->name, MVM_6model_get_stable_debug_name(tc, st));
}
MVMint64 MVM_REPR_DEFAULT_GET_INT(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data) {
    MVM_exception_throw_adhoc(tc,
        "This representation (%s) cannot unbox to a native int (for type %s)", st->REPR->name, MVM_6model_get_stable_debug_name(tc, st));
}
void MVM_REPR_DEFAULT_SET_NUM(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMnum64 value) {
    MVM_exception_throw_adhoc(tc,
        "This representation (%s) cannot box a native num (for type %s)", st->REPR->name, MVM_6model_get_stable_debug_name(tc, st));
}
MVMnum64 MVM_REPR_DEFAULT_GET_NUM(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data) {
    MVM_exception_throw_adhoc(tc,
        "This representation (%s) cannot unbox to a native num (for type %s)", st->REPR->name, MVM_6model_get_stable_debug_name(tc, st));
}
void MVM_REPR_DEFAULT_SET_STR(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMString *value) {
    MVM_exception_throw_adhoc(tc,
        "This representation (%s) cannot box a native string (for type %s)", st->REPR->name, MVM_6model_get_stable_debug_name(tc, st));
}
MVMString * MVM_REPR_DEFAULT_GET_STR(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data) {
    MVM_exception_throw_adhoc(tc,
        "This representation (%s) cannot unbox to a native string (for type %s)", st->REPR->name, MVM_6model_get_stable_debug_name(tc, st));
}
void MVM_REPR_DEFAULT_SET_UINT(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMuint64 value) {
    MVM_exception_throw_adhoc(tc,
        "This representation (%s) cannot box an unsigned native int (for type %s)", st->REPR->name, MVM_6model_get_stable_debug_name(tc, st));
}
MVMuint64 MVM_REPR_DEFAULT_GET_UINT(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data) {
    MVM_exception_throw_adhoc(tc,
        "This representation (%s) cannot unbox to an unsigned native int (for type %s)", st->REPR->name, MVM_6model_get_stable_debug_name(tc, st));
}
void * MVM_REPR_DEFAULT_GET_BOXED_REF(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMuint32 repr_id) {
    MVM_exception_throw_adhoc(tc,
        "This representation (%s) cannot unbox to other types (for type %s)", st->REPR->name, MVM_6model_get_stable_debug_name(tc, st));
}
MVM_NO_RETURN static void die_no_pos(MVMThreadContext *tc, const char *repr_name, const char *debug_name) MVM_NO_RETURN_ATTRIBUTE;
static void die_no_pos(MVMThreadContext *tc, const char *repr_name, const char *debug_name) {
    MVM_exception_throw_adhoc(tc,
        "This representation (%s) does not support positional access (for type %s)", repr_name, debug_name);
}
MVM_NO_RETURN static void die_no_multidim(MVMThreadContext *tc, const char *repr_name, const char *debug_name) MVM_NO_RETURN_ATTRIBUTE;
static void die_no_multidim(MVMThreadContext *tc, const char *repr_name, const char *debug_name) {
    MVM_exception_throw_adhoc(tc,
        "This representation (%s) does not support multidimensional positional access (for type %s)", repr_name, debug_name);
}
void MVM_REPR_DEFAULT_AT_POS(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMint64 index, MVMRegister *value, MVMuint16 kind) {
    die_no_pos(tc, st->REPR->name, MVM_6model_get_stable_debug_name(tc, st));
}
void MVM_REPR_DEFAULT_BIND_POS(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMint64 index, MVMRegister value, MVMuint16 kind) {
    die_no_pos(tc, st->REPR->name, MVM_6model_get_stable_debug_name(tc, st));
}
void MVM_REPR_DEFAULT_SET_ELEMS(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMuint64 count) {
    die_no_pos(tc, st->REPR->name, MVM_6model_get_stable_debug_name(tc, st));
}
void MVM_REPR_DEFAULT_PUSH(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMRegister value, MVMuint16 kind) {
    die_no_pos(tc, st->REPR->name, MVM_6model_get_stable_debug_name(tc, st));
}
void MVM_REPR_DEFAULT_POP(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMRegister *value, MVMuint16 kind) {
    die_no_pos(tc, st->REPR->name, MVM_6model_get_stable_debug_name(tc, st));
}
void MVM_REPR_DEFAULT_UNSHIFT(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMRegister value, MVMuint16 kind) {
    die_no_pos(tc, st->REPR->name, MVM_6model_get_stable_debug_name(tc, st));
}
void MVM_REPR_DEFAULT_SHIFT(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMRegister *value, MVMuint16 kind) {
    die_no_pos(tc, st->REPR->name, MVM_6model_get_stable_debug_name(tc, st));
}
void MVM_REPR_DEFAULT_SLICE(MVMThreadContext *tc, MVMSTable *st, MVMObject *src, void *data, MVMObject *dest, MVMint64 start, MVMint64 end) {
    die_no_pos(tc, st->REPR->name, MVM_6model_get_stable_debug_name(tc, st));
}

void MVM_REPR_DEFAULT_AT_POS_MULTIDIM(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMint64 num_indices, MVMint64 *indices, MVMRegister *value, MVMuint16 kind) {
    die_no_pos(tc, st->REPR->name, MVM_6model_get_stable_debug_name(tc, st));
}
void MVM_REPR_DEFAULT_BIND_POS_MULTIDIM(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMint64 num_indices, MVMint64 *indices, MVMRegister value, MVMuint16 kind) {
    die_no_pos(tc, st->REPR->name, MVM_6model_get_stable_debug_name(tc, st));
}
void MVM_REPR_DEFAULT_DIMENSIONS(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMint64 *num_dimensions, MVMint64 **dimensions) {
    die_no_pos(tc, st->REPR->name, MVM_6model_get_stable_debug_name(tc, st));
}
void MVM_REPR_DEFAULT_SET_DIMENSIONS(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMint64 num_dimensions, MVMint64 *dimensions) {
    die_no_pos(tc, st->REPR->name, MVM_6model_get_stable_debug_name(tc, st));
}

void MVM_REPR_DEFAULT_AT_POS_MULTIDIM_NO_MULTIDIM(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMint64 num_indices, MVMint64 *indices, MVMRegister *value, MVMuint16 kind) {
    die_no_multidim(tc, st->REPR->name, MVM_6model_get_stable_debug_name(tc, st));
}
void MVM_REPR_DEFAULT_BIND_POS_MULTIDIM_NO_MULTIDIM(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMint64 num_indices, MVMint64 *indices, MVMRegister value, MVMuint16 kind) {
    die_no_multidim(tc, st->REPR->name, MVM_6model_get_stable_debug_name(tc, st));
}
void MVM_REPR_DEFAULT_DIMENSIONS_NO_MULTIDIM(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMint64 *num_dimensions, MVMint64 **dimensions) {
    die_no_multidim(tc, st->REPR->name, MVM_6model_get_stable_debug_name(tc, st));
}
void MVM_REPR_DEFAULT_SET_DIMENSIONS_NO_MULTIDIM(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMint64 num_dimensions, MVMint64 *dimensions) {
    die_no_multidim(tc, st->REPR->name, MVM_6model_get_stable_debug_name(tc, st));
}
GCC_DIAG_OFF(return-type)
MVMStorageSpec MVM_REPR_DEFAULT_GET_ELEM_STORAGE_SPEC(MVMThreadContext *tc, MVMSTable *st) {
    die_no_pos(tc, st->REPR->name, MVM_6model_get_stable_debug_name(tc, st));
}
AO_t * MVM_REPR_DEFAULT_POS_AS_ATOMIC(MVMThreadContext *tc, MVMSTable *st, MVMObject *root,
                                      void *data, MVMint64 index) {
    die_no_pos(tc, st->REPR->name, MVM_6model_get_stable_debug_name(tc, st));
}
AO_t * MVM_REPR_DEFAULT_POS_AS_ATOMIC_MULTIDIM(MVMThreadContext *tc, MVMSTable *st,
                                               MVMObject *root, void *data,
                                               MVMint64 num_indices, MVMint64 *indices) {
    die_no_pos(tc, st->REPR->name, MVM_6model_get_stable_debug_name(tc, st));
}
void MVM_REPR_DEFAULT_POS_WRITE_BUF(MVMThreadContext *tc, MVMSTable *st, MVMObject *root,
                                      void *data, char *from, MVMint64 offset, MVMuint64 elems) {
    die_no_pos(tc, st->REPR->name, MVM_6model_get_stable_debug_name(tc, st));
}
MVMint64 MVM_REPR_DEFAULT_POS_READ_BUF(MVMThreadContext *tc, MVMSTable *st, MVMObject *root,
                                      void *data, MVMint64 offset, MVMuint64 elems) {
    die_no_pos(tc, st->REPR->name, MVM_6model_get_stable_debug_name(tc, st));
}
GCC_DIAG_ON(return-type)
void MVM_REPR_DEFAULT_SPLICE(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMObject *target_array, MVMint64 offset, MVMuint64 elems) {
    die_no_pos(tc, st->REPR->name, MVM_6model_get_stable_debug_name(tc, st));
}
MVM_NO_RETURN static void die_no_ass(MVMThreadContext *tc, const char *repr_name, const char *debug_name) MVM_NO_RETURN_ATTRIBUTE;
static void die_no_ass(MVMThreadContext *tc, const char *repr_name, const char *debug_name) {
    MVM_exception_throw_adhoc(tc,
        "This representation (%s) does not support associative access (for type %s)", repr_name, debug_name);
}
void MVM_REPR_DEFAULT_AT_KEY(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMObject *key, MVMRegister *result, MVMuint16 kind) {
    die_no_ass(tc, st->REPR->name, MVM_6model_get_stable_debug_name(tc, st));
}
void MVM_REPR_DEFAULT_BIND_KEY(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMObject *key, MVMRegister value, MVMuint16 kind) {
    die_no_ass(tc, st->REPR->name, MVM_6model_get_stable_debug_name(tc, st));
}
GCC_DIAG_OFF(return-type)
MVMint64 MVM_REPR_DEFAULT_EXISTS_KEY(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMObject *key) {
    die_no_ass(tc, st->REPR->name, MVM_6model_get_stable_debug_name(tc, st));
}
GCC_DIAG_ON(return-type)
void MVM_REPR_DEFAULT_DELETE_KEY(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMObject *key) {
    die_no_ass(tc, st->REPR->name, MVM_6model_get_stable_debug_name(tc, st));
}
GCC_DIAG_OFF(return-type)
MVMStorageSpec MVM_REPR_DEFAULT_GET_VALUE_STORAGE_SPEC(MVMThreadContext *tc, MVMSTable *st) {
    die_no_ass(tc, st->REPR->name, MVM_6model_get_stable_debug_name(tc, st));
}
GCC_DIAG_ON(return-type)

/* Registers a representation. */
static void register_repr(MVMThreadContext *tc, const MVMREPROps *repr, MVMString *name) {
    /* register_core_repr calls us with name NULL. MVM_string_ascii_decode_nt
     * returns a concrete MVMString, will always pass the
     * MVM_str_hash_key_is_valid check.
     * Our other caller has already validated name. */

    if (!name)
        name = MVM_string_ascii_decode_nt(tc, tc->instance->VMString,
                repr->name);

    /* Enter into registry. */
    tc->instance->repr_vtables[repr->ID] = repr;
    tc->instance->repr_names[repr->ID] = name;
    MVM_index_hash_insert_nocheck(tc, &tc->instance->repr_hash, tc->instance->repr_names, repr->ID);

    /* Name should become a permanent GC root. */
    MVM_gc_root_add_permanent_desc(tc, (MVMCollectable **)&tc->instance->repr_names[repr->ID], "REPR name");
}

int MVM_repr_register_dynamic_repr(MVMThreadContext *tc, MVMREPROps *repr) {
    MVMString *name = MVM_string_ascii_decode_nt(tc, tc->instance->VMString, repr->name);

    uv_mutex_lock(&tc->instance->mutex_repr_registry);

    MVMuint32 idx = MVM_index_hash_fetch_nocheck(tc, &tc->instance->repr_hash, tc->instance->repr_names, name);
    if (idx != MVM_INDEX_HASH_NOT_FOUND) {
        uv_mutex_unlock(&tc->instance->mutex_repr_registry);
        return 0;
    }

    if (!(tc->instance->num_reprs < MVM_REPR_MAX_COUNT)) {
        uv_mutex_unlock(&tc->instance->mutex_repr_registry);
        MVM_exception_throw_adhoc(tc,
                "Cannot register more than %u representations",
                MVM_REPR_MAX_COUNT);
    }

    repr->ID = tc->instance->num_reprs++;
    register_repr(tc, repr, name);

    uv_mutex_unlock(&tc->instance->mutex_repr_registry);
    return 1;
}

/* Core representations contain their IDs in their MVMREPROps. assert that they
 * are the order is consistent. */

#define register_core_repr(name) {                              \
        const MVMREPROps *repr = MVM##name##_initialize(tc);    \
        assert(repr->ID == tc->instance->num_reprs);            \
        register_repr(tc, repr, NULL);                          \
        tc->instance->num_reprs++;                              \
    }

/* Initializes the representations registry, building up all of the various
 * representations. */
void MVM_repr_initialize_registry(MVMThreadContext *tc) {
    tc->instance->repr_vtables = MVM_malloc(
            MVM_REPR_MAX_COUNT * sizeof *tc->instance->repr_vtables);
    tc->instance->repr_names = MVM_malloc(
            MVM_REPR_MAX_COUNT * sizeof *tc->instance->repr_names);

    tc->instance->num_reprs = 0;
    /* Add all core representations. */
    register_core_repr(String);
    register_core_repr(Array);
    register_core_repr(Hash);
    register_core_repr(CFunction);
    register_core_repr(KnowHOWREPR);
    register_core_repr(P6opaque);
    register_core_repr(Code);
    register_core_repr(OSHandle);
    register_core_repr(P6int);
    register_core_repr(P6num);
    register_core_repr(Uninstantiable);
    register_core_repr(HashAttrStore);
    register_core_repr(KnowHOWAttributeREPR);
    register_core_repr(P6str);
    register_core_repr(Thread);
    register_core_repr(Iter);
    register_core_repr(Context);
    register_core_repr(SCRef);
    register_core_repr(SpeshLog);
    register_core_repr(CallCapture);
    register_core_repr(P6bigint);
    register_core_repr(NFA);
    register_core_repr(Exception);
    register_core_repr(StaticFrame);
    register_core_repr(CompUnit);
    register_core_repr(DLLSym);
    register_core_repr(MultiCache);
    register_core_repr(Continuation);
    register_core_repr(NativeCall);
    register_core_repr(CPointer);
    register_core_repr(CStr);
    register_core_repr(CArray);
    register_core_repr(CStruct);
    register_core_repr(ReentrantMutex);
    register_core_repr(ConditionVariable);
    register_core_repr(Semaphore);
    register_core_repr(ConcBlockingQueue);
    register_core_repr(AsyncTask);
    register_core_repr(Null);
    register_core_repr(NativeRef);
    register_core_repr(CUnion);
    register_core_repr(MultiDimArray);
    register_core_repr(CPPStruct);
    register_core_repr(Decoder);
    register_core_repr(StaticFrameSpesh);
    register_core_repr(SpeshPluginState);
    register_core_repr(SpeshCandidate);
    register_core_repr(Capture);

    assert(tc->instance->num_reprs == MVM_REPR_CORE_COUNT);
}

/* Get a representation's ID from its name. Note that the IDs may change so
 * it's best not to store references to them in e.g. the bytecode stream. */
MVMuint32 MVM_repr_name_to_id(MVMThreadContext *tc, MVMString *name) {
    if (!MVM_str_hash_key_is_valid(tc, name)) {
        MVM_str_hash_key_throw_invalid(tc, name);
    }

    uv_mutex_lock(&tc->instance->mutex_repr_registry);
    MVMuint32 idx = MVM_index_hash_fetch_nocheck(tc, &tc->instance->repr_hash, tc->instance->repr_names, name);
    if (idx == MVM_INDEX_HASH_NOT_FOUND) {
        char *c_name = MVM_string_ascii_encode_any(tc, name);
        char *waste[] = { c_name, NULL };
        uv_mutex_unlock(&tc->instance->mutex_repr_registry);
        MVM_exception_throw_adhoc_free(tc, waste, "Lookup by name of unknown REPR: %s",
                                       c_name);
    }
    uv_mutex_unlock(&tc->instance->mutex_repr_registry);

    return idx;
}

/* Gets a representation by ID. */
const MVMREPROps * MVM_repr_get_by_id(MVMThreadContext *tc, MVMuint32 id) {
    if (id >= tc->instance->num_reprs)
        MVM_exception_throw_adhoc(tc, "REPR lookup by invalid ID %" PRIu32, id);

    return tc->instance->repr_vtables[id];
}

/* Gets a representation by name. */
const MVMREPROps * MVM_repr_get_by_name(MVMThreadContext *tc, MVMString *name) {
    MVMuint32 id = MVM_repr_name_to_id(tc, name);
    return tc->instance->repr_vtables[id];
}
