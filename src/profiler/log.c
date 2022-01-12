#include "moar.h"

#define CONFPROG_DEBUG_LEVEL_PROFILER_RESULTS 4

static void debugprint(MVMuint8 active, MVMThreadContext *tc, const char *str, ...) {
    va_list args;
    va_start(args, str);

    if (active) {
        fprintf(stderr, "%p: ", tc);
        vfprintf(stderr, str, args);
        fprintf(stderr, "\n");
    }

    va_end(args);
}

#define DEBUG_LVL(level) ((debug_level) & CONFPROG_DEBUG_LEVEL_ ## level)

/* Gets the current thread's profiling data structure, creating it if needed. */
static MVMProfileThreadData * get_thread_data(MVMThreadContext *tc) {
    if (!tc->prof_data) {
        tc->prof_data = MVM_calloc(1, sizeof(MVMProfileThreadData));
        tc->prof_data->start_time = uv_hrtime();
    }
    return tc->prof_data;
}

static MVMProfileCallNode *make_new_pcn(MVMProfileThreadData *ptd, MVMuint64 current_hrtime) {
    MVMProfileCallNode *current_call = ptd->current_call;
    MVMProfileCallNode *pcn     = MVM_calloc(1, sizeof(MVMProfileCallNode));
    pcn->first_entry_time = current_hrtime;
    if (current_call) {
        MVMProfileCallNode *pred = current_call;
        pcn->pred = pred;
        if (pred->num_succ == pred->alloc_succ) {
            pred->alloc_succ += 8;
            pred->succ = MVM_realloc(pred->succ,
                    pred->alloc_succ * sizeof(MVMProfileCallNode *));
        }
        pred->succ[pred->num_succ] = pcn;
        pred->num_succ++;
    }
    else {
        if (!ptd->call_graph)
            ptd->call_graph = pcn;
    }

    return pcn;
}

/* Log that we're entering a new frame. */
void MVM_profile_log_enter(MVMThreadContext *tc, MVMStaticFrame *sf, MVMuint64 mode) {
    if (tc->instance->profiling) {
        MVMProfileThreadData *ptd = get_thread_data(tc);

        MVMuint64 current_hrtime = uv_hrtime();
        
        MVMuint8 was_entered_via_confprog = 0;

        /* Try to locate the entry node, if it's in the call graph already. */
        MVMProfileCallNode *pcn = NULL;
        MVMuint32 i;
        if (ptd->current_call)
            for (i = 0; i < ptd->current_call->num_succ; i++)
                if (ptd->staticframe_array[ptd->current_call->succ[i]->sf_idx] == sf)
                    pcn = ptd->current_call->succ[i];

        if (MVM_UNLIKELY(!ptd->current_call)) {
            /* debug_level used by the debugprint macros */
            MVMuint8 debug_level = tc->instance->confprog ? tc->instance->confprog->debugging_level : 0;
            MVMuint8 has_confprogs = tc->instance->confprog && (MVM_confprog_has_entrypoint(tc, MVM_PROGRAM_ENTRYPOINT_PROFILER_STATIC)
                        || MVM_confprog_has_entrypoint(tc, MVM_PROGRAM_ENTRYPOINT_PROFILER_STATIC));
            /* At the very beginning of profiling with a confprog set, the
             * root call_graph node needs to be created. */
            if (has_confprogs && !ptd->call_graph) {
                debugprint(DEBUG_LVL(PROFILER_RESULTS), tc, "initialized initial call graph node\n");
                ptd->call_graph = MVM_calloc(1, sizeof(MVMProfileCallNode));
            }
            /* In that case, we've got to check if the SF in question is
             * desired as an entry point */
            if (sf->body.instrumentation && has_confprogs) {
                MVMStaticFrameInstrumentation *instrumentation = sf->body.instrumentation;
                if (instrumentation->profiler_confprog_result == MVM_CONFPROG_SF_RESULT_NEVER) {
                    goto confprog_refused_enter;
                }
                else if (instrumentation->profiler_confprog_result == MVM_CONFPROG_SF_RESULT_TO_BE_DETERMINED) {
                    if (MVM_confprog_has_entrypoint(tc, MVM_PROGRAM_ENTRYPOINT_PROFILER_STATIC)) {
                        MVMuint8 result;
                        debugprint(DEBUG_LVL(PROFILER_RESULTS), tc, "running 'profiler_static' entrypoint in confprog");
                        result = MVM_confprog_run(tc, (void *)sf, MVM_PROGRAM_ENTRYPOINT_PROFILER_STATIC, MVM_CONFPROG_SF_RESULT_ALWAYS);
                        instrumentation->profiler_confprog_result = result;
                        if (result == MVM_CONFPROG_SF_RESULT_NEVER) {
                            debugprint(DEBUG_LVL(PROFILER_RESULTS), tc, "  confprog for SF resulted in 'never profile'");
                            goto confprog_refused_enter;
                        }
                        if (DEBUG_LVL(PROFILER_RESULTS)) {
                            switch (result) {
                                case MVM_CONFPROG_SF_RESULT_DYNAMIC_SUGGEST_YES:
                                case MVM_CONFPROG_SF_RESULT_DYNAMIC_SUGGEST_NO:
                                    debugprint(DEBUG_LVL(PROFILER_RESULTS), tc, "  confprog result: run dynamic program with default value '%s' (result value: %d)", result == MVM_CONFPROG_SF_RESULT_DYNAMIC_SUGGEST_YES ? "yes" : "no", result);
                                    break;
                                case MVM_CONFPROG_SF_RESULT_ALWAYS:
                                    debugprint(DEBUG_LVL(PROFILER_RESULTS), tc, "  confprog result: always profile from this SF (result value: %d)", result);
                                    break;
                                case MVM_CONFPROG_SF_RESULT_TO_BE_DETERMINED:
                                    debugprint(DEBUG_LVL(PROFILER_RESULTS), tc, "  confprog result: to be determined (result value %d) - will enter this time, but re-run next time", result);
                                    break;
                                default:
                                    debugprint(DEBUG_LVL(PROFILER_RESULTS), tc, "  unrecognized result value from confprog: %d", result);
                                    break;
                            }
                            debugprint(DEBUG_LVL(PROFILER_RESULTS), tc, "  confprog for SF resulted in 'never profile'");
                        }
                    }
                    else if (MVM_confprog_has_entrypoint(tc, MVM_PROGRAM_ENTRYPOINT_PROFILER_DYNAMIC)) {
                        instrumentation->profiler_confprog_result = MVM_CONFPROG_SF_RESULT_DYNAMIC_SUGGEST_YES;
                    }
                    else {
                        MVM_oops(tc, "here we are, what now?");
                    }
                }
                if (instrumentation->profiler_confprog_result == MVM_CONFPROG_SF_RESULT_DYNAMIC_SUGGEST_NO
                        || instrumentation->profiler_confprog_result == MVM_CONFPROG_SF_RESULT_DYNAMIC_SUGGEST_YES) {
                    debugprint(DEBUG_LVL(PROFILER_RESULTS), tc, "running 'profiler_dynamic' entrypoint in confprog\n");
                    if (MVM_confprog_has_entrypoint(tc, MVM_PROGRAM_ENTRYPOINT_PROFILER_DYNAMIC)) {
                        if (!MVM_confprog_run(tc, (void *)tc->cur_frame, MVM_PROGRAM_ENTRYPOINT_PROFILER_DYNAMIC, instrumentation->profiler_confprog_result == MVM_CONFPROG_SF_RESULT_DYNAMIC_SUGGEST_YES)) {
                            debugprint(DEBUG_LVL(PROFILER_RESULTS), tc, "  confprog result: no.\n");
                            goto confprog_refused_enter;
                        }
                        debugprint(DEBUG_LVL(PROFILER_RESULTS), tc, "  confprog result: yes.\n");
                    }
                    else {
                        debugprint(DEBUG_LVL(PROFILER_RESULTS), tc, "  static confprog said to run dynamic confprog, but none is installed - not profiling.\n");
                        goto confprog_refused_enter;
                    }
                }
                was_entered_via_confprog = 1;
            }
        }
        /*else {*/
            /*fprintf(stderr, "there actually was a current_call. also, pcn is %p\n", pcn);*/
        /*}*/

        /* If we didn't find a call graph node, then create one and add it to the
         * graph. */
        if (!pcn) {
            MVMuint32 search;
            if (was_entered_via_confprog)
                ptd->current_call = ptd->call_graph;
            pcn = make_new_pcn(ptd, current_hrtime);
            for (search = 0; search < MVM_VECTOR_ELEMS(ptd->staticframe_array); search++) {
                if (ptd->staticframe_array[search] == sf) {
                    break;
                }
            }
            if (search == MVM_VECTOR_ELEMS(ptd->staticframe_array)) {
                MVM_VECTOR_PUSH(ptd->staticframe_array, sf);
            }
            pcn->sf_idx = search;
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
        pcn->entry_mode =  mode;

        /* Log entry time; clear skip time. */
        pcn->cur_entry_time = current_hrtime;
        pcn->cur_skip_time  = 0;

        /* The current call graph node becomes this one. */
        ptd->current_call = pcn;

        return;
    confprog_refused_enter:
        ptd->non_calltree_depth++;
    }
}

/* Log that we've entered a native routine */
void MVM_profile_log_enter_native(MVMThreadContext *tc, MVMObject *nativecallsite) {
    MVMProfileThreadData *ptd = get_thread_data(tc);
    MVMProfileCallNode *pcn = NULL;
    MVMuint64 current_hrtime = uv_hrtime();
    MVMNativeCallBody *callbody;
    MVMuint32 i;

    /* We locate the right call node by looking at sf being NULL and the
     * native_target_name matching our intended target. */
    callbody = MVM_nativecall_get_nc_body(tc, nativecallsite);
    if (ptd->current_call)
        for (i = 0; i < ptd->current_call->num_succ; i++)
            if (ptd->staticframe_array[ptd->current_call->succ[i]->sf_idx] == NULL)
                if (strcmp(callbody->sym_name,
                           ptd->current_call->succ[i]->native_target_name) == 0) {
                    pcn = ptd->current_call->succ[i];
                    break;
                }

    /* If we didn't find a call graph node, then create one and add it to the
     * graph. */
    if (!pcn) {
        pcn = make_new_pcn(ptd, current_hrtime);
        pcn->native_target_name = callbody->sym_name;
    }

    /* Increment entry counts. */
    pcn->total_entries++;
    pcn->entry_mode = 0;

    /* Log entry time; clear skip time. */
    pcn->cur_entry_time = current_hrtime;
    pcn->cur_skip_time  = 0;

    /* The current call graph node becomes this one. */
    ptd->current_call = pcn;
}

/* Frame exit handler, used for unwind and normal exit. */
static void log_exit(MVMThreadContext *tc, MVMuint32 unwind) {
    MVMProfileThreadData *ptd = get_thread_data(tc);

    /* Ensure we've a current frame. */
    MVMProfileCallNode *pcn = ptd->current_call;
    if (!pcn) {
        if (tc->instance->profiling) {
            if (ptd->non_calltree_depth-- > 0) {
                return;
            }
            /* No frame but still profiling; corruption. */
            MVM_dump_backtrace(tc);
            MVM_panic(1, "Profiler lost sequence");
        }
        else {
            /* We already finished profiling. */
            return;
        }
    }

    /* Add to total time. */
    pcn->total_time += (uv_hrtime() - pcn->cur_entry_time) - pcn->cur_skip_time;

    /* Move back to predecessor in call graph. */
    ptd->current_call = pcn->pred;
}

/* Log that we're exiting a frame normally. */
void MVM_profile_log_exit(MVMThreadContext *tc) {
    log_exit(tc, 0);
}

/* Called when we unwind. Since we're also potentially leaving some inlined
 * frames, we need to exit until we hit the target one. */
void MVM_profile_log_unwind(MVMThreadContext *tc) {
    MVMProfileThreadData *ptd  = get_thread_data(tc);
    MVMProfileCallNode   *lpcn;
    do {
        MVMProfileCallNode *pcn  = ptd->current_call;
        if (!pcn)
            return;
        lpcn = pcn;
        log_exit(tc, 1);
    } while (ptd->staticframe_array[lpcn->sf_idx] != tc->cur_frame->static_info);
}

/* Called when we take a continuation. Leaves the static frames from the point
 * of view of the profiler, and saves each of them. */
MVMProfileContinuationData * MVM_profile_log_continuation_control(MVMThreadContext *tc, const MVMFrame *root_frame) {
    MVMProfileThreadData        *ptd       = get_thread_data(tc);
    MVMProfileContinuationData  *cd        = MVM_malloc(sizeof(MVMProfileContinuationData));
    MVMStaticFrame             **sfs       = NULL;
    MVMuint64                   *modes     = NULL;
    MVMFrame                    *cur_frame = tc->cur_frame;
    MVMuint64                    alloc_sfs = 0;
    MVMuint64                    num_sfs   = 0;
    MVMFrame                   *last_frame;

    do {
        MVMProfileCallNode   *lpcn;
        do {
            MVMProfileCallNode *pcn = ptd->current_call;
            if (!pcn)
                MVM_panic(1, "Profiler lost sequence in continuation control");

            if (num_sfs == alloc_sfs) {
                alloc_sfs += 16;
                sfs        = MVM_realloc(sfs, alloc_sfs * sizeof(MVMStaticFrame *));
                modes      = MVM_realloc(modes, alloc_sfs * sizeof(MVMuint64));
            }
            sfs[num_sfs]   = ptd->staticframe_array[pcn->sf_idx];
            modes[num_sfs] = pcn->entry_mode;
            num_sfs++;

            lpcn = pcn;
            log_exit(tc, 1);
        } while (ptd->staticframe_array[lpcn->sf_idx] != cur_frame->static_info);

        last_frame = cur_frame;
        cur_frame = cur_frame->caller;
    } while (last_frame != root_frame);

    cd->sfs     = sfs;
    cd->num_sfs = num_sfs;
    cd->modes   = modes;
    return cd;
}

/* Called when we invoke a continuation. Enters all the static frames we left
 * at the point we took the continuation. */
void MVM_profile_log_continuation_invoke(MVMThreadContext *tc, const MVMProfileContinuationData *cd) {
    MVMuint64 i = cd->num_sfs;
    while (i--)
        MVM_profile_log_enter(tc, cd->sfs[i], cd->modes[i]);
}

/* Called when a new thread is spawned off of another thread */
void MVM_profile_log_thread_created(MVMThreadContext *tc, MVMThreadContext *child_tc) {
    MVMProfileThreadData *prof_data = get_thread_data(child_tc);
    prof_data->parent_thread_id = tc->thread_id;
}

/* Logs one allocation, potentially scalar replaced. */
static void log_one_allocation(MVMThreadContext *tc, MVMObject *obj, MVMProfileCallNode *pcn, MVMuint8 replaced) {
    MVMObject *what = STABLE(obj)->WHAT;
    MVMuint32 i;
    MVMuint8 allocation_target;
    MVM_ASSERT_NOT_FROMSPACE(tc, what);
    if (replaced) {
        allocation_target = 3;
    } else if (pcn->entry_mode == MVM_PROFILE_ENTER_SPESH || pcn->entry_mode == MVM_PROFILE_ENTER_SPESH_INLINE) {
        allocation_target = 1;
    } else if (pcn->entry_mode == MVM_PROFILE_ENTER_JIT || pcn->entry_mode == MVM_PROFILE_ENTER_JIT_INLINE) {
        allocation_target = 2;
    } else {
        allocation_target = 0;
    }

    /* See if there's an existing node to update. */
    for (i = 0; i < pcn->num_alloc; i++) {
        if (tc->prof_data->type_array[pcn->alloc[i].type_idx] == what) {
            if (allocation_target == 0)
                pcn->alloc[i].allocations_interp++;
            else if (allocation_target == 1)
                pcn->alloc[i].allocations_spesh++;
            else if (allocation_target == 2)
                pcn->alloc[i].allocations_jit++;
            else if (allocation_target == 3)
                pcn->alloc[i].scalar_replaced++;
            return;
        }
    }

    /* No entry; create one. */
    if (pcn->num_alloc == pcn->alloc_alloc) {
        if (pcn->alloc_alloc == 0) {
            pcn->alloc_alloc++;
            pcn->alloc = MVM_malloc(pcn->alloc_alloc * sizeof(MVMProfileAllocationCount));
        }
        else {
            pcn->alloc_alloc *= 2;
            pcn->alloc = MVM_realloc(
                    pcn->alloc,
                    pcn->alloc_alloc * sizeof(MVMProfileAllocationCount));
        }
    }
    {
        MVMuint32 search;
        for (search = 0; search < MVM_VECTOR_ELEMS(tc->prof_data->type_array); search++) {
            if (tc->prof_data->type_array[search] == what) {
                break;
            }
        }
        if (search == MVM_VECTOR_ELEMS(tc->prof_data->type_array)) {
            MVM_VECTOR_PUSH(tc->prof_data->type_array, what);
        }
        pcn->alloc[pcn->num_alloc].type_idx    = search;
    }

    pcn->alloc[pcn->num_alloc].allocations_interp = allocation_target == 0;
    pcn->alloc[pcn->num_alloc].allocations_spesh  = allocation_target == 1;
    pcn->alloc[pcn->num_alloc].allocations_jit    = allocation_target == 2;
    pcn->alloc[pcn->num_alloc].scalar_replaced    = allocation_target == 3;
    pcn->num_alloc++;
}

/* Log that we've just allocated the passed object (just log the type). */
void MVM_profile_log_allocated(MVMThreadContext *tc, MVMObject *obj) {
    MVMProfileThreadData *ptd  = get_thread_data(tc);
    MVMProfileCallNode   *pcn  = ptd->current_call;
    if (pcn) {
        /* First, let's see if the allocation is actually at the end of the
         * nursery; we may have generated some "allocated" log instructions
         * after operations that may or may not allocate what they return.
         */
        MVMuint32 distance = (uintptr_t)tc->nursery_alloc - (uintptr_t)obj;

        if (!obj) {
            return;
        }

        /* Since some ops first allocate, then call something else that may
         * also allocate, we may have to allow for a bit of grace distance. */
        if ((uintptr_t)obj > (uintptr_t)tc->nursery_tospace && distance <= obj->header.size && obj != ptd->last_counted_allocation) {
            log_one_allocation(tc, obj, pcn, 0);
            ptd->last_counted_allocation = obj;
        }
    }
}
void MVM_profiler_log_gc_deallocate(MVMThreadContext *tc, MVMObject *object) {
    if (tc->instance->profiling && STABLE(object)) {
        MVMProfileGC *pgc = &tc->prof_data->gcs[tc->prof_data->num_gcs];
        MVMObject *what = STABLE(object)->WHAT;
        MVMCollectable *item = (MVMCollectable *)object;
        MVMuint32 i;

        MVMuint8 dealloc_target = 0;

        if (what->header.flags2 & MVM_CF_FORWARDER_VALID)
            what = (MVMObject *)what->header.sc_forward_u.forwarder;

        MVM_ASSERT_NOT_FROMSPACE(tc, what);

        if (item->flags2 & MVM_CF_SECOND_GEN)
            dealloc_target = 2;
        else if (item->flags2 & MVM_CF_NURSERY_SEEN)
            dealloc_target = 1;

        /* See if there's an existing node to update. */
        for (i = 0; i < pgc->num_dealloc; i++) {
            if (pgc->deallocs[i].type == what) {
                if (dealloc_target == 2)
                    pgc->deallocs[i].deallocs_gen2++;
                else if (dealloc_target == 1)
                    pgc->deallocs[i].deallocs_nursery_seen++;
                else
                    pgc->deallocs[i].deallocs_nursery_fresh++;
                return;
            }
        }

        /* No entry; create one. */
        if (pgc->num_dealloc == pgc->alloc_dealloc) {
            if (pgc->alloc_dealloc == 0) {
                pgc->alloc_dealloc++;
                pgc->deallocs = MVM_malloc(pgc->alloc_dealloc * sizeof(MVMProfileDeallocationCount));
            }
            else {
                pgc->alloc_dealloc *= 2;
                pgc->deallocs = MVM_realloc(
                        pgc->deallocs,
                        pgc->alloc_dealloc * sizeof(MVMProfileDeallocationCount));
            }
        }
        pgc->deallocs[pgc->num_dealloc].type                   = what;
        pgc->deallocs[pgc->num_dealloc].deallocs_nursery_fresh = dealloc_target == 0;
        pgc->deallocs[pgc->num_dealloc].deallocs_nursery_seen  = dealloc_target == 1;
        pgc->deallocs[pgc->num_dealloc].deallocs_gen2          = dealloc_target == 2;
        pgc->num_dealloc++;
    }
}

/* Logs a scalar-replaced allocation. */
void MVM_profile_log_scalar_replaced(MVMThreadContext *tc, MVMSTable *st) {
    MVMProfileThreadData *ptd  = get_thread_data(tc);
    MVMProfileCallNode   *pcn  = ptd->current_call;
    if (pcn)
        log_one_allocation(tc, st->WHAT, pcn, 1);
}

/* Logs the start of a GC run. */
void MVM_profiler_log_gc_start(MVMThreadContext *tc, MVMuint32 full, MVMuint32 this_thread_responsible) {
    MVMProfileThreadData *ptd = get_thread_data(tc);
    MVMProfileGC *gc;

    /* Make a new entry in the GCs. We use the cleared_bytes to store the
     * maximum that could be cleared, and after GC is done will subtract
     * retained bytes and promoted bytes. */
    if (ptd->num_gcs == ptd->alloc_gcs) {
        ptd->alloc_gcs += 16;
        ptd->gcs = MVM_realloc(ptd->gcs, ptd->alloc_gcs * sizeof(MVMProfileGC));
    }

    ptd->gc_promoted_unmanaged_bytes = 0;

    gc = &ptd->gcs[ptd->num_gcs];
    gc->full          = full;
    gc->cleared_bytes = (char *)tc->nursery_alloc -
                        (char *)tc->nursery_tospace;
    gc->responsible   = this_thread_responsible;
    gc->gc_seq_num    = MVM_load(&tc->instance->gc_seq_number);

    gc->num_dealloc = 0;
    gc->alloc_dealloc = 0;
    gc->deallocs = NULL;
    gc->num_stolen_gen2roots = 0;

    /* Record start time. */
    ptd->cur_gc_start_time = uv_hrtime();
    /* Also store this time in the GC data */
    gc->abstime = ptd->cur_gc_start_time;
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
    ptd->gcs[ptd->num_gcs].promoted_bytes = tc->gc_promoted_bytes - ptd->gc_promoted_unmanaged_bytes;
    ptd->gcs[ptd->num_gcs].promoted_unmanaged_bytes = ptd->gc_promoted_unmanaged_bytes;

    ptd->gcs[ptd->num_gcs].retained_bytes = retained_bytes;

    /* Tweak cleared bytes count. */
    ptd->gcs[ptd->num_gcs].cleared_bytes -= (retained_bytes + tc->gc_promoted_bytes - ptd->gc_promoted_unmanaged_bytes);

    /* Record number of gen 2 roots (from gen2 to nursery) */
    ptd->gcs[ptd->num_gcs].num_gen2roots = tc->num_gen2roots;

    /* Increment the number of GCs we've done. */
    ptd->num_gcs++;

    /* Discount GC time from all active frames. */
    while (pcn) {
        pcn->cur_skip_time += gc_time;
        pcn = pcn->pred;
    }
}

void MVM_profiler_log_unmanaged_data_promoted(MVMThreadContext *tc, MVMuint64 amount) {
    MVMProfileThreadData *ptd = get_thread_data(tc);

    ptd->gc_promoted_unmanaged_bytes += amount;
}

void MVM_profiler_log_gen2_roots(MVMThreadContext *tc, MVMuint64 amount, MVMThreadContext *other) {
    if (tc != other) {
        MVMProfileThreadData *ptd = get_thread_data(tc);

        ptd->gcs[ptd->num_gcs].num_stolen_gen2roots += amount;
    }
}

/* Log that we're starting some work on bytecode specialization or JIT. */
void MVM_profiler_log_spesh_start(MVMThreadContext *tc) {
    /* Record start time. */
    MVMProfileThreadData *ptd = get_thread_data(tc->instance->main_thread);
    ptd->cur_spesh_start_time = uv_hrtime();
}

/* Log that we've finished doing bytecode specialization or JIT. */
void MVM_profiler_log_spesh_end(MVMThreadContext *tc) {
    MVMProfileThreadData *ptd = get_thread_data(tc->instance->main_thread);
    MVMuint64 spesh_time;

    /* Because spesh workers might start before profiling starts,
     * MVM_profiler_log_spesh_end might get called before
     * MVM_profiler_log_spesh_start. */
    if (ptd->cur_spesh_start_time == 0)
        ptd->cur_spesh_start_time = ptd->start_time;

    /* Record time spent. */
    spesh_time = uv_hrtime() - ptd->cur_spesh_start_time;
    ptd->spesh_time += spesh_time;
}

/* Log that an on stack replacement took place. */
void MVM_profiler_log_osr(MVMThreadContext *tc, MVMuint64 jitted) {
    MVMProfileThreadData *ptd = get_thread_data(tc);
    MVMProfileCallNode   *pcn = ptd->current_call;
    if (pcn) {
        pcn->osr_count++;
        if (jitted)
            pcn->jit_entries++;
        else
            pcn->specialized_entries++;
    }
}

/* Log that local deoptimization took pace. */
void MVM_profiler_log_deopt_one(MVMThreadContext *tc) {
    MVMProfileThreadData *ptd = get_thread_data(tc);
    MVMProfileCallNode   *pcn = ptd->current_call;
    if (pcn)
        pcn->deopt_one_count++;
}

/* Log that full-stack deoptimization took pace. */
void MVM_profiler_log_deopt_all(MVMThreadContext *tc) {
    MVMProfileThreadData *ptd = get_thread_data(tc);
    MVMProfileCallNode   *pcn = ptd->current_call;
    if (pcn)
        pcn->deopt_all_count++;
}
