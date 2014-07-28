typedef void MVMExtOpFunc(MVMThreadContext *tc);
typedef void MVMExtOpSpesh(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB *bb, MVMSpeshIns *ins);
typedef void MVMExtOpFactDiscover(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshIns *ins);

/* Flags we might put on an extension op to indicate its properties. */
#define MVM_EXTOP_PURE          1
#define MVM_EXTOP_NOINLINE      2

struct MVMExtRegistry {
    MVMDLLSym *sym;
    MVMString *name;
    UT_hash_handle hash_handle;
};

struct MVMExtOpRegistry {
    MVMString *name;
    MVMExtOpFunc *func;
    MVMOpInfo info;
    MVMExtOpSpesh *spesh;
    MVMExtOpFactDiscover *discover;
    UT_hash_handle hash_handle;
};

int MVM_ext_load(MVMThreadContext *tc, MVMString *lib, MVMString *ext);
MVM_PUBLIC int MVM_ext_register_extop(MVMThreadContext *tc, const char *cname,
        MVMExtOpFunc func, MVMuint8 num_operands, MVMuint8 operands[],
        MVMExtOpSpesh *spesh, MVMExtOpFactDiscover *discover, MVMuint32 flags);
const MVMOpInfo * MVM_ext_resolve_extop_record(MVMThreadContext *tc,
        MVMExtOpRecord *record);
