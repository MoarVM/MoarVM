/* Possible values for the thread execution interrupt flag. */
typedef enum {
    /* No interruption needed, continue execution. */
    MVMInterrupt_NONE   = 0,

    /* Stop and do a GC scan. */
    MVMInterrupt_GCSCAN = 1
} MVMInterruptType;

/* Information associated with an executing thread. */
struct _MVMInstance;
typedef struct _MVMThreadContext {
    /* The current allocation pointer, where the next object to be allocated
     * should be placed. */
    void *nursery_alloc;
    
    /* The end of the space we're allowed to allocate to. */
    void *nursery_alloc_limit;
    
    /* Execution interrupt flag. */
    MVMInterruptType interrupt;
    
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
    
    /* The VM instance that this thread belongs to. */
    struct _MVMInstance *instance;
    
    /* Start of the mutator's thread-local allocation space; put another way,
     * the current nursery. */
    void *nursery_fromspace;
    
    /* Where we evacuate objects to when collecting this thread's nursery. */
    void *nursery_tospace;
    
    /* Internal ID of the thread. */
    MVMuint32 thread_id;
    
    /* OS thread handle. */
    void *os_thread; /* XXX Whatever APR uses for thread handles... */
} MVMThreadContext;

MVMThreadContext * MVM_tc_create(struct _MVMInstance *instance);
void MVM_tc_destroy(MVMThreadContext *tc);
