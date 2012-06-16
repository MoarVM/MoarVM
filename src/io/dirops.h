void MVM_dir_mkdir(MVMThreadContext *tc, MVMString *f);
void MVM_dir_rmdir(MVMThreadContext *tc, MVMString *f);
MVMObject * MVM_dir_open(MVMThreadContext *tc, MVMObject *type_object, MVMString *dirname);
MVMString * MVM_dir_read(MVMThreadContext *tc, MVMObject *oshandle);
void MVM_dir_close(MVMThreadContext *tc, MVMObject *oshandle);
void MVM_dir_chdir(MVMThreadContext *tc, MVMString *dir);
