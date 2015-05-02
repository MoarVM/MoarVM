/* Representation used by VM-level iterators. */

#define MVM_ITER_MODE_ARRAY         0
#define MVM_ITER_MODE_ARRAY_INT     1
#define MVM_ITER_MODE_ARRAY_NUM     2
#define MVM_ITER_MODE_ARRAY_STR     3
#define MVM_ITER_MODE_HASH          4

struct MVMIterBody {
    /* whether hash or array */
    MVMuint32 mode;

    /* array or hash being iterated */
    MVMObject *target;

    /* next hash item to give or next array index */
    union {
        struct {
            MVMHashEntry *curr, *next;
            unsigned      bucket_state;
        } hash_state;
        struct {
            MVMint64 index;
            MVMint64 limit;
        } array_state;
    };
};
struct MVMIter {
    MVMObject common;
    MVMIterBody body;
};

/* Function for REPR setup. */
const MVMREPROps * MVMIter_initialize(MVMThreadContext *tc);

MVMObject * MVM_iter(MVMThreadContext *tc, MVMObject *target);
MVMint64 MVM_iter_istrue(MVMThreadContext *tc, MVMIter *iter);
MVMString * MVM_iterkey_s(MVMThreadContext *tc, MVMIter *iterator);
MVMObject * MVM_iterval(MVMThreadContext *tc, MVMIter *iterator);
