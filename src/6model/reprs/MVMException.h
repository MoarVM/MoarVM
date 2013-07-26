/* Representation for an exception in MoarVM. */
typedef struct _MVMExceptionBody {
    /* The exception message. */
    MVMString *message;
    
    /* The payload (object thrown with the exception). */
    MVMObject *payload;
    
    /* The exception category. */
    MVMint64 category;
    
    /* Where was the exception thrown from? */
    MVMFrame *origin;
    
    /* Is the exception resumable? */
    MVMuint8 resumable;
} MVMExceptionBody;
typedef struct _MVMException {
    MVMObject common;
    MVMExceptionBody body;
} MVMException;

/* Function for REPR setup. */
MVMREPROps * MVMException_initialize(MVMThreadContext *tc);
