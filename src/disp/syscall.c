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
    .expected_concrete = { 1, 1 }
};

/* dispatcher-track-arg */
static void dispatcher_track_arg_impl(MVMThreadContext *tc, MVMArgs arg_info) {
    MVMArgProcContext arg_ctx;
    MVM_args_proc_setup(tc, &arg_ctx, arg_info);
    MVMObject *capture = MVM_args_get_required_pos_obj(tc, &arg_ctx, 0);
    MVMint64 idx = MVM_args_get_required_pos_int(tc, &arg_ctx, 1);
    MVMObject *tracked = MVM_disp_program_record_track_arg(tc, capture, (MVMuint32)idx);
    MVM_args_set_result_obj(tc, tracked, MVM_RETURN_CURRENT_FRAME);
}
static MVMDispSysCall dispatcher_track_arg = {
    .c_name = "dispatcher-track-arg",
    .implementation = dispatcher_track_arg_impl,
    .min_args = 2,
    .max_args = 2,
    .expected_kinds = { MVM_CALLSITE_ARG_OBJ, MVM_CALLSITE_ARG_INT },
    .expected_reprs = { MVM_REPR_ID_MVMCapture, 0 },
    .expected_concrete = { 1, 1 }
};

/* dispatcher-track-attr */
static void dispatcher_track_attr_impl(MVMThreadContext *tc, MVMArgs arg_info) {
    MVMArgProcContext arg_ctx;
    MVM_args_proc_setup(tc, &arg_ctx, arg_info);
    MVMObject *tracked_in = MVM_args_get_required_pos_obj(tc, &arg_ctx, 0);
    MVMObject *class_handle = MVM_args_get_required_pos_obj(tc, &arg_ctx, 1);
    MVMString *name = MVM_args_get_required_pos_str(tc, &arg_ctx, 2);
    MVMObject *tracked_out = MVM_disp_program_record_track_attr(tc, tracked_in,
            class_handle, name);
    MVM_args_set_result_obj(tc, tracked_out, MVM_RETURN_CURRENT_FRAME);
}
static MVMDispSysCall dispatcher_track_attr = {
    .c_name = "dispatcher-track-attr",
    .implementation = dispatcher_track_attr_impl,
    .min_args = 3,
    .max_args = 3,
    .expected_kinds = { MVM_CALLSITE_ARG_OBJ, MVM_CALLSITE_ARG_OBJ, MVM_CALLSITE_ARG_STR },
    .expected_reprs = { MVM_REPR_ID_MVMTracked, 0, 0 },
    .expected_concrete = { 1, 0, 0 }
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
    .expected_concrete = { 1, 1 }
};

/* dispatcher-insert-arg */
static void dispatcher_insert_arg_impl(MVMThreadContext *tc, MVMArgs arg_info) {
    MVMArgProcContext arg_ctx;
    MVM_args_proc_setup(tc, &arg_ctx, arg_info);
    MVMObject *capture = MVM_args_get_required_pos_obj(tc, &arg_ctx, 0);
    MVMint64 idx = MVM_args_get_required_pos_int(tc, &arg_ctx, 1);
    MVMObject *tracked = MVM_args_get_required_pos_obj(tc, &arg_ctx, 2);
    MVMObject *derived = MVM_disp_program_record_capture_insert_arg(tc, capture,
            (MVMuint32)idx, tracked);
    MVM_args_set_result_obj(tc, derived, MVM_RETURN_CURRENT_FRAME);
}
static MVMDispSysCall dispatcher_insert_arg = {
    .c_name = "dispatcher-insert-arg",
    .implementation = dispatcher_insert_arg_impl,
    .min_args = 3,
    .max_args = 3,
    .expected_kinds = { MVM_CALLSITE_ARG_OBJ, MVM_CALLSITE_ARG_INT, MVM_CALLSITE_ARG_OBJ },
    .expected_reprs = { MVM_REPR_ID_MVMCapture, 0, MVM_REPR_ID_MVMTracked },
    .expected_concrete = { 1, 1, 1 }
};

/* dispatcher-insert-arg-literal-obj */
static void dispatcher_insert_arg_literal_obj_impl(MVMThreadContext *tc, MVMArgs arg_info) {
    MVMArgProcContext arg_ctx;
    MVM_args_proc_setup(tc, &arg_ctx, arg_info);
    MVMObject *capture = MVM_args_get_required_pos_obj(tc, &arg_ctx, 0);
    MVMint64 idx = MVM_args_get_required_pos_int(tc, &arg_ctx, 1);
    MVMRegister insertee = { .o = MVM_args_get_required_pos_obj(tc, &arg_ctx, 2) };
    MVMObject *derived = MVM_disp_program_record_capture_insert_constant_arg(tc,
            capture, (MVMuint32)idx, MVM_CALLSITE_ARG_OBJ, insertee);
    MVM_args_set_result_obj(tc, derived, MVM_RETURN_CURRENT_FRAME);
}
static MVMDispSysCall dispatcher_insert_arg_literal_obj = {
    .c_name = "dispatcher-insert-arg-literal-obj",
    .implementation = dispatcher_insert_arg_literal_obj_impl,
    .min_args = 3,
    .max_args = 3,
    .expected_kinds = { MVM_CALLSITE_ARG_OBJ, MVM_CALLSITE_ARG_INT, MVM_CALLSITE_ARG_OBJ },
    .expected_reprs = { MVM_REPR_ID_MVMCapture, 0, 0 },
    .expected_concrete = { 1, 1, 0 }
};

/* dispatcher-insert-arg-literal-str */
static void dispatcher_insert_arg_literal_str_impl(MVMThreadContext *tc, MVMArgs arg_info) {
    MVMArgProcContext arg_ctx;
    MVM_args_proc_setup(tc, &arg_ctx, arg_info);
    MVMObject *capture = MVM_args_get_required_pos_obj(tc, &arg_ctx, 0);
    MVMint64 idx = MVM_args_get_required_pos_int(tc, &arg_ctx, 1);
    MVMRegister insertee = { .s = MVM_args_get_required_pos_str(tc, &arg_ctx, 2) };
    MVMObject *derived = MVM_disp_program_record_capture_insert_constant_arg(tc,
            capture, (MVMuint32)idx, MVM_CALLSITE_ARG_STR, insertee);
    MVM_args_set_result_obj(tc, derived, MVM_RETURN_CURRENT_FRAME);
}
static MVMDispSysCall dispatcher_insert_arg_literal_str = {
    .c_name = "dispatcher-insert-arg-literal-str",
    .implementation = dispatcher_insert_arg_literal_str_impl,
    .min_args = 3,
    .max_args = 3,
    .expected_kinds = { MVM_CALLSITE_ARG_OBJ, MVM_CALLSITE_ARG_INT, MVM_CALLSITE_ARG_STR },
    .expected_reprs = { MVM_REPR_ID_MVMCapture, 0, 0 },
    .expected_concrete = { 1, 1, 1 }
};

/* dispatcher-guard-type */
static void dispatcher_guard_type_impl(MVMThreadContext *tc, MVMArgs arg_info) {
    MVMArgProcContext arg_ctx;
    MVM_args_proc_setup(tc, &arg_ctx, arg_info);
    MVMObject *tracked = MVM_args_get_required_pos_obj(tc, &arg_ctx, 0);
    MVM_disp_program_record_guard_type(tc, tracked);
    MVM_args_set_result_obj(tc, tracked, MVM_RETURN_CURRENT_FRAME);
}
static MVMDispSysCall dispatcher_guard_type = {
    .c_name = "dispatcher-guard-type",
    .implementation = dispatcher_guard_type_impl,
    .min_args = 1,
    .max_args = 1,
    .expected_kinds = { MVM_CALLSITE_ARG_OBJ },
    .expected_reprs = { MVM_REPR_ID_MVMTracked },
    .expected_concrete = { 1 }
};

/* dispatcher-guard-concreteness */
static void dispatcher_guard_concreteness_impl(MVMThreadContext *tc, MVMArgs arg_info) {
    MVMArgProcContext arg_ctx;
    MVM_args_proc_setup(tc, &arg_ctx, arg_info);
    MVMObject *tracked = MVM_args_get_required_pos_obj(tc, &arg_ctx, 0);
    MVM_disp_program_record_guard_concreteness(tc, tracked);
    MVM_args_set_result_obj(tc, tracked, MVM_RETURN_CURRENT_FRAME);
}
static MVMDispSysCall dispatcher_guard_concreteness = {
    .c_name = "dispatcher-guard-concreteness",
    .implementation = dispatcher_guard_concreteness_impl,
    .min_args = 1,
    .max_args = 1,
    .expected_kinds = { MVM_CALLSITE_ARG_OBJ },
    .expected_reprs = { MVM_REPR_ID_MVMTracked },
    .expected_concrete = { 1 }
};

/* dispatcher-guard-literal */
static void dispatcher_guard_literal_impl(MVMThreadContext *tc, MVMArgs arg_info) {
    MVMArgProcContext arg_ctx;
    MVM_args_proc_setup(tc, &arg_ctx, arg_info);
    MVMObject *tracked = MVM_args_get_required_pos_obj(tc, &arg_ctx, 0);
    MVM_disp_program_record_guard_literal(tc, tracked);
    MVM_args_set_result_obj(tc, tracked, MVM_RETURN_CURRENT_FRAME);
}
static MVMDispSysCall dispatcher_guard_literal = {
    .c_name = "dispatcher-guard-literal",
    .implementation = dispatcher_guard_literal_impl,
    .min_args = 1,
    .max_args = 1,
    .expected_kinds = { MVM_CALLSITE_ARG_OBJ },
    .expected_reprs = { MVM_REPR_ID_MVMTracked },
    .expected_concrete = { 1 }
};

/* dispatcher-guard-not-literal-obj */
static void dispatcher_guard_not_literal_obj_impl(MVMThreadContext *tc, MVMArgs arg_info) {
    MVMArgProcContext arg_ctx;
    MVM_args_proc_setup(tc, &arg_ctx, arg_info);
    MVMObject *tracked = MVM_args_get_required_pos_obj(tc, &arg_ctx, 0);
    MVMObject *object = MVM_args_get_required_pos_obj(tc, &arg_ctx, 1);
    MVM_disp_program_record_guard_not_literal_obj(tc, tracked, object);
    MVM_args_set_result_obj(tc, tracked, MVM_RETURN_CURRENT_FRAME);
}
static MVMDispSysCall dispatcher_guard_not_literal_obj = {
    .c_name = "dispatcher-guard-not-literal-obj",
    .implementation = dispatcher_guard_not_literal_obj_impl,
    .min_args = 2,
    .max_args = 2,
    .expected_kinds = { MVM_CALLSITE_ARG_OBJ, MVM_CALLSITE_ARG_OBJ },
    .expected_reprs = { MVM_REPR_ID_MVMTracked, 0 },
    .expected_concrete = { 1, 0 }
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
    add_to_hash(tc, &dispatcher_track_arg);
    add_to_hash(tc, &dispatcher_track_attr);
    add_to_hash(tc, &dispatcher_drop_arg);
    add_to_hash(tc, &dispatcher_insert_arg);
    add_to_hash(tc, &dispatcher_insert_arg_literal_obj);
    add_to_hash(tc, &dispatcher_insert_arg_literal_str);
    add_to_hash(tc, &dispatcher_guard_type);
    add_to_hash(tc, &dispatcher_guard_concreteness);
    add_to_hash(tc, &dispatcher_guard_literal);
    add_to_hash(tc, &dispatcher_guard_not_literal_obj);
    MVM_gc_allocate_gen2_default_clear(tc);
}

/* Look up a syscall by name. Returns NULL if it's not found. */
MVMDispSysCall * MVM_disp_syscall_find(MVMThreadContext *tc, MVMString *name) {
    MVMDispSysCallHashEntry *entry = MVM_str_hash_fetch_nocheck(tc, &tc->instance->syscalls, name);
    return entry ? entry->syscall : NULL;
}
