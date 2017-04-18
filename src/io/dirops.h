void MVM_dir_mkdir(MVMThreadContext *tc, MVMString *path, MVMint64 mode);
void MVM_dir_rmdir(MVMThreadContext *tc, MVMString *path);
MVMObject * MVM_dir_open(MVMThreadContext *tc, MVMString *dirname);
MVMString * MVM_dir_read(MVMThreadContext *tc, MVMObject *oshandle);
void MVM_dir_close(MVMThreadContext *tc, MVMObject *oshandle);
MVMString * MVM_dir_cwd(MVMThreadContext *tc);
int MVM_dir_chdir_C_string(MVMThreadContext *tc, const char *dirstring);
void MVM_dir_chdir(MVMThreadContext *tc, MVMString *dir);
