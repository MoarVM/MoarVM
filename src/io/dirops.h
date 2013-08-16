void MVM_dir_mkdir(MVMThreadContext *tc, MVMString *path, MVMint64 mode);
void MVM_dir_rmdir(MVMThreadContext *tc, MVMString *path);
MVMObject * MVM_dir_open(MVMThreadContext *tc, MVMObject *type_object, MVMString *dirname, MVMint64 encoding_flag);
MVMString * MVM_dir_read(MVMThreadContext *tc, MVMObject *oshandle);
void MVM_dir_close(MVMThreadContext *tc, MVMObject *oshandle);
void MVM_dir_chdir(MVMThreadContext *tc, MVMString *dir);
