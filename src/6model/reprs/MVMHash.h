/* Representation used by VM-level hashes. */

struct MVMHashEntry {
    /* hash handle inline struct, including the key. */
    struct MVMStrHashHandle hash_handle;
    /* value object */
    MVMObject *value;
};

struct MVMHashBody {
    MVMStrHashTable hashtable;
};
struct MVMHash {
    MVMObject common;
    MVMHashBody body;
};

/* Function for REPR setup. */
const MVMREPROps * MVMHash_initialize(MVMThreadContext *tc);

#define MVM_HASH_BIND_FREE(tc, hash, key, value, block) \
    do { \
        if (MVM_str_hash_key_is_valid(tc, key)) { \
            HASH_ADD_KEYPTR_VM_STR(tc, hash_handle, hash, key, value); \
        } \
        else { \
            block \
            MVM_str_hash_key_throw_invalid(tc, key); \
        } \
    } while (0);

#define MVM_HASH_BIND(tc, hash, key, value) \
    MVM_HASH_BIND_FREE(tc, hash, key, value, {});

#define MVM_HASH_GET(tc, hash, key, value) \
    do { \
        if (MVM_str_hash_key_is_valid(tc, key)) { \
            HASH_FIND_VM_STR(tc, hash_handle, hash, key, value); \
        } \
        else { \
            MVM_str_hash_key_throw_invalid(tc, key); \
        } \
    } while (0);

#define MVM_HASH_KEY(entry) ((entry)->hash_handle.key)

#define MVM_HASH_DESTROY(tc, hash_handle, hashentry_type, head_node) do { \
    hashentry_type *current, *tmp; \
    HASH_ITER_FAST(tc, hash_handle, head_node, current, { \
        if (current != head_node) \
            MVM_free(current); \
    }); \
    tmp = head_node; \
    HASH_CLEAR(tc, hash_handle, head_node); \
    MVM_free(tmp); \
} while (0)

void MVMHash_at_key(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMObject *key_obj, MVMRegister *result, MVMuint16 kind);
void MVMHash_bind_key(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMObject *key_obj, MVMRegister value, MVMuint16 kind);
