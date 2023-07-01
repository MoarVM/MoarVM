struct MVMStatBody {
    uv_stat_t *uv_stat;
    MVMint64   exists;
};
struct MVMStat {
    MVMObject common;
    MVMStatBody body;
};

/* Function for REPR setup. */
const MVMREPROps * MVMStat_initialize(MVMThreadContext *tc);

