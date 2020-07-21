/* Representation used for holding data logged by the interpreter for the
 * purpose of producing specializations. */

/* The kind of log entry we have. */
typedef enum {
    /* Entry to a callframe. */
    MVM_SPESH_LOG_ENTRY,
    /* Parameter type information. */
    MVM_SPESH_LOG_PARAMETER,
    /* Parameter type if we were to decontainerize the parameter. Recorded
     * when the parameter is a container type. */
    MVM_SPESH_LOG_PARAMETER_DECONT,
    /* Decont, attribute lookup, or lexical lookup type information. */
    MVM_SPESH_LOG_TYPE,
    /* Invoked static frame, and whether we are its outer. */
    MVM_SPESH_LOG_INVOKE,
    /* OSR point. */
    MVM_SPESH_LOG_OSR,
    /* Return from a callframe, possibly with a logged type. */
    MVM_SPESH_LOG_RETURN,
    /* Return from a logged callframe to an unlogged one, needed to keep
     * the spesh simulation stack in sync. */
    MVM_SPESH_LOG_RETURN_TO_UNLOGGED,
    /* Dispatch program resolution result. */
    MVM_SPESH_LOG_DISPATCH_RESOLUTION,
} MVMSpeshLogEntryKind;

/* Flags on types. */
#define MVM_SPESH_LOG_TYPE_FLAG_CONCRETE 1
#define MVM_SPESH_LOG_TYPE_FLAG_RW_CONT  2

/* An entry in the spesh log. */
struct MVMSpeshLogEntry {
    /* The kind of log entry it is; discriminator for the union. */
    MVMint32 kind;

    /* Call frame correlation ID. */
    MVMint32 id;

    union {
        /* Entry to a call frame (ENTRY). */
        struct {
            MVMStaticFrame *sf;
            MVMCallsite *cs;
        } entry;

        /* Observed parameter type (PARAMETER, PARAMETER_DECONT). */
        struct {
            MVMObject *type;
            MVMint32 flags;
            MVMuint16 arg_idx;
        } param;

        /* Observed type (TYPE, RETURN). */
        struct {
            MVMObject *type;
            MVMint32 flags;
            MVMuint32 bytecode_offset;
        } type;

        /* Observed invocation (INVOKE). */
        struct {
            MVMStaticFrame *sf;
            MVMint16 caller_is_outer;
            MVMuint16 was_multi;
            MVMuint32 bytecode_offset;
        } invoke;

        /* Observed OSR point (OSR). */
        struct {
            MVMuint32 bytecode_offset;
        } osr;

        /* Dispatch resolution (DISPATCH_RESOLUTION). */
        struct {
            MVMuint32 bytecode_offset;
            MVMuint16 result_index;
        } dispatch;
    };
};

/* The spesh log representation itself. */
struct MVMSpeshLogBody {
    /* The sending thread. */
    MVMThread *thread;

    /* Array of log entries. */
    MVMSpeshLogEntry *entries;

    /* Number of log entries so far and limit. */
    MVMuint32 used;
    MVMuint32 limit;

    /* If this was created due to a new compilation unit (heuristic to do
     * better at outer-loop OSR); we go over-quota for those, and this is
     * to help us restore it again. */
    MVMuint8 was_compunit_bumped;

    /* When in debug mode, mutex and condition variable used to block the
     * thread sending a log until the spesh worker has processed it. */
    uv_mutex_t *block_mutex;
    uv_cond_t *block_condvar;
    AO_t completed;
};
struct MVMSpeshLog {
    MVMObject common;
    MVMSpeshLogBody body;
};

/* Function for REPR setup. */
const MVMREPROps * MVMSpeshLog_initialize(MVMThreadContext *tc);
