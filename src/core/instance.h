/* The various "bootstrap" types, based straight off of some core
 * representations. They are used during the 6model bootstrap. */
struct _MVMBootTypes {
    MVMObject *BOOTStr;
    MVMObject *BOOTArray;
    MVMObject *BOOTHash;
    MVMObject *BOOTCCode;
    MVMObject *BOOTCode;
};

typedef struct _MVMREPRHashEntry {
    /* index of the REPR */
    MVMuint32 value;
    
    /* the uthash hash handle inline struct. */
    UT_hash_handle hash_handle;
} MVMREPRHashEntry;

/* Represents a MoarVM instance. */
typedef struct _MVMInstance {
    /* The list of active threads. */
    MVMThreadContext **threads;
    
    /* The number of active threads. */
    MVMuint16 num_threads;
    
    /* The KnowHOW meta-object; all other meta-objects (which are
     * built in user-space) are built out of this. */
    MVMObject *KnowHOW;
    
    /* The KnowHOWAttribute meta-object; used for declaring attributes
     * on a KnowHOW. */
    MVMObject *KnowHOWAttribute;
    
    /* Set of bootstrapping types. */
    struct _MVMBootTypes *boot_types;
    
    /* An array mapping representation IDs to function tables. */
    MVMREPROps **repr_registry;

    /* Number of representations registered so far. */
    MVMuint32 num_reprs;

    /* Hash mapping representation names to IDs. */
    MVMREPRHashEntry *repr_name_to_id_hash;
    
    /* The second GC generation allocator. */
    struct _MVMGen2Allocator *gen2;
    
    /* Number of permanent GC roots we've got, allocated space for, and
     * a list of the addresses to them. The mutex controls writing to the
     * list, just in case multiple threads somehow end up doing so. Note
     * that during a GC the world is stopped so reading is safe. */
    MVMuint32             num_permroots;
    MVMuint32             alloc_permroots;
    MVMCollectable     ***permroots;
    apr_thread_mutex_t   *mutex_permroots;
    
    /* The current GC run sequence number. May wrap around over time; that
     * is fine since only equality ever matters. */
    MVMuint32 gc_seq_number;
    
    /* APR memory pool for the instance. */
    apr_pool_t *apr_pool;
    
    /* Number of passed command-line args */
    MVMint64        num_clargs;
    /* raw command line args from APR */
    char          **raw_clargs;
    /* cached parsed command line args */
    MVMObject      *clargs;
    
    /* Atomically-incremented counter of newly invoked frames,
       so each can obtain an index into each threadcontext's pool table */
    MVMuint32 num_frame_pools;
} MVMInstance;
