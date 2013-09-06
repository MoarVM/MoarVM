/* The various "bootstrap" types, based straight off of some core
 * representations. They are used during the 6model bootstrap. */
struct MVMBootTypes {
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
    MVMObject *BOOTStaticFrame;
    MVMObject *BOOTCompUnit;
};

/* Various common string constants. */
struct MVMStringConsts {
    MVMString *empty;
    MVMString *Str;
    MVMString *Num;
};

struct MVMREPRHashEntry {
    /* index of the REPR */
    MVMuint32 value;

    /* the uthash hash handle inline struct. */
    UT_hash_handle hash_handle;
};

/* Represents a MoarVM instance. */
struct MVMInstance {
    /* libuv loop */
    uv_loop_t *default_loop;

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
    MVMBootTypes *boot_types;

    /* Set of string constants. */
    MVMStringConsts *str_consts;

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
    uv_mutex_t            mutex_permroots;

    /* The current GC run sequence number. May wrap around over time; that
     * is fine since only equality ever matters. */
    AO_t gc_seq_number;
    /* The number of threads that vote for starting GC. */
    AO_t gc_start;
    /* The number of threads that still need to vote for considering GC done. */
    AO_t gc_finish;
    /* The number of threads that have yet to acknowledge the finish. */
    AO_t gc_ack;
    /* Linked list (via forwarder) of STables to free. */
    MVMSTable *stables_to_free;

    /* MVMThreads completed starting, running, and/or exited. */
    /* note: used atomically */
    MVMThread *threads;

    /* Number of passed command-line args */
    MVMint64        num_clargs;
    /* raw command line args from APR */
    char          **raw_clargs;
    /* program name; becomes first clargs entry */
    const char     *prog_name;
    /* cached parsed command line args */
    MVMObject      *clargs;
    
    /* Cached environment. */
    MVMObject *env_hash;

    /* Hash of HLLConfig objects. */
    MVMHLLConfig *hll_configs;
    uv_mutex_t    mutex_hllconfigs;

    /* Atomically-incremented counter of newly invoked frames,
     * so each can obtain an index into each threadcontext's pool table */
    AO_t num_frame_pools;

    /* Hash of compiler objects keyed by name */
    MVMObject          *compiler_registry;
    uv_mutex_t    mutex_compiler_registry;

    /* Hash of hashes of symbol tables per hll. */
    MVMObject          *hll_syms;
    uv_mutex_t    mutex_hll_syms;

    MVMContainerRegistry *container_registry;     /* Container registry */
    uv_mutex_t      mutex_container_registry;     /* mutex for container registry */

    /* Hash of all known serialization contexts. Marked for GC iff
     * the item is unresolved. */
    MVMSerializationContextBody *sc_weakhash;
    uv_mutex_t                   mutex_sc_weakhash;

    /* Hash of filenames of compunits loaded from disk. */
    MVMLoadedCompUnitName *loaded_compunits;
    uv_mutex_t       mutex_loaded_compunits;
};
