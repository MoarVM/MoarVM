/* This represents the things we statically know about a call frame. */
typedef struct _MVMStaticFrame {
    /* The start of the stream of bytecode for this routine. */
    MVMuint8 *bytecode;
    
    /* The compilation unit this frame belongs to. */
    struct _MVMCompUnit *cu;
    
    /* Lexicals name map. */
    /* XXX */
    
    /* The list of local types. */
    MVMuint16 *local_types;
    
    /* The list of lexical types. */
    MVMuint16 *lexical_types;

    /* Flag for if this frame has been invoked ever. */
    MVMuint32 invoked;

    /* The size in bytes to allocate for the lexical environment. */
    MVMuint32 env_size;
    
    /* The size in bytes to allocate for the work and arguments area. */
    MVMuint32 work_size;

    /* The size of the bytecode. */
    MVMuint32 bytecode_size;
    
    /* Count of locals. */
    MVMuint32 num_locals;
    
    /* Count of lexicals. */
    MVMuint32 num_lexicals;
    
    /* The compilation unit unique ID of this frame. */
    struct _MVMString *cuuid;
    
    /* The name of this frame. */
    struct _MVMString *name;
    
    /* This frame's static outer frame. */
    struct _MVMStaticFrame *outer;
    
    /* GC run sequence number that we last saw static this frame during. */
    MVMuint32 gc_seq_number;
} MVMStaticFrame;

/* This represents an active call frame. */
typedef struct _MVMFrame {
    /* The thread that is executing, or executed, this frame. */
    MVMThreadContext *tc;

    /* The environment for this frame, which lives beyond its execution.
     * Has space for, for instance, lexicals. */
    MVMRegister *env;

    /* The temporary work space for this frame. After a call is over, this
     * can be freed up. Must be NULLed out when this happens. */
    MVMRegister *work;
    
    /* The args buffer. Actually a pointer into an area inside of *work, to
     * decrease number of allocations. */
    MVMRegister *args;

    /* The outer frame, thus forming the static chain. */
    struct _MVMFrame *outer;

    /* The caller frame, thus forming the dynamic chain. */
    struct _MVMFrame *caller;

    /* The static frame information. Holds all we statically know about
     * this kind of frame, including information needed to GC-trace it. */
    MVMStaticFrame *static_info;

    /* Parameters received by this frame. */
    MVMArgProcContext params;

    /* Reference count for the frame. */
    MVMuint32 ref_count;
    
    /* Address of the next op to execute if we return to this frame. */
    MVMuint8 *return_address;
    
    /* The register we should store the return value in, if any. */
    MVMRegister *return_value;
    
    /* The type of return value that is expected. */
    MVMReturnType return_type;
    
    /* GC run sequence number that we last saw this frame during. */
    MVMuint32 gc_seq_number;
} MVMFrame;

void MVM_frame_invoke(MVMThreadContext *tc, MVMStaticFrame *static_frame,
                      MVMCallsite *callsite, MVMRegister *args,
                      MVMFrame *outer);
MVMuint64 MVM_frame_try_return(MVMThreadContext *tc);
MVMFrame * MVM_frame_inc_ref(MVMThreadContext *tc, MVMFrame *frame);
void MVM_frame_dec_ref(MVMThreadContext *tc, MVMFrame *frame);
