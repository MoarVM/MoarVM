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
    MVMProfileThreadData *ptd  = get_thread_data(tc);
    MVMProfileCallNode   *pcn  = ptd->current_call;
    MVMObject            *what = STABLE(obj)->WHAT;
    if (pcn) {
        /* See if there's an existing node to update. */
        MVMuint32 i;
        for (i = 0; i < pcn->num_alloc; i++) {
            if (pcn->alloc[i].type == what) {
                pcn->alloc[i].allocations++;
                return;
            }
        }

        /* No entry; create one. */
        if (pcn->num_alloc == pcn->alloc_alloc) {
            pcn->alloc_alloc += 8;
            pcn->alloc = realloc(pcn->alloc,
                pcn->alloc_alloc * sizeof(MVMProfileAllocationCount));
        }
        pcn->alloc[pcn->num_alloc].type        = what;
        pcn->alloc[pcn->num_alloc].allocations = 1;
        pcn->num_alloc++;
    }
}

/* Logs the start of a GC run. */
void MVM_profiler_log_gc_start(MVMThreadContext *tc, MVMuint32 full) {
    MVMProfileThreadData *ptd = get_thread_data(tc);

    /* Make a new entry in the GCs. We use the cleared_bytes to store the
     * maximum that could be cleared, and after GC is done will subtract
     * retained bytes and promoted bytes. */
    if (ptd->num_gcs == ptd->alloc_gcs) {
        ptd->alloc_gcs += 16;
        ptd->gcs = realloc(ptd->gcs, ptd->alloc_gcs * sizeof(MVMProfileGC));
    }
    ptd->gcs[ptd->num_gcs].full          = full;
    ptd->gcs[ptd->num_gcs].cleared_bytes = (char *)tc->nursery_alloc -
                                           (char *)tc->nursery_tospace;

    /* Zero promoted bytes counter. */
    tc->gc_promoted_bytes = 0;

    /* Record start time. */
    ptd->cur_gc_start_time = uv_hrtime();
}

/* Logs the end of a GC run. */
void MVM_profiler_log_gc_end(MVMThreadContext *tc) {
    MVMProfileThreadData *ptd = get_thread_data(tc);
    MVMProfileCallNode   *pcn = ptd->current_call;
    MVMuint64 gc_time;
    MVMint32  retained_bytes;

    /* Record time spent. */
    gc_time = uv_hrtime() - ptd->cur_gc_start_time;
    ptd->gcs[ptd->num_gcs].time = gc_time;

    /* Record retained and promoted bytes. */
    retained_bytes = (char *)tc->nursery_alloc - (char *)tc->nursery_tospace;
    ptd->gcs[ptd->num_gcs].promoted_bytes = tc->gc_promoted_bytes;
    ptd->gcs[ptd->num_gcs].retained_bytes = retained_bytes;

    /* Tweak cleared bytes count. */
    ptd->gcs[ptd->num_gcs].cleared_bytes -= (retained_bytes + tc->gc_promoted_bytes);

    /* Increment the number of GCs we've done. */
    ptd->num_gcs++;

    /* Discount GC time from all active frames. */
    while (pcn) {
        pcn->cur_skip_time += gc_time;
        pcn = pcn->pred;
    }
}
