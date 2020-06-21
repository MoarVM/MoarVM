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
