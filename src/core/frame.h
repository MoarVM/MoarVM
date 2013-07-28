/* Lexical hash entry for ->lexical_names on a frame. */
typedef struct _MVMLexicalHashEntry {
    /* index of the lexical entry. */
    MVMuint32 value;

    /* the uthash hash handle inline struct. */
    UT_hash_handle hash_handle;
} MVMLexicalHashEntry;

/* This represents the things we statically know about a call frame. */
typedef struct _MVMStaticFrame {
    /* The start of the stream of bytecode for this routine. */
    MVMuint8 *bytecode;

    /* The compilation unit this frame belongs to. */
    struct _MVMCompUnit *cu;

    /* The list of local types. */
    MVMuint16 *local_types;

    /* The list of lexical types. */
    MVMuint16 *lexical_types;

    /* Lexicals name map. */
    MVMLexicalHashEntry *lexical_names;/* The environment for this frame, which lives beyond its execution. */

    /* Defaults for lexicals upon new frame creation. */
    MVMRegister *static_env;

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

    /* The frame of the prior invocation of this static frame. */
    struct _MVMFrame *prior_invocation;

    /* The number of exception handlers this frame has. */
    MVMuint32 num_handlers;

    /* Frame exception handlers information. */
    MVMFrameHandler *handlers;

    /* The compilation unit unique ID of this frame. */
    struct _MVMString *cuuid;

    /* The name of this frame. */
    struct _MVMString *name;

    /* This frame's static outer frame. */
    struct _MVMStaticFrame *outer;

    /* GC run sequence number that we last saw static this frame during. */
    MVMuint32 gc_seq_number;

    /* Index into each threadcontext's table of frame pools. */
    MVMuint32 pool_index;

    /* Annotation details */
    MVMuint32 num_annotations;
    MVMuint8 *annotations;
} MVMStaticFrame;

/* Function pointer type of special return handler. These are used to allow
 * return to be intercepted in some way, for things that need to do multiple
 * calls into the runloop in some C-managed process. Essentially, instead of
 * nested runloops, you just re-work the C code in question into CPS. */
 typedef void (* MVMSpecialReturn)(MVMThreadContext *tc, void *data);

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

    /* The code ref object for this frame. */
    MVMObject *code_ref;

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

    /* If we want to invoke a special handler upon a return to this
     * frame, this function pointer is set. */
    MVMSpecialReturn special_return;

    /* Data slot for the special return handler function. */
    void *special_return_data;

    /* GC run sequence number that we last saw this frame during. */
    MVMuint32 gc_seq_number;
} MVMFrame;

/* How do we invoke this thing? Specifies either an attribute to look at for
 * an invokable thing, or alternatively a method to call. */
typedef struct _MVMInvocationSpec {
    /*
     * Class handle where we find the attribute to invoke.
     */
    struct _MVMObject *class_handle;

    /*
     * Attribute name where we find the attribute to invoke.
     */
    struct _MVMString *attr_name;

    /*
     * Attribute lookup hint used in gradual typing.
     */
    MVMint64 hint;

    /*
     * Thing that handles invocation.
     */
    struct _MVMObject *invocation_handler;
} MVMInvocationSpec;

void MVM_frame_invoke(MVMThreadContext *tc, MVMStaticFrame *static_frame,
                      MVMCallsite *callsite, MVMRegister *args,
                      MVMFrame *outer, MVMObject *code_ref);
MVMFrame * MVM_frame_create_context_only(MVMThreadContext *tc, MVMStaticFrame *static_frame,
        MVMObject *code_ref);
MVMuint64 MVM_frame_try_return(MVMThreadContext *tc);
MVMuint64 MVM_frame_try_unwind(MVMThreadContext *tc);
MVMFrame * MVM_frame_inc_ref(MVMThreadContext *tc, MVMFrame *frame);
void MVM_frame_dec_ref(MVMThreadContext *tc, MVMFrame *frame);
MVMObject * MVM_frame_takeclosure(MVMThreadContext *tc, MVMObject *code);
MVMRegister * MVM_frame_find_lexical_by_name(MVMThreadContext *tc, struct _MVMString *name, MVMuint16 type);
MVMRegister * MVM_frame_find_contextual_by_name(MVMThreadContext *tc, struct _MVMString *name, MVMuint16 *type);
MVMObject * MVM_frame_getdynlex(MVMThreadContext *tc, struct _MVMString *name);
void MVM_frame_binddynlex(MVMThreadContext *tc, struct _MVMString *name, MVMObject *value);
MVMRegister * MVM_frame_lexical(MVMThreadContext *tc, MVMFrame *f, struct _MVMString *name);
MVMuint16 MVM_frame_lexical_primspec(MVMThreadContext *tc, MVMFrame *f, struct _MVMString *name);
MVMObject * MVM_frame_find_invokee(MVMThreadContext *tc, MVMObject *code);
