struct MVMStatBody {
    uv_stat_t *uv_stat;
    MVMint64   exists;
#ifdef _WIN32
    MVMString *filename;
#endif
};
struct MVMStat {
    MVMObject common;
    MVMStatBody body;
};

/* Function for REPR setup. */
const MVMREPROps * MVMStat_initialize(MVMThreadContext *tc);

