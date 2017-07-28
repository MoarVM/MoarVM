/* Representation used for holding specializer (dynamic optimization) data
 * about a static frame (logged statistics, generated specializations, and
 * so forth). */

struct MVMStaticFrameSpeshBody {
    void *dummy;
};
struct MVMStaticFrameSpesh {
    MVMObject common;
    MVMStaticFrameSpeshBody body;
};

/* Function for REPR setup. */
const MVMREPROps * MVMStaticFrameSpesh_initialize(MVMThreadContext *tc);
