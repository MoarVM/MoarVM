struct MVMPtrBody {
    void *cobj;
    MVMBlob *blob;
};

struct MVMPtr {
    MVMObject common;
    MVMPtrBody body;
};

const MVMREPROps * MVMPtr_initialize(MVMThreadContext *tc);
