void MVM_line_coverage_instrument(MVMThreadContext *tc, MVMStaticFrame *static_frame);
void MVM_line_coverage_report(MVMThreadContext *tc, MVMString *filename, MVMuint32 line_number, MVMuint16 cache_slot, MVMObject *cache);
