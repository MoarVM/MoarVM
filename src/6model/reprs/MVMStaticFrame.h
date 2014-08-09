/* Representation for static code in the VM. Partially populated on first
 * call or usage. */
struct MVMStaticFrameBody {
    /* The start of the stream of bytecode for this routine. */
    MVMuint8 *bytecode;

    /* The compilation unit this frame belongs to. */
    MVMCompUnit *cu;

    /* The list of local types. */
    MVMuint16 *local_types;

    /* The list of lexical types. */
    MVMuint16 *lexical_types;

    /* Lexicals name map. */
    MVMLexicalRegistry *lexical_names;
    MVMLexicalRegistry **lexical_names_list;

    /* Defaults for lexicals upon new frame creation. */
    MVMRegister *static_env;

    /* Flags for static environment (0 = static, 1 = clone, 2 = state). */
    MVMuint8 *static_env_flags;

    /* If the frame has state variables. */
    MVMuint16 has_state_vars;

    /* Flag for if this frame has been invoked ever. */
    MVMuint16 invoked;

    /* Rough call count. May be hit up by multiple threads, and lose the odd
     * count, but that's fine; it's just a rough indicator, used to make
     * decisions about optimization. */
    MVMuint32 invocations;

    /* Number of times we should invoke before spesh applies. */
    MVMuint32 spesh_threshold;

    /* Specializations array, if there are any. */
    MVMSpeshCandidate *spesh_candidates;
    MVMuint32          num_spesh_candidates;

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

    /* Frame exception handlers information. */
    MVMFrameHandler *handlers;

    /* The number of exception handlers this frame has. */
    MVMuint32 num_handlers;

    /* The compilation unit unique ID of this frame. */
    MVMString *cuuid;

    /* The name of this frame. */
    MVMString *name;

    /* This frame's static outer frame. */
    MVMStaticFrame *outer;

    /* the static coderef */
    MVMCode *static_code;

    /* Index into each threadcontext's table of frame pools. */
    MVMuint32 pool_index;

    /* Annotation details */
    MVMuint32              num_annotations;
    MVMuint8              *annotations_data;

    /* Cached instruction offsets */
    MVMuint8 *instr_offsets;

    /* Does the frame have an exit handler we need to run? */
    MVMuint8 has_exit_handler;

    /* Is the frame a thunk, and thus hidden to caller/outer? */
    MVMuint8 is_thunk;

    /* Is the frame full deserialized? */
    MVMuint8 fully_deserialized;

    /* The original bytecode for this frame (before endian swapping). */
    MVMuint8 *orig_bytecode;

    /* The serialized data about this frame, used to set up the things above
     * marked (lazy). Also, once we've done that, the static lexical wvals
     * data pos; we may be able to re-use the same slot for these to. */
    MVMuint8 *frame_data_pos;
    MVMuint8 *frame_static_lex_pos;
};
struct MVMStaticFrame {
    MVMObject common;
    MVMStaticFrameBody body;
};

/* Function for REPR setup. */
const MVMREPROps * MVMStaticFrame_initialize(MVMThreadContext *tc);
