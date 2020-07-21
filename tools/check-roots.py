import gcc
from gccutils import get_src_for_loc, pformat, cfg_to_dot, invoke_dot

# We'll implement this as a custom pass, to be called directly after the
# builtin "cfg" pass, which generates the CFG:

sixmodel_types = [
'struct MVMCollectable *',
'struct MVMObject *',
'struct MVMObjectStooge *',
'struct MVMSTable *',
'struct MVMFrame *',

# Generated with: MoarVM> ack -h -B1 MVMObject\ common src/6model/reprs/*.h | grep struct | cut -d' ' -f1,2 | sort -u | sed "s/^/'/" | sed "s/\$/ *',/"
'struct MVMArray *',
'struct MVMAsyncTask *',
'struct MVMCArray *',
'struct MVMCFunction *',
'struct MVMCPPStruct *',
'struct MVMCPointer *',
'struct MVMCStr *',
'struct MVMCStruct *',
'struct MVMCUnion *',
'struct MVMCallCapture *',
'struct MVMCode *',
#'struct MVMCompUnit *', # CompUnits are always allocated in gen2 directly
'struct MVMConcBlockingQueue *',
'struct MVMConditionVariable *',
'struct MVMContext *',
'struct MVMContinuation *',
'struct MVMDLLSym *',
'struct MVMDecoder *',
'struct MVMException *',
'struct MVMHash *',
'struct MVMHashAttrStore *',
'struct MVMIter *',
'struct MVMKnowHOWAttributeREPR *',
'struct MVMKnowHOWREPR *',
'struct MVMMultiCache *',
'struct MVMMultiDimArray *',
'struct MVMNFA *',
'struct MVMNativeCall *',
'struct MVMNativeRef *',
'struct MVMNull *',
'struct MVMOSHandle *',
'struct MVMP6bigint *',
'struct MVMP6int *',
'struct MVMP6num *',
'struct MVMP6opaque *',
'struct MVMP6str *',
'struct MVMReentrantMutex *',
'struct MVMSemaphore *',
'struct MVMSerializationContext *',
'struct MVMSpeshLog *',
'struct MVMSpeshPluginState *',
'struct MVMStaticFrame *',
#'struct MVMStaticFrameSpesh *', # StaticFrameSpesh are always allocated in gen2 directly
'struct MVMString *',
'struct MVMThread *',
'struct MVMUninstantiable *',
'struct MVMSpeshCandidate *',
]

gen2_allocated_types = [
'struct MVMCompUnit *',
'struct MVMStaticFrameSpesh *',
]

allocators = [
'MVMDLLSym_initialize',
'MVM_6model_bootstrap',
'MVM_6model_can_method',
'MVM_6model_can_method_cache_only',
'MVM_6model_find_method',
'MVM_6model_find_method_cache_only',
'MVM_6model_find_method_spesh',
'MVM_6model_get_how',
'MVM_6model_get_how_obj',
'MVM_6model_istype',
'MVM_args_assert_void_return_ok',
'MVM_args_bind_failed',
'MVM_args_set_result_int',
'MVM_args_set_result_num',
'MVM_args_set_result_obj',
'MVM_args_set_result_str',
'MVM_bigint_pow',
'MVM_bytecode_dump',
'MVM_bytecode_finish_frame',
'MVM_coerce_smart_intify',
'MVM_coerce_smart_numify',
'MVM_coerce_smart_stringify',
'MVM_concblockingqueue_jit_poll',
'MVM_concblockingqueue_poll',
'MVM_conditionvariable_from_lock',
'MVM_conditionvariable_wait',
'MVM_confprog_run',
'MVM_context_caller_lookup',
'MVM_context_dynamic_lookup',
'MVM_context_from_frame',
'MVM_context_lexical_lookup',
'MVM_context_lexicals_as_hash',
'MVM_continuation_control',
'MVM_continuation_invoke',
'MVM_debugserver_breakpoint_check',
'MVM_debugserver_init',
'MVM_debugserver_notify_unhandled_exception',
'MVM_exception_die',
'MVM_exception_resume',
'MVM_exception_throwcat',
'MVM_exception_throwobj',
'MVM_exception_throwpayload',
'MVM_frame_binddynlex',
'MVM_frame_capture_inner',
'MVM_frame_capturelex',
'MVM_frame_create_context_only',
'MVM_frame_create_for_deopt',
'MVM_frame_debugserver_move_to_heap',
'MVM_frame_find_contextual_by_name',
'MVM_frame_find_dynamic_using_frame_walker',
'MVM_frame_find_lexical_by_name',
'MVM_frame_find_lexical_by_name_outer',
'MVM_frame_find_lexical_by_name_rel',
'MVM_frame_find_lexical_by_name_rel_caller',
'MVM_frame_force_to_heap',
'MVM_frame_get_code_object',
'MVM_frame_getdynlex',
'MVM_frame_getdynlex_with_frame_walker',
'MVM_frame_invoke',
'MVM_frame_invoke_code',
'MVM_frame_lexical_lookup_using_frame_walker',
'MVM_frame_move_to_heap',
'MVM_frame_takeclosure',
'MVM_frame_try_get_lexical',
'MVM_frame_unwind_to',
'MVM_frame_vivify_lexical',
'MVM_gc_allocate',
'MVM_gc_allocate_frame',
'MVM_gc_allocate_nursery',
'MVM_gc_allocate_object',
'MVM_gc_allocate_stable',
'MVM_gc_allocate_type_object',
'MVM_gc_allocate_zeroed',
'MVM_gc_enter_from_allocator',
'MVM_gc_enter_from_interrupt',
'MVM_gc_mark_thread_blocked',
'MVM_gc_mark_thread_unblocked',
'MVM_interp_run',
'MVM_io_accept',
'MVM_io_bind',
'MVM_io_close',
'MVM_io_connect',
'MVM_io_eof',
'MVM_io_eventloop_destroy',
'MVM_io_eventloop_join',
'MVM_io_eventloop_permit',
'MVM_io_eventloop_start',
'MVM_io_fileno',
'MVM_io_flush',
'MVM_io_flush_standard_handles',
'MVM_io_get_async_task_handle',
'MVM_io_getport',
'MVM_io_is_tty',
'MVM_io_lock',
'MVM_io_read_bytes',
'MVM_io_read_bytes_async',
'MVM_io_resolve_host_name',
'MVM_io_seek',
'MVM_io_set_buffer_size',
'MVM_io_socket_connect_async',
'MVM_io_socket_listen_async',
'MVM_io_socket_udp_async',
'MVM_io_tell',
'MVM_io_truncate',
'MVM_io_unlock',
'MVM_io_write_bytes',
'MVM_io_write_bytes_async',
'MVM_io_write_bytes_c',
'MVM_io_write_bytes_to_async',
'MVM_iter',
'MVM_jit_emit_block_branch',
'MVM_jit_emit_branch',
'MVM_jit_emit_guard',
'MVM_jit_emit_invoke',
'MVM_jit_emit_primitive',
'MVM_load_bytecode',
'MVM_load_bytecode_buffer',
'MVM_load_bytecode_buffer_to_cu',
'MVM_load_bytecode_fh',
'MVM_nativecall_invoke',
'MVM_nativecall_jit_graph_for_caller_code',
'MVM_nativeref_attr_i',
'MVM_nativeref_attr_n',
'MVM_nativeref_attr_s',
'MVM_nativeref_lex_i',
'MVM_nativeref_lex_n',
'MVM_nativeref_lex_name_i',
'MVM_nativeref_lex_name_n',
'MVM_nativeref_lex_name_s',
'MVM_nativeref_lex_s',
'MVM_nativeref_multidim_i',
'MVM_nativeref_multidim_n',
'MVM_nativeref_multidim_s',
'MVM_nativeref_pos_i',
'MVM_nativeref_pos_n',
'MVM_nativeref_pos_s',
'MVM_proc_getenvhash',
'MVM_profile_end',
'MVM_profile_heap_end',
'MVM_profile_instrumented_end',
'MVM_reentrantmutex_lock',
'MVM_reentrantmutex_lock_checked',
'MVM_sc_disclaim',
'MVM_sc_get_code',
'MVM_sc_get_object',
'MVM_sc_get_sc_object',
'MVM_sc_get_stable',
'MVM_semaphore_acquire',
'MVM_serialization_demand_code',
'MVM_serialization_demand_object',
'MVM_serialization_demand_stable',
'MVM_serialization_deserialize',
'MVM_serialization_finish_deserialize_method_cache',
'MVM_serialization_force_stable',
'MVM_serialization_read_ref',
'MVM_serialization_read_stable_ref',
'MVM_serialization_serialize',
'MVM_serialization_write_ref',
'MVM_spesh_candidate_add',
'MVM_spesh_deopt_all',
'MVM_spesh_deopt_one',
'MVM_spesh_facts_discover',
'MVM_spesh_frame_walker_get_lex',
'MVM_spesh_frame_walker_get_lexicals_hash',
'MVM_spesh_inline',
'MVM_spesh_inline_try_get_graph',
'MVM_spesh_inline_try_get_graph_from_unspecialized',
'MVM_spesh_log_decont',
'MVM_spesh_log_entry',
'MVM_spesh_log_invoke_target',
'MVM_spesh_log_new_compunit',
'MVM_spesh_log_osr',
'MVM_spesh_log_return_to_unlogged',
'MVM_spesh_log_return_type',
'MVM_spesh_log_static',
'MVM_spesh_log_type',
'MVM_spesh_optimize',
'MVM_spesh_try_can_method',
'MVM_spesh_try_find_method',
'MVM_spesh_worker_join',
'MVM_string_print',
'MVM_string_say',
'MVM_thread_join',
'MVM_thread_join_foreground',
'MVM_thread_run',
'MVM_validate_static_frame',
'MVM_vm_create_instance',
'MVM_vm_destroy_instance',
'MVM_vm_dump_file',
'MVM_vm_exit',
'MVM_vm_run_bytecode',
'MVM_vm_run_file',
'accepts_type_sr',
'acquire_mutex',
'add_attribute',
'add_bb_facts',
'add_method',
'allocate',
'allocate_frame',
'allocate_heap_frame',
'async_handler',
'at_key',
'at_pos',
'attr_box_target',
'attr_compose',
'attr_name',
'attr_new',
'attr_type',
'attrref',
'autoclose',
'bind_error_return',
'bind_key',
'bootstrap_KnowHOW',
'callback_handler',
'cancel_work',
'closefh',
'code_pair_deserialize',
'code_pair_serialize',
'commit_entry',
'compose',
'compute_allocation_strategy',
'consume_ins',
'consume_invoke',
'continue_unwind',
'create_KnowHOWAttribute',
'create_caller_code',
'create_caller_or_outer_context_debug_handle',
'create_context_only',
'create_context_or_code_obj_debug_handle',
'debugserver_worker',
'deopt_frame',
'deserialize',
'deserialize_closure',
'deserialize_context',
'deserialize_method_cache_lazy',
'deserialize_repr_data',
'deserialize_stable',
'deserialize_stable_size',
'do_accepts_type_check',
'dump_data',
'emit_fastcreate',
'exists_key',
'fastcreate',
'fix_wval',
'flush',
'flush_output_buffer',
'get_method_cache',
'index_mapping_and_flat_list',
'instrumentation_level_barrier',
'invoke_handler',
'lex_ref',
'lexref_by_name',
'lock',
'log_param_type',
'log_parameter',
'main',
'materialize_object',
'materialize_replaced_objects',
'md_posref',
'merge_graph',
'mvm_tell',
'new_type',
'op_to_func',
'optimize_bb',
'optimize_bb_switch',
'optimize_call',
'optimize_can_op',
'optimize_method_lookup',
'optimize_smart_coerce',
'panic_unhandled_ex',
'perform_write',
'permit_work',
'posref',
'prepare_and_verify_static_frame',
'push',
'read_array_var',
'read_bytes',
'read_code_ref',
'read_hash_str_var',
'read_obj_ref',
'read_object_table_entry',
'read_one_packet',
'read_op',
'repossess',
'request_all_threads_resume',
'request_all_threads_suspend',
'request_context_lexicals',
'request_thread_resumes',
'request_thread_suspends',
'resolve_param_interns',
'run_comp_unit',
'run_deserialization_frame',
'run_gc',
'run_handler',
'run_load',
'seek',
'send_log',
'serialize',
'serialize_context',
'serialize_repr_data',
'serialize_stable',
'set_buffer_size',
'setup_step',
'setup_work',
'shift',
'socket_accept',
'socket_bind',
'socket_connect',
'socket_read_bytes',
'socket_write_bytes',
'spesh',
'start_thread',
'stop_point_hit',
'stub_object',
'stub_stable',
'toplevel_initial_invoke',
'try_join',
'type_object_for',
'uninline',
'unlock',
'unshift',
'unwind_after_handler',
'validate_block',
'validate_lex_operand',
'validate_operand',
'validate_operands',
'validate_sequence',
'work_loop',
'write_array_var',
'write_bytes',
'write_bytes_to',
'write_hash_str_var',
'wvalfrom_facts',

'MVM_6model_bootstrap',
'MVM_6model_parametric_setup',
'MVM_args_bind_failed',
'MVM_args_save_capture',
'MVM_args_set_result_int',
'MVM_args_set_result_num',
'MVM_args_set_result_str',
'MVM_args_use_capture',
'MVM_backend_config',
'MVM_bigint_div',
'MVM_bigint_expmod',
'MVM_bigint_from_bigint',
'MVM_bigint_from_num',
'MVM_bigint_mod',
'MVM_bigint_not',
'MVM_bigint_pow',
'MVM_bigint_radix',
'MVM_bigint_rand',
'MVM_bigint_shl',
'MVM_bigint_shr',
'MVM_bigint_to_str',
'MVM_bytecode_unpack',
'MVM_code_location',
'MVM_coerce_i_s',
'MVM_coerce_n_s',
'MVM_coerce_sI',
'MVM_coerce_smart_stringify',
'MVM_coerce_u_s',
'MVM_confprog_run',
'MVM_context_apply_traversal',
'MVM_context_from_frame',
'MVM_context_lexicals_as_hash',
'MVM_continuation_control',
'MVM_cu_from_bytes',
'MVM_debugserver_init',
'MVM_decoder_take_bytes',
'MVM_dll_find_symbol',
'MVM_exception_backtrace',
'MVM_exception_backtrace_strings',
'MVM_exception_die',
'MVM_file_handle_from_fd',
'MVM_file_open_fh',
'MVM_frame_try_return',
'MVM_hll_map',
'MVM_hll_set_config',
'MVM_hll_sym_get',
'MVM_intcache_for',
'MVM_interp_run',
'MVM_io_eventloop_permit',
'MVM_io_eventloop_start',
'MVM_io_file_watch',
'MVM_io_get_signals',
'MVM_io_signal_handle',
'MVM_io_socket_connect_async',
'MVM_io_socket_create',
'MVM_io_socket_listen_async',
'MVM_io_socket_udp_async',
'MVM_io_timer_create',
'MVM_iter',
'MVM_multi_cache_add',
'MVM_nativecall_cast',
'MVM_nativecall_global',
'MVM_nativecall_invoke',
'MVM_nativecall_jit_graph_for_caller_code',
'MVM_nativecall_make_int',
'MVM_nativecall_make_num',
'MVM_nativecall_make_str',
'MVM_nativecall_make_uint',
'MVM_nfa_from_statelist',
'MVM_nfa_run_proto',
'MVM_platform_uname',
'MVM_proc_getenvhash',
'MVM_profile_dump_instrumented_data',
'MVM_radix',
'MVM_repr_alloc_init',
'MVM_repr_box_int',
'MVM_repr_box_num',
'MVM_repr_box_str',
'MVM_repr_box_uint',
'MVM_repr_dimensions',
'MVM_repr_pos_slice',
'MVM_serialization_read_ref',
'MVM_serialization_serialize',
'MVM_spesh_frame_walker_get_lexicals_hash',
'MVM_spesh_log_create',
'MVM_spesh_log_initialize_thread',
'MVM_spesh_log_new_compunit',
'MVM_spesh_worker_start',
'MVM_string_ascii_from_buf_nocheck',
'MVM_string_concatenate',
'MVM_string_decodestream_get_all',
'MVM_string_decodestream_get_available',
'MVM_string_decodestream_get_chars',
'MVM_string_decodestream_get_until_sep',
'MVM_string_decodestream_get_until_sep_eof',
'MVM_string_escape',
'MVM_string_flip',
'MVM_string_join',
'MVM_string_repeat',
'MVM_string_split',
'MVM_string_substring',
'MVM_thread_new',
'MVM_unicode_codepoints_c_array_to_nfg_string',
'MVM_unicode_codepoints_to_nfg_string',
'MVM_unicode_string_from_name',
'MVM_vm_create_instance',
'NFG_checker',
'add_meta_object',
'add_type_to_types_array',
'at_pos',
'boot_typed_array',
'box_i',
'box_s',
'call_resolver',
'callback_handler',
'close_socket',
'collapse_strands',
'connect_setup',
'create_KnowHOWAttribute',
'deserialize_frames',
'deserialize_stable',
'do_case_change',
'dump_call_graph_node',
'dump_call_graph_node_loop',
'dump_data',
'dump_thread_data',
'evaluate_guards',
'finalize_handler_caller',
'get_all_in_buffer',
'get_attribute',
'index_mapping_and_flat_list',
'initialize',
'insert_if_not_exists',
'listen_setup',
'make_wrapper',
'mkdir_p',
'native_ref_fetch',
'nativecall_cast',
'new_array',
'new_hash',
'on_changed',
'on_connect',
'on_connection',
'on_read',
'on_write',
'op_to_func',
'prepare_and_verify_static_frame',
'push_name_and_port',
're_nfg',
'read_array_int',
'read_array_str',
'read_array_var',
'read_bytes',
'read_hash_str_var',
'read_param_intern',
'read_setup',
'collect_param_interns',
'run_handler',
'send_log',
'setup',
'setup_for_guard_recording',
'setup_setup',
'shift',
'signal_cb',
'socket_accept',
'start_thread',
'take_chars',
'worker',
'write_bytes',
'write_bytes_to',
'write_setup',
]

def check_var(node, var, hits):
    if node == var:
        hits.append(node)

def arg_is_var(arg, var):
    if isinstance(arg, gcc.AddrExpr):
        return arg.operand == var
    else:
        return arg == var

def collect_control_flows(bb, path, seen):
    if bb.index in seen:
        seen[bb.index] = 2
    else:
        seen[bb.index] = 1
    paths = []
    new_path = [bb]
    new_path.extend(path)
    if not bb.preds:
        paths.append(new_path)
    for edge in bb.preds:
        pred = edge.src
        if pred.index in seen and seen[pred.index] > 1:
            continue
        paths.extend(collect_control_flows(pred, new_path, seen))
    return paths

def check_code_for_unneeded_mvmroot(fun):
    for bb in fun.cfg.basic_blocks:
        for ins in bb.gimple:
            if isinstance(ins, gcc.GimpleCall) \
                and isinstance(ins.fn, gcc.AddrExpr) \
                and ins.fn.operand.name == 'MVM_gc_root_temp_push' \
                and isinstance(ins.args[1], gcc.AddrExpr):

                arg = ins.args[1].operand
                if str(arg.type) in gen2_allocated_types:
                    print('Unnecessary root for `' + arg.name + '` in ' + str(ins.loc))

def check_code_for_imbalanced_mvmroot(fun):
    for bb in fun.cfg.basic_blocks:
        for ins in bb.gimple:
            hits = []
            if isinstance(ins, gcc.GimpleReturn):
                cfs = collect_control_flows(bb, [], {})

                for cf in cfs:
                    root_stack = []
                    for bb in cf:
                        for ins in bb.gimple:
                            if isinstance(ins, gcc.GimpleCall):
                                if isinstance(ins.fn, gcc.AddrExpr): # plain function call is AddrExpr, other things could be function pointers
                                    if ins.fn.operand.name == 'MVM_gc_root_temp_push':
                                        arg = ins.args[1]
                                        root_stack.append(arg)
                                    if ins.fn.operand.name == 'MVM_gc_root_temp_pop':
                                        if not root_stack:
                                            print("Skipping function %s because of complicated rooting" % fun.decl.name)
                                            return
                                        root_stack.pop()
                                    if ins.fn.operand.name == 'MVM_gc_root_temp_pop_n':
                                        if not root_stack or not isinstance(ins.args[1], gcc.Constant):
                                            print("Skipping function %s because of complicated rooting" % fun.decl.name)
                                            return
                                        for i in range(0, ins.args[1].constant):
                                            root_stack.pop()
                    if root_stack:
                        print("Imbalanced temp root stack in " + str(fun.decl.name) + " at " + str(cf[-1].gimple[-1].loc) + " " + str(root_stack))

def check_code_for_var(fun, var, orig_initialized, warned={}):
    #print('    ' + str(var.type) + ' ' + var.name)

    for bb in fun.cfg.basic_blocks:
        for ins in bb.gimple:
            hits = []
            ins.walk_tree(check_var, var, hits)
            if hits:
                cfs = collect_control_flows(bb, [], {})

                for cf in cfs:
                    rooted = False
                    allocating_in_gen2 = False
                    allocated_while_not_rooted = []
                    root_stack = []
                    initialized = orig_initialized
                    for bb in cf:
                        for ins in bb.gimple:
                            if isinstance(ins, gcc.GimpleAssign):
                                if ins.lhs == var:
                                    if not (len(ins.rhs) == 1 and isinstance(ins.rhs[0], gcc.IntegerCst) and ins.rhs[0].constant == 0):
                                        initialized = True
                                    allocated_while_not_rooted = []
                            if isinstance(ins, gcc.GimpleCall):
                                if isinstance(ins.fn, gcc.AddrExpr): # plain function call is AddrExpr, other things could be function pointers
                                    if ins.fn.operand.name in ('MVM_serialization_write_ref', 'MVM_serialization_read_ref'):
                                        # serialization code always allocates in gen2 directly
                                        return
                                    if ins.fn.operand.name == 'MVM_gc_allocate_gen2_default_set':
                                        allocating_in_gen2 = True
                                    if ins.fn.operand.name == 'MVM_gc_allocate_gen2_default_clear':
                                        allocating_in_gen2 = False
                                    if ins.fn.operand.name == 'MVM_gc_root_temp_push':
                                        arg = ins.args[1]
                                        root_stack.append(arg)
                                        if arg_is_var(arg, var):
                                            rooted = True
                                    if ins.fn.operand.name == 'MVM_gc_root_temp_pop':
                                        if not root_stack:
                                            print("Skipping function %s because of complicated rooting" % fun.decl.name)
                                            return
                                        if arg_is_var(root_stack.pop(), var):
                                            rooted = False
                                    if ins.fn.operand.name == 'MVM_gc_root_temp_pop_n':
                                        if not root_stack or not isinstance(ins.args[1], gcc.Constant):
                                            print("Skipping function %s because of complicated rooting" % fun.decl.name)
                                            return
                                        for i in range(0, ins.args[1].constant):
                                            if arg_is_var(root_stack.pop(), var):
                                                rooted = False
                                    if initialized and not allocating_in_gen2 and ins.fn.operand.name in allocators:
                                        if ins.lhs != var and not (isinstance(ins.lhs, gcc.SsaName) and ins.lhs.var == var):
                                            if not rooted:
                                                allocated_while_not_rooted.append([ins, bb])
                                                continue
                                if ins.lhs == var:
                                    initialized = True
                                    allocated_while_not_rooted = []
                            hits = []
                            ins.walk_tree(check_var, var, hits)
                            if hits and not str(var.type) in gen2_allocated_types:
                                for missing in allocated_while_not_rooted:
                                    warning = 'Missing root for `' + var.name + '` in ' + str(missing[0]) + ' at ' + str(missing[0].loc) + ' used in ' + str(ins) + ' at ' + str(ins.loc)
                                    if not warning in warned:
                                        warned[warning] = 1
                                        print(warning)
                                    #dot = cfg_to_dot(fun.cfg)
                                    #invoke_dot(dot)

class CheckRoots(gcc.GimplePass):
    def execute(self, fun):
        # (the CFG should be set up by this point, and the GIMPLE is not yet
        # in SSA form)
        if fun and fun.cfg:
            #print(fun.decl.name + ':')
            for var in fun.decl.arguments:
                if not var.is_artificial and str(var.type) in sixmodel_types:
                    check_code_for_var(fun, var, True)
            for var in fun.local_decls:
                if not var.is_artificial and str(var.type) in sixmodel_types:
                    check_code_for_var(fun, var, False)
            check_code_for_unneeded_mvmroot(fun)
            check_code_for_imbalanced_mvmroot(fun)

ps = CheckRoots(name='check-roots')
ps.register_after('cfg')
