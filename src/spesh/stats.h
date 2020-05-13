/* Statistics are stored per static frame. This data structure is only ever
 * read/written by the specializer thread. */
struct MVMSpeshStats {
    /* Statistics on a per-callsite basis. */
    MVMSpeshStatsByCallsite *by_callsite;

    /* The number of entries in by_callsite. */
    MVMuint32 num_by_callsite;

    /* The number of entries in static_values. */
    MVMuint32 num_static_values;

    /* Total calls across all callsites. */
    MVMuint32 hits;

    /* Total OSR hits across all callsites. */
    MVMuint32 osr_hits;

    /* The latest version of the statistics when this was updated. Used to
     * help decide when to throw out data that is no longer evolving, to
     * reduce memory use. */
    MVMuint32 last_update;
};

/* Statistics by callsite. */
struct MVMSpeshStatsByCallsite {
    /* The callsite, or NULL for the case of no interned callsite. */
    MVMCallsite *cs;

    /* Statistics aggregated by parameter type information. */
    MVMSpeshStatsByType *by_type;

    /* The number of entries in by_type. Zero if cs == NULL. */
    MVMuint32 num_by_type;

    /* Total calls to this callsite. */
    MVMuint32 hits;

    /* Total OSR hits for this callsite. */
    MVMuint32 osr_hits;

    /* The maximum callstack depth we observed this at. */
    MVMuint32 max_depth;
};

/* Statistics by type. */
struct MVMSpeshStatsByType {
    /* Argument type information. Length of this is determined by the callsite
     * of the specialization. */
    MVMSpeshStatsType *arg_types;

    /* Total calls with this callsite/type combination. */
    MVMuint32 hits;

    /* Total OSR hits for this callsite/type combination. */
    MVMuint32 osr_hits;

    /* Logged type and logged value counts, by bytecode offset. */
    MVMSpeshStatsByOffset *by_offset;

    /* Number of stats by offset we have. */
    MVMuint32 num_by_offset;

    /* The maximum callstack depth we observed this at. */
    MVMuint32 max_depth;
};

/* Type statistics. */
struct MVMSpeshStatsType {
    /* The type logged. */
    MVMObject *type;

    /* If applicable, and if the type is a container type, the type of the
     * value logged inside of it. */
    MVMObject *decont_type;

    /* Whether the type and decont type were concrete. */
    MVMuint8 type_concrete;
    MVMuint8 decont_type_concrete;

    /* If there is a container type, whether it must be rw. */
    MVMuint8 rw_cont;
};

/* Statistics by bytecode offset. */
struct MVMSpeshStatsByOffset {
    /* The bytecode offset these types/values were recorded at. */
    MVMuint32 bytecode_offset;

    /* Number of types recorded, with counts. */
    MVMuint32 num_types;
    MVMSpeshStatsTypeCount *types;

    /* Number of invocation targets recorded, with counts. */
    MVMSpeshStatsInvokeCount *invokes;
    MVMuint32 num_invokes;

    /* Number of type tuples recorded, with counts. (Type tuples are actually
     * recorded by the callee, and then also accumulated at the callsite of
     * the caller.) */
    MVMuint32 num_type_tuples;
    MVMSpeshStatsTypeTupleCount *type_tuples;

    /* Number of times spesh plugin guard indexes were recorded. */
    MVMSpeshStatsPluginGuardCount *plugin_guards;
    MVMuint32 num_plugin_guards;
};

/* Counts of a given type that has shown up at a bytecode offset. */
struct MVMSpeshStatsTypeCount {
    /* The type and its concreteness. */
    MVMObject *type;
    MVMuint8 type_concrete;

    /* The number of times we've seen it. */
    MVMuint32 count;
};

/* Counts of a given static frame that was invoked at a bytecode offset. */
struct MVMSpeshStatsInvokeCount {
    /* The static frame. */
    MVMStaticFrame *sf;

    /* The number of times the caller frame was also the outer frame. */
    MVMuint32 caller_is_outer_count;

    /* The number of times it was resolved from a multi-dispatch. */
    MVMuint32 was_multi_count;

    /* The number of times we've seen it. */
    MVMuint32 count;
};

/* Counts of a given type tuple has shown up at the callsite at a bytecode
 * offset. */
struct MVMSpeshStatsTypeTupleCount {
    /* The callsite. */
    MVMCallsite *cs;

    /* The type tuple. */
    MVMSpeshStatsType *arg_types;

    /* The number of times we've seen it. */
    MVMuint32 count;
};

/* Counts of a given spesh plugin guard index. */
struct MVMSpeshStatsPluginGuardCount {
    /* The guard index that the plugin resolved to. */
    MVMuint32 guard_index;

    /* The number of times we've seen it. */
    MVMuint32 count;
};

/* Static values table entry. */
struct MVMSpeshStatsStatic {
    /* The value. */
    MVMObject *value;

    /* The bytecode offset it was recorded at. */
    MVMuint32 bytecode_offset;
};

/* The maximum number of spesh stats updates before we consider a frame's
 * stats out of date and throw them out. */
#define MVM_SPESH_STATS_MAX_AGE 10

/* Logs are linear recordings marked with frame correlation IDs. We need to
 * simulate the call stack as part of the analysis. This is the model for the
 * stack simulation. */
struct MVMSpeshSimStack {
    /* Array of frames. */
    MVMSpeshSimStackFrame *frames;

    /* Current frame index and allocated space. */
    MVMuint32 used;
    MVMuint32 limit;

    /* Current stack depth. */
    MVMuint32 depth;
};

/* This is the model of a frame on the simulated stack. */
struct MVMSpeshSimStackFrame {
    /* The static frame. */
    MVMStaticFrame *sf;

    /* Spesh stats for the stack frame. */
    MVMSpeshStats *ss;

    /* Correlation ID. */
    MVMuint32 cid;

    /* Callsite stats index (not pointer in case of realloc). */
    MVMuint32 callsite_idx;

    /* Type stats index (not pointer in case of realloc); -1 if not yet set.
     * This is resolved once using arg_types, and then remembered, so we can
     * correlate the statistics across spesh log buffers. */
    MVMint32 type_idx;

    /* Argument types logged. Sized by number of callsite flags. */
    MVMSpeshStatsType *arg_types;

    /* Spesh log entries for types and values, for later processing. */
    MVMSpeshLogEntry **offset_logs;
    MVMuint32 offset_logs_used;
    MVMuint32 offset_logs_limit;

    /* Type tuples observed at a given callsite offset, for later
     * processing. */
    MVMSpeshSimCallType *call_type_info;
    MVMuint32 call_type_info_used;
    MVMuint32 call_type_info_limit;

    /* Number of times we crossed an OSR point. */
    MVMuint32 osr_hits;

    /* The last bytecode offset and static frame seen in an invoke recording;
     * used for producing callsite type stats based on callee type tuples. */
    MVMuint32 last_invoke_offset;
    MVMStaticFrame *last_invoke_sf;
};

/* We associate recoded type tuples in callees with their caller's callsites.
 * This is kept as a flat view, and then folded in when the caller's sim
 * frame (see next) is popped. */
struct MVMSpeshSimCallType {
    MVMuint32 bytecode_offset;
    MVMCallsite *cs;
    MVMSpeshStatsType *arg_types;
};

void MVM_spesh_stats_update(MVMThreadContext *tc, MVMSpeshLog *sl, MVMObject *sf_updated, MVMuint64 *newly_seen, MVMuint64 *updated);
void MVM_spesh_stats_cleanup(MVMThreadContext *tc, MVMObject *check_frames);
void MVM_spesh_stats_gc_mark(MVMThreadContext *tc, MVMSpeshStats *ss, MVMGCWorklist *worklist);
void MVM_spesh_stats_gc_describe(MVMThreadContext *tc, MVMHeapSnapshotState *snapshot, MVMSpeshStats *ss);
void MVM_spesh_stats_destroy(MVMThreadContext *tc, MVMSpeshStats *ss);
void MVM_spesh_sim_stack_gc_mark(MVMThreadContext *tc, MVMSpeshSimStack *sims,
    MVMGCWorklist *worklist);
void MVM_spesh_sim_stack_gc_describe(MVMThreadContext *tc, MVMHeapSnapshotState *ss, MVMSpeshSimStack *sims);
void MVM_spesh_sim_stack_destroy(MVMThreadContext *tc, MVMSpeshSimStack *sims);
