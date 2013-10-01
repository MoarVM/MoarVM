struct MVMDLLSymBody {
    void *ptr;
    MVMDLLRegistry *dll;
};

struct MVMDLLSym {
    MVMObject common;
    MVMDLLSymBody body;
};

const MVMREPROps * MVMDLLSym_initialize(MVMThreadContext *tc);
