struct MVMBlobBody {
    char *storage;
    MVMuint64  size;
    MVMuint64 *refmap;
};

struct MVMBlob {
    MVMObject common;
    MVMBlobBody body;
};

const MVMREPROps * MVMBlob_initialize(MVMThreadContext *tc);
