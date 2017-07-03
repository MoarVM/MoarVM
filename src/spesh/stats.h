/* Statistics are stored per static frame. This data structure is only ever
 * read/written by the specializer thread. */
struct MVMSpeshStats {
    /* Statistics on a per-callsite basis. */
    MVMSpeshStatsByCallsite *by_callsite;

    /* Map of MVM_SPESH_LOG_STATIC entries for this routine. Held at the top
     * level as they represent static resolutions, so no need to duplicate
     * this information across callsites. */
    MVMSpeshStatsStatic *static_values;

    /* The number of entries in by_callsite. */
    MVMuint32 num_by_callsite;

    /* The number of entries in static_values. */
    MVMuint32 num_static_values;

    /* Total calls across all callsites. */
    MVMuint32 hits;

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

    /* TODO Aggregated logs of types and invokees inside of here */
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
};

/* Static values table entry. */
struct MVMSpeshStatsStatic {
    /* The value. */
    MVMObject *value;

    /* The bytecode offset it was recorded at. */
    MVMint32 bytecode_offset;
};

void MVM_spesh_stats_gc_mark(MVMThreadContext *tc, MVMSpeshStats *ss, MVMGCWorklist *worklist);
void MVM_spesh_stats_destroy(MVMThreadContext *tc, MVMSpeshStats *ss);
