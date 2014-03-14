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
    MVMString *type;
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
};

struct MVMReprRegistry {
    /* name of the REPR */
    MVMString *name;

    /* the REPR vtable */
    const MVMREPROps *repr;

    /* the uthash hash handle inline struct. */
    UT_hash_handle hash_handle;
};

#if MVM_HLL_PROFILE_CALLS
typedef struct _MVMCallsiteProfileData {
    MVMuint32 static_frame_id;
    MVMuint8 *cuuid, *name;

} MVMCallsiteProfileData;
#endif

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
    MVMBootTypes boot_types;

    /* Set of raw types. */
    MVMRawTypes raw_types;

    /* Set of string constants. */
    MVMStringConsts str_consts;

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
     * the item is unresolved. */
    MVMSerializationContextBody *sc_weakhash;
    uv_mutex_t                   mutex_sc_weakhash;

    /* Hash of filenames of compunits loaded from disk. */
    MVMLoadedCompUnitName *loaded_compunits;
    uv_mutex_t       mutex_loaded_compunits;

    MVMObject *stdin_handle;
    MVMObject *stdout_handle;
    MVMObject *stderr_handle;

    /* Next type cache ID, to go in STable. */
    AO_t cur_type_cache_id;

#if MVM_HLL_PROFILE_CALLS
    /* allocated size of profile_data in count */
    MVMuint32 callsite_index;
    /* next index of record to store */
    MVMuint32 profile_index;
#endif
};
