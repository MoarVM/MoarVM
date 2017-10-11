/* Representation used by VM-level hashes. */

struct MVMHashEntry {
    /* value object */
    MVMObject *value;

    /* the uthash hash handle inline struct, including the key. */
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
        if (!MVM_is_null(tc, (MVMObject *)key) && REPR(key)->ID == MVM_REPR_ID_MVMString \
                && IS_CONCRETE(key)) { \
            HASH_ADD_KEYPTR_VM_STR_FSA(tc, hash_handle, hash, key, value); \
        } \
        else { \
            MVM_exception_throw_adhoc(tc, "Hash keys must be concrete strings"); \
        } \
    } while (0);

#define MVM_HASH_GET(tc, hash, key, value) \
    do { \
        if (!MVM_is_null(tc, (MVMObject *)key) && REPR(key)->ID == MVM_REPR_ID_MVMString \
                && IS_CONCRETE(key)) { \
            HASH_FIND_VM_STR(tc, hash_handle, hash, key, value); \
        } \
        else { \
            MVM_exception_throw_adhoc(tc, "Hash keys must be concrete strings"); \
        } \
    } while (0);

#define MVM_HASH_KEY(entry) ((MVMString *)(entry)->hash_handle.key)

#define MVM_HASH_DESTROY(tc, hash_handle, hashentry_type, head_node) do { \
    hashentry_type *current, *tmp; \
    unsigned bucket_tmp; \
    HASH_ITER(hash_handle, head_node, current, tmp, bucket_tmp) { \
        if (current != head_node) \
            MVM_free(current); \
    } \
    tmp = head_node; \
    HASH_CLEAR_FSA(tc, hash_handle, head_node); \
    MVM_free(tmp); \
} while (0)
