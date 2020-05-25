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

/* The outcome of a dispatch program. Used for both the record and run case. */
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

/* Recording state of a dispatch program, updated as we move through the record
 * phase. */
struct MVMDispProgramRecording {
    /* The initial argument capture. */
    MVMObject *initial_capture;

    /* The values that we have encountered while recording, and maybe added
     * guards against. */
    MVM_VECTOR_DECL(MVMDispProgramRecordingValue, values);

    /* Tree of captures derived from the initial one. */
    MVM_VECTOR_DECL(MVMDispProgramRecordingCapture, captures);
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

            /* The tracking object. */
            MVMObject *tracked;
        } capture;
        struct {
            /* The literal value and its kind. */
            MVMRegister value;
            MVMCallsiteFlags kind;
        } literal;
        // TODO struct { } attribute;
    };

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
    MVMDispProgramRecordingDrop,
    MVMDispProgramRecordingInsert
} MVMDispProgramRecordingTransformation;
struct MVMDispProgramRecordingCapture {
    /* The capture object we produced and handed back. */
    MVMObject *capture;

    /* The kind of transformation that it did. */
    MVMDispProgramRecordingTransformation transformation;

    /* The index involved in the inert or drop. */
    MVMuint32 index;

    /* For inserts, the index of the value that was involved. */
    MVMuint32 value_index;

    /* Tree of captures further derived from the this one. */
    MVM_VECTOR_DECL(MVMDispProgramRecordingCapture, captures);
};

/* Functions called during the recording. */
void MVM_disp_program_run_dispatch(MVMThreadContext *tc, MVMDispDefinition *disp, MVMObject *capture);
MVMObject * MVM_disp_program_record_track_arg(MVMThreadContext *tc, MVMObject *capture,
        MVMuint32 index);
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
void MVM_disp_program_record_result_constant(MVMThreadContext *tc, MVMObject *result);
void MVM_disp_program_record_result_capture_value(MVMThreadContext *tc, MVMObject *capture,
        MVMuint32 index);
void MVM_disp_program_record_code_constant(MVMThreadContext *tc, MVMCode *result, MVMObject *capture);
void MVM_disp_program_record_c_code_constant(MVMThreadContext *tc, MVMCFunction *result,
        MVMObject *capture);
MVMuint32 MVM_disp_program_record_end(MVMThreadContext *tc, MVMCallStackDispatchRecord* record,
        MVMuint32 *thunked);
