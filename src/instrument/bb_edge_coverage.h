#define MVM_BB_COVERAGE_DUMP_FIRST_EDGE_HIT 4
#define MVM_BB_COVERAGE_DUMP_BB_IDS 8
#define MVM_BB_COVERAGE_DUMP_BB_LINENOS 32
#define MVM_BB_COVERAGE_NFA_FEEDBACK 128
#define MVM_BB_COVERAGE_BACKTRACE_ON_SELECTED_EDGES 1024

void MVM_edge_coverage_instrument(MVMThreadContext *tc, MVMStaticFrame *static_frame);

void MVM_edge_coverage_report_bb_edge_hit(MVMThreadContext *tc, MVMuint64 bb_id);
MVM_STATIC_INLINE void MVM_edge_coverage_report_bb_edge_hit_quickcheck(MVMThreadContext *tc, MVMuint64 bb_id) {
    if (!tc->suppress_coverage)
        MVM_edge_coverage_report_bb_edge_hit(tc, bb_id);
}


void MVM_fuzzing_cmplog_rtn_hook_atkey_hook(MVMThreadContext *tc, MVMObject *hash, MVMString *str, uint64_t caller_id);
void MVM_fuzzing_cmplog_ins_hook8(uint64_t arg1, uint64_t arg2, uint64_t caller_id, uint8_t attr);
