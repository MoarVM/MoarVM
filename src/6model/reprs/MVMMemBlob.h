struct MVMMemBlobBody {
    void *address;
    MVMuint64  size;
    MVMuint64 *refmap;
};

struct MVMMemBlob {
    MVMObject common;
    MVMMemBlobBody body;
};

const MVMREPROps * MVMMemBlob_initialize(MVMThreadContext *tc);
