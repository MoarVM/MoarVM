/* Representation used by VM-level iterators. */

#define MVM_ITER_MODE_ARRAY 0
#define MVM_ITER_MODE_HASH 1

typedef struct _MVMIterBody {
    /* whether hash or array */
    MVMuint32 mode;

    /* array or hash being iterated */
    MVMObject *target;

    /* next hash item to give or next array index */
    union {
        struct {
            MVMHashEntry *next;
            MVMHashEntry *curr;
        } hash_state;
        struct {
            MVMint64 index;
            MVMint64 limit;
        } array_state;
    };
} MVMIterBody;
typedef struct _MVMIter {
    MVMObject common;
    MVMIterBody body;
} MVMIter;

/* Function for REPR setup. */
MVMREPROps * MVMIter_initialize(MVMThreadContext *tc);

MVMObject * MVM_iter(MVMThreadContext *tc, MVMObject *target);
MVMint64 MVM_iter_istrue(MVMThreadContext *tc, MVMIter *iter);
MVMString * MVM_iterkey_s(MVMThreadContext *tc, MVMIter *iterator);
MVMObject * MVM_iterval(MVMThreadContext *tc, MVMIter *iterator);
