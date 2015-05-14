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
    MVMString *stdout_chars;
    MVMString *stdout_bytes;
    MVMString *stderr_chars;
    MVMString *stderr_bytes;
    MVMString *buf_type;
    MVMString *write;
    MVMString *nativeref;
    MVMString *refkind;
    MVMString *positional;
    MVMString *lexical;
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
    /* The main thread. */
    MVMThreadContext *main_thread;

    /* The ID to allocate the next-created thread. */
    AO_t next_user_thread_id;

    /* The number of active user threads. */
    MVMuint16 num_user_threads;

    /* The event loop thread, a mutex to avoid start-races, a concurrent
     * queue of tasks that need to be processed by the event loop thread
     * and an array of active tasks, for the purpose of keeping them GC
     * marked. */
    MVMThreadContext *event_loop_thread;
    uv_mutex_t        mutex_event_loop_start;
    MVMObject        *event_loop_todo_queue;
    MVMObject        *event_loop_cancel_queue;
    MVMObject        *event_loop_active;

    /* The VM null object. */
    MVMObject *VMNull;

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

    /* Thread type, representing a VM-level thread. */
    MVMObject *Thread;

    /* Set of bootstrapping types. */
    MVMBootTypes boot_types;

    /* Set of raw types. */
    MVMRawTypes raw_types;

    /* Set of string constants. */
    MVMStringConsts str_consts;

    /* int -> str cache */
    MVMString **int_to_str_cache;

    /* Multi-dispatch cache and specialization installation mutexes
     * (global, as the additions are quite low contention, so no
     * real motivation to have it more fine-grained at present). */
    uv_mutex_t mutex_multi_cache_add;
    uv_mutex_t mutex_spesh_install;

    /* Log file for specializations, if we're to log them. */
    FILE *spesh_log_fh;

    /* Log file for dynamic var performance, if we're to log it. */
    FILE *dynvar_log_fh;

    /* Flag for if spesh (and certain spesh features) are enabled. */
    MVMint8 spesh_enabled;
    MVMint8 spesh_inline_enabled;
    MVMint8 spesh_osr_enabled;

    /* Flag for if NFA debugging is enabled. */
    MVMint8 nfa_debug_enabled;

    /* Flag for if jit is enabled */
    MVMint32 jit_enabled;

    /* File for JIT logging */
    FILE *jit_log_fh;

    /* Directory name for JIT bytecode dumps */
    char *jit_bytecode_dir;

    /* Number of representations registered so far. */
    MVMuint32 num_reprs;

    /* An array mapping representation IDs to registry entries. */
    MVMReprRegistry **repr_list;

    /* A hash mapping representation names to registry entries. */
    MVMReprRegistry *repr_hash;

    /* Mutex for REPR registration. */
    uv_mutex_t mutex_repr_registry;

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
    /* Whether the coordinator considers all in-trays clear. */
    AO_t gc_intrays_clearing;
    /* The number of threads that have yet to acknowledge the finish. */
    AO_t gc_ack;
    /* Linked list (via forwarder) of STables to free. */
    MVMSTable *stables_to_free;

    /* How many bytes of data have we promoted from the nursery to gen2
     * since we last did a full collection? */
    AO_t gc_promoted_bytes_since_last_full;

    /* Persistent object ID hash, used to give nursery objects a lifetime
     * unique ID. Plus a lock to protect it. */
    MVMObjectId *object_ids;
    uv_mutex_t    mutex_object_ids;

    /* MVMThreads completed starting, running, and/or exited. */
    /* note: used atomically */
    MVMThread *threads;

    /* raw command line args from APR */
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

    /* Hashes of HLLConfig objects. compiler_hll_configs is those for the
     * running compiler, and the default. compilee_hll_configs is used if
     * hll_compilee_depth is > 0. */
    MVMHLLConfig *compiler_hll_configs;
    MVMHLLConfig *compilee_hll_configs;
    MVMint64      hll_compilee_depth;
    uv_mutex_t    mutex_hllconfigs;

    /* By far the most common integers are between 0 and 8, but we cache up to 15
     * so that it lines up properly. */
    MVMIntConstCache    *int_const_cache;
    uv_mutex_t mutex_int_const_cache;

    /* Atomically-incremented counter of newly invoked frames, used for
     * lexotic caching. */
    AO_t num_frames_run;

    /* Hash of compiler objects keyed by name */
    MVMObject          *compiler_registry;
    uv_mutex_t    mutex_compiler_registry;

    /* Hash of hashes of symbol tables per hll. */
    MVMObject          *hll_syms;
    uv_mutex_t    mutex_hll_syms;

    MVMContainerRegistry *container_registry;     /* Container registry */
    uv_mutex_t      mutex_container_registry;     /* mutex for container registry */

    /* Hash of all loaded DLLs. */
    MVMDLLRegistry  *dll_registry;
    uv_mutex_t mutex_dll_registry;

    /* Hash of all loaded extensions. */
    MVMExtRegistry  *ext_registry;
    uv_mutex_t mutex_ext_registry;

    /* Hash of all registered extension ops. */
    MVMExtOpRegistry *extop_registry;
    uv_mutex_t  mutex_extop_registry;

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

    /* Hash of filenames of compunits loaded from disk. */
    MVMLoadedCompUnitName *loaded_compunits;
    uv_mutex_t       mutex_loaded_compunits;

    /* Interned callsites. */
    MVMCallsiteInterns *callsite_interns;
    uv_mutex_t          mutex_callsite_interns;

    /* Standard file handles. */
    MVMObject *stdin_handle;
    MVMObject *stdout_handle;
    MVMObject *stderr_handle;

    /* Fixed size allocator. */
    MVMFixedSizeAlloc *fsa;

    /* Normal Form Grapheme state (synthetics table, lookup, etc.). */
    MVMNFGState *nfg;

    /* Next type cache ID, to go in STable. */
    AO_t cur_type_cache_id;

    /* The current instrumentation level. Each time we turn on/off some kind
     * of instrumentation, such as profiling, this is incremented. The next
     * entry to a frame then knows it should instrument or switch back to an
     * uninstrumented version. As a special case, when we start up this is set
     * to 1 which also triggers frame verification. */
    MVMuint32 instrumentation_level;

    /* Whether profiling is turned on or not. */
    MVMuint32 profiling;

    /* Cached backend config hash. */
    MVMObject *cached_backend_config;
};

/* Returns a true value if we have created user threads (and so are running a
 * multi-threaded application). */
MVM_STATIC_INLINE MVMint32 MVM_instance_have_user_threads(MVMThreadContext *tc) {
    return tc->instance->next_user_thread_id != 2;
}
