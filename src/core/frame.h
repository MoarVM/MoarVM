/* This represents the things we statically know about a call frame. */
typedef struct _MVMStaticFrame {
    /* The start of the stream of bytecode for this routine. */
    MVMuint8 *bytecode;

    /* The size of the bytecode. */
    MVMuint32 bytecode_size;
    
    /* The list of local types. */
    MVMuint16 *local_types;
    
    /* Count of locals. */
    MVMuint32 num_locals;
    
    /* The list of lexical types. */
    MVMuint16 *lexical_types;
    
    /* Count of lexicals. */
    MVMuint32 num_lexicals;
    
    /* Lexicals name map. */
    /* XXX */
    
    /* The compilation unit this frame belongs to. */
    struct _MVMCompUnit *cu;
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

    /* The outer frame, thus forming the static chain. */
    struct _MVMFrame *outer;

    /* The caller frame, thus forming the dynamic chain. */
    struct _MVMFrame *caller;

    /* The static frame information. Holds all we statically know about
     * this kind of frame, including information needed to GC-trace it. */
    MVMStaticFrame *static_info;
} MVMFrame;
