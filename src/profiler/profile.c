#include "moar.h"

/* Simple allocation functions. */
static MVMObject * new_array(MVMThreadContext *tc) {
    return MVM_repr_alloc_init(tc, MVM_hll_current(tc)->slurpy_array_type);
}
static MVMObject * new_hash(MVMThreadContext *tc) {
    return MVM_repr_alloc_init(tc, MVM_hll_current(tc)->slurpy_hash_type);
}
static MVMObject * box_i(MVMThreadContext *tc, MVMint64 i) {
    return MVM_repr_box_int(tc, MVM_hll_current(tc)->int_box_type, i);
}
static MVMObject * box_s(MVMThreadContext *tc, MVMString *s) {
    return MVM_repr_box_str(tc, MVM_hll_current(tc)->str_box_type, s);
}
static MVMString * str(MVMThreadContext *tc, const char *buf) {
    return MVM_string_ascii_decode_nt(tc, tc->instance->VMString, buf);
}

/* String constants we'll reuse. */
typedef struct {
    MVMString *total_time;
    MVMString *call_graph;
    MVMString *name;
    MVMString *id;
    MVMString *file;
    MVMString *line;
    MVMString *entries;
    MVMString *spesh_entries;
    MVMString *jit_entries;
    MVMString *inlined_entries;
    MVMString *inclusive_time;
    MVMString *exclusive_time;
    MVMString *callees;
    MVMString *allocations;
    MVMString *type;
    MVMString *count;
    MVMString *gcs;
    MVMString *time;
    MVMString *full;
    MVMString *cleared_bytes;
    MVMString *retained_bytes;
    MVMString *promoted_bytes;
    MVMString *osr;
    MVMString *deopt_one;
    MVMString *deopt_all;
    MVMString *spesh_time;
    MVMString *is_collection;
} ProfDumpStrs;

static void call_node_to_hash(MVMThreadContext *tc, ProfDumpStrs *pds,
                              MVMProfileCallNode *pcn, MVMObject *node_hash) {
    MVMObject *alloc_list = new_array(tc);
    MVMuint32  i;

    /* Try to resolve the code filename and line number. */
    MVMBytecodeAnnotation *annot = MVM_bytecode_resolve_annotation(tc,
        &(pcn->sf->body), 0);
    MVMint32 fshi = annot ? (MVMint32)annot->filename_string_heap_index : -1;

    /* Add name of code object. */
    MVM_repr_bind_key_o(tc, node_hash,
        pds->name, box_s(tc, pcn->sf->body.name));

    /* Add line number and file name. */
    if (fshi >= 0 && fshi < pcn->sf->body.cu->body.num_strings)
        MVM_repr_bind_key_o(tc, node_hash, pds->file,
            box_s(tc, pcn->sf->body.cu->body.strings[fshi]));
    else if (pcn->sf->body.cu->body.filename)
        MVM_repr_bind_key_o(tc, node_hash, pds->file,
            box_s(tc, pcn->sf->body.cu->body.filename));
    else
        MVM_repr_bind_key_o(tc, node_hash, pds->file,
            box_s(tc, tc->instance->str_consts.empty));
    MVM_repr_bind_key_o(tc, node_hash, pds->line,
        box_i(tc, annot ? (MVMint32)annot->line_number : -1));

    /* Use static frame memory address to get a unique ID. */
    MVM_repr_bind_key_o(tc, node_hash, pds->id,
        box_i(tc, (MVMint64)pcn->sf));

    /* Entry counts. */
    MVM_repr_bind_key_o(tc, node_hash, pds->entries,
        box_i(tc, pcn->total_entries));
    MVM_repr_bind_key_o(tc, node_hash, pds->spesh_entries,
        box_i(tc, pcn->specialized_entries));
    MVM_repr_bind_key_o(tc, node_hash, pds->jit_entries,
        box_i(tc, pcn->jit_entries));
    MVM_repr_bind_key_o(tc, node_hash, pds->inlined_entries,
        box_i(tc, pcn->inlined_entries));

    /* Total (inclusive) time. */
    MVM_repr_bind_key_o(tc, node_hash, pds->inclusive_time,
        box_i(tc, pcn->total_time / 1000));

    /* OSR and deopt counts. */
    MVM_repr_bind_key_o(tc, node_hash, pds->osr,
        box_i(tc, pcn->osr_count));
    MVM_repr_bind_key_o(tc, node_hash, pds->deopt_one,
        box_i(tc, pcn->deopt_one_count));
    MVM_repr_bind_key_o(tc, node_hash, pds->deopt_all,
        box_i(tc, pcn->deopt_all_count));

    /* Emit allocations. */
    MVM_repr_bind_key_o(tc, node_hash, pds->allocations, alloc_list);
    for (i = 0; i < pcn->num_alloc; i++) {
        MVMObject *alloc_info = new_hash(tc);
        MVMObject *type       = pcn->alloc[i].type;
        MVM_repr_bind_key_o(tc, alloc_info, pds->id, box_i(tc, (MVMint64)type));
        MVM_repr_bind_key_o(tc, alloc_info, pds->type, type);
        MVM_repr_bind_key_o(tc, alloc_info, pds->count,
            box_i(tc, pcn->alloc[i].allocations));
        MVM_repr_push_o(tc, alloc_list, alloc_info);
    }

}

static void aggregate_nodes(MVMThreadContext *tc, ProfDumpStrs *pds, MVMProfileCallNode *pcn,
                            MVMObject *existing_hash, MVMObject *existing_callees) {
    /* First, let's see if there's already an entry in the existing callees
     * that has the same ID as our call data object */
    MVMuint64 existing_elems = MVM_repr_elems(tc, existing_callees);
    MVMuint64 target_id = (MVMuint64)pcn->sf;
    MVMObject *node_hash;
    MVMint64 index;

    for (index = 0; index < existing_elems; index++) {
        MVMObject *callee = MVM_repr_at_pos_o(tc, existing_callees, index);
        MVMuint64 other_id = MVM_repr_get_int(tc, MVM_repr_at_key_o(tc, callee, pds->id));

        if (other_id == target_id) {
            MVMint64 alloc_i;
            MVMint64 alloc_k;
            MVMint64 existing_allocations;

            MVMObject *alloc_list;

            node_hash = callee;

            /* add up entry counts. */
            MVM_repr_bind_key_o(tc, node_hash, pds->entries,
                box_i(tc, pcn->total_entries
                      + MVM_repr_get_int(tc, MVM_repr_at_key_o(tc, node_hash, pds->entries))));

            MVM_repr_bind_key_o(tc, node_hash, pds->spesh_entries,
                  box_i(tc, pcn->specialized_entries
                        + MVM_repr_get_int(tc, MVM_repr_at_key_o(tc, node_hash, pds->spesh_entries))));

            MVM_repr_bind_key_o(tc, node_hash, pds->jit_entries,
                box_i(tc, pcn->jit_entries
                      + MVM_repr_get_int(tc, MVM_repr_at_key_o(tc, node_hash, pds->jit_entries))));

            MVM_repr_bind_key_o(tc, node_hash, pds->inlined_entries,
                box_i(tc, pcn->inlined_entries
                      + MVM_repr_get_int(tc, MVM_repr_at_key_o(tc, node_hash, pds->inlined_entries))));

            /* add more total (inclusive) time. */
            MVM_repr_bind_key_o(tc, node_hash, pds->inclusive_time,
                box_i(tc, pcn->total_time / 1000
                      + MVM_repr_get_int(tc, MVM_repr_at_key_o(tc, node_hash, pds->inclusive_time)) / 1000));

            /* OSR and deopt counts. */
            MVM_repr_bind_key_o(tc, node_hash, pds->osr,
                box_i(tc, pcn->osr_count
                      + MVM_repr_get_int(tc, MVM_repr_at_key_o(tc, node_hash, pds->osr))));
            MVM_repr_bind_key_o(tc, node_hash, pds->deopt_one,
                box_i(tc, pcn->deopt_one_count
                      + MVM_repr_get_int(tc, MVM_repr_at_key_o(tc, node_hash, pds->deopt_one))));
            MVM_repr_bind_key_o(tc, node_hash, pds->deopt_all,
                box_i(tc, pcn->deopt_all_count
                      + MVM_repr_get_int(tc, MVM_repr_at_key_o(tc, node_hash, pds->deopt_all))));

            /* Merging allocations is a tiny bit more difficult,
             * as we have to go through another list to find a matching entry
             * to merge with. */

            /* Emit allocations. */
            alloc_list  = MVM_repr_at_key_o(tc, node_hash, pds->allocations);
            existing_allocations = MVM_repr_elems(tc, alloc_list);
            for (alloc_i = 0; alloc_i < pcn->num_alloc; alloc_i++) {
                MVMObject *alloc_info = NULL;
                for (alloc_k = 0; alloc_k < existing_allocations && alloc_info == NULL; alloc_k++) {
                    alloc_info = MVM_repr_at_pos_o(tc, alloc_list, alloc_k);
                    if (MVM_repr_get_int(tc, MVM_repr_at_key_o(tc, alloc_info, pds->id)) != (MVMint64)pcn->alloc[alloc_i].type) {
                        alloc_info = NULL;
                    }
                }
                if (alloc_info != NULL) {
                    MVM_repr_bind_key_o(tc, alloc_info, pds->count,
                            box_i(tc, pcn->alloc[alloc_i].allocations
                                  + MVM_repr_get_int(tc, MVM_repr_at_key_o(tc, alloc_info, pds->count))));
                } else {
                    MVMObject *type       = pcn->alloc[alloc_i].type;
                    MVMObject *alloc_info = new_hash(tc);
                    MVM_repr_bind_key_o(tc, alloc_info, pds->id, box_i(tc, (MVMint64)type));
                    MVM_repr_bind_key_o(tc, alloc_info, pds->type, type);
                    MVM_repr_bind_key_o(tc, alloc_info, pds->count,
                        box_i(tc, pcn->alloc[alloc_i].allocations));
                    MVM_repr_push_o(tc, alloc_list, alloc_info);
                }
            }

            goto do_successors;
        }
    }

    {
        node_hash  = new_hash(tc);

        /* We got here, that means we have to generate a new node */
        call_node_to_hash(tc, pds, pcn, node_hash);

        MVM_repr_push_o(tc, existing_callees, node_hash);
    }

do_successors:

    if (pcn->num_succ) {
        MVMuint32 i;
        MVMuint64  exclusive_time = pcn->total_time;
        for (i = 0; i < pcn->num_succ; i++) {
            MVM_repr_bind_key_o(tc, node_hash, pds->is_collection, box_i(tc, 1));
            aggregate_nodes(tc, pds, pcn->succ[i], node_hash, existing_callees);
            exclusive_time -= pcn->succ[i]->total_time;
        }
        MVM_repr_bind_key_o(tc, node_hash, pds->exclusive_time,
            box_i(tc, exclusive_time / 1000));
    }
    else {
        MVM_repr_bind_key_o(tc, node_hash, pds->exclusive_time,
            box_i(tc, pcn->total_time / 1000));
    }
}

/* Dumps a call graph node. */
static MVMObject * dump_call_graph_node(MVMThreadContext *tc, ProfDumpStrs *pds,
                                        MVMProfileCallNode *pcn, MVMuint8 depth) {
    MVMObject *node_hash  = new_hash(tc);
    MVMuint32 i;

    call_node_to_hash(tc, pds, pcn, node_hash);

    /* Visit successors in the call graph, dumping them and working out the
     * exclusive time. */
    if (pcn->num_succ) {
        MVMObject *callees        = new_array(tc);
        MVMuint64  exclusive_time = pcn->total_time;
        for (i = 0; i < pcn->num_succ; i++) {
            if (depth < 32) {
                MVM_repr_push_o(tc, callees,
                    dump_call_graph_node(tc, pds, pcn->succ[i], depth + 1));
                exclusive_time -= pcn->succ[i]->total_time;
            }
            else {
                MVM_repr_bind_key_o(tc, node_hash, pds->is_collection, box_i(tc, 1));
                aggregate_nodes(tc, pds, pcn->succ[i], node_hash, callees);
                exclusive_time -= pcn->succ[i]->total_time;
            }
        }
        MVM_repr_bind_key_o(tc, node_hash, pds->exclusive_time,
            box_i(tc, exclusive_time / 1000));
        MVM_repr_bind_key_o(tc, node_hash, pds->callees, callees);
    }
    else {
        MVM_repr_bind_key_o(tc, node_hash, pds->exclusive_time,
            box_i(tc, pcn->total_time / 1000));
    }

    return node_hash;
}


/* Dumps data from a single thread. */
static MVMObject * dump_thread_data(MVMThreadContext *tc, ProfDumpStrs *pds,
                                    MVMProfileThreadData *ptd) {
    MVMObject *thread_hash = new_hash(tc);
    MVMObject *thread_gcs  = new_array(tc);
    MVMuint32  i;

    /* Add time. */
    MVM_repr_bind_key_o(tc, thread_hash, pds->total_time,
        box_i(tc, (ptd->end_time - ptd->start_time) / 1000));

    /* Add call graph. */
    if (ptd->call_graph)
        MVM_repr_bind_key_o(tc, thread_hash, pds->call_graph,
            dump_call_graph_node(tc, pds, ptd->call_graph, 0));

    /* Add GCs. */
    for (i = 0; i < ptd->num_gcs; i++) {
        MVMObject *gc_hash = new_hash(tc);
        MVM_repr_bind_key_o(tc, gc_hash, pds->time,
            box_i(tc, ptd->gcs[i].time / 1000));
        MVM_repr_bind_key_o(tc, gc_hash, pds->full,
            box_i(tc, ptd->gcs[i].full));
        MVM_repr_bind_key_o(tc, gc_hash, pds->cleared_bytes,
            box_i(tc, ptd->gcs[i].cleared_bytes));
        MVM_repr_bind_key_o(tc, gc_hash, pds->retained_bytes,
            box_i(tc, ptd->gcs[i].retained_bytes));
        MVM_repr_bind_key_o(tc, gc_hash, pds->promoted_bytes,
            box_i(tc, ptd->gcs[i].promoted_bytes));
        MVM_repr_push_o(tc, thread_gcs, gc_hash);
    }
    MVM_repr_bind_key_o(tc, thread_hash, pds->gcs, thread_gcs);

    /* Add spesh time. */
    MVM_repr_bind_key_o(tc, thread_hash, pds->spesh_time,
        box_i(tc, ptd->spesh_time / 1000));

    return thread_hash;
}

/* Dumps data from all threads into an array of per-thread data. */
static MVMObject * dump_data(MVMThreadContext *tc) {
    MVMObject *threads_array;
    ProfDumpStrs pds;

    /* We'll allocate the data in gen2, but as we want to keep it, but to be
     * sure we don't trigger a GC run. */
    MVM_gc_allocate_gen2_default_set(tc);

    /* Some string constants to re-use. */
    pds.total_time      = str(tc, "total_time");
    pds.call_graph      = str(tc, "call_graph");
    pds.name            = str(tc, "name");
    pds.id              = str(tc, "id");
    pds.file            = str(tc, "file");
    pds.line            = str(tc, "line");
    pds.entries         = str(tc, "entries");
    pds.spesh_entries   = str(tc, "spesh_entries");
    pds.jit_entries     = str(tc, "jit_entries");
    pds.inlined_entries = str(tc, "inlined_entries");
    pds.inclusive_time  = str(tc, "inclusive_time");
    pds.exclusive_time  = str(tc, "exclusive_time");
    pds.callees         = str(tc, "callees");
    pds.allocations     = str(tc, "allocations");
    pds.type            = str(tc, "type");
    pds.count           = str(tc, "count");
    pds.gcs             = str(tc, "gcs");
    pds.time            = str(tc, "time");
    pds.full            = str(tc, "full");
    pds.cleared_bytes   = str(tc, "cleared_bytes");
    pds.retained_bytes  = str(tc, "retained_bytes");
    pds.promoted_bytes  = str(tc, "promoted_bytes");
    pds.osr             = str(tc, "osr");
    pds.deopt_one       = str(tc, "deopt_one");
    pds.deopt_all       = str(tc, "deopt_all");
    pds.spesh_time      = str(tc, "spesh_time");
    pds.is_collection   = str(tc, "is_collection");

    /* Build up threads array. */
    /* XXX Only main thread for now. */
    threads_array = new_array(tc);
    if (tc->prof_data)
        MVM_repr_push_o(tc, threads_array, dump_thread_data(tc, &pds, tc->prof_data));

    /* Switch back to default allocation and return result; */
    MVM_gc_allocate_gen2_default_clear(tc);
    return threads_array;
}

/* Starts profiling with the specified configuration. */
void MVM_profile_start(MVMThreadContext *tc, MVMObject *config) {
    /* Enable profiling. */
    if (tc->instance->profiling)
        MVM_exception_throw_adhoc(tc, "Profiling is already started");
    tc->instance->profiling = 1;
    tc->instance->instrumentation_level++;
}

/* Ends profiling, builds the result data structure, and returns it. */
MVMObject * MVM_profile_end(MVMThreadContext *tc) {
    /* Disable profiling. */
    /* XXX Needs to account for multiple threads. */
    if (!tc->instance->profiling)
        MVM_exception_throw_adhoc(tc, "Cannot end profiling if not profiling");
    tc->instance->profiling = 0;
    tc->instance->instrumentation_level++;

    /* Record end time. */
    if (tc->prof_data)
        tc->prof_data->end_time = uv_hrtime();

    /* Build and return result data structure. */
    return dump_data(tc);
}

/* Marks objects held in the profiling graph. */
static void mark_call_graph_node(MVMThreadContext *tc, MVMProfileCallNode *node, MVMGCWorklist *worklist) {
    MVMuint32 i;
    MVM_gc_worklist_add(tc, worklist, &(node->sf));
    for (i = 0; i < node->num_alloc; i++)
        MVM_gc_worklist_add(tc, worklist, &(node->alloc[i].type));
    for (i = 0; i < node->num_succ; i++)
        mark_call_graph_node(tc, node->succ[i], worklist);
}
void MVM_profile_mark_data(MVMThreadContext *tc, MVMGCWorklist *worklist) {
    if (tc->prof_data)
        mark_call_graph_node(tc, tc->prof_data->call_graph, worklist);
}
