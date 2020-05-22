#include "moar.h"

/* dispatcher-register */
static void dispatcher_register_impl(MVMThreadContext *tc, MVMArgs arg_info) {
    MVMArgProcContext arg_ctx;
    MVM_args_proc_setup(tc, &arg_ctx, arg_info);
    MVMString *id = MVM_args_get_required_pos_str(tc, &arg_ctx, 0);
    MVMObject *dispatch = MVM_args_get_required_pos_obj(tc, &arg_ctx, 1);
    MVMObject *resume = arg_info.callsite->num_pos > 2
        ? MVM_args_get_required_pos_obj(tc, &arg_ctx, 2)
        : NULL;
    MVM_disp_registry_register(tc, id, dispatch, resume);
    MVM_args_set_result_obj(tc, tc->instance->VMNull, MVM_RETURN_CURRENT_FRAME);
}
static MVMDispSysCall dispatcher_register = {
    .c_name = "dispatcher-register",
    .implementation = dispatcher_register_impl,
    .min_args = 2,
    .max_args = 3,
    .expected_kinds = { MVM_CALLSITE_ARG_STR, MVM_CALLSITE_ARG_OBJ, MVM_CALLSITE_ARG_OBJ },
    .expected_reprs = { 0, MVM_REPR_ID_MVMCode, MVM_REPR_ID_MVMCode },
    .expected_concrete = { 1, 1, 1 },
};

/* dispatcher-delegate */
static void dispatcher_delegate_impl(MVMThreadContext *tc, MVMArgs arg_info) {
    MVMArgProcContext arg_ctx;
    MVM_args_proc_setup(tc, &arg_ctx, arg_info);
    MVMString *id = MVM_args_get_required_pos_str(tc, &arg_ctx, 0);
    MVMObject *capture = MVM_args_get_required_pos_obj(tc, &arg_ctx, 1);
    MVM_disp_program_record_delegate(tc, id, capture);
    MVM_args_set_result_obj(tc, tc->instance->VMNull, MVM_RETURN_CURRENT_FRAME);
}
static MVMDispSysCall dispatcher_delegate = {
    .c_name = "dispatcher-delegate",
    .implementation = dispatcher_delegate_impl,
    .min_args = 2,
    .max_args = 2,
    .expected_kinds = { MVM_CALLSITE_ARG_STR, MVM_CALLSITE_ARG_OBJ },
    .expected_reprs = { 0, MVM_REPR_ID_MVMCapture },
    .expected_concrete = { 1, 1 },
    .hash_handle = EMPTY_HASH_HANDLE
};

/* dispatcher-drop-arg */
static void dispatcher_drop_arg_impl(MVMThreadContext *tc, MVMArgs arg_info) {
    MVMArgProcContext arg_ctx;
    MVM_args_proc_setup(tc, &arg_ctx, arg_info);
    MVMObject *capture = MVM_args_get_required_pos_obj(tc, &arg_ctx, 0);
    MVMint64 idx = MVM_args_get_required_pos_int(tc, &arg_ctx, 1);
    MVMObject *derived = MVM_disp_program_record_capture_drop_arg(tc, capture, (MVMuint32)idx);
    MVM_args_set_result_obj(tc, derived, MVM_RETURN_CURRENT_FRAME);
}
static MVMDispSysCall dispatcher_drop_arg = {
    .c_name = "dispatcher-drop-arg",
    .implementation = dispatcher_drop_arg_impl,
    .min_args = 2,
    .max_args = 2,
    .expected_kinds = { MVM_CALLSITE_ARG_OBJ, MVM_CALLSITE_ARG_INT },
    .expected_reprs = { MVM_REPR_ID_MVMCapture, 0 },
    .expected_concrete = { 1, 1 },
    .hash_handle = EMPTY_HASH_HANDLE
};

/* Add all of the syscalls into the hash. */
MVM_STATIC_INLINE void add_to_hash(MVMThreadContext *tc, MVMDispSysCall *syscall) {
    MVMString *name = MVM_string_ascii_decode_nt(tc, tc->instance->VMString, syscall->c_name);
    MVMDispSysCallHashEntry *entry = MVM_str_hash_lvalue_fetch_nocheck(tc,
            &tc->instance->syscalls, name);
    entry->hash_handle.key = name;
    entry->syscall = syscall;

    MVMObject *BOOTCCode = tc->instance->boot_types.BOOTCCode;
    MVMObject *code_obj = REPR(BOOTCCode)->allocate(tc, STABLE(BOOTCCode));
    ((MVMCFunction *)code_obj)->body.func = syscall->implementation;
    syscall->wrapper = (MVMCFunction *)code_obj;
    MVM_gc_root_add_permanent_desc(tc, (MVMCollectable **)&(syscall->wrapper), "MoarVM syscall wrapper");
}
void MVM_disp_syscall_setup(MVMThreadContext *tc) {
    MVM_gc_allocate_gen2_default_set(tc);
    MVM_str_hash_build(tc, &tc->instance->syscalls, sizeof(MVMDispSysCallHashEntry), 64);
    add_to_hash(tc, &dispatcher_register);
    add_to_hash(tc, &dispatcher_delegate);
    add_to_hash(tc, &dispatcher_drop_arg);
    MVM_gc_allocate_gen2_default_clear(tc);
}

/* Look up a syscall by name. Returns NULL if it's not found. */
MVMDispSysCall * MVM_disp_syscall_find(MVMThreadContext *tc, MVMString *name) {
    MVMDispSysCallHashEntry *entry = MVM_str_hash_fetch_nocheck(tc, &tc->instance->syscalls, name);
    return entry ? entry->syscall : NULL;
}
