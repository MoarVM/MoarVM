#define MVM_PIPE_INHERIT        1
#define MVM_PIPE_IGNORE         2
#define MVM_PIPE_CAPTURE        4
#define MVM_PIPE_INHERIT_IN     1
#define MVM_PIPE_IGNORE_IN      2
#define MVM_PIPE_CAPTURE_IN     4
#define MVM_PIPE_INHERIT_OUT    8
#define MVM_PIPE_IGNORE_OUT    16
#define MVM_PIPE_CAPTURE_OUT   32
#define MVM_PIPE_INHERIT_ERR   64
#define MVM_PIPE_IGNORE_ERR   128
#define MVM_PIPE_CAPTURE_ERR  256

MVMObject * MVM_proc_getenvhash(MVMThreadContext *tc);
MVMint64 MVM_proc_shell(MVMThreadContext *tc, MVMString *cmd, MVMString *cwd, MVMObject *env,
        MVMObject *in, MVMObject *out, MVMObject *err, MVMint64 flags);
MVMint64 MVM_proc_spawn(MVMThreadContext *tc, MVMObject *argv, MVMString *cwd, MVMObject *env,
        MVMObject *in, MVMObject *out, MVMObject *err, MVMint64 flags);
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
