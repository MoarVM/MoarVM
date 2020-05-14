#define MVMGCSTATUS_MASK 3
#define MVMSUSPENDSTATUS_MASK 12

#define MVM_NUM_TEMP_BIGINTS 3
#include "tommath.h"

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
    MVMGCStatus_STOLEN = 3,
} MVMGCStatus;

typedef enum {
    /* Indicates the thread shall continue executing. */
    MVMSuspendState_NONE = 0,

    /* Indicates the thread shall suspend execution as soon as practical. */
    MVMSuspendState_SUSPEND_REQUEST = 4,

    /* Indicates the thread has suspended execution and is waiting for
     * a signal to resume execution. */
    MVMSuspendState_SUSPENDED = 12,
} MVMSuspendStatus;

typedef enum {
    /* Just pass by any line number annotation */
    MVMDebugSteppingMode_NONE = 0,

    /* Step Over:
     *  Line annotation: If the line number doesn't match, but the frame does.
     *  Return from Frame: If the frame matches. */
    MVMDebugSteppingMode_STEP_OVER = 1,

    /* Step Into:
     *  Line annotation: If the line number doesn't match in the same frame,
     *    if the frame doesn't match.
     *  Return from Frame: If the frame matches. */
    MVMDebugSteppingMode_STEP_INTO = 2,

    /* Step Out:
     *  Line annotation: -
     *  Return from Frame: If the frame matches. */
    MVMDebugSteppingMode_STEP_OUT = 3,
} MVMDebugSteppingMode;


/* Information associated with an executing thread. */
struct MVMThreadContext {
    /************************************************************************
     * Information about this thread
     ************************************************************************/

    /* Internal ID of the thread. */
    MVMuint32 thread_id;

    /* Thread object representing the thread. */
    MVMThread *thread_obj;

    /* The VM instance that this thread belongs to. */
    MVMInstance *instance;

    /* The number of locks the thread is holding. */
    MVMint64 num_locks;

    /************************************************************************
     * Garbage collection and memory management
     ************************************************************************/

    /* Start of fromspace, the place we're copying objects from during a
     * copying collection or processing dead objects that need to do extra
     * resource release afterwards. */
    void *nursery_fromspace;

    /* Where we evacuate objects to when collecting this thread's nursery, or
     * allocate new ones. */
    void *nursery_tospace;

    /* The current allocation pointer, where the next object to be allocated
     * should be placed. */
    void *nursery_alloc;

    /* The end of the space we're allowed to allocate to. */
    void *nursery_alloc_limit;

    /* This thread's GC status. */
    AO_t gc_status;

    /* The second GC generation allocator. */
    MVMGen2Allocator *gen2;

    /* The current sizes of the nursery fromspace/tospace for this thread, in
     * bytes. Used to handle growing it over time depending on usage. */
    MVMuint32 nursery_fromspace_size;
    MVMuint32 nursery_tospace_size;

    /* Non-zero is we should allocate in gen2; incremented/decremented as we
     * enter/leave a region wanting gen2 allocation. */
    MVMuint32 allocate_in_gen2;

    /* Number of bytes promoted to gen2 in current GC run. */
    MVMuint32 gc_promoted_bytes;

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

    /* Finalize queue objects, which need to have a finalizer invoked once
     * they are no longer referenced from anywhere except this queue. */
    MVMuint32             num_finalize;
    MVMuint32             alloc_finalize;
    MVMObject           **finalize;

    /* List of objects we're in the process of finalizing. */
    MVMuint32             num_finalizing;
    MVMuint32             alloc_finalizing;
    MVMObject           **finalizing;

    /* The GC's cross-thread in-tray of processing work. */
    MVMGCPassedWork *gc_in_tray;

    /* Threads we will do GC work for this run (ourself plus any that we stole
     * work from because they were blocked). */
    MVMWorkThread   *gc_work;
    MVMuint32        gc_work_size;
    MVMuint32        gc_work_count;

    /* Per-thread fixed size allocator state. */
    MVMFixedSizeAllocThread *thread_fsa;

    /************************************************************************
     * Interpreter state
     ************************************************************************/

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

    /* Jump buffer, used when an exception is thrown from C-land and we need
     * to fall back into the interpreter. These things are huge, so put it
     * near the end to keep the hotter stuff on the same cacheline. */
    jmp_buf interp_jump;

    /************************************************************************
     * Frames, call stack, and exception state
     ************************************************************************/

    /* The bytecode frame we're currently executing. */
    MVMFrame *cur_frame;

    /* The frame lying at the base of the current thread. */
    MVMFrame *thread_entry_frame;

    /* First call stack memory region, so we can traverse them for cleanup. */
    MVMCallStackRegion *stack_first_region;

    /* Current call stack region, which the next record will be allocated
     * so long as there is space. */
    MVMCallStackRegion *stack_current_region;

    /* The current call stack record on the top of the stack. */
    MVMCallStackRecord *stack_top;

    /* Linked list of exception handlers that we're currently executing, topmost
     * one first in the list. */
    MVMActiveHandler *active_handlers;

    /* Result object of the last-run exception handler. */
    MVMObject *last_handler_result;

    /* Last payload made available in a payload-goto exception handler. */
    MVMObject *last_payload;

    /************************************************************************
     * Specialization and JIT compilation
     ************************************************************************/

    /* Frame sequence numbers in order to cheaply identify the place of a frame
     * in the call stack */
    MVMint32 current_frame_nr;
    MVMint32 next_frame_nr;

    /* JIT return address pointer, so we can figure out the current position in
     * the code */
    void **jit_return_address;

    /* This thread's current spesh log to write in to, if there curently is
     * one. */
    MVMSpeshLog *spesh_log;

    /* How many spesh logs we can produce, inclusive of the current one.
     * Ensures the spesh worker isn't overwhelmed with data before it has a
     * change to produce some specializations. */
    AO_t spesh_log_quota;

    /* The spesh stack simulation, perserved between processing logs. */
    MVMSpeshSimStack *spesh_sim_stack;

    /* The current spesh graph that we are optimizing, retained here so we
     * can GC mark it and so be able to GC at certain points during the
     * optimization process, giving less GC latency. */
    MVMSpeshGraph *spesh_active_graph;

    /* We try to do better at OSR by creating a fresh log when we enter a new
     * compilation unit. However, for things that EVAL or do a ton of BEGIN,
     * we risk high memory use. Use this to throttle it by limiting the number
     * of such extra logs that might exist at a time. */
    AO_t num_compunit_extra_logs;

    /* The current specialization correlation ID, used in logging. */
    MVMuint32 spesh_cid;

#if MVM_GC_DEBUG
    /* Whether we are currently in the specializer. Used to catch GC runs that
     * take place at times they never should. */
    MVMint32 in_spesh;
#endif

    /* State to cheaply determine if we should look again for the availability
     * of optimzied code at an OSR point. When the current state seen by the
     * interpreter of frame number of spesh candidates matches, we know there
     * was no change since the last OSR point. */
    MVMint32 osr_hunt_frame_nr;
    MVMint32 osr_hunt_num_spesh_candidates;

    /* If we are currently in a spesh plugin, the current set of guards we
     * have recorded. */
    MVMSpeshPluginGuard *plugin_guards;
    MVMObject *plugin_guard_args;
    MVMuint32 num_plugin_guards;

    MVMSpeshPluginGuard *temp_plugin_guards;
    MVMObject *temp_plugin_guard_args;
    MVMuint32 temp_num_plugin_guards;

    /************************************************************************
     * Per-thread state held by assorted VM subsystems
     ************************************************************************/

    /* Mutex that must be released if we throw an exception. Used in places
     * like I/O, which grab a mutex but may throw an exception. */
    uv_mutex_t *ex_release_mutex;

    /* Serialization context write barrier disabled depth (anything non-zero
     * means disabled). */
    MVMint32           sc_wb_disable_depth;

    /* Any serialization contexts we are compiling. The current one is at
     * index 0. */
    MVMObject     *compiling_scs;

    /* Dispatcher for next invocation that matches _for to take. If _for is
     * NULL then anything matches. */
    MVMObject     *cur_dispatcher;
    MVMObject     *cur_dispatcher_for;

    /* Dispatcher to return control to when current dispatcher exhausts. */
    MVMObject     *next_dispatcher;
    MVMObject     *next_dispatcher_for;

    /* Cache of native code callback data. */
    MVMStrHashTable native_callback_cache;

    /* Random number generator state. */
    MVMuint64 rand_state[2];

    /* Temporary big integers for when we need to upgrade operands in order
     * to perform an operation. */
    mp_int *temp_bigints[MVM_NUM_TEMP_BIGINTS];

    /* NFA evaluator memory cache, to avoid many allocations; see NFA.c. */
    MVMuint32 *nfa_done;
    MVMuint32 *nfa_curst;
    MVMuint32 *nfa_nextst;
    MVMint64   nfa_alloc_states;
    MVMint64 *nfa_fates;
    MVMint64  nfa_fates_len;
    MVMint64 *nfa_longlit;
    MVMint64  nfa_longlit_len;

    /* Memory for doing multi-dim indexing with late-bound dimension counts. */
    MVMint64 *multi_dim_indices;
    MVMint64  num_multi_dim_indices;

    /* Profiling data collected for this thread, if profiling is on. */
    MVMProfileThreadData *prof_data;

    /* Debug server stepping mode and settings */
    MVMDebugSteppingMode step_mode;
    MVMFrame *step_mode_frame;
    MVMuint32 step_mode_file_idx;
    MVMuint32 step_mode_line_no;
    MVMuint64 step_message_id;

    /* Whether the debugserver could request an invocation here.
     * Requires the current op to be marked invokish and do the
     * necessary things to cur_op and such. */
    MVMuint8  debugserver_can_invoke_here;

    MVMuint32 cur_file_idx;
    MVMuint32 cur_line_no;

    int nested_interpreter;
};

MVMThreadContext * MVM_tc_create(MVMThreadContext *parent, MVMInstance *instance);
void MVM_tc_destroy(MVMThreadContext *tc);
void MVM_tc_set_ex_release_mutex(MVMThreadContext *tc, uv_mutex_t *mutex);
void MVM_tc_set_ex_release_atomic(MVMThreadContext *tc, AO_t *flag);
void MVM_tc_release_ex_release_mutex(MVMThreadContext *tc);
void MVM_tc_clear_ex_release_mutex(MVMThreadContext *tc);

/* UV always defines this for Win32 but not Unix. Which is fine, as we can probe
 * for it on Unix more easily than on Win32. */
#if !defined(MVM_THREAD_LOCAL) && defined(UV_THREAD_LOCAL)
#define MVM_THREAD_LOCAL UV_THREAD_LOCAL
#endif

#ifdef MVM_THREAD_LOCAL

extern MVM_THREAD_LOCAL MVMThreadContext *MVM_running_threads_context;

MVM_STATIC_INLINE MVMThreadContext *MVM_get_running_threads_context(void) {
    return MVM_running_threads_context;
}

MVM_STATIC_INLINE void MVM_set_running_threads_context(MVMThreadContext *tc) {
    MVM_running_threads_context = tc;
}

#else

/* Fallback to an implememtation using UV's APIs (pretty much pthreads) */
extern uv_key_t MVM_running_threads_context_key;

MVM_STATIC_INLINE MVMThreadContext *MVM_get_running_threads_context(void) {
    return uv_key_get(&MVM_running_threads_context_key);
}

MVM_STATIC_INLINE void MVM_set_running_threads_context(MVMThreadContext *tc) {
    uv_key_set(&MVM_running_threads_context_key, tc);
}

#endif
