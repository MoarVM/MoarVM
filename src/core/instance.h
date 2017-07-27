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
    MVMObject *BOOTMultiCache;
    MVMObject *BOOTContinuation;
    MVMObject *BOOTQueue;
    MVMObject *BOOTAsync;
    MVMObject *BOOTReentrantMutex;
};

/* Various raw types that don't need a HOW */
typedef struct {
    MVMObject *RawDLLSym;
} MVMRawTypes;

/* Various common string constants. */
struct MVMStringConsts {
    MVMString *empty;
    MVMString *Str;
    MVMString *Num;
    MVMString *integer;
    MVMString *float_str;
    MVMString *bits;
    MVMString *unsigned_str;
    MVMString *find_method;
    MVMString *type_check;
    MVMString *accepts_type;
    MVMString *name;
    MVMString *attribute;
    MVMString *of;
    MVMString *rw;
    MVMString *type;
    MVMString *typeobj;
    MVMString *free_str;
    MVMString *callback_args;
    MVMString *encoding;
    MVMString *inlined;
    MVMString *repr;
    MVMString *anon;
    MVMString *P6opaque;
    MVMString *array;
    MVMString *box_target;
    MVMString *positional_delegate;
    MVMString *associative_delegate;
    MVMString *auto_viv_container;
    MVMString *done;
    MVMString *error;
    MVMString *stdout_bytes;
    MVMString *stderr_bytes;
    MVMString *merge_bytes;
    MVMString *buf_type;
    MVMString *write;
    MVMString *stdin_fd;
    MVMString *stdout_fd;
    MVMString *stderr_fd;
    MVMString *nativeref;
    MVMString *refkind;
    MVMString *positional;
    MVMString *lexical;
    MVMString *dimensions;
    MVMString *ready;
    MVMString *multidim;
    MVMString *entry_point;
    MVMString *kind;
    MVMString *instrumented;
    MVMString *heap;
    MVMString *translate_newlines;
    MVMString *platform_newline;
};

/* An entry in the representations registry. */
struct MVMReprRegistry {
    /* name of the REPR */
    MVMString *name;

    /* the REPR vtable */
    const MVMREPROps *repr;

    /* the uthash hash handle inline struct. */
    UT_hash_handle hash_handle;
};

/* An entry in the persistent object IDs hash, used to give still-movable
 * objects a lifetime-unique ID. */
struct MVMObjectId {
    /* The current object address. */
    MVMObject *current;

    /* Then gen2 address that forms the persistent ID, and where we'll move
     * the object to if it lives long enough. */
    MVMCollectable *gen2_addr;

    /* Hash handle. */
    UT_hash_handle hash_handle;
};

/* Represents a MoarVM instance. */
struct MVMInstance {
    /************************************************************************
     * Threads
     ************************************************************************/

    /* The main thread. */
    MVMThreadContext *main_thread;

    /* The ID to allocate the next-created thread. */
    AO_t next_user_thread_id;

    /* MVMThreads completed starting, running, and/or exited. Modifications
     * and walks that need an accurate picture of it protected by mutex. */
    MVMThread *threads;
    uv_mutex_t mutex_threads;

    /************************************************************************
     * Garbage collection and memory management
     ************************************************************************/

    /* Number of permanent GC roots we've got, allocated space for, and
     * a list of the addresses to them. The mutex controls writing to the
     * list, just in case multiple threads somehow end up doing so. Note
     * that during a GC the world is stopped so reading is safe. We also
     * keep a list of names for these, for the purpose of heap debugging
     * and heap profiling. */
    MVMuint32             num_permroots;
    MVMuint32             alloc_permroots;
    MVMCollectable     ***permroots;
    char                **permroot_descriptions;
    uv_mutex_t            mutex_permroots;

    /* The current GC run sequence number. May wrap around over time; that
     * is fine since only equality ever matters. */
    AO_t gc_seq_number;

    /* Mutex used to protect GC orchestration state, and held to wait on or
     * signal condition variable changes. */
    uv_mutex_t mutex_gc_orchestrate;

    /* The number of threads that vote for starting GC, and condition variable
     * for when it changes. */
    AO_t gc_start;
    uv_cond_t cond_gc_start;

    /* The number of threads that still need to vote for considering GC done,
     * and condition variable for when it changes. */
    AO_t gc_finish;
    uv_cond_t cond_gc_finish;

    /* Whether the coordinator considers all in-trays clear, and condition
     * variable for when it changes. */
    AO_t gc_intrays_clearing;
    uv_cond_t cond_gc_intrays_clearing;

    /* The number of threads that have yet to acknowledge the finish. */
    AO_t gc_ack;

    /* Linked list (via forwarder) of STables to free. */
    MVMSTable *stables_to_free;

    /* Whether the current GC run is a full collection. */
    MVMuint32 gc_full_collect;

    /* How many bytes of data have we promoted from the nursery to gen2
     * since we last did a full collection? */
    AO_t gc_promoted_bytes_since_last_full;

    /* Persistent object ID hash, used to give nursery objects a lifetime
     * unique ID. Plus a lock to protect it. */
    MVMObjectId *object_ids;
    uv_mutex_t    mutex_object_ids;

    /* Fixed size allocator. */
    MVMFixedSizeAlloc *fsa;

    /************************************************************************
     * Object system
     ************************************************************************/

    /* Number of representations registered so far. */
    MVMuint32 num_reprs;

    /* An array mapping representation IDs to registry entries. */
    MVMReprRegistry **repr_list;

    /* A hash mapping representation names to registry entries. */
    MVMReprRegistry *repr_hash;

    /* Mutex for REPR registration. */
    uv_mutex_t mutex_repr_registry;

    /* Container type registry and mutex to protect it. */
    MVMContainerRegistry *container_registry;
    uv_mutex_t      mutex_container_registry;

    /* Hash of all known serialization contexts. Marked for GC iff
     * the item is unresolved. Also, array of all SCs, used for the
     * index stored in object headers. When an SC goes away this is
     * simply nulled. That makes it a small memory leak if a lot of
     * SCs are created and go away over time. */
    MVMSerializationContextBody  *sc_weakhash;
    uv_mutex_t                    mutex_sc_weakhash;
    MVMSerializationContextBody **all_scs;
    MVMuint32                     all_scs_next_idx;
    MVMuint32                     all_scs_alloc;

    /************************************************************************
     * Specializer (dynamic optimization)
     ************************************************************************/

    /* Log file for specializations, if we're to log them. */
    FILE *spesh_log_fh;

    /* Flag for if spesh (and certain spesh features) are enabled. */
    MVMint8 spesh_enabled;
    MVMint8 spesh_inline_enabled;
    MVMint8 spesh_osr_enabled;
    MVMint8 spesh_nodelay;
    MVMint8 spesh_blocking;

    /* Number of specializations produced, and limit on number of
     * specializations (zero if no limit). */
    MVMint32 spesh_produced;
    MVMint32 spesh_limit;

    /* Mutex taken when install specializations. */
    uv_mutex_t mutex_spesh_install;

    /* The concurrent queue used to send logs to spesh_thread, provided it
     * is enabled. */
    MVMObject *spesh_queue;

    /* The current specialization plan; hung off here so we can mark it. */
    MVMSpeshPlan *spesh_plan;

    /* The latest statistics version (incremented each time a spesh log is
     * received by the worker thread). */
    MVMuint32 spesh_stats_version;

    /* Lock and condition variable for when something needs to wait for the
     * specialization worker to finish what it's doing before continuing.
     * Used by the profiler, which doesn't want the specializer tripping over
     * frame bytecode changing to instrumented versions. */
    uv_mutex_t mutex_spesh_sync;
    uv_cond_t cond_spesh_sync;
    MVMuint32 spesh_working;

    /************************************************************************
     * JIT compilation
     ************************************************************************/

    /* Flag for if jit is enabled */
    MVMint32 jit_enabled;

    /* File for JIT logging */
    FILE *jit_log_fh;

    /* Directory name for JIT bytecode dumps */
    char *jit_bytecode_dir;

    /* File for map of frame information for bytecode dumps */
    FILE *jit_bytecode_map;

    /* sequence number for JIT compiled frames */
    AO_t  jit_seq_nr;

    /************************************************************************
     * I/O and process state
     ************************************************************************/

    /* The event loop thread, a mutex to avoid start-races, a concurrent
     * queue of tasks that need to be processed by the event loop thread
     * and an array of active tasks, for the purpose of keeping them GC
     * marked. */
    MVMThreadContext *event_loop_thread;
    uv_mutex_t        mutex_event_loop_start;
    uv_sem_t          sem_event_loop_started;
    MVMObject        *event_loop_todo_queue;
    MVMObject        *event_loop_permit_queue;
    MVMObject        *event_loop_cancel_queue;
    MVMObject        *event_loop_active;
    uv_async_t       *event_loop_wakeup;

    /* Standard file handles. */
    MVMObject *stdin_handle;
    MVMObject *stdout_handle;
    MVMObject *stderr_handle;

    /* Raw command line args */
    char          **raw_clargs;
    /* Number of passed command-line args */
    MVMint64        num_clargs;
    /* executable name */
    const char     *exec_name;
    /* program name; becomes first clargs entry */
    const char     *prog_name;
    /* cached parsed command line args */
    MVMObject      *clargs;
    /* Any --libpath=... options, to prefix in loadbytecode lookups. */
    const char     *lib_path[8];

    /************************************************************************
     * Caching and interning
     ************************************************************************/

    /* int -> str cache */
    MVMString **int_to_str_cache;

    /* By far the most common integers are between 0 and 8, but we cache up to 15
     * so that it lines up properly. */
    MVMIntConstCache    *int_const_cache;
    uv_mutex_t mutex_int_const_cache;

    /* Multi-dispatch cache addition mutex (additions are relatively
     * rare, so little motivation to have it more fine-grained). */ 
    uv_mutex_t mutex_multi_cache_add;

    /* Next type cache ID, to go in STable. */
    AO_t cur_type_cache_id;

    /* Cached backend config hash. */
    MVMObject *cached_backend_config;

    /* Interned callsites. */
    MVMCallsiteInterns *callsite_interns;
    uv_mutex_t          mutex_callsite_interns;

    /* Normal Form Grapheme state (synthetics table, lookup, etc.). */
    MVMNFGState *nfg;

    /************************************************************************
     * Type objects for built-in types and special values
     ************************************************************************/

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

    /* CallCapture type, used by custom dispatchers. */
    MVMObject *CallCapture;

    /* Thread type, representing a VM-level thread. */
    MVMObject *Thread;

    /* SpeshLog type, for passing specialization logs between threads, and
     * StaticFrameSpesh type for hanging spesh data off frames. */
    MVMObject *SpeshLog;
    MVMObject *StaticFrameSpesh;

    /* Set of bootstrapping types. */
    MVMBootTypes boot_types;

    /* Set of raw types. */
    MVMRawTypes raw_types;

    /* The VM null object. */
    MVMObject *VMNull;

    /* Set of string constants. */
    MVMStringConsts str_consts;

    /************************************************************************
     * Per-language state, compiler registry, and VM extensions
     ************************************************************************/

    /* Hashes of HLLConfig objects. compiler_hll_configs is those for the
     * running compiler, and the default. compilee_hll_configs is used if
     * hll_compilee_depth is > 0. */
    MVMHLLConfig *compiler_hll_configs;
    MVMHLLConfig *compilee_hll_configs;
    MVMint64      hll_compilee_depth;
    uv_mutex_t    mutex_hllconfigs;

    /* Hash of hashes of symbol tables per hll. */
    MVMObject          *hll_syms;
    uv_mutex_t    mutex_hll_syms;

    /* Hash of compiler objects keyed by name */
    MVMObject          *compiler_registry;
    uv_mutex_t    mutex_compiler_registry;

    /* Hash of filenames of compunits loaded from disk. */
    MVMLoadedCompUnitName *loaded_compunits;
    uv_mutex_t       mutex_loaded_compunits;

    /* Hash of all loaded DLLs. */
    MVMDLLRegistry  *dll_registry;
    uv_mutex_t mutex_dll_registry;

    /* Hash of all loaded extensions. */
    MVMExtRegistry  *ext_registry;
    uv_mutex_t mutex_ext_registry;

    /* Hash of all registered extension ops. */
    MVMExtOpRegistry *extop_registry;
    uv_mutex_t  mutex_extop_registry;

    /************************************************************************
     * Bytecode instrumentations (profiler, coverage, etc.)
     ************************************************************************/

    /* The current instrumentation level. Each time we turn on/off some kind
     * of instrumentation, such as profiling, this is incremented. The next
     * entry to a frame then knows it should instrument or switch back to an
     * uninstrumented version. As a special case, when we start up this is set
     * to 1 which also triggers frame verification. */
    MVMuint32 instrumentation_level;

    /* Whether instrumented profiling is turned on or not. */
    MVMuint32 profiling;

    /* Heap snapshots, if we're doing heap snapshotting. */
    MVMHeapSnapshotCollection *heap_snapshots;

    /* Whether cross-thread write logging is turned on or not, and an output
     * mutex for it. */
    MVMuint32  cross_thread_write_logging;
    MVMuint32  cross_thread_write_logging_include_locked;
    uv_mutex_t mutex_cross_thread_write_logging;

    /* Log file for coverage logging. */
    MVMuint32  coverage_logging;
    FILE *coverage_log_fh;
    MVMuint32  coverage_control;

    /************************************************************************
     * Debugging
     ************************************************************************/

    /* Log file for dynamic var performance, if we're to log it. */
    FILE *dynvar_log_fh;
    MVMint64 dynvar_log_lasttime;

    /* Flag for if NFA debugging is enabled. */
    MVMint8 nfa_debug_enabled;
};
