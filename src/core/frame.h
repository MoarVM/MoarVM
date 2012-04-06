/* This represents the things we statically know about a call frame. */
typedef struct MVMStaticFrame {    
    /* MVM AST representing the code we execute. */
    MVMAST *ast;
};

/* This represents an active call frame. */
typedef struct {
    /* The thread that is executing, or executed, this frame. */
    MVMThreadContext *tc;

    /* The environment for this frame, which lives beyond its execution.
     * Has space for, for instance, lexicals. */
    void *env;

    /* The temporary work space for this frame. After a call is over, this
     * can be freed up. Must be NULLed out when this happens. */
    void *work;

    /* The outer frame, thus forming the static chain. */
    MVMFrame *outer;

    /* The caller frame, thus forming the dynamic chain. */
    MVMFrame *caller;

    /* The static frame information. Holds all we statically know about
     * this kind of frame, including information needed to GC-trace it. */
    MVMStaticFrame *sfi;
} MVMFrame;
