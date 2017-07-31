/* Representation used for holding specializer (dynamic optimization) data
 * about a static frame (logged statistics, generated specializations, and
 * so forth). */

struct MVMStaticFrameSpeshBody {
    /* Specialization argument guard tree, for selecting a specialization. */
    MVMSpeshArgGuard *spesh_arg_guard;

    /* Specializations array, if there are any. Candidates themselves never
     * move in memory; the array of pointers to them is managed using the
     * fixed size allocator and freed at the next safepoint. */
    MVMSpeshCandidate **spesh_candidates;
    MVMuint32 num_spesh_candidates;

    /* Recorded count for data recording for the specializer. Incremented
     * until the recording threshold is reached, and may be cleared by the
     * specialization worker later if it wants more data recorded. Allowed
     * to be a bit racey between threads; it's not a problem if we get an
     * extra recording or so. */
    MVMuint32 spesh_entries_recorded;

    /* Specialization statistics assembled by the specialization worker thread
     * from logs. */
    MVMSpeshStats *spesh_stats;

    /* Number of times the frame was promoted to the heap, when it was not
     * specialized. Used to decide whether we'll directly allocate this frame
     * on the heap. */
    MVMuint32 num_heap_promotions;
};
struct MVMStaticFrameSpesh {
    MVMObject common;
    MVMStaticFrameSpeshBody body;
};

/* Function for REPR setup. */
const MVMREPROps * MVMStaticFrameSpesh_initialize(MVMThreadContext *tc);
