/* Representation used by VM-level hashes. */

struct MVMHashEntry {
    /* key object (must be MVMString REPR) */
    MVMObject *key;

    /* value object */
    MVMObject *value;

    /* the uthash hash handle inline struct. */
    UT_hash_handle hash_handle;
};

struct MVMHashBody {

    /* uthash updates this pointer directly. */
    MVMHashEntry *hash_head;

};
struct MVMHash {
    MVMObject common;
    MVMHashBody body;
};

/* Function for REPR setup. */
const MVMREPROps * MVMHash_initialize(MVMThreadContext *tc);

#define MVM_HASH_BIND(tc, hash, key, value) \
    do { \
        MVM_string_flatten(tc, key); \
        HASH_ADD_KEYPTR_VM_STR(tc, hash_handle, hash, key, value); \
    } while (0);

#define MVM_HASH_GET(tc, hash, key, value) \
    do { \
        if (!MVM_is_null(tc, (MVMObject *)key) && REPR(key)->ID == MVM_REPR_ID_MVMString \
                && IS_CONCRETE(key)) { \
            MVM_string_flatten(tc, key); \
            HASH_FIND_VM_STR(tc, hash_handle, hash, key, value); \
        } \
        else { \
            MVM_exception_throw_adhoc(tc, "Hash keys must be concrete strings"); \
        } \
    } while (0);

#define MVM_HASH_DESTROY(hash_handle, hashentry_type, head_node) do { \
    hashentry_type *current, *tmp; \
    unsigned bucket_tmp; \
    HASH_ITER(hash_handle, head_node, current, tmp, bucket_tmp) { \
        if (current != head_node) \
            MVM_free(current); \
    } \
    tmp = head_node; \
    HASH_CLEAR(hash_handle, head_node); \
    MVM_free(tmp); \
} while (0)
