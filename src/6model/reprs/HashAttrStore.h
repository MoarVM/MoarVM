/* Representation used by HashAttrStore. */
struct MVMHashAttrStoreBody {
    MVMStrHashTable hashtable;
};
struct MVMHashAttrStore {
    MVMObject common;
    MVMHashAttrStoreBody body;
};

/* Function for REPR setup. */
const MVMREPROps * MVMHashAttrStore_initialize(MVMThreadContext *tc);
