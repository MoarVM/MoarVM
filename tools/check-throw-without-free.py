import gcc
from gccutils import get_src_for_loc, pformat, cfg_to_dot, invoke_dot


# The below diff is required to get any useful output, because otherwise MVM_exception_throw_adhoc
# causes blocks to not point to the exit, so don't have a GimpleReturn and aren't processed.
# Also, adding `-Wno-return-type -Wno-implicit-fallthrough` to CFLAGS in the Makefile helps reduce
# compiler warnings (but isn't required).

# diff --git a/src/core/exceptions.c b/src/core/exceptions.c
# index f5429fbc1..567d29c01 100644
# --- a/src/core/exceptions.c
# +++ b/src/core/exceptions.c
# @@ -873,7 +873,7 @@ MVM_NO_RETURN void MVM_oops(MVMThreadContext *tc, const char *messageFormat, ...
#  }
# 
#  /* Throws an ad-hoc (untyped) exception. */
# -MVM_NO_RETURN void MVM_exception_throw_adhoc(MVMThreadContext *tc, const char *messageFormat, ...) {
# +void MVM_exception_throw_adhoc(MVMThreadContext *tc, const char *messageFormat, ...) {
#      va_list args;
#      va_start(args, messageFormat);
#      MVM_exception_throw_adhoc_free_va(tc, NULL, messageFormat, args);
# @@ -881,13 +881,13 @@ MVM_NO_RETURN void MVM_exception_throw_adhoc(MVMThreadContext *tc, const char *m
#  }
# 
#  /* Throws an ad-hoc (untyped) exception. */
# -MVM_NO_RETURN void MVM_exception_throw_adhoc_va(MVMThreadContext *tc, const char *messageFormat, va_list args) {
# +void MVM_exception_throw_adhoc_va(MVMThreadContext *tc, const char *messageFormat, va_list args) {
#      MVM_exception_throw_adhoc_free_va(tc, NULL, messageFormat, args);
#  }
# 
#  /* Throws an ad-hoc (untyped) exception, taking a NULL-terminated array of
#   * char pointers to deallocate after message construction. */
# -MVM_NO_RETURN void MVM_exception_throw_adhoc_free(MVMThreadContext *tc, char **waste, const char *messageFormat, ...) {
# +void MVM_exception_throw_adhoc_free(MVMThreadContext *tc, char **waste, const char *messageFormat, ...) {
#      va_list args;
#      va_start(args, messageFormat);
#      MVM_exception_throw_adhoc_free_va(tc, waste, messageFormat, args);
# @@ -896,7 +896,7 @@ MVM_NO_RETURN void MVM_exception_throw_adhoc_free(MVMThreadContext *tc, char **w
# 
#  /* Throws an ad-hoc (untyped) exception, taking a NULL-terminated array of
#   * char pointers to deallocate after message construction. */
# -MVM_NO_RETURN void MVM_exception_throw_adhoc_free_va(MVMThreadContext *tc, char **waste, const char *messageFormat, va_list args) {
# +void MVM_exception_throw_adhoc_free_va(MVMThreadContext *tc, char **waste, const char *messageFormat, va_list args) {
#      LocatedHandler lh;
#      MVMException *ex;
#      /* The current frame will be assigned as the thrower of the exception, so
# @@ -944,11 +944,11 @@ MVM_NO_RETURN void MVM_exception_throw_adhoc_free_va(MVMThreadContext *tc, char
#              vfprintf(stderr, messageFormat, args);
#              fwrite("\n", 1, 1, stderr);
#              MVM_dump_backtrace(tc);
# -            abort();
# +            //abort();
#          }
#          else {
#              /* No, just the usual panic. */
# -            panic_unhandled_ex(tc, ex);
# +            //panic_unhandled_ex(tc, ex);
#          }
#      }
# 
# @@ -962,7 +962,7 @@ MVM_NO_RETURN void MVM_exception_throw_adhoc_free_va(MVMThreadContext *tc, char
#      MVM_tc_release_ex_release_mutex(tc);
# 
#      /* Jump back into the interpreter. */
# -    longjmp(tc->interp_jump, 1);
# +    //longjmp(tc->interp_jump, 1);
#  }
# 
#  void MVM_crash_on_error(void) {
# diff --git a/src/core/exceptions.h b/src/core/exceptions.h
# index 210075965..503434eb4 100644
# --- a/src/core/exceptions.h
# +++ b/src/core/exceptions.h
# @@ -90,10 +90,10 @@ void MVM_exception_resume(MVMThreadContext *tc, MVMObject *exObj);
#  MVM_PUBLIC MVM_NO_RETURN void MVM_panic_allocation_failed(size_t len) MVM_NO_RETURN_ATTRIBUTE;
#  MVM_PUBLIC MVM_NO_RETURN void MVM_panic(MVMint32 exitCode, const char *messageFormat, ...) MVM_NO_RETURN_ATTRIBUTE MVM_FORMAT(printf, 2, 3);
#  MVM_PUBLIC MVM_NO_RETURN void MVM_oops(MVMThreadContext *tc, const char *messageFormat, ...) MVM_NO_RETURN_ATTRIBUTE MVM_FORMAT(printf, 2, 3);
# -MVM_PUBLIC MVM_NO_RETURN void MVM_exception_throw_adhoc(MVMThreadContext *tc, const char *messageFormat, ...) MVM_NO_RETURN_ATTRIBUTE MVM_FORMAT(printf, 2, 3);
# -MVM_NO_RETURN void MVM_exception_throw_adhoc_va(MVMThreadContext *tc, const char *messageFormat, va_list args) MVM_NO_RETURN_ATTRIBUTE;
# -MVM_PUBLIC MVM_NO_RETURN void MVM_exception_throw_adhoc_free(MVMThreadContext *tc, char **waste, const char *messageFormat, ...) MVM_NO_RETURN_ATTRIBUTE MVM_FORMAT(printf, 3, 4);
# -MVM_NO_RETURN void MVM_exception_throw_adhoc_free_va(MVMThreadContext *tc, char **waste, const char *messageFormat, va_list args) MVM_NO_RETURN_ATTRIBUTE;
# +MVM_PUBLIC void MVM_exception_throw_adhoc(MVMThreadContext *tc, const char *messageFormat, ...) MVM_FORMAT(printf, 2, 3);
# +void MVM_exception_throw_adhoc_va(MVMThreadContext *tc, const char *messageFormat, va_list args);
# +MVM_PUBLIC void MVM_exception_throw_adhoc_free(MVMThreadContext *tc, char **waste, const char *messageFormat, ...) MVM_FORMAT(printf, 3, 4);
# +void MVM_exception_throw_adhoc_free_va(MVMThreadContext *tc, char **waste, const char *messageFormat, va_list args);
#  MVM_PUBLIC void MVM_crash_on_error(void);
#  char * MVM_exception_backtrace_line(MVMThreadContext *tc, MVMFrame *cur_frame, MVMuint16 not_top, MVMuint8 *throw_address);
#  MVMint32 MVM_get_exception_category(MVMThreadContext *tc, MVMObject *ex);
# diff --git a/src/strings/uthash.h b/src/strings/uthash.h
# index 7186c87e9..bbb3b4d34 100644
# --- a/src/strings/uthash.h
# +++ b/src/strings/uthash.h
# @@ -223,7 +223,7 @@ MVM_STATIC_INLINE MVMuint32 ptr_hash_32_to_32(MVMuint32 u) {
#  #ifndef ROTL
#  #define ROTL(x, b) ( ((x) << (b)) | ( (x) >> ((sizeof(x)*8) - (b))) )
#  #endif
# -MVM_PUBLIC MVM_NO_RETURN void MVM_exception_throw_adhoc(MVMThreadContext *tc, const char *messageFormat, ...) MVM_NO_RETURN_ATTRIBUTE MVM_FORMAT(printf, 2, 3);
# +MVM_PUBLIC void MVM_exception_throw_adhoc(MVMThreadContext *tc, const char *messageFormat, ...) MVM_FORMAT(printf, 2, 3);
#  MVM_STATIC_INLINE void HASH_MAKE_TABLE(MVMThreadContext *tc, void *head, UT_hash_handle *head_hh) {
#      head_hh->tbl = (UT_hash_table*)uthash_malloc_zeroed(tc, sizeof(UT_hash_table));
#      head_hh->tbl->num_buckets = HASH_INITIAL_NUM_BUCKETS;


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
alloc_funcs = {'MVM_malloc', 'MVM_calloc', 'MVM_fixed_size_allocate', 'MVM_fixed_size_allocate_zeroed', 'ANSIToUTF8', 'MVM_bytecode_dump', 'MVM_exception_backtrace_line', 'MVM_nativecall_unmarshal_string', 'MVM_reg_get_debug_name', 'MVM_serialization_read_cstr', 'MVM_spesh_dump', 'MVM_spesh_dump_arg_guard', 'MVM_spesh_dump_planned', 'MVM_spesh_dump_stats', 'MVM_staticframe_file_location', 'MVM_string_ascii_encode', 'MVM_string_ascii_encode_any', 'MVM_string_ascii_encode_substr', 'MVM_string_encode', 'MVM_string_encoding_cname', 'MVM_string_gb18030_encode_substr', 'MVM_string_gb2312_encode_substr', 'MVM_string_latin1_encode', 'MVM_string_latin1_encode_substr', 'MVM_string_shiftjis_encode_substr', 'MVM_string_utf16_encode', 'MVM_string_utf16_encode_substr', 'MVM_string_utf16_encode_substr_main', 'MVM_string_utf16be_encode_substr', 'MVM_string_utf16le_encode_substr', 'MVM_string_utf8_c8_encode', 'MVM_string_utf8_c8_encode_substr', 'MVM_string_utf8_encode', 'MVM_string_utf8_encode_C_string', 'MVM_string_utf8_encode_substr', 'MVM_string_utf8_maybe_encode_C_string', 'MVM_string_windows1251_encode_substr', 'MVM_string_windows1252_encode_substr', 'MVM_string_windows125X_encode_substr', 'NFG_check_make_debug_string', 'NFG_checker', 'UnicodeToUTF8', 'UnicodeToUTF8', 'base64_encode', 'callback_handler', 'get_signature_char', 'twoway_memmem_uint32', 'u64toa_naive_worker'}
free_funcs = {'MVM_free', 'MVM_free_null', 'MVM_fixed_size_free', 'MVM_fixed_size_free_at_safepoint'}

def check_code_for_throw_without_free(fun):
    for bb in fun.cfg.basic_blocks:
        for ins in bb.gimple:
            if isinstance(ins, gcc.GimpleReturn):
                cfs = collect_control_flows(bb, [], {})

                for cf in cfs:
                    allocs = set()
                    for bb in cf:
                        for ins in bb.gimple:
                            if isinstance(ins, gcc.GimpleCall):
                                if isinstance(ins.fn, gcc.AddrExpr): # plain function call is AddrExpr, other things could be function pointers
                                    if ins.fn.operand.name in alloc_funcs:
                                        allocs.add(ins.lhs)
                                    if ins.fn.operand.name in free_funcs:
                                        if ins.args[0] in allocs: # there can be false positives because the lhs of the alloc and the argument to the free can get different temp names from gcc
                                            allocs.remove(ins.args[0])
                                    if ins.fn.operand.name == 'MVM_exception_throw_adhoc':
                                        if allocs:
                                            print('\nPossible missing free before a throw in ' + str(fun.decl.name) + ' at ' + str(cf[-2].gimple[-1].loc) + ', things that might need to be freed: ' + str(allocs) + '\n')

class CheckThrowWithoutFree(gcc.GimplePass):
    def execute(self, fun):
        # (the CFG should be set up by this point, and the GIMPLE is not yet
        # in SSA form)
        if fun and fun.cfg:
            check_code_for_throw_without_free(fun)

ps = CheckThrowWithoutFree(name='check-throw-without-free')
ps.register_after('cfg')
