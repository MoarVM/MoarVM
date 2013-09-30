typedef struct {
    MVMuint64 size;
    MVMuint64 align;
    MVMint64 (*fetch_int)(MVMThreadContext *tc, const void *storage);
    void (*store_int)(MVMThreadContext *tc, void *storage, MVMint64 value);
    MVMnum64 (*fetch_num)(MVMThreadContext *tc, const void *storage);
    void (*store_num)(MVMThreadContext *tc, void *storage, MVMnum64 value);
} MVMCScalarSpec;

typedef struct {
    MVMuint64  elem_count;
    MVMuint64  elem_size;
    MVMObject *elem_type;
} MVMCArraySpec;

typedef struct {
    MVMObject *type;
    MVMuint64 offset;
} MVMCMemberSpec;

typedef struct {
    MVMuint64 size;
    MVMuint64 align;
    MVMuint64 member_count;
    MVMString **member_names;
    MVMCMemberSpec *members;
} MVMCStructSpec;

typedef struct {
    MVMuint64 size;
    MVMuint64 align;
    MVMuint64 member_count;
    MVMString **member_names;
    MVMObject **member_types;
} MVMCUnionSpec;

#if 0
typedef union {
    MVMPtrBody PTR;
    struct {
        void *cobj;
        MVMBlob *blob;
        MVMCArray *flexibles;
    } FS;
} MVMCFlexStructBody;

typedef union {
    MVMPtr PTR;
    struct {
        MVMObject common;
        MVMCFlexStructBody body;
    } FS;
} MVMCFlexStruct;
#endif

const MVMREPROps * MVMCScalar_initialize(MVMThreadContext *tc);
const MVMREPROps * MVMCPtr_initialize(MVMThreadContext *tc);
const MVMREPROps * MVMCArray_initialize(MVMThreadContext *tc);
const MVMREPROps * MVMCStruct_initialize(MVMThreadContext *tc);
const MVMREPROps * MVMCUnion_initialize(MVMThreadContext *tc);
const MVMREPROps * MVMCFlexStruct_initialize(MVMThreadContext *tc);
