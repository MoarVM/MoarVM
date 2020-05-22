#define MVM_DISP_SYSCALL_MAX_ARGS   8

/* Information about a VM-provided call. */
struct MVMDispSysCall {
    /* Syscall name. */
    const char *c_name;

    /* The implementation. This assumes it can pull out arguments without
     * any validation being required on argument count, kinds, and
     * representations, which are checked below (and their checks thus
     * lifted out as guards, which may be eliminated in optimized code). */
    void (*implementation) (MVMThreadContext *tc, MVMArgs arg_info);

    /* The function wrapper around the implementation. */
    MVMCFunction *wrapper;

    /* Minimum and maximum acceptable number of positional arguments. */
    MVMuint8 min_args;
    MVMuint8 max_args;

    /* Expected argument kinds. */
    MVMCallsiteFlags expected_kinds[MVM_DISP_SYSCALL_MAX_ARGS];

    /* Expected argument representations. 0 is used to mean "unimportant"
     * (the number is actually given to MVMString, but that should always be
     * in an s register, not an o register, and so this should never be
     * an issue). Only relevant for obj kind registers. */
    MVMuint8 expected_reprs[MVM_DISP_SYSCALL_MAX_ARGS];

    /* Set to 1 if we expect it to be concrete, otherwise means we don't
     * care. Only relevant for obj kind registers. */
    MVMuint8 expected_concrete[MVM_DISP_SYSCALL_MAX_ARGS];
};

/* Entry in the hash for looking up syscalls on first dispatch. */
struct MVMDispSysCallHashEntry {
    struct MVMStrHashHandle hash_handle;
    MVMDispSysCall *syscall;
};

void MVM_disp_syscall_setup(MVMThreadContext *tc);
MVMDispSysCall * MVM_disp_syscall_find(MVMThreadContext *tc, MVMString *name);
