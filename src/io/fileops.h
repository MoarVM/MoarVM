void MVM_file_copy(MVMThreadContext *tc, MVMString *src, MVMString *dest);
void MVM_file_delete(MVMThreadContext *tc, MVMString *f);
MVMint64 MVM_file_exists(MVMThreadContext *tc, MVMString *f);
MVMString * MVM_file_slurp(MVMThreadContext *tc, MVMString *filename);
MVMObject * MVM_file_get_stdout(MVMThreadContext *tc);
char * MVM_file_get_full_path(MVMThreadContext *tc, apr_pool_t *tmp_pool, char *path);