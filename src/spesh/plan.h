/* The minimum number OSR hits a static frame as a whole has to receive
 * (across all callsites and type tuples) before it is hot enough to further
 * consider. */
#define MVM_SPESH_PLAN_SF_MIN_OSR   100

/* The minimum number of hits or OSR hits a given static frame and interned
 * callsite combination have to have before it is hot enough to further
 * consider. */
#define MVM_SPESH_PLAN_CS_MIN_OSR   100

/* The percentage of hits or OSR hits that a type tuple should receive, out of
 * the total callsite hits, to receive an "observed types" specialization. */
#define MVM_SPESH_PLAN_TT_OBS_PERCENT       25
#define MVM_SPESH_PLAN_TT_OBS_PERCENT_OSR   25

/* The plan of what specializations to produce. */
struct MVMSpeshPlan {
    /* List of planned specializations. */
    MVMSpeshPlanned *planned;

    /* Number of planned specializations. */
    MVMuint32 num_planned;

    /* The number of specialization plans space is allocated for. */
    MVMuint32 alloc_planned;
};

/* Kinds of specializations we might decide to produce. */
typedef enum {
    /* A certain specialization based only on callsite. */
    MVM_SPESH_PLANNED_CERTAIN,

    /* A specialization based on an exact observed argument type tuple. */
    MVM_SPESH_PLANNED_OBSERVED_TYPES,

    /* A specialization based on analysis of various argument types that
     * showed up. This may happen when one argument type is predcitable, but
     * others are not. */
    MVM_SPESH_PLANNED_DERIVED_TYPES
} MVMSpeshPlannedKind;

/* An planned specialization that should be produced. */
struct MVMSpeshPlanned {
    /* What kind of specialization we're planning. */
    MVMSpeshPlannedKind kind;

    /* The maximum stack depth this was seen at; used to sort the plan so we
     * can specialize deepest first, in hope of having callees specialized
     * ahead of callers. */
    MVMuint32 max_depth;

    /* The static frame with the code to specialize. */
    MVMStaticFrame *sf;

    /* The callsite statistics entry that this specialization was planned as
     * a result of (by extension, we find the callsite, if any). */
    MVMSpeshStatsByCallsite *cs_stats;

    /* The type tuple to produce the specialization for, if this is a type
     * based specialization. NULL for certain specializations. The memory
     * associated with this tuple will always have been allocated by the
     * planner, not shared with the statistics structure, even if this is a
     * specialization for an exactly observed type. */
    MVMSpeshStatsType *type_tuple;

    /* Type statistics, if any, that the plan was formed based upon. */
    MVMSpeshStatsByType **type_stats;

    /* Number of entries in the type_stats array. (For an observed type
     * specialization, this would be 1.) */
    MVMuint32 num_type_stats;
};

MVMSpeshPlan * MVM_spesh_plan(MVMThreadContext *tc, MVMObject *updated_static_frames, MVMuint64 *certain_specialization, MVMuint64 *observed_specialization, MVMuint64 *osr_specialization);
void MVM_spesh_plan_gc_mark(MVMThreadContext *tc, MVMSpeshPlan *plan, MVMGCWorklist *worklist);
void MVM_spesh_plan_gc_describe(MVMThreadContext *tc, MVMHeapSnapshotState *ss, MVMSpeshPlan *plan);
void MVM_spesh_plan_destroy(MVMThreadContext *tc, MVMSpeshPlan *plan);
MVMSpeshStatsType * MVM_spesh_plan_copy_type_tuple(MVMThreadContext *tc, MVMCallsite *cs, MVMSpeshStatsType *to_copy);
