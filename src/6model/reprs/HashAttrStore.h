/* Representation used by HashAttrStore. */
struct MVMHashAttrStoreBody {
    /* The head of the hash, or null if the hash is empty.
     * The UT_HASH macros update this pointer directly. */
    MVMHashEntry *hash_head;
};
struct MVMHashAttrStore {
    MVMObject common;
    MVMHashAttrStoreBody body;
};

/* Function for REPR setup. */
const MVMREPROps * MVMHashAttrStore_initialize(MVMThreadContext *tc);
