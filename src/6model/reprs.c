#include "moarvm.h"
#include "gcc_diag.h"
#include "bithacks.h"

/* Default REPR function handlers. */
GCC_DIAG_OFF(return-type)
MVMuint64 MVM_REPR_DEFAULT_ELEMS(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data) {
    MVM_exception_throw_adhoc(tc,
        "This representation (%s) does not support elems",
        st->REPR->name);
}
GCC_DIAG_ON(return-type)
MVM_NO_RETURN static void die_no_attrs(MVMThreadContext *tc, const char *repr_name) MVM_NO_RETURN_GCC;
static void die_no_attrs(MVMThreadContext *tc, const char *repr_name) {
    MVM_exception_throw_adhoc(tc,
        "This representation (%s) does not support attribute storage", repr_name);
}
void MVM_REPR_DEFAULT_GET_ATTRIBUTE(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMObject *class_handle, MVMString *name, MVMint64 hint, MVMRegister *result, MVMuint16 kind) {
    die_no_attrs(tc, st->REPR->name);
}
void MVM_REPR_DEFAULT_BIND_ATTRIBUTE(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMObject *class_handle, MVMString *name, MVMint64 hint, MVMRegister value, MVMuint16 kind) {
    die_no_attrs(tc, st->REPR->name);
}
GCC_DIAG_OFF(return-type)
MVMint64 MVM_REPR_DEFAULT_IS_ATTRIBUTE_INITIALIZED(MVMThreadContext *tc, MVMSTable *st, void *data, MVMObject *class_handle, MVMString *name, MVMint64 hint) {
    die_no_attrs(tc, st->REPR->name);
}
GCC_DIAG_ON(return-type)
MVMint64 MVM_REPR_DEFAULT_HINT_FOR(MVMThreadContext *tc, MVMSTable *st, MVMObject *class_handle, MVMString *name) {
    return MVM_NO_HINT;
}
void MVM_REPR_DEFAULT_SET_INT(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMint64 value) {
    MVM_exception_throw_adhoc(tc,
        "This representation (%s) cannot box a native int", st->REPR->name);
}
MVMint64 MVM_REPR_DEFAULT_GET_INT(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data) {
    MVM_exception_throw_adhoc(tc,
        "This representation (%s) cannot unbox to a native int", st->REPR->name);
}
void MVM_REPR_DEFAULT_SET_NUM(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMnum64 value) {
    MVM_exception_throw_adhoc(tc,
        "This representation (%s) cannot box a native num", st->REPR->name);
}
MVMnum64 MVM_REPR_DEFAULT_GET_NUM(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data) {
    MVM_exception_throw_adhoc(tc,
        "This representation (%s) cannot unbox to a native num", st->REPR->name);
}
void MVM_REPR_DEFAULT_SET_STR(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMString *value) {
    MVM_exception_throw_adhoc(tc,
        "This representation (%s) cannot box a native string", st->REPR->name);
}
MVMString * MVM_REPR_DEFAULT_GET_STR(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data) {
    MVM_exception_throw_adhoc(tc,
        "This representation (%s) cannot unbox to a native string", st->REPR->name);
}
void * MVM_REPR_DEFAULT_GET_BOXED_REF(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMuint32 repr_id) {
    MVM_exception_throw_adhoc(tc,
        "This representation (%s) cannot unbox to other types", st->REPR->name);
}
MVM_NO_RETURN static void die_no_pos(MVMThreadContext *tc, const char *repr_name) MVM_NO_RETURN_GCC;
static void die_no_pos(MVMThreadContext *tc, const char *repr_name) {
    MVM_exception_throw_adhoc(tc,
        "This representation (%s) does not support positional access", repr_name);
}
void MVM_REPR_DEFAULT_AT_POS(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMint64 index, MVMRegister *value, MVMuint16 kind) {
    die_no_pos(tc, st->REPR->name);
}
void MVM_REPR_DEFAULT_BIND_POS(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMint64 index, MVMRegister value, MVMuint16 kind) {
    die_no_pos(tc, st->REPR->name);
}
void MVM_REPR_DEFAULT_SET_ELEMS(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMuint64 count) {
    die_no_pos(tc, st->REPR->name);
}
MVMint64 MVM_REPR_DEFAULT_EXISTS_POS(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMint64 index) {
    die_no_pos(tc, st->REPR->name);
}
void MVM_REPR_DEFAULT_PUSH(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMRegister value, MVMuint16 kind) {
    die_no_pos(tc, st->REPR->name);
}
void MVM_REPR_DEFAULT_POP(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMRegister *value, MVMuint16 kind) {
    die_no_pos(tc, st->REPR->name);
}
void MVM_REPR_DEFAULT_UNSHIFT(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMRegister value, MVMuint16 kind) {
    die_no_pos(tc, st->REPR->name);
}
void MVM_REPR_DEFAULT_SHIFT(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMRegister *value, MVMuint16 kind) {
    die_no_pos(tc, st->REPR->name);
}
GCC_DIAG_OFF(return-type)
MVMStorageSpec MVM_REPR_DEFAULT_GET_ELEM_STORAGE_SPEC(MVMThreadContext *tc, MVMSTable *st) {
    die_no_pos(tc, st->REPR->name);
}
GCC_DIAG_ON(return-type)
void MVM_REPR_DEFAULT_SPLICE(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMObject *target_array, MVMint64 offset, MVMuint64 elems) {
    die_no_pos(tc, st->REPR->name);
}
MVM_NO_RETURN static void die_no_ass(MVMThreadContext *tc, const char *repr_name) MVM_NO_RETURN_GCC;
static void die_no_ass(MVMThreadContext *tc, const char *repr_name) {
    MVM_exception_throw_adhoc(tc,
        "This representation (%s) does not support associative access", repr_name);
}
GCC_DIAG_OFF(return-type)
void * MVM_REPR_DEFAULT_AT_KEY_REF(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMObject *key) {
    die_no_ass(tc, st->REPR->name);
}
MVMObject * MVM_REPR_DEFAULT_AT_KEY_BOXED(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMObject *key) {
    die_no_ass(tc, st->REPR->name);
}
GCC_DIAG_ON(return-type)
void MVM_REPR_DEFAULT_BIND_KEY_REF(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMObject *key, void *value_addr) {
    die_no_ass(tc, st->REPR->name);
}
void MVM_REPR_DEFAULT_BIND_KEY_BOXED(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMObject *key, MVMObject *value) {
    die_no_ass(tc, st->REPR->name);
}
GCC_DIAG_OFF(return-type)
MVMint64 MVM_REPR_DEFAULT_EXISTS_KEY(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMObject *key) {
    die_no_ass(tc, st->REPR->name);
}
GCC_DIAG_ON(return-type)
void MVM_REPR_DEFAULT_DELETE_KEY(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMObject *key) {
    die_no_ass(tc, st->REPR->name);
}
GCC_DIAG_OFF(return-type)
MVMStorageSpec MVM_REPR_DEFAULT_GET_VALUE_STORAGE_SPEC(MVMThreadContext *tc, MVMSTable *st) {
    die_no_ass(tc, st->REPR->name);
}
GCC_DIAG_ON(return-type)

/* Registers a representation. */
static void register_repr(MVMThreadContext *tc, const MVMREPROps *repr, MVMString *name) {
    MVMReprRegistry *entry;

    if (!name)
        name = MVM_string_ascii_decode_nt(tc, tc->instance->VMString,
                repr->name);

    /* Fill a registry entry. */
    entry = malloc(sizeof(MVMReprRegistry));
    entry->name = name;
    entry->repr = repr;

    /* Name should become a permanent GC root. */
    MVM_gc_root_add_permanent(tc, (MVMCollectable **)&entry->name);

    /* Enter into registry. */
    tc->instance->repr_list[repr->ID] = entry;
    MVM_string_flatten(tc, name);
    MVM_HASH_BIND(tc, tc->instance->repr_hash, name, entry);
}

int MVM_repr_register_dynamic_repr(MVMThreadContext *tc, MVMREPROps *repr) {
    MVMReprRegistry *entry;
    MVMString *name;

    uv_mutex_lock(&tc->instance->mutex_repr_registry);

    name = MVM_string_ascii_decode_nt(tc, tc->instance->VMString, repr->name);
    MVM_string_flatten(tc, name);
    MVM_HASH_GET(tc, tc->instance->repr_hash, name, entry);
    if (entry) {
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

#define register_static_repr(name) \
    register_repr(tc, MVM##name##_initialize(tc), NULL)

/* Initializes the representations registry, building up all of the various
 * representations. */
void MVM_repr_initialize_registry(MVMThreadContext *tc) {
    tc->instance->repr_list = malloc(
            MVM_REPR_MAX_COUNT * sizeof *tc->instance->repr_list);

    /* Add all core representations. */
    register_static_repr(String);
    register_static_repr(Array);
    register_static_repr(Hash);
    register_static_repr(CFunction);
    register_static_repr(KnowHOWREPR);
    register_static_repr(P6opaque);
    register_static_repr(Code);
    register_static_repr(OSHandle);
    register_static_repr(P6int);
    register_static_repr(P6num);
    register_static_repr(Uninstantiable);
    register_static_repr(HashAttrStore);
    register_static_repr(KnowHOWAttributeREPR);
    register_static_repr(P6str);
    register_static_repr(Thread);
    register_static_repr(Iter);
    register_static_repr(Context);
    register_static_repr(SCRef);
    register_static_repr(Lexotic);
    register_static_repr(CallCapture);
    register_static_repr(P6bigint);
    register_static_repr(NFA);
    register_static_repr(Exception);
    register_static_repr(StaticFrame);
    register_static_repr(CompUnit);
    register_static_repr(Blob);
    register_static_repr(Ptr);

    /* Add all C representations. */
    register_static_repr(CVoid);
    register_static_repr(CChar);
    register_static_repr(CUChar);
    register_static_repr(CShort);
    register_static_repr(CUShort);
    register_static_repr(CInt);
    register_static_repr(CUInt);
    register_static_repr(CLong);
    register_static_repr(CULong);
    register_static_repr(CLLong);
    register_static_repr(CULLong);
    register_static_repr(CInt8);
    register_static_repr(CUInt8);
    register_static_repr(CInt16);
    register_static_repr(CUInt16);
    register_static_repr(CInt32);
    register_static_repr(CUInt32);
    register_static_repr(CInt64);
    register_static_repr(CUInt64);
    register_static_repr(CIntPtr);
    register_static_repr(CUIntPtr);
    register_static_repr(CIntMax);
    register_static_repr(CUIntMax);
    register_static_repr(CFloat);
    register_static_repr(CDouble);
    register_static_repr(CLDouble);
    register_static_repr(CPtr);
    register_static_repr(CFPtr);
    register_static_repr(CArray);
    register_static_repr(CStruct);
    register_static_repr(CUnion);
    register_static_repr(CFlexStruct);

    tc->instance->num_reprs = MVM_REPR_CORE_COUNT + MVM_REPR_NATIVE_COUNT;
}

static MVMReprRegistry * find_repr_by_name(MVMThreadContext *tc,
        MVMString *name) {
    MVMReprRegistry *entry;

    MVM_string_flatten(tc, name);
    MVM_HASH_GET(tc, tc->instance->repr_hash, name, entry)

    if (entry == NULL)
        MVM_exception_throw_adhoc(tc, "Lookup by name of unknown REPR: %s",
            MVM_string_ascii_encode_any(tc, name));

    return entry;
}

/* Get a representation's ID from its name. Note that the IDs may change so
 * it's best not to store references to them in e.g. the bytecode stream. */
MVMuint32 MVM_repr_name_to_id(MVMThreadContext *tc, MVMString *name) {
    return find_repr_by_name(tc, name)->repr->ID;
}

/* Gets a representation by ID. */
const MVMREPROps * MVM_repr_get_by_id(MVMThreadContext *tc, MVMuint32 id) {
    if (id >= tc->instance->num_reprs)
        MVM_exception_throw_adhoc(tc, "REPR lookup by invalid ID %" PRIu32, id);

    return tc->instance->repr_list[id]->repr;
}

/* Gets a representation by name. */
const MVMREPROps * MVM_repr_get_by_name(MVMThreadContext *tc, MVMString *name) {
    return find_repr_by_name(tc, name)->repr;
}
