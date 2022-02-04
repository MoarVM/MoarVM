from __future__ import print_function
import sys

import gcc

# Need to add `-fplugin=python.so -fplugin-arg-python-script=tools/check-throw-without-free.py` to CFLAGS in the Makefile

# We'll implement this as a custom pass, to be called directly after the
# builtin 'cfg' pass, which generates the CFG:

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

#alloc_funcs = {'MVM_string_utf8_c8_encode_C_string'} # currently adding this causes a segfault in gcc when compiling src/io/filewatchers.c
alloc_funcs = {'MVM_malloc', 'MVM_calloc', 'MVM_fixed_size_allocate', 'MVM_fixed_size_allocate_zeroed', 'ANSIToUTF8', 'MVM_bytecode_dump', 'MVM_exception_backtrace_line', 'MVM_nativecall_unmarshal_string', 'MVM_serialization_read_cstr', 'MVM_spesh_dump', 'MVM_spesh_dump_arg_guard', 'MVM_spesh_dump_planned', 'MVM_spesh_dump_stats', 'MVM_staticframe_file_location', 'MVM_string_ascii_encode', 'MVM_string_ascii_encode_any', 'MVM_string_ascii_encode_substr', 'MVM_string_encode', 'MVM_string_gb18030_encode_substr', 'MVM_string_gb2312_encode_substr', 'MVM_string_latin1_encode', 'MVM_string_latin1_encode_substr', 'MVM_string_shiftjis_encode_substr', 'MVM_string_utf16_encode', 'MVM_string_utf16_encode_substr', 'MVM_string_utf16_encode_substr_main', 'MVM_string_utf16be_encode_substr', 'MVM_string_utf16le_encode_substr', 'MVM_string_utf8_c8_encode', 'MVM_string_utf8_c8_encode_substr', 'MVM_string_utf8_encode', 'MVM_string_utf8_encode_C_string', 'MVM_string_utf8_encode_substr', 'MVM_string_utf8_maybe_encode_C_string', 'MVM_string_windows1251_encode_substr', 'MVM_string_windows1252_encode_substr', 'MVM_string_windows125X_encode_substr', 'NFG_check_make_debug_string', 'NFG_checker', 'UnicodeToUTF8', 'UnicodeToUTF8', 'base64_encode', 'callback_handler', 'get_signature_char', 'twoway_memmem_uint32', 'u64toa_naive_worker'}
free_funcs = {'MVM_free', 'MVM_free_null', 'MVM_fixed_size_free', 'MVM_fixed_size_free_at_safepoint', 'free_repr_data', 'cleanup_all', 'MVM_tc_destroy'}

def check_code_for_throw_without_free(fun):
    for bb in fun.cfg.basic_blocks:
        for ins in bb.gimple:
            if isinstance(ins, gcc.GimpleCall) and isinstance(ins.fn, gcc.AddrExpr) and ins.fn.operand.name == 'MVM_exception_throw_adhoc':
                cfs = collect_control_flows(bb, [], {})

                for cf in cfs:
                    allocs = set()
                    for cf_bb in cf:
                        for cf_ins in cf_bb.gimple:
                            if isinstance(cf_ins, gcc.GimpleCall):
                                if isinstance(cf_ins.fn, gcc.AddrExpr): # plain function call is AddrExpr, other things could be function pointers
                                    if cf_ins.fn.operand.name in alloc_funcs:
                                        allocs.add(cf_ins.lhs)
                                    if cf_ins.fn.operand.name in free_funcs:
                                        if cf_ins.args[0] in allocs: # there can be false positives because the lhs of the alloc and the argument to the free can get different temp names from gcc
                                            allocs.remove(cf_ins.args[0])
                                    if cf_ins.fn.operand.name == 'MVM_exception_throw_adhoc':
                                        if allocs:
                                            print('\nPossible missing free before a throw in ' + str(fun.decl.name) + ' at ' + str(cf[-2].gimple[-1].loc) + ', things that might need to be freed: ' + str(allocs) + '\n', file=sys.stderr)

class CheckThrowWithoutFree(gcc.GimplePass):
    def execute(self, fun):
        # (the CFG should be set up by this point, and the GIMPLE is not yet
        # in SSA form)
        if fun and fun.cfg:
            check_code_for_throw_without_free(fun)

ps = CheckThrowWithoutFree(name='check-throw-without-free')
ps.register_after('cfg')
