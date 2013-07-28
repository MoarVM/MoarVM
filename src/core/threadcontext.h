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

/* Information associated with an executing thread. */
struct _MVMInstance;
struct _MVMConcatState;
typedef struct _MVMThreadContext {
    /* The current allocation pointer, where the next object to be allocated
     * should be placed. */
    void *nursery_alloc;

    /* The end of the space we're allowed to allocate to. */
    void *nursery_alloc_limit;

    /* This thread's GC status. */
    MVMint32 gc_status;

    /* Where we're allocating. */
    MVMAllocationTarget allocate_in;

    /* Pointer to where the interpreter's current opcode is stored. */
    MVMuint8 **interp_cur_op;

    /* Pointer to where the interpreter's bytecode start pointer is stored. */
    MVMuint8 **interp_bytecode_start;

    /* Pointer to where the interpreter's base of the current register
     * set is stored. */
    union _MVMRegister **interp_reg_base;

    /* Pointer to where the interpreter's current compilation unit pointer
     * is stored. */
    struct _MVMCompUnit **interp_cu;

    /* The frame we're currently executing. */
    struct _MVMFrame *cur_frame;

    /* The usecapture op can, without allocating, have a way to talk about the
     * arguments of the current call. This is the (pre-thread) object that is
     * used by that op. */
    struct _MVMObject *cur_usecapture;

    /* Linked list of exception handlers that we're currently executing, topmost
     * one first in the list. */
    struct _MVMActiveHandler *active_handlers;

    /* The VM instance that this thread belongs to. */
    struct _MVMInstance *instance;

    /* Start of fromspace, the place we're copying objects from during a
     * copying collection or processing dead objects that need to do extra
     * resource release afterwards. */
    void *nursery_fromspace;

    /* Where we evacuate objects to when collecting this thread's nursery, or
     * allocate new ones. */
    void *nursery_tospace;

    /* The second GC generation allocator. */
    struct _MVMGen2Allocator *gen2;

    /* Internal ID of the thread. */
    MVMuint32 thread_id;

    /* Thread object representing the thread. */
    struct _MVMThread *thread_obj;

    /* The frame lying at the base of the current thread. */
    struct _MVMFrame *thread_entry_frame;

    /* Temporarily rooted objects. This is generally used by code written in
     * C that wants to keep references to objects. Since those may change
     * if the code in question also allocates, there is a need to register
     * them; this ensures the GC will not swallow them but also that they
     * will get updated if a GC run happens. Note that this is used as a
     * stack and is also thread-local, so it's cheap to push/pop. */
    MVMuint32             num_temproots;
    MVMuint32             alloc_temproots;
    MVMCollectable     ***temproots;

    /* Nursery collectables (maybe STables) rooted because something in
     * generation 2 is pointing at them. */
    MVMuint32             num_gen2roots;
    MVMuint32             alloc_gen2roots;
    MVMCollectable      **gen2roots;

    /* The GC's cross-thread in-tray of processing work. */
    struct _MVMGCPassedWork *gc_in_tray;

    /* The GC's thread-local "sent items" list, by next_by_sender. */
    struct _MVMGCPassedWork *gc_sent_items;
    struct _MVMGCPassedWork *gc_next_to_check;

    /* threads to process this gc run. */
    struct _MVMWorkThread   *gc_work;
    MVMuint32                gc_work_size;
    MVMuint32                gc_work_count;

    /* Pool table of chains of frames for each static frame. */
    struct _MVMFrame **frame_pool_table;

    /* Size of the pool table, so it can grow on demand. */
    MVMuint32          frame_pool_table_size;

    /* Serialization context write barrier disabled depth (anything non-zero
     * means disabled). */
    MVMint32           sc_wb_disable_depth;

    /* Any serialization contexts we are compiling. */
    struct _MVMObject     *compiling_scs;
} MVMThreadContext;

MVMThreadContext * MVM_tc_create(struct _MVMInstance *instance);
void MVM_tc_destroy(MVMThreadContext *tc);
