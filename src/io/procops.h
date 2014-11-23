MVMObject * MVM_proc_getenvhash(MVMThreadContext *tc);
MVMObject * MVM_file_openpipe(MVMThreadContext *tc, MVMString *cmd, MVMString *cwd, MVMObject *env, MVMString *err_path);
MVMint64 MVM_proc_shell(MVMThreadContext *tc, MVMString *cmd_s, MVMString *cwd, MVMObject *env);
MVMint64 MVM_proc_spawn(MVMThreadContext *tc, MVMObject *argv, MVMString *cwd, MVMObject *env);
MVMObject * MVM_proc_spawn_async(MVMThreadContext *tc, MVMObject *queue, MVMObject *args,
         MVMString *cwd, MVMObject *env, MVMObject *callbacks);
void MVM_proc_kill_async(MVMThreadContext *tc, MVMObject *handle, MVMint64 signal);
MVMint64 MVM_proc_getpid(MVMThreadContext *tc);
MVMint64 MVM_proc_rand_i(MVMThreadContext *tc);
MVMnum64 MVM_proc_rand_n(MVMThreadContext *tc);
MVMnum64 MVM_proc_randscale_n(MVMThreadContext *tc, MVMnum64 scale);
void MVM_proc_seed(MVMThreadContext *tc, MVMint64 seed);
MVMint64 MVM_proc_time_i(MVMThreadContext *tc);
MVMObject * MVM_proc_clargs(MVMThreadContext *tc);
MVMnum64 MVM_proc_time_n(MVMThreadContext *tc);
MVMString * MVM_executable_name(MVMThreadContext *tc);
