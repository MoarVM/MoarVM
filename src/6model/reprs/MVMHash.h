/* Representation used by VM-level hashes. */

typedef struct _MVMHashEntry {
    /* key object (must be MVMString REPR) */
    MVMObject *key;

    /* value object */
    MVMObject *value;

    /* the uthash hash handle inline struct. */
    UT_hash_handle hash_handle;
} MVMHashEntry;

typedef struct _MVMHashBody {

    /* uthash updates this pointer directly. */
    MVMHashEntry *hash_head;

} MVMHashBody;
typedef struct _MVMHash {
    MVMObject common;
    MVMHashBody body;
} MVMHash;

/* Function for REPR setup. */
MVMREPROps * MVMHash_initialize(MVMThreadContext *tc);

#define MVM_HASH_ACTION(tc, hash, name, entry, action, member, size) \
    action(hash_handle, hash, \
        name->body.int32s, NUM_GRAPHS(name) * sizeof(size), entry); \

#define MVM_HASH_ACTION_SELECT(tc, hash, name, entry, action) \
if (IS_WIDE(name)) \
    MVM_HASH_ACTION(tc, hash, name, entry, action, int32s, MVMCodepoint32) \
else \
    MVM_HASH_ACTION(tc, hash, name, entry, action, uint8s, MVMCodepoint8)

#define MVM_HASH_BIND(tc, hash, name, entry) \
    MVM_HASH_ACTION_SELECT(tc, hash, name, entry, HASH_ADD_KEYPTR)

#define MVM_HASH_GET(tc, hash, name, entry) \
    MVM_HASH_ACTION_SELECT(tc, hash, name, entry, HASH_FIND)

#define MVM_HASH_EXTRACT_KEY(tc, kdata, klen, key, error) \
if (REPR(key)->ID == MVM_REPR_ID_MVMString && IS_CONCRETE(key)) { \
    MVM_string_flatten(tc, (MVMString *)key); \
    if (IS_WIDE(key)) { \
        *kdata = ((MVMString *)key)->body.int32s; \
        *klen  = ((MVMString *)key)->body.graphs * sizeof(MVMCodepoint32); \
    } \
    else { \
        *kdata = ((MVMString *)key)->body.uint8s; \
        *klen  = ((MVMString *)key)->body.graphs * sizeof(MVMCodepoint8); \
    } \
} \
else { \
    MVM_exception_throw_adhoc(tc, error); \
}
