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
        struct {
            /* The code object to invoke. Marked. */
            struct {
                MVMCode *code;
                MVMCFunction *c_code;
            };
            /* Arguments for an invocation (must point into otherwise marked
             * areas). */
            MVMArgs args;
        };
    };
};

/* Functions called during the recording. */
MVMObject * MVM_disp_program_record_capture_drop_arg(MVMThreadContext *tc, MVMObject *capture,
        MVMuint32 index);
void MVM_disp_program_record_result_constant(MVMThreadContext *tc, MVMObject *result);
void MVM_disp_program_record_result_capture_value(MVMThreadContext *tc, MVMObject *capture,
        MVMuint32 index);
void MVM_disp_program_record_code_constant(MVMThreadContext *tc, MVMCode *result, MVMObject *capture);
MVMuint32 MVM_disp_program_record_end(MVMThreadContext *tc, MVMCallStackDispatchRecord* record);
