/* The various "bootstrap" types, based straight off of some core
 * representations. They are used during the 6model bootstrap. */
struct _MVMBootTypes {
    MVMObject *BOOTInt;
    MVMObject *BOOTNum;
    MVMObject *BOOTStr;
    MVMObject *BOOTArray;
    MVMObject *BOOTHash;
    MVMObject *BOOTCCode;
    MVMObject *BOOTCode;
    MVMObject *BOOTThread;
    MVMObject *BOOTIter;
    MVMObject *BOOTContext;
    MVMObject *BOOTIntArray;
    MVMObject *BOOTNumArray;
    MVMObject *BOOTStrArray;
    MVMObject *BOOTIO;
    MVMObject *BOOTException;
};

/* Various common string constants. */
struct _MVMStringConsts {
    struct _MVMString *empty;
    struct _MVMString *Str;
    struct _MVMString *Num;
};

typedef struct _MVMREPRHashEntry {
    /* index of the REPR */
    MVMuint32 value;

    /* the uthash hash handle inline struct. */
    UT_hash_handle hash_handle;
} MVMREPRHashEntry;

/* Represents a MoarVM instance. */
typedef struct _MVMInstance {
    /* The main thread. */
    MVMThreadContext *main_thread;

    /* The ID to allocate the next-created thread. */
    AO_t next_user_thread_id;

    /* The number of active user threads. */
    MVMuint16 num_user_threads;

    /* The KnowHOW meta-object; all other meta-objects (which are
     * built in user-space) are built out of this. */
    MVMObject *KnowHOW;

    /* The KnowHOWAttribute meta-object; used for declaring attributes
     * on a KnowHOW. */
    MVMObject *KnowHOWAttribute;

    /* The VM's native string type, using MVMString. Note that this is a
     * native string, not an object boxing one. */
    MVMObject *VMString;

    /* Serialization context type (known as SCRef, but it's actually the
     * serialization context itself). */
    MVMObject *SCRef;

    /* Lexotic type, used in implementing return handling. */
    MVMObject *Lexotic;

    /* CallCapture type, used by custom dispatchers. */
    MVMObject *CallCapture;

    /* Set of bootstrapping types. */
    struct _MVMBootTypes *boot_types;

    /* Set of string constants. */
    struct _MVMStringConsts *str_consts;

    /* An array mapping representation IDs to function tables. */
    MVMREPROps **repr_registry;

    /* Number of representations registered so far. */
    MVMuint32 num_reprs;

    /* Hash mapping representation names to IDs. */
    MVMREPRHashEntry *repr_name_to_id_hash;

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
    /* The number of threads that vote for starting GC. */
    AO_t gc_start;
    /* The number of threads that still need to vote for considering GC done. */
    AO_t gc_finish;
    /* The number of threads that have yet to acknowledge the finish. */
    AO_t gc_ack;

    /* MVMThreads completed starting, running, and/or exited. */
    struct _MVMThread *threads;

    /* Linked list of compilation units that we have loaded. */
    struct _MVMCompUnit *head_compunit;

    /* APR memory pool for the instance. */
    apr_pool_t *apr_pool;

    /* Number of passed command-line args */
    MVMint64        num_clargs;
    /* raw command line args from APR */
    char          **raw_clargs;
    /* cached parsed command line args */
    MVMObject      *clargs;

    /* Hash of HLLConfig objects. */
    struct _MVMHLLConfig *hll_configs;
    apr_thread_mutex_t   *mutex_hllconfigs;

    /* Atomically-incremented counter of newly invoked frames,
     * so each can obtain an index into each threadcontext's pool table */
    MVMuint32 num_frame_pools;

    /* Hash of compiler objects keyed by name */
    MVMObject          *compiler_registry;
    apr_thread_mutex_t *mutex_compiler_registry;

    /* Hash of hashes of symbol tables per hll. */
    MVMObject          *hll_syms;
    apr_thread_mutex_t *mutex_hll_syms;

    /* mutex for container registry */
    apr_thread_mutex_t *mutex_container_registry;

    /* Hash of all known serialization contexts. Not marked for GC; an SC
     * removes it from this when it gets GC'd. */
    struct _MVMSerializationContextBody *sc_weakhash;
    apr_thread_mutex_t                  *mutex_sc_weakhash;
} MVMInstance;
