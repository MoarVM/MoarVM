MVM_PUBLIC char * MVM_spesh_dump(MVMThreadContext *tc, MVMSpeshGraph *g);
MVM_PUBLIC char * MVM_spesh_dump_stats(MVMThreadContext *tc, MVMStaticFrame *sf);
MVM_PUBLIC char * MVM_spesh_dump_planned(MVMThreadContext *tc, MVMSpeshPlanned *p);
MVM_PUBLIC char * MVM_spesh_dump_arg_guard(MVMThreadContext *tc, MVMStaticFrame *sf,
        MVMSpeshArgGuard *ag);
