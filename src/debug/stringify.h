
MVM_PUBLIC void MVM_string_printh(MVMThreadContext *tc, MVMOSHandle *handle, MVMString *a);
MVM_PUBLIC void MVM_string_note(MVMThreadContext *tc, MVMString *a);
MVM_PUBLIC void MVM_ascii_note(MVMThreadContext *tc, const char *note);
MVM_PUBLIC char * MVM_ascii_sprintf(MVMThreadContext *tc, const char *fmt, ...);
MVM_PUBLIC MVMString * MVM_string_sprintf(MVMThreadContext *tc, const char *fmt, ...);

MVM_PUBLIC MVMArray * MVM_make_string_table(MVMThreadContext *tc, MVMuint32 size);
MVM_PUBLIC void MVM_dispose_string_table(MVMThreadContext *tc, MVMArray *stable);

MVM_PUBLIC MVMArray * MVM_stringify_MVMObject(MVMThreadContext *tc, MVMString *prefix, MVMObject *o, MVMuint32 max_deph, MVMuint32 depth);
MVM_PUBLIC MVMArray * MVM_stringify_MVMSTable(MVMThreadContext *tc, MVMString *prefix, MVMSTable *st, MVMuint32 max_depth, MVMuint32 depth);

#if MVM_DEBUG
MVM_PUBLIC void MVM_debug_global(MVMuint8 on);
MVM_PUBLIC void MVM_debug_thread(MVMThreadContext *tc, MVMuint8 on);
MVM_PUBLIC void MVM_debug_printf(MVMThreadContext *tc, const char *env, const char *fmt, ...);
#else
#define MVM_debug_global(ON) do {} while(0);
#define MVM_debug_thread(ON) do {} while(0);
#define MVM_debug_printf(TC, ENV, FMT, ...) do {} while(0);
#endif
