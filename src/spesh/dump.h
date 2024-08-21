/* Auto-growing buffer. */
typedef struct {
    char   *buffer;
    size_t  alloc;
    size_t  pos;
} MVMDumpStr;

#ifdef MVM_INTERNAL_HELPERS
void MVM_ds_append(MVMDumpStr *ds, char *to_add);

size_t MVM_ds_tell(MVMDumpStr *ds);

void MVM_ds_rewind(MVMDumpStr *ds, size_t target);

/* Formats a string and then MVM_ds_appends it. */
MVM_FORMAT(printf, 2, 3)
void MVM_ds_appendf(MVMDumpStr *ds, const char *fmt, ...);

/* Turns a MoarVM string into a C string and MVM_ds_appends it. */
void MVM_ds_append_str(MVMThreadContext *tc, MVMDumpStr *ds, MVMString *s);

/* MVM_ds_appends a null at the end. */
void MVM_ds_append_null(MVMDumpStr *ds);
#endif


MVM_PUBLIC char * MVM_spesh_dump(MVMThreadContext *tc, MVMSpeshGraph *g);
MVM_PUBLIC void MVM_spesh_dump_to_ds(MVMThreadContext *tc, MVMSpeshGraph *g, MVMDumpStr *ds);
MVM_PUBLIC void MVM_spesh_dump_stats(MVMThreadContext *tc, MVMStaticFrame *sf, MVMDumpStr *ds);
MVM_PUBLIC void MVM_spesh_dump_planned(MVMThreadContext *tc, MVMSpeshPlanned *p, MVMDumpStr *ds);
MVM_PUBLIC void MVM_spesh_dump_arg_guard(MVMThreadContext *tc, MVMStaticFrame *sf,
        MVMSpeshArgGuard *ag, MVMDumpStr *ds);
