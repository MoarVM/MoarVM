/* Representation used by HashAttrStore. */
 typedef struct _HashAttrStoreBody {
    /* The head of the hash, or null if the hash is empty.
     * The UT_HASH macros update this pointer directly. */
    MVMHashEntry *hash_head;
} HashAttrStoreBody;
typedef struct _HashAttrStore {
    MVMObject common;
    HashAttrStoreBody body;
} HashAttrStore;

/* Function for REPR setup. */
MVMREPROps * HashAttrStore_initialize(MVMThreadContext *tc);
