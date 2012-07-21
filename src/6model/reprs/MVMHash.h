/* Representation used by VM-level hashes. */

typedef struct _MVMHashEntry {
    /* key object (must be MVMString REPR) */
    MVMObject *key;
    
    /* value object */
    MVMObject *value;
    
    /* the uthash hash handle. MUST be initialized to NULL. */
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
