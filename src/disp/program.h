/* A dispatch program is set of operations that we execute in order to perform
 * a dispatch. A dispatch callsite goes through a number of states:
 *
 * 1. Unlinked. This means that we will unconditionally run the dispatch
 *    callback. This can set guards, do capture transformations, delegate,
 *    and eventually ends up at a dispatch terminal. This set of steps make
 *    up the dispatch program; we say that the dispatch callback "records" a
 *    dispatch program.
 * 2. Monomorphic. This means that we have a single dispatch program recorded
 *    and we try to play it back. The dispatch program is a series of linear
 *    steps.
 * 3. Polymorphic. This means that we have recorded multiple dispatch programs
 *    at this callsite. We store each program, but may also store a tree or
 *    other prefix-lifting form of the programs. (However, the first cut will
 *    probably just run them in order.)
 * 4. Megamorphic. This means that we have reached the maximum number of
 *    dispatch programs we're willing to record here. Now we either call a
 *    megamorphic handler callback if one is registered, or otherwise run
 *    the dispatch program but without building up any guards.
 *
 * These states are modelled as different callbacks in the inline cache at the
 * callsite.
 */

/* Kinds of outcome of a dispatch program. */
typedef enum {
    /* Indicates we failed to produce an outcome, probably due to an exception. */
    MVM_DISP_OUTCOME_FAILED,

    /* Indicates we expect the userspace dispatch we're running to specify another
     * dispatcher to delegate to. */
    MVM_DISP_OUTCOME_EXPECT_DELEGATE,

    /* Return a value (produced by the dispatch program). */
    MVM_DISP_OUTCOME_VALUE,

    /* Invoke bytecode. */
    MVM_DISP_OUTCOME_BYTECODE,

    /* Invoke a C function. */
    MVM_DISP_OUTCOME_CFUNCTION
} MVMDispProgramOutcomeKind;

/* The outcome of a dispatch program. Used only in the record case; the run
 * just does the effect of that outcome. */
struct MVMDispProgramOutcome {
    /* The kind of outcome we have. */
    MVMDispProgramOutcomeKind kind;

    /* Data that goes with each outcome kind. */
    union {
        /* A value to return, along with the kind of value it is. Marked. */
        struct {
            MVMRegister result_value;
            MVMuint8 result_kind;
        };
        /* A invocation of either bytecode or a C function. */
        struct {
            union {
                /* The code object to invoke. Marked. */
                MVMCode *code;
                /* The C function to be invoked. */
                void (*c_func) (MVMThreadContext *tc, MVMArgs arg_info);
            };
            /* Arguments for an invocation (must point into otherwise marked
             * areas). */
            MVMArgs args;
        };
        /* A dispatcher delegation. */
        struct {
            MVMDispDefinition *delegate_disp;
            MVMObject *delegate_capture;
        };
    };
};

/* A value that is involved in the dispatch. It may come from the initial
 * arguments, it may be an inserted constant, or it may be from an attribute
 * read out of another value. */
typedef enum {
    /* A value from the initial capture. */
    MVMDispProgramRecordingCaptureValue,
    /* A literal constant value. */
    MVMDispProgramRecordingLiteralValue,
    /* A read of an attribute from a dependent value. */
    MVMDispProgramRecordingAttributeValue
} MVMDispProgramRecordingValueSource;
struct MVMDispProgramRecordingValue {
    /* The source of the value, which determines which part of the union below
     * that we shall look at. */
    MVMDispProgramRecordingValueSource source;

    /* Details (depends on source). */
    union {
        struct {
            /* The index within the initial arguments capture. */
            MVMuint32 index;
        } capture;
        struct {
            /* The literal value and its kind. */
            MVMRegister value;
            MVMCallsiteFlags kind;
        } literal;
        struct {
            /* The value that we'll read from. */
            MVMuint32 from_value;
            /* The offset of that object we'll read from. */
            MVMuint32 offset;
            /* The kind of value we'll read. */
            MVMCallsiteFlags kind;
        } attribute;
    };

    /* The tracking object, if any. */
    MVMObject *tracked;

    /* Basic guards that have been applied. When we compile the guard program,
     * we'll often simplify this; for example, if the incoming argument was a
     * literal string then we'd drop the literal value guard, or if it has a
     * literal guard then the type and concreteness guards are implicitly
     * covered anyway. */
    MVMuint8 guard_type;
    MVMuint8 guard_concreteness;
    MVMuint8 guard_literal;

    /* A list of objects that this value must *not* be. */
    MVM_VECTOR_DECL(MVMObject *, not_literal_guards);
};

/* A derived capture. */
typedef enum {
    MVMDispProgramRecordingInitial,
    MVMDispProgramRecordingDrop,
    MVMDispProgramRecordingInsert
} MVMDispProgramRecordingTransformation;
struct MVMDispProgramRecordingCapture {
    /* The capture object we produced and handed back. */
    MVMObject *capture;

    /* The kind of transformation that it did. */
    MVMDispProgramRecordingTransformation transformation;

    /* The index involved in the insert or drop. */
    MVMuint32 index;

    /* For inserts, the index of the value that was involved. */
    MVMuint32 value_index;

    /* Tree of captures further derived from the this one. */
    MVM_VECTOR_DECL(MVMDispProgramRecordingCapture, captures);
};

/* Recording state of a dispatch program, updated as we move through the record
 * phase. */
struct MVMDispProgramRecording {
    /* The initial argument capture, top of the tree of captures. */
    MVMDispProgramRecordingCapture initial_capture;

    /* The values that we have encountered while recording, and maybe added
     * guards against. */
    MVM_VECTOR_DECL(MVMDispProgramRecordingValue, values);

    /* The index of the value that is the outcome of the dispatch. For a value
     * outcome, it's the value we'll produce. For the invocations, it's the
     * code or C function value. */
    MVMuint32 outcome_value;

    /* For an invocation outcome, this is the capture that we shall invoke the
     * arguments with. Will be located within the capture tree somewhere. */
    MVMObject *outcome_capture;
};

/* A "compiled" dispatch program, which is what we interpret at a callsite
 * (at least, until specialization, where more exciting things happen). */
struct MVMDispProgram {
    /* Non-GC-managed constants used in the dispatch. */
    MVMDispProgramConstant *constants;

    /* GC-managed constants (objects, strings, and STables) used in the
     * dispatch. */
    MVMCollectable **gc_constants;

    /* The number of GC-managed constants (for more efficient marking of
     * them). */
    MVMuint32 num_gc_constants;

    /* The number of temporaries. Temporaries are used while evaluating
     * the dispatch program, and may also be used as an argument buffer
     * when we cannot just point into a tail of the original one. */
    MVMuint32 num_temporaries;

    /* The first of the temporaries that is used in order to store an
     * argument buffer. These live long enough that we need to mark
     * them using the callsite. Equals num_temporaries if there aren't
     * any. */
    MVMuint32 first_args_temporary;

    /* Ops we execute to evaluate the dispatch program. */
    MVMuint32 num_ops;
    MVMDispProgramOp *ops;
};

/* Various kinds of constant we use during a dispatch program, to let us keep
 * the ops part more compact. */
union MVMDispProgramConstant {
    MVMCallsite *cs;
    MVMint64 i64;
    MVMnum64 n64;
};

/* Opcodes we may execute in a dispatch program. */
typedef enum {
    /* Guard that the type of an incoming argument is as expected. */
    MVMDispOpcodeGuardArgType,
    /* Guard that the type of an incoming argument is as expected and also
     * we have a concrete object. */
    MVMDispOpcodeGuardArgTypeConc,
    /* Guard that the type of an incoming argument is as expected and also
     * we have a type object. */
    MVMDispOpcodeGuardArgTypeTypeObject,
    /* Guard that the incoming argument is concrete. */
    MVMDispOpcodeGuardArgConc,
    /* Guard that the incoming argument is a type object. */
    MVMDispOpcodeGuardArgTypeObject,
    /* Guard that the type of an incoming argument is an expected object
     * literal. */
    MVMDispOpcodeGuardArgLiteralObj,
    /* Guard that the type of an incoming argument is an expected string
     * literal. */
    MVMDispOpcodeGuardArgLiteralStr,
    /* Guard that the type of an incoming argument is an expected int
     * literal. */
    MVMDispOpcodeGuardArgLiteralInt,
    /* Guard that the type of an incoming argument is an expected num
     * literal. */
    MVMDispOpcodeGuardArgLiteralNum,
    /* Guard that the type of an incoming argument is not an unexpected
     * literal. */
    MVMDispOpcodeGuardArgNotLiteralObj,
    /* Guard that the type of a temporary is as expected. */
    MVMDispOpcodeGuardTempType,
    /* Guard that the type of a temporary is as expected and also
     * we have a concrete object. */
    MVMDispOpcodeGuardTempTypeConc,
    /* Guard that the type of a temporary is as expected and also
     * we have a type object. */
    MVMDispOpcodeGuardTempTypeTypeObject,
    /* Guard that the incoming argument is concrete. */
    MVMDispOpcodeGuardTempConc,
    /* Guard that the incoming argument is a type object. */
    MVMDispOpcodeGuardTempTypeObject,
    /* Guard that the type of a temporary is an expected object
     * literal. */
    MVMDispOpcodeGuardTempLiteralObj,
    /* Guard that the type of a temporary is an expected string
     * literal. */
    MVMDispOpcodeGuardTempLiteralStr,
    /* Guard that the type of a temporary is an expected int
     * literal. */
    MVMDispOpcodeGuardTempLiteralInt,
    /* Guard that the type of a temporary is an expected num
     * literal. */
    MVMDispOpcodeGuardTempLiteralNum,
    /* Guard that the type of a temporary is not an unexpected
     * literal. */
    MVMDispOpcodeGuardTempNotLiteralObj,
    /* Load a capture value into a temporary. */
    MVMDispOpcodeLoadCaptureValue,
    /* Load a constant object or string into a temporary. */
    MVMDispOpcodeLoadConstantObjOrStr,
    /* Load a constant int into a temporary. */
    MVMDispOpcodeLoadConstantInt,
    /* Load a constant num into a temporary. */
    MVMDispOpcodeLoadConstantNum,
    /* Load an attribute object value into a temporary. */
    MVMDispOpcodeLoadAttributeObj,
    /* Load an attribute int value into a temporary. */
    MVMDispOpcodeLoadAttributeInt,
    /* Load an attribute num value into a temporary. */
    MVMDispOpcodeLoadAttributeNum,
    /* Load an attribute string value into a temporary. */
    MVMDispOpcodeLoadAttributeStr,
    /* Set one temp to the value of another. */
    MVMDispOpcodeSet,
    /* Set an object result outcome from a temporary. */
    MVMDispOpcodeResultValueObj,
    /* Set a string result outcome from a temporary. */
    MVMDispOpcodeResultValueStr,
    /* Set an integer result outcome from a temporary. */
    MVMDispOpcodeResultValueInt,
    /* Set a num result outcome from a temporary. */
    MVMDispOpcodeResultValueNum,
    /* Set the args buffer to use in the call to be the tail of that of
     * the initial capture. (Actually it's really the map that we take
     * the tail of.) The argument is how many of the args to skip from
     * the start. This is used when the dispatch should use everything
     * except the first args. */
    MVMDispOpcodeUseArgsTail,
    /* When we have to really produce a new set of arguments, we use this
     * op. Before it, we use load instructions to build the start of the
     * argument list. We then pass in a number of arguments to copy from
     * the incoming argument list, which may be 0. The arguments are
     * sourced by looking at the trailing N arguments of the initial
     * capture. They are copied into the last N temporaries. The args
     * are set to come from the argument temporaries, using the identity
     * map. */
    MVMDispOpcodeCopyArgsTail,
    /* Set a bytecode object result, specifying invokee and callsite.
     * The args should already have been set up. */
    MVMDispOpcodeResultBytecode,
    /* Set a C function object result, specifying invokee and callsite.
     * The args should already have been set up. */
    MVMDispOpcodeResultCFunction
} MVMDispProgramOpcode;

/* An operation, with its operands, in a dispatch program. */
struct MVMDispProgramOp {
    /* The opcode. */
    MVMDispProgramOpcode code;

    /* Operands. */
    union {
        struct {
            /* The argument index in the incoming capture. */
            MVMuint16 arg_idx;
            /* The thing to check it against (looked up in one of the constant
             * tables). */
            MVMuint32 checkee;
        } arg_guard;
        struct {
            /* The temporary to guard. */
            MVMuint32 temp;
            /* The thing to check it against (looked up in one of the constant
             * tables). */
            MVMuint32 checkee;
        } temp_guard;
        struct {
            /* The temporary to load into. */
            MVMuint32 temp;
            /* The index to load from. The thing we're indexing is part of
             * the instruction. For attribute loads, this is the offset into
             * the object to read (and we expect temp to start out containing
             * the object to read from, unlike any other load; this keeps our
             * union to 64 bits). */
            MVMuint32 idx;
        } load;
        struct {
            /* The number of args to skip when we use the tail of the incoming
             * capture. */
            MVMuint32 skip_args;
            /* The callsite index. */
            MVMuint32 callsite_idx;
        } use_arg_tail;
        struct {
            /* The number of args to copy from the tail of the incoming callsite
             * to the tail of the args temporaries area. */
            MVMuint32 tail_args;
            /* The callsite index. */
            MVMuint32 callsite_idx;
        } copy_arg_tail;
        struct {
            /* The temporary holding the result. */
            MVMuint32 temp;
        } res_value;
        struct {
            /* The temporary holding the thing to invoke. */
            MVMuint32 temp_invokee;
        } res_code;
    };
};

/* Functions called during the recording. */
void MVM_disp_program_run_dispatch(MVMThreadContext *tc, MVMDispDefinition *disp,
        MVMObject *capture, MVMDispInlineCacheEntry **ic_entry_ptr,
        MVMDispInlineCacheEntry *ic_entry, MVMStaticFrame *update_sf);
MVMObject * MVM_disp_program_record_track_arg(MVMThreadContext *tc, MVMObject *capture,
        MVMuint32 index);
MVMObject * MVM_disp_program_record_track_attr(MVMThreadContext *tc, MVMObject *tracked,
        MVMObject *class_handle, MVMString *name);
void MVM_disp_program_record_guard_type(MVMThreadContext *tc, MVMObject *tracked);
void MVM_disp_program_record_guard_concreteness(MVMThreadContext *tc, MVMObject *tracked);
void MVM_disp_program_record_guard_literal(MVMThreadContext *tc, MVMObject *tracked);
void MVM_disp_program_record_guard_not_literal_obj(MVMThreadContext *tc,
       MVMObject *tracked, MVMObject *object);
MVMObject * MVM_disp_program_record_capture_drop_arg(MVMThreadContext *tc, MVMObject *capture,
        MVMuint32 index);
MVMObject * MVM_disp_program_record_capture_insert_constant_arg(MVMThreadContext *tc,
        MVMObject *capture, MVMuint32 index, MVMCallsiteFlags kind, MVMRegister value);
MVMObject * MVM_disp_program_record_capture_insert_arg(MVMThreadContext *tc,
        MVMObject *capture, MVMuint32 index, MVMObject *tracked);
void MVM_disp_program_record_delegate(MVMThreadContext *tc, MVMString *dispatcher_id,
        MVMObject *capture);
void MVM_disp_program_record_result_constant(MVMThreadContext *tc, MVMCallsiteFlags kind,
        MVMRegister value);
void MVM_disp_program_record_result_tracked_value(MVMThreadContext *tc, MVMObject *tracked);
void MVM_disp_program_record_code_constant(MVMThreadContext *tc, MVMCode *result, MVMObject *capture);
void MVM_disp_program_record_c_code_constant(MVMThreadContext *tc, MVMCFunction *result,
        MVMObject *capture);
MVMuint32 MVM_disp_program_record_end(MVMThreadContext *tc, MVMCallStackDispatchRecord* record,
        MVMuint32 *thunked);

/* Functions to run dispatch programs. */
MVMint64 MVM_disp_program_run(MVMThreadContext *tc, MVMDispProgram *dp,
        MVMCallStackDispatchRun *disp_run);

/* Memory management of dispatch programs. */
void MVM_disp_program_mark(MVMThreadContext *tc, MVMDispProgram *dp, MVMGCWorklist *worklist);
void MVM_disp_program_mark_recording(MVMThreadContext *tc, MVMDispProgramRecording *rec,
        MVMGCWorklist *worklist);
void MVM_disp_program_mark_run_temps(MVMThreadContext *tc, MVMDispProgram *dp,
        MVMCallsite *cs, MVMRegister *temps, MVMGCWorklist *worklist);
void MVM_disp_program_mark_outcome(MVMThreadContext *tc, MVMDispProgramOutcome *outcome,
        MVMGCWorklist *worklist);
void MVM_disp_program_destroy(MVMThreadContext *tc, MVMDispProgram *dp);
void MVM_disp_program_recording_destroy(MVMThreadContext *tc, MVMDispProgramRecording *rec);
