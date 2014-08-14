#include "moar.h"

/* Gets the current thread's profiling data structure, creating it if needed. */
static MVMProfileThreadData * get_thread_data(MVMThreadContext *tc) {
    if (!tc->prof_data) {
        tc->prof_data = calloc(1, sizeof(MVMProfileThreadData));
        tc->prof_data->start_time = uv_hrtime();
    }
    return tc->prof_data;
}

/* Log that we're entering a new frame. */
void MVM_profile_log_enter(MVMThreadContext *tc, MVMStaticFrame *sf, MVMuint64 mode) {
    MVMProfileThreadData *ptd = get_thread_data(tc);

    /* Try to locate the entry node, if it's in the call graph already. */
    MVMProfileCallNode *pcn = NULL;
    MVMuint32 i;
    if (ptd->current_call)
        for (i = 0; i < ptd->current_call->num_succ; i++)
            if (ptd->current_call->succ[i]->sf == sf)
                pcn = ptd->current_call->succ[i];

    /* If we didn't find a call graph node, then create one and add it to the
     * graph. */
    if (!pcn) {
        pcn     = calloc(1, sizeof(MVMProfileCallNode));
        pcn->sf = sf;
        if (ptd->current_call) {
            MVMProfileCallNode *pred = ptd->current_call;
            pcn->pred = pred;
            if (pred->num_succ == pred->alloc_succ) {
                pred->alloc_succ += 8;
                pred->succ = realloc(pred->succ,
                    pred->alloc_succ * sizeof(MVMProfileCallNode *));
            }
            pred->succ[pred->num_succ] = pcn;
            pred->num_succ++;
        }
        else {
            if (!ptd->call_graph)
                ptd->call_graph = pcn;
        }
    }

    /* Increment entry counts. */
    pcn->total_entries++;
    switch (mode) {
        case MVM_PROFILE_ENTER_SPESH:
            pcn->specialized_entries++;
            break;
        case MVM_PROFILE_ENTER_SPESH_INLINE:
            pcn->specialized_entries++;
            pcn->inlined_entries++;
            break;
        case MVM_PROFILE_ENTER_JIT:
            pcn->jit_entries++;
            break;
        case MVM_PROFILE_ENTER_JIT_INLINE:
            pcn->jit_entries++;
            pcn->inlined_entries++;
            break;
    }

    /* Log entry time; clear skip time. */
    pcn->cur_entry_time = uv_hrtime();
    pcn->cur_skip_time  = 0;

    /* The current call graph node becomes this one. */
    ptd->current_call = pcn;
}

/* Log that we're exiting a frame normally. */
void MVM_profile_log_exit(MVMThreadContext *tc) {
    MVMProfileThreadData *ptd = get_thread_data(tc);

    /* Ensure we've a current frame; panic if not. */
    /* XXX in future, don't panic, try to cope. This is for debugging
     * profiler issues. */
    MVMProfileCallNode *pcn = ptd->current_call;
    if (!pcn)
        MVM_panic(1, "Profiler lost sequence");

    /* Add to total time. */
    pcn->total_time += (uv_hrtime() - pcn->cur_entry_time) - pcn->cur_skip_time;

    /* Move back to predecessor in call graph. */
    ptd->current_call = pcn->pred;
}

/* Log that we've just allocated the passed object (just log the type). */
void MVM_profile_log_allocated(MVMThreadContext *tc, MVMObject *obj) {
    MVMProfileThreadData *ptd = get_thread_data(tc);
}
