struct MVMUnsafePtrBody {
    void *address;
};

struct MVMUnsafePtr {
    MVMObject common;
    MVMUnsafePtrBody body;
};

const MVMREPROps * MVMUnsafePtr_initialize(MVMThreadContext *tc);
