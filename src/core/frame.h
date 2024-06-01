/* Frame flags; provide some HLLs can alias. */
#define MVM_FRAME_FLAG_STATE_INIT       1 << 0
#define MVM_FRAME_FLAG_EXIT_HAND_RUN    1 << 1
#define MVM_FRAME_FLAG_HLL_1            1 << 3
#define MVM_FRAME_FLAG_HLL_2            1 << 4
#define MVM_FRAME_FLAG_HLL_3            1 << 5
#define MVM_FRAME_FLAG_HLL_4            1 << 6

/* Function pointer type of special return handler. These are used to allow
 * return to be intercepted in some way, for things that need to do multiple
 * calls into the runloop in some C-managed process. Essentially, instead of
 * nested runloops, you just re-work the C code in question into CPS. */
typedef void (* MVMSpecialReturn)(MVMThreadContext *tc, void *data);

/* Function pointer for marking the special return handler data. */
typedef void (* MVMSpecialReturnMark)(MVMThreadContext *tc, void *data,
                                      MVMGCWorklist *worklist);

/* This represents an call frame, aka invocation record. It may exist either on
 * the heap, in which case its header will have the MVM_CF_FRAME flag set, or
 * in on a thread-local stack, in which case the collectable header will be
 * fully zeroed. */
struct MVMFrame {
    /* Commonalities that all collectable entities have. */
    MVMCollectable header;

    /* The environment for this frame, which lives beyond its execution.
     * Has space for, for instance, lexicals. */
    MVMRegister *env;

    /* The temporary work space for this frame. After a call is over, this
     * can be freed up. Must be NULLed out when this happens. */
    MVMRegister *work;

    /* The outer frame, thus forming the static chain. */
    MVMFrame *outer;

    /* The caller frame, thus forming the dynamic chain. */
    MVMFrame *caller;

    /* The static frame information. Holds all we statically know about
     * this kind of frame, including information needed to GC-trace it. */
    MVMStaticFrame *static_info;

    /* The code ref object for this frame. */
    MVMObject *code_ref;

    /* Parameters received by this frame. */
    MVMArgProcContext params;

    /* Effective set of spesh slots, if any. */
    MVMCollectable **effective_spesh_slots;

    /* The spesh candidate information, if we're in one. */
    MVMSpeshCandidate *spesh_cand;

    /* Address of the next op to execute if we return to this frame. */
    MVMuint8 *return_address;

    /* The register we should store the return value in, if any. */
    MVMRegister *return_value;

    /* The type of return value that is expected. */
    MVMReturnType return_type;

    /* Assorted frame flags. */
    MVMuint8 flags;

    /* The allocated work/env sizes. */
    MVMuint32 allocd_work;
    MVMuint32 allocd_env;

    /* The current spesh correlation ID, if we're interpreting code and
     * recording logs. Zero if interpreting unspecialized and not recording.
     * Junk if running specialized code. */
    MVMint32 spesh_correlation_id;

    /* The 'entry label' is a sort of indirect return address for the JIT */
    void * jit_entry_label;

    /* Extra data that some frames need, allocated on demand. If allocated,
     * lives for the dynamic scope of the frame. */
    MVMFrameExtra *extra;
};

/* Extra data that a handful of call frames optionally need. It is needed
 * only while the frame is in dynamic scope; after that it can go away. */
struct MVMFrameExtra {
    /* Cache for dynlex lookup; if the name is non-null, the cache is valid
     * and the register can be accessed directly to find the contextual. */
    MVMString   *dynlex_cache_name;
    MVMRegister *dynlex_cache_reg;
    MVMuint16    dynlex_cache_type;

    /* If we use the ctx op, then we need to preserve the caller chain for
     * walking. We don't want to do that in the general case, since it can
     * cause memory leaks in certain patterns of closure use. We also
     * differentiate needing just the caller chain (exceptions) or the
     * caller position (contexts). */
    MVMuint8 caller_info_needed;
    MVMuint8 caller_pos_needed;

    /* If we use the ctx op and we have inlining, we no longer have an
     * immutable chain of callers to walk. When our caller had inlines, we
     * record here in the callee the deopt index or JIT position. Then, when
     * we traverse, we can precisely recreate the stack trace. This works,
     * since we forbid inlining of the ctx op, so there's always a clear
     * starter frame. */
    MVMint32 caller_deopt_idx;
    void *caller_jit_position;

    /* Often when we have an exit handler we're returning a value, so can find
     * the returned value in the callee's return result register. However, if
     * we have a void caller, then we stash it here so we can pass it to the
     * exit handler. */
    MVMObject *exit_handler_result;
};

/* Checks if a frame is allocated on a call stack or on the heap. If it is on
 * the call stack, then it will have zeroed flags (since heap-allocated frames
 * always have the "I'm a heap frame" bit set - MVM_CF_FRAME). */
MVM_STATIC_INLINE MVMuint32 MVM_FRAME_IS_ON_CALLSTACK(MVMThreadContext *tc, MVMFrame *frame) {
    return frame->header.flags1 == 0;
}

/* Forces a frame to the callstack if needed. Done as a static inline to make
 * the quite common case where nothing is needed cheaper. */
MVM_PUBLIC MVMFrame * MVM_frame_move_to_heap(MVMThreadContext *tc, MVMFrame *frame);
MVM_STATIC_INLINE MVMFrame * MVM_frame_force_to_heap(MVMThreadContext *tc, MVMFrame *frame) {
    return MVM_FRAME_IS_ON_CALLSTACK(tc, frame)
        ? MVM_frame_move_to_heap(tc, frame)
        : frame;
}

MVMFrame * MVM_frame_debugserver_move_to_heap(MVMThreadContext *tc, MVMThreadContext *owner, MVMFrame *frame);

MVMRegister * MVM_frame_initial_work(MVMThreadContext *tc, MVMuint16 *local_types,
                                     MVMuint16 num_locals);
void MVM_frame_dispatch(MVMThreadContext *tc, MVMCode *code, MVMArgs args, MVMint32 spesh_cand);
MVM_PUBLIC void MVM_frame_dispatch_zero_args(MVMThreadContext *tc, MVMCode *code);
void MVM_frame_dispatch_from_c(MVMThreadContext *tc, MVMCode *code,
        MVMCallStackArgsFromC *args_record, MVMRegister *return_value,
        MVMReturnType return_type);
MVMFrame * MVM_frame_create_context_only(MVMThreadContext *tc, MVMStaticFrame *static_frame,
        MVMObject *code_ref);
void MVM_frame_setup_deopt(MVMThreadContext *tc, MVMFrame *frame, MVMStaticFrame *static_frame,
        MVMCode *code_ref);
MVM_PUBLIC MVMuint64 MVM_frame_try_return(MVMThreadContext *tc);
MVM_PUBLIC MVMuint64 MVM_frame_try_return_no_exit_handlers(MVMThreadContext *tc);
MVMint8 MVM_frame_continue_conflicting_unwind(MVMThreadContext *tc, MVMFrame *up_to,
        MVMuint8 exceptional);
void MVM_frame_unwind_to(MVMThreadContext *tc, MVMFrame *frame, MVMuint8 *abs_addr,
                         MVMuint32 rel_addr, MVMObject *return_value,
                         void *jit_return_label, MVMuint8 exceptional);
MVM_PUBLIC void MVM_frame_destroy(MVMThreadContext *tc, MVMFrame *frame);
MVM_PUBLIC MVMObject * MVM_frame_get_code_object(MVMThreadContext *tc, MVMCode *code);
MVM_PUBLIC void MVM_frame_capturelex(MVMThreadContext *tc, MVMObject *code);
MVM_PUBLIC void MVM_frame_capture_inner(MVMThreadContext *tc, MVMObject *code);
MVM_PUBLIC MVMObject * MVM_frame_takeclosure(MVMThreadContext *tc, MVMObject *code);
MVM_PUBLIC MVMObject * MVM_frame_vivify_lexical(MVMThreadContext *tc, MVMFrame *f, MVMuint16 idx);
MVM_PUBLIC int MVM_frame_find_lexical_by_name(MVMThreadContext *tc, MVMString *name, MVMuint16 type, MVMRegister *result);
MVM_PUBLIC void MVM_frame_bind_lexical_by_name(MVMThreadContext *tc, MVMString *name, MVMuint16 type, MVMRegister value);
void MVM_frame_find_lexical_by_name_outer(MVMThreadContext *tc, MVMString *name, MVMRegister *result);
MVM_PUBLIC int MVM_frame_find_lexical_by_name_rel(MVMThreadContext *tc, MVMString *name, MVMFrame *cur_frame, MVMRegister *result);
MVMRegister * MVM_frame_lexical_lookup_using_frame_walker(MVMThreadContext *tc,
        MVMSpeshFrameWalker *fw, MVMString *name, MVMuint16 type);
MVM_PUBLIC MVMRegister * MVM_frame_find_lexical_by_name_rel_caller(MVMThreadContext *tc, MVMString *name, MVMFrame *cur_caller_frame);
MVMRegister * MVM_frame_find_dynamic_using_frame_walker(MVMThreadContext *tc,
        MVMSpeshFrameWalker *fw, MVMString *name, MVMuint16 *type, MVMFrame *initial_frame,
        MVMint32 vivify, MVMFrame **found_frame);
MVMRegister * MVM_frame_find_contextual_by_name(MVMThreadContext *tc, MVMString *name, MVMuint16 *type, MVMFrame *cur_frame, MVMint32 vivify, MVMFrame **found_frame);
void MVM_frame_getdynlex_with_frame_walker(MVMThreadContext *tc, MVMSpeshFrameWalker *fw,
        MVMString *name, MVMRegister *result);
void MVM_frame_getdynlex(MVMThreadContext *tc, MVMString *name, MVMFrame *cur_frame, MVMRegister *result);
void MVM_frame_binddynlex(MVMThreadContext *tc, MVMString *name, MVMObject *value, MVMFrame *cur_frame);
MVMRegister * MVM_frame_lexical(MVMThreadContext *tc, MVMFrame *f, MVMString *name);
MVM_PUBLIC MVMRegister * MVM_frame_try_get_lexical(MVMThreadContext *tc, MVMFrame *f, MVMString *name, MVMuint16 type);
MVMuint16 MVM_frame_translate_to_primspec(MVMThreadContext *tc, MVMuint16 kind);
MVMuint16 MVM_frame_lexical_primspec(MVMThreadContext *tc, MVMFrame *f, MVMString *name);
MVMFrameExtra * MVM_frame_extra(MVMThreadContext *tc, MVMFrame *f);
MVMObject * MVM_frame_caller_code(MVMThreadContext *tc);
