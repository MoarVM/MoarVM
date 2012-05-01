/* Representation used by VM-level arrays. */
typedef struct _MVMArrayBody {
    /* The number of elements in the array. */
    MVMuint64 elems;
    
    /* The amount of space allocated in the array. */
    MVMuint64 alloc;
    
    /* The array data segment. */
    MVMObject **data;
} MVMArrayBody;
typedef struct _MVMArray {
    MVMObject common;
    MVMArrayBody body;
} MVMArray;

/* Function for REPR setup. */
MVMREPROps * MVMArray_initialize(MVMThreadContext *tc);
