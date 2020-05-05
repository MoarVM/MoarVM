/* Representation for static code in the VM. Partially populated on first
 * call or usage. */
struct MVMStaticFrameBody {
    /* The start of the stream of bytecode for this routine. */
    MVMuint8 *bytecode;

    /* The compilation unit this frame belongs to. */
    MVMCompUnit *cu;

    /* Inline cache, used for speeding up dispatch-y things. */
    MVMDispInlineCache inline_cache;

    /* The list of local types. */
    MVMuint16 *local_types;

    /* The list of lexical types. */
    MVMuint16 *lexical_types;

    /* Lexicals name map. */
    MVMIndexHashTable lexical_names;
    MVMString **lexical_names_list;

    /* Defaults for lexicals upon new frame creation. */
    MVMRegister *static_env;

    /* Flags for static environment (0 = static, 1 = clone, 2 = state). */
    MVMuint8 *static_env_flags;

    /* If the frame has state variables. */
    MVMuint8 has_state_vars;

    /* Should we allocate the frame directly on the heap? Doing so may avoid
     * needing to promote it there later. Set by measuring the number of times
     * the frame is promoted to the heap relative to the number of times it is
     * invoked, and then only pre-specialization. */
    MVMuint8 allocate_on_heap;

    /* Is the frame marked as not being allowed to inline? */
    MVMuint8 no_inline;

    /* Does the frame contain specializable instructions? */
    MVMuint8 specializable;
    /* Zero if the frame was never invoked. Above zero is the instrumentation
     * level the VM was atlast time the frame was invoked. See MVMInstance for
     * the VM instance wide field for this. */
    MVMuint32 instrumentation_level;

    /* Specialization-related information. Attached when a frame is first
     * verified. Held in a separate object rather than the MVMStaticFrame
     * itself partly to decrease the size of this object for frames that
     * we never even call, but also because we sample nursery objects and
     * they end up in the statistics; forcing the static frame into the
     * inter-generational roots leads to a lot more marking work. */
    MVMStaticFrameSpesh *spesh;

    /* The size in bytes to allocate for the lexical environment. */
    MVMuint32 env_size;

    /* The size in bytes to allocate for the work and arguments area. */
    MVMuint32 work_size;

    /* Count of lexicals. */
    MVMuint32 num_lexicals;

    /* Count of annotations (see further down below */
    MVMuint32              num_annotations;

    /* Inital contents of the work area, copied into place to make sure we have
     * VMNulls in all the object slots. */
    MVMRegister *work_initial;

    /* The size of the bytecode. */
    MVMuint32 bytecode_size;

    /* Count of locals. */
    MVMuint32 num_locals;

    /* Frame exception handlers information. */
    MVMFrameHandler *handlers;

    /* The number of exception handlers this frame has. */
    MVMuint32 num_handlers;

    /* Is the frame full deserialized? */
    MVMuint8 fully_deserialized;

    /* Is the frame a thunk, and thus hidden to caller/outer? */
    MVMuint8 is_thunk;

    /* Does the frame have an exit handler we need to run? */
    MVMuint8 has_exit_handler;

    /* The compilation unit unique ID of this frame. */
    MVMString *cuuid;

    /* The name of this frame. */
    MVMString *name;

    /* This frame's static outer frame. */
    MVMStaticFrame *outer;

    /* the static coderef */
    MVMCode *static_code;

    /* Annotation details */
    MVMuint8              *annotations_data;

    /* The original bytecode for this frame (before endian swapping). */
    MVMuint8 *orig_bytecode;

    /* The serialized data about this frame, used to set up the things above
     * marked (lazy). Also, once we've done that, the static lexical wvals
     * data pos; we may be able to re-use the same slot for these to. */
    MVMuint8 *frame_data_pos;
    MVMuint8 *frame_static_lex_pos;

    /* Off-by-one SC dependency index (zero indicates invalid) for the code
     * object, plus the index of it within that SC. This is relevant for the
     * static_code only. */
    MVMint32 code_obj_sc_dep_idx;
    MVMint32 code_obj_sc_idx;

    /* Extra profiling/instrumentation state. */
    MVMStaticFrameInstrumentation *instrumentation;
};
struct MVMStaticFrame {
    MVMObject common;
    MVMStaticFrameBody body;
};

/* Extra state that static frames carry when instrumented, so that the
 * can later be removed again. */
struct MVMStaticFrameInstrumentation {
    MVMuint8        *instrumented_bytecode;
    MVMuint8        *uninstrumented_bytecode;
    MVMFrameHandler *instrumented_handlers;
    MVMFrameHandler *uninstrumented_handlers;
    MVMuint32        uninstrumented_bytecode_size;
    MVMuint32        instrumented_bytecode_size;
    MVMStrHashTable  debug_locals;

    MVMuint8         profiler_confprog_result;
    MVMuint8         profiler_confprog_version;
};

struct MVMStaticFrameDebugLocal {
    /* The lexical's name is the key: */
    struct MVMStrHashHandle hash_handle;
    /* The index of the local where the value lives. */
    MVMuint16 local_idx;
};

/* Function for REPR setup. */
const MVMREPROps * MVMStaticFrame_initialize(MVMThreadContext *tc);

/* Debugging help. */
char * MVM_staticframe_file_location(MVMThreadContext *tc, MVMStaticFrame *sf);

MVMuint32 MVM_get_lexical_by_name(MVMThreadContext *tc, MVMStaticFrame *sf, MVMString *name);
