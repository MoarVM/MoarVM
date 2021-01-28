MVMuint32 MVM_disp_resume_find_topmost(MVMThreadContext *tc, MVMDispProgram **found_disp_program,
        MVMCallStackRecord **found_record);
MVMObject * MVM_disp_resume_init_capture(MVMThreadContext *tc, MVMCallStackRecord *record,
        MVMuint32 resumption_idx);
