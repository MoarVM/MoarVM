/* Possible values for the thread execution interrupt flag. */
typedef enum {
    /* Indicates that the thread is currently executing, and should
     * continue to do so. */
    MVMGCStatus_NONE = 0,

    /* Set when another thread decides it wants to do a GC run. The
     * current thread, on detecting this condition at a safe point,
     * should join in with the current GC run. */
    MVMGCStatus_INTERRUPT = 1,

    /* Set by a thread when it is unable to do any GC work because it
     * is currently blocked waiting on an operation in the outside
     * world (such as, waiting for another thread to join, or for
     * some I/O to complete). */
    MVMGCStatus_UNABLE = 2,

    /* Indicates that, while the thread was in unable status, a GC
     * run was triggered and the scanning work was stolen. A thread
     * that becomes unblocked upon seeing this will wait for the GC
     * run to be done. */
    MVMGCStatus_STOLEN = 3
} MVMGCStatus;

/* Are we allocating in the nursery, or direct into generation 2? (The
 * latter is used in the case of deserialization, when we know the
 * incoming objects are likely to survive, but also don't want to have
 * to worry about triggering GC in the deserialization process. */
typedef enum {
    /* Allocate in the nursery. */
    MVMAllocate_Nursery = 0,

    /* Allocate straight into generation 2. */
    MVMAllocate_Gen2    = 1
} MVMAllocationTarget;

/* To manage memory more efficiently, we cache MVMFrame instances.
 * The initial frame pool table size sets the initial guess at the
 * number of different types of frame (that is, an MVMStaticFrame)
 * that we'll encounter and cache. If we do deep recursion, we run
 * the risk of caching an enormous number of frames, so the length
 * limit sets how many frames of a given static frame type we will
 * keep around. */
#define MVMInitialFramePoolTableSize    64
#define MVMFramePoolLengthLimit         64

#if MVM_HLL_PROFILE_CALLS
typedef struct _MVMProfileRecord {
    MVMuint32 callsite_id;
    MVMuint32 code_id;
    MVMuint64 duration_nanos;
} MVMProfileRecord;
#endif

/* Information associated with an executing thread. */
struct MVMThreadContext {
    /* The current allocation pointer, where the next object to be allocated
     * should be placed. */
    void *nursery_alloc;

    /* The end of the space we're allowed to allocate to. */
    void *nursery_alloc_limit;

    /* This thread's GC status. */
    AO_t gc_status;

    /* Where we're allocating. */
    MVMAllocationTarget allocate_in;

    /* Internal ID of the thread. */
    MVMuint32 thread_id;

    /* Thread object representing the thread. */
    MVMThread *thread_obj;

    /* The frame lying at the base of the current thread. */
    MVMFrame *thread_entry_frame;

    /* Pointer to where the interpreter's current opcode is stored. */
    MVMuint8 **interp_cur_op;

    /* Pointer to where the interpreter's bytecode start pointer is stored. */
    MVMuint8 **interp_bytecode_start;

    /* Pointer to where the interpreter's base of the current register
     * set is stored. */
    MVMRegister **interp_reg_base;

    /* Pointer to where the interpreter's current compilation unit pointer
     * is stored. */
    MVMCompUnit **interp_cu;

    /* The frame we're currently executing. */
    MVMFrame *cur_frame;

    /* libuv event loop */
    uv_loop_t *loop;

    /* The usecapture op can, without allocating, have a way to talk about the
     * arguments of the current call. This is the (pre-thread) object that is
     * used by that op. */
    MVMObject *cur_usecapture;

    /* Linked list of exception handlers that we're currently executing, topmost
     * one first in the list. */
    MVMActiveHandler *active_handlers;
    
    /* Result object of the last-run exception handler. */
    MVMObject *last_handler_result;

    /* Mutex that must be released if we throw an exception. Used in places
     * like I/O, which grab a mutex but may throw an exception. */
    uv_mutex_t *ex_release_mutex;

    /* The VM instance that this thread belongs to. */
    MVMInstance *instance;

    /* Start of fromspace, the place we're copying objects from during a
     * copying collection or processing dead objects that need to do extra
     * resource release afterwards. */
    void *nursery_fromspace;

    /* Where we evacuate objects to when collecting this thread's nursery, or
     * allocate new ones. */
    void *nursery_tospace;

    /* The second GC generation allocator. */
    MVMGen2Allocator *gen2;

    /* Memory buffer pointing to the last thing we serialized, intended to go
     * into the next compilation unit we write. */
    char         *serialized;
    MVMint32      serialized_size;

    /* Temporarily rooted objects. This is generally used by code written in
     * C that wants to keep references to objects. Since those may change
     * if the code in question also allocates, there is a need to register
     * them; this ensures the GC will not swallow them but also that they
     * will get updated if a GC run happens. Note that this is used as a
     * stack and is also thread-local, so it's cheap to push/pop. */
    MVMuint32             num_temproots;
    MVMuint32             mark_temproots;
    MVMuint32             alloc_temproots;
    MVMCollectable     ***temproots;

    /* Nursery collectables (maybe STables) rooted because something in
     * generation 2 is pointing at them. */
    MVMuint32             num_gen2roots;
    MVMuint32             alloc_gen2roots;
    MVMCollectable      **gen2roots;

    /* The GC's cross-thread in-tray of processing work. */
    MVMGCPassedWork *gc_in_tray;

    /* Threads we will do GC work for this run (ourself plus any that we stole
     * work from because they were blocked). */
    MVMWorkThread   *gc_work;
    MVMuint32        gc_work_size;
    MVMuint32        gc_work_count;

    /* Pool table of chains of frames for each static frame. */
    MVMFrame **frame_pool_table;

    /* Pool of Lexotics for various static frames, held per thread since the
     * result being returned is per thread. Indexes are same as used in the
     * frame_pool_table above. */
    MVMLexotic **lexotic_cache;

    /* Size of the frame pool table and lexotic cache, so they can grow on
     * demand. */
    MVMuint32 frame_pool_table_size;
    MVMuint32 lexotic_cache_size;

    /* Serialization context write barrier disabled depth (anything non-zero
     * means disabled). */
    MVMint32           sc_wb_disable_depth;

    /* Any serialization contexts we are compiling. The current one is at
     * index 0. */
    MVMObject     *compiling_scs;

    /* Dispatcher set for next invocation to take. */
    MVMObject     *cur_dispatcher;

    /* Cache of native code callback data. */
    MVMNativeCallbackCacheHead *native_callback_cache;

    /* Random number generator state. */
    MVMuint64 rand_state[2];

    /* Jump buffer, used when an exception is thrown from C-land and we need
     * to fall back into the interpreter. These things are huge, so put it
     * near the end to keep the hotter stuff on the same cacheline. */
    jmp_buf interp_jump;

    /* NFA evaluator memory cache, to avoid many allocations; see NFA.c. */
    MVMint64 *nfa_done;
    MVMint64 *nfa_curst;
    MVMint64 *nfa_nextst;
    MVMint64  nfa_alloc_states;
    MVMint64 *nfa_fates;
    MVMint64  nfa_fates_len;

#if MVM_HLL_PROFILE_CALLS
    /* storage of profile timings */
    MVMProfileRecord *profile_data;
    /* allocated size of profile_data in count */
    MVMuint32 profile_data_size;
    /* next index of record to store */
    MVMuint32 profile_index;
#endif
};

MVMThreadContext * MVM_tc_create(MVMInstance *instance);
void MVM_tc_destroy(MVMThreadContext *tc);
void MVM_tc_set_ex_release_mutex(MVMThreadContext *tc, uv_mutex_t *mutex);
void MVM_tc_release_ex_release_mutex(MVMThreadContext *tc);
void MVM_tc_clear_ex_release_mutex(MVMThreadContext *tc);
