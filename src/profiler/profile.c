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
    MVMString *entries;
    MVMString *spesh_entries;
    MVMString *jit_entries;
    MVMString *inlined_entries;
    MVMString *inclusive_time;
    MVMString *exclusive_time;
    MVMString *callees;
} ProfDumpStrs;

/* Dumps a call graph node. */
static MVMObject * dump_call_graph_node(MVMThreadContext *tc, ProfDumpStrs *pds,
                                        MVMProfileCallNode *pcn) {
    MVMObject *node_hash = new_hash(tc);

    /* Add name of code object. */
    MVM_repr_bind_key_o(tc, node_hash,
        pds->name, box_s(tc, pcn->sf->body.name));

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
        box_i(tc, pcn->total_time));

    /* Visit successors in the call graph, dumping them and working out the
     * exclusive time. */
    if (pcn->num_succ) {
        MVMObject *callees        = new_array(tc);
        MVMuint64  exclusive_time = pcn->total_time;
        MVMuint32  i;
        for (i = 0; i < pcn->num_succ; i++) {
            MVM_repr_push_o(tc, callees,
                dump_call_graph_node(tc, pds, pcn->succ[i]));
            exclusive_time -= pcn->succ[i]->total_time;
        }
        MVM_repr_bind_key_o(tc, node_hash, pds->exclusive_time,
            box_i(tc, exclusive_time));
        MVM_repr_bind_key_o(tc, node_hash, pds->callees, callees);
    }
    else {
        MVM_repr_bind_key_o(tc, node_hash, pds->exclusive_time,
            box_i(tc, pcn->total_time));
    }

    return node_hash;
}

/* Dumps data from a single thread. */
static MVMObject * dump_thread_data(MVMThreadContext *tc, ProfDumpStrs *pds,
                                    MVMProfileThreadData *ptd) {
    MVMObject *thread_hash = new_hash(tc);

    /* Add time. */
    MVM_repr_bind_key_o(tc, thread_hash, pds->total_time,
        box_i(tc, ptd->end_time - ptd->start_time));

    /* Add call graph. */
    if (ptd->call_graph)
        MVM_repr_bind_key_o(tc, thread_hash, pds->call_graph,
            dump_call_graph_node(tc, pds, ptd->call_graph));

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
    pds.entries         = str(tc, "entries");
    pds.spesh_entries   = str(tc, "spesh_entries");
    pds.jit_entries     = str(tc, "jit_entries");
    pds.inlined_entries = str(tc, "inlined_entries");
    pds.inclusive_time  = str(tc, "inclusive_time");
    pds.exclusive_time  = str(tc, "exclusive_time");
    pds.callees         = str(tc, "callees");

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
