/* Frame flags; provide some HLLs can alias. */
#define MVM_FRAME_FLAG_STATE_INIT       1 << 0
#define MVM_FRAME_FLAG_EXIT_HAND_RUN    1 << 1
#define MVM_FRAME_FLAG_HLL_1            1 << 3
#define MVM_FRAME_FLAG_HLL_2            1 << 4
#define MVM_FRAME_FLAG_HLL_3            1 << 5
#define MVM_FRAME_FLAG_HLL_4            1 << 6

/* Lexical hash entry for ->lexical_names on a frame. */
struct MVMLexicalRegistry {
    /* key string */
    MVMString *key;

    /* index of the lexical entry. */
    MVMuint32 value;

    /* the uthash hash handle inline struct. */
    UT_hash_handle hash_handle;
};

/* Entry in the linked list of continuation tags for the frame. */
struct MVMContinuationTag {
    /* The tag itself. */
    MVMObject *tag;

    /* The active exception handler at the point the tag was taken. */
    MVMActiveHandler *active_handlers;

    /* The next continuation tag entry. */
    MVMContinuationTag *next;
};

/* Function pointer type of special return handler. These are used to allow
 * return to be intercepted in some way, for things that need to do multiple
 * calls into the runloop in some C-managed process. Essentially, instead of
 * nested runloops, you just re-work the C code in question into CPS. */
typedef void (* MVMSpecialReturn)(MVMThreadContext *tc, void *data);

/* Function pointer for marking the special return handler data. */
typedef void (* MVMSpecialReturnDataMark)(MVMThreadContext *tc, MVMFrame *frame,
                                          MVMGCWorklist *worklist);

/* This represents an active call frame. */
struct MVMFrame {
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

    /* Callsite that indicates how the current args buffer is being used, if
     * it is. */
    MVMCallsite *cur_args_callsite;

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

    /* Reference count for the frame. */
    AO_t ref_count;

    /* Effective bytecode for the frame (either the original bytecode or a
     * specialization of it). */
    MVMuint8 *effective_bytecode;

    /* Effective set of frame handlers (to go with the effective bytecode). */
    MVMFrameHandler *effective_handlers;

    /* Effective set of spesh slots, if any. */
    MVMCollectable **effective_spesh_slots;

    /* Effective set of spesh logging slots, if any. */
    MVMCollectable **spesh_log_slots;

    /* The spesh candidate information, if we're in one. */
    MVMSpeshCandidate *spesh_cand;

    /* Address of the next op to execute if we return to this frame. */
    MVMuint8 *return_address;

    /* The register we should store the return value in, if any. */
    MVMRegister *return_value;

    /* The type of return value that is expected. */
    MVMReturnType return_type;

    /* If we want to invoke a special handler upon a return to this
     * frame, this function pointer is set. */
    MVMSpecialReturn special_return;

    /* If we want to invoke a special handler upon unwinding past a
     * frame, this function pointer is set. */
    MVMSpecialReturn special_unwind;

    /* Data slot for the special return handler function. */
    void *special_return_data;

    /* Flag for if special_return_data need to be GC marked. */
    MVMSpecialReturnDataMark mark_special_return_data;

    /* GC run sequence number that we last saw this frame during. */
    AO_t gc_seq_number;

    /* Address of the last op executed that threw an exeption; used just
     * for error reporting. */
    MVMuint8 *throw_address;

    /* Linked list of any continuation tags we have. */
    MVMContinuationTag *continuation_tags;
    
    /* Linked MVMContext object, so we can track the
     * serialization context and such. */
    /* note: used atomically */
    MVMObject *context_object;

    /* Cache for dynlex lookup; if the name is non-null, the cache is valid
     * and the register can be accessed directly to find the contextual. */
    MVMString   *dynlex_cache_name;
    MVMRegister *dynlex_cache_reg;
    MVMuint16    dynlex_cache_type;

    /* The allocated work/env sizes. */
    MVMuint16 allocd_work;
    MVMuint16 allocd_env;

    /* Flags that the caller chain should be kept in place after return or
     * unwind; used to make sure we can get a backtrace after an exception. */
    MVMuint8 keep_caller;

    /* Flags that the frame has been captured in a continuation, and as
     * such we should keep everything in place for multiple invocations. */
    MVMuint8 in_continuation;

    /* Assorted frame flags. */
    MVMuint8 flags;

    /* If we're in a logging spesh run, the index to log at in this
     * invocation. -1 if we're not in a logging spesh run, junk if no
     * spesh_cand is set in this frame at all. */
    MVMint8 spesh_log_idx;

    /* On Stack Replacement iteration counter; incremented in loops, and will
     * trigger if the limit is hit. */
    MVMuint8 osr_counter;

    /* The 'entry label' is a sort of indirect return address
       for the JIT */
    void * jit_entry_label;
};

/* How do we invoke this thing? Specifies either an attribute to look at for
 * an invokable thing, a method to call, and maybe a multi-dispatch cache to
 * look in first for an answer. */
struct MVMInvocationSpec {
    /* Class handle, name and hint for attribute holding code to invoke. */
    MVMObject *class_handle;
    MVMString *attr_name;
    MVMint64   hint;

    /* Thing that handles invocation. */
    MVMObject *invocation_handler;

    /* Multi-dispatch info class handle, and name/hint of attribute that
     * holds the cache itself and a flag to check if it's allowed to
     * consider the cache. */
    MVMObject *md_class_handle;
    MVMString *md_cache_attr_name;
    MVMint64   md_cache_hint;
    MVMint64   md_valid_hint;
    MVMString *md_valid_attr_name;
};

void MVM_frame_invoke_code(MVMThreadContext *tc, MVMCode *code,
                           MVMCallsite *callsite, MVMint32 spesh_cand);
void MVM_frame_invoke(MVMThreadContext *tc, MVMStaticFrame *static_frame,
                      MVMCallsite *callsite, MVMRegister *args,
                      MVMFrame *outer, MVMObject *code_ref, MVMint32 spesh_cand);
MVMFrame * MVM_frame_create_context_only(MVMThreadContext *tc, MVMStaticFrame *static_frame,
        MVMObject *code_ref);
MVMFrame * MVM_frame_create_for_deopt(MVMThreadContext *tc, MVMStaticFrame *static_frame,
                                      MVMCode *code_ref);
MVM_PUBLIC MVMuint64 MVM_frame_try_return(MVMThreadContext *tc);
void MVM_frame_unwind_to(MVMThreadContext *tc, MVMFrame *frame, MVMuint8 *abs_addr,
                         MVMuint32 rel_addr, MVMObject *return_value);
MVM_PUBLIC MVMFrame * MVM_frame_inc_ref(MVMThreadContext *tc, MVMFrame *frame);
MVM_PUBLIC MVMFrame * MVM_frame_dec_ref(MVMThreadContext *tc, MVMFrame *frame);
MVM_PUBLIC MVMObject * MVM_frame_get_code_object(MVMThreadContext *tc, MVMCode *code);
MVM_PUBLIC void MVM_frame_capturelex(MVMThreadContext *tc, MVMObject *code);
MVM_PUBLIC MVMObject * MVM_frame_takeclosure(MVMThreadContext *tc, MVMObject *code);
MVM_PUBLIC MVMObject * MVM_frame_vivify_lexical(MVMThreadContext *tc, MVMFrame *f, MVMuint16 idx);
MVM_PUBLIC MVMRegister * MVM_frame_find_lexical_by_name(MVMThreadContext *tc, MVMString *name, MVMuint16 type);
MVMObject * MVM_frame_find_lexical_by_name_outer(MVMThreadContext *tc, MVMString *name);
MVM_PUBLIC MVMRegister * MVM_frame_find_lexical_by_name_rel(MVMThreadContext *tc, MVMString *name, MVMFrame *cur_frame);
MVM_PUBLIC MVMRegister * MVM_frame_find_lexical_by_name_rel_caller(MVMThreadContext *tc, MVMString *name, MVMFrame *cur_caller_frame);
MVM_PUBLIC MVMRegister * MVM_frame_find_contextual_by_name(MVMThreadContext *tc, MVMString *name, MVMuint16 *type, MVMFrame *cur_frame, MVMint32 vivify);
MVMObject * MVM_frame_getdynlex(MVMThreadContext *tc, MVMString *name, MVMFrame *cur_frame);
void MVM_frame_binddynlex(MVMThreadContext *tc, MVMString *name, MVMObject *value, MVMFrame *cur_frame);
MVMRegister * MVM_frame_lexical(MVMThreadContext *tc, MVMFrame *f, MVMString *name);
MVM_PUBLIC MVMRegister * MVM_frame_try_get_lexical(MVMThreadContext *tc, MVMFrame *f, MVMString *name, MVMuint16 type);
MVMuint16 MVM_frame_lexical_primspec(MVMThreadContext *tc, MVMFrame *f, MVMString *name);
MVM_PUBLIC MVMObject * MVM_frame_find_invokee(MVMThreadContext *tc, MVMObject *code, MVMCallsite **tweak_cs);
MVMObject * MVM_frame_find_invokee_multi_ok(MVMThreadContext *tc, MVMObject *code, MVMCallsite **tweak_cs, MVMRegister *args);
MVMObject * MVM_frame_context_wrapper(MVMThreadContext *tc, MVMFrame *f);
MVMFrame * MVM_frame_clone(MVMThreadContext *tc, MVMFrame *f);
