#include "moar.h"
#include "math.h"
#include "platform/time.h"

/* Macros for getting things from the bytecode stream. */
#define GET_REG(pc, idx)    reg_base[*((MVMuint16 *)(pc + idx))]
#define GET_LEX(pc, idx, f) f->env[*((MVMuint16 *)(pc + idx))]
#define GET_I16(pc, idx)    *((MVMint16 *)(pc + idx))
#define GET_UI16(pc, idx)   *((MVMuint16 *)(pc + idx))
#define GET_I32(pc, idx)    *((MVMint32 *)(pc + idx))
#define GET_UI32(pc, idx)   *((MVMuint32 *)(pc + idx))
#define GET_N32(pc, idx)    *((MVMnum32 *)(pc + idx))

#define NEXT_OP (op = *(MVMuint16 *)(cur_op), cur_op += 2, op)

#if MVM_CGOTO
#define DISPATCH(op)
#define OP(name) OP_ ## name
#define NEXT *LABELS[NEXT_OP]
#else
#define DISPATCH(op) switch (op)
#define OP(name) case MVM_OP_ ## name
#define NEXT runloop
#endif

static int tracing_enabled = 0;

/* This is the interpreter run loop. We have one of these per thread. */
void MVM_interp_run(MVMThreadContext *tc, void (*initial_invoke)(MVMThreadContext *, void *), void *invoke_data) {
#if MVM_CGOTO
#include "oplabels.h"
#endif

    /* Points to the current opcode. */
    MVMuint8 *cur_op = NULL;

    /* The current frame's bytecode start. */
    MVMuint8 *bytecode_start = NULL;

    /* Points to the base of the current register set for the frame we
     * are presently in. */
    MVMRegister *reg_base = NULL;

    /* Points to the current compilation unit. */
    MVMCompUnit *cu = NULL;

    /* The current call site we're constructing. */
    MVMCallsite *cur_callsite = NULL;

    /* Stash addresses of current op, register base and SC deref base
     * in the TC; this will be used by anything that needs to switch
     * the current place we're interpreting. */
    tc->interp_cur_op         = &cur_op;
    tc->interp_bytecode_start = &bytecode_start;
    tc->interp_reg_base       = &reg_base;
    tc->interp_cu             = &cu;

    /* With everything set up, do the initial invocation (exactly what this does
     * varies depending on if this is starting a new thread or is the top-level
     * program entry point). */
    initial_invoke(tc, invoke_data);

    /* Set jump point, for if we arrive back in the interpreter from an
     * exception thrown from C code. */
    setjmp(tc->interp_jump);

    /* Enter runloop. */
    runloop: {
        MVMuint16 op;

#if MVM_TRACING
        if (tracing_enabled) {
            char *trace_line;
            tc->cur_frame->throw_address = cur_op;
            trace_line = MVM_exception_backtrace_line(tc, tc->cur_frame, 0);
            fprintf(stderr, "Op %d%s\n", (int)*((MVMuint16 *)cur_op), trace_line);
            /* slow tracing is slow. Feel free to speed it. */
            MVM_free(trace_line);
        }
#endif

        DISPATCH(NEXT_OP) {
            OP(no_op):
                goto NEXT;
            OP(goto):
                cur_op = bytecode_start + GET_UI32(cur_op, 0);
                GC_SYNC_POINT(tc);
                goto NEXT;
            OP(if_i):
                if (GET_REG(cur_op, 0).i64)
                    cur_op = bytecode_start + GET_UI32(cur_op, 2);
                else
                    cur_op += 6;
                GC_SYNC_POINT(tc);
                goto NEXT;
            OP(unless_i):
                if (GET_REG(cur_op, 0).i64)
                    cur_op += 6;
                else
                    cur_op = bytecode_start + GET_UI32(cur_op, 2);
                GC_SYNC_POINT(tc);
                goto NEXT;
            OP(if_n):
                if (GET_REG(cur_op, 0).n64 != 0.0)
                    cur_op = bytecode_start + GET_UI32(cur_op, 2);
                else
                    cur_op += 6;
                GC_SYNC_POINT(tc);
                goto NEXT;
            OP(unless_n):
                if (GET_REG(cur_op, 0).n64 != 0.0)
                    cur_op += 6;
                else
                    cur_op = bytecode_start + GET_UI32(cur_op, 2);
                GC_SYNC_POINT(tc);
                goto NEXT;
            OP(if_s): {
                MVMString *str = GET_REG(cur_op, 0).s;
                if (!str || MVM_string_graphs(tc, str) == 0)
                    cur_op += 6;
                else
                    cur_op = bytecode_start + GET_UI32(cur_op, 2);
                GC_SYNC_POINT(tc);
                goto NEXT;
            }
            OP(unless_s): {
                MVMString *str = GET_REG(cur_op, 0).s;
                if (!str || MVM_string_graphs(tc, str) == 0)
                    cur_op = bytecode_start + GET_UI32(cur_op, 2);
                else
                    cur_op += 6;
                GC_SYNC_POINT(tc);
                goto NEXT;
            }
            OP(if_s0): {
                MVMString *str = GET_REG(cur_op, 0).s;
                if (!MVM_coerce_istrue_s(tc, str))
                    cur_op += 6;
                else
                    cur_op = bytecode_start + GET_UI32(cur_op, 2);
                GC_SYNC_POINT(tc);
                goto NEXT;
            }
            OP(unless_s0): {
                MVMString *str = GET_REG(cur_op, 0).s;
                if (!MVM_coerce_istrue_s(tc, str))
                    cur_op = bytecode_start + GET_UI32(cur_op, 2);
                else
                    cur_op += 6;
                GC_SYNC_POINT(tc);
                goto NEXT;
            }
            OP(if_o):
                GC_SYNC_POINT(tc);
                MVM_coerce_istrue(tc, GET_REG(cur_op, 0).o, NULL,
                    bytecode_start + GET_UI32(cur_op, 2),
                    cur_op + 6,
                    0);
                goto NEXT;
            OP(unless_o):
                GC_SYNC_POINT(tc);
                MVM_coerce_istrue(tc, GET_REG(cur_op, 0).o, NULL,
                    bytecode_start + GET_UI32(cur_op, 2),
                    cur_op + 6,
                    1);
                goto NEXT;
            OP(extend_u8):
            OP(extend_u16):
            OP(extend_u32):
            OP(extend_i8):
            OP(extend_i16):
            OP(extend_i32):
            OP(trunc_u8):
            OP(trunc_u16):
            OP(trunc_u32):
            OP(trunc_i8):
            OP(trunc_i16):
            OP(trunc_i32):
            OP(extend_n32):
            OP(trunc_n32):
                MVM_exception_throw_adhoc(tc, "extend/trunc NYI");
            OP(set):
                GET_REG(cur_op, 0) = GET_REG(cur_op, 2);
                cur_op += 4;
                goto NEXT;
            OP(getlex): {
                MVMFrame    *f = tc->cur_frame;
                MVMuint16    outers = GET_UI16(cur_op, 4);
                MVMRegister  found;
                while (outers) {
                    if (!f)
                        MVM_exception_throw_adhoc(tc, "getlex: outer index out of range");
                    f = f->outer;
                    outers--;
                }
                GET_REG(cur_op, 0) = found = GET_LEX(cur_op, 2, f);
                if (found.o == NULL) {
                    MVMuint16 idx = GET_UI16(cur_op, 2);
                    MVMuint16 *lexical_types = f->spesh_cand && f->spesh_cand->lexical_types
                        ? f->spesh_cand->lexical_types
                        : f->static_info->body.lexical_types;
                    if (lexical_types[idx] == MVM_reg_obj)
                        GET_REG(cur_op, 0).o = MVM_frame_vivify_lexical(tc, f, idx);
                }
                cur_op += 6;
                goto NEXT;
            }
            OP(bindlex): {
                MVMFrame *f = tc->cur_frame;
                MVMuint16 outers = GET_UI16(cur_op, 2);
                while (outers) {
                    if (!f)
                        MVM_exception_throw_adhoc(tc, "bindlex: outer index out of range");
                    f = f->outer;
                    outers--;
                }
                GET_LEX(cur_op, 0, f) = GET_REG(cur_op, 4);
                cur_op += 6;
                goto NEXT;
            }
            OP(getlex_ni):
                GET_REG(cur_op, 0).i64 = MVM_frame_find_lexical_by_name(tc,
                    cu->body.strings[GET_UI32(cur_op, 2)], MVM_reg_int64)->i64;
                cur_op += 6;
                goto NEXT;
            OP(getlex_nn):
                GET_REG(cur_op, 0).n64 = MVM_frame_find_lexical_by_name(tc,
                    cu->body.strings[GET_UI32(cur_op, 2)], MVM_reg_num64)->n64;
                cur_op += 6;
                goto NEXT;
            OP(getlex_ns):
                GET_REG(cur_op, 0).s = MVM_frame_find_lexical_by_name(tc,
                    cu->body.strings[GET_UI32(cur_op, 2)], MVM_reg_str)->s;
                cur_op += 6;
                goto NEXT;
            OP(getlex_no): {
                MVMRegister *found = MVM_frame_find_lexical_by_name(tc,
                    cu->body.strings[GET_UI32(cur_op, 2)], MVM_reg_obj);
                GET_REG(cur_op, 0).o = found ? found->o : tc->instance->VMNull;
                cur_op += 6;
                goto NEXT;
            }
            OP(bindlex_ni):
                MVM_frame_find_lexical_by_name(tc, cu->body.strings[GET_UI32(cur_op, 0)],
                    MVM_reg_int64)->i64 = GET_REG(cur_op, 4).i64;
                cur_op += 6;
                goto NEXT;
            OP(bindlex_nn):
                MVM_frame_find_lexical_by_name(tc, cu->body.strings[GET_UI32(cur_op, 0)],
                    MVM_reg_num64)->n64 = GET_REG(cur_op, 4).n64;
                cur_op += 6;
                goto NEXT;
            OP(bindlex_ns):
                MVM_frame_find_lexical_by_name(tc, cu->body.strings[GET_UI32(cur_op, 0)],
                    MVM_reg_str)->s = GET_REG(cur_op, 4).s;
                cur_op += 6;
                goto NEXT;
            OP(bindlex_no): {
                MVMString *str = cu->body.strings[GET_UI32(cur_op, 0)];
                MVMRegister *r = MVM_frame_find_lexical_by_name(tc, str, MVM_reg_obj);
                if (r)
                    r->o = GET_REG(cur_op, 4).o;
                else
                    MVM_exception_throw_adhoc(tc, "Cannot bind to non-existing object lexical '%s'",
                        MVM_string_utf8_encode_C_string(tc, str));
                cur_op += 6;
                goto NEXT;
            }
            OP(getlex_ng):
            OP(bindlex_ng):
                MVM_exception_throw_adhoc(tc, "get/bindlex_ng NYI");
            OP(return_i):
                MVM_args_set_result_int(tc, GET_REG(cur_op, 0).i64,
                    MVM_RETURN_CALLER_FRAME);
                if (MVM_frame_try_return(tc) == 0)
                    goto return_label;
                goto NEXT;
            OP(return_n):
                MVM_args_set_result_num(tc, GET_REG(cur_op, 0).n64,
                    MVM_RETURN_CALLER_FRAME);
                if (MVM_frame_try_return(tc) == 0)
                    goto return_label;
                goto NEXT;
            OP(return_s):
                MVM_args_set_result_str(tc, GET_REG(cur_op, 0).s,
                    MVM_RETURN_CALLER_FRAME);
                if (MVM_frame_try_return(tc) == 0)
                    goto return_label;
                goto NEXT;
            OP(return_o):
                MVM_args_set_result_obj(tc, GET_REG(cur_op, 0).o,
                    MVM_RETURN_CALLER_FRAME);
                if (MVM_frame_try_return(tc) == 0)
                    goto return_label;
                goto NEXT;
            OP(return):
                MVM_args_assert_void_return_ok(tc, MVM_RETURN_CALLER_FRAME);
                if (MVM_frame_try_return(tc) == 0)
                    goto return_label;
                goto NEXT;
            OP(const_i8):
            OP(const_i16):
            OP(const_i32):
                MVM_exception_throw_adhoc(tc, "const_iX NYI");
            OP(const_i64):
                GET_REG(cur_op, 0).i64 = MVM_BC_get_I64(cur_op, 2);
                cur_op += 10;
                goto NEXT;
            OP(const_n32):
                MVM_exception_throw_adhoc(tc, "const_n32 NYI");
            OP(const_n64):
                GET_REG(cur_op, 0).n64 = MVM_BC_get_N64(cur_op, 2);
                cur_op += 10;
                goto NEXT;
            OP(const_s):
                GET_REG(cur_op, 0).s = cu->body.strings[GET_UI32(cur_op, 2)];
                cur_op += 6;
                goto NEXT;
            OP(add_i):
                GET_REG(cur_op, 0).i64 = GET_REG(cur_op, 2).i64 + GET_REG(cur_op, 4).i64;
                cur_op += 6;
                goto NEXT;
            OP(sub_i):
                GET_REG(cur_op, 0).i64 = GET_REG(cur_op, 2).i64 - GET_REG(cur_op, 4).i64;
                cur_op += 6;
                goto NEXT;
            OP(mul_i):
                GET_REG(cur_op, 0).i64 = GET_REG(cur_op, 2).i64 * GET_REG(cur_op, 4).i64;
                cur_op += 6;
                goto NEXT;
            OP(div_i): {
                int num   = GET_REG(cur_op, 2).i64;
                int denom = GET_REG(cur_op, 4).i64;
                // if we have a negative result, make sure we floor rather
                // than rounding towards zero.
                if (denom == 0)
                    MVM_exception_throw_adhoc(tc, "Division by zero");
                if ((num < 0) ^ (denom < 0)) {
                    if ((num % denom) != 0) {
                        GET_REG(cur_op, 0).i64 = num / denom - 1;
                    } else {
                        GET_REG(cur_op, 0).i64 = num / denom;
                    }
                } else {
                    GET_REG(cur_op, 0).i64 = num / denom;
                }
                cur_op += 6;
                goto NEXT;
            }
            OP(div_u):
                GET_REG(cur_op, 0).ui64 = GET_REG(cur_op, 2).ui64 / GET_REG(cur_op, 4).ui64;
                cur_op += 6;
                goto NEXT;
            OP(mod_i): {
                MVMint64 numer = GET_REG(cur_op, 2).i64;
                MVMint64 denom = GET_REG(cur_op, 4).i64;
                if (denom == 0)
                    MVM_exception_throw_adhoc(tc, "Modulation by zero");
                GET_REG(cur_op, 0).i64 = numer % denom;
                cur_op += 6;
                goto NEXT;
            }
            OP(mod_u):
                GET_REG(cur_op, 0).ui64 = GET_REG(cur_op, 2).ui64 % GET_REG(cur_op, 4).ui64;
                cur_op += 6;
                goto NEXT;
            OP(neg_i):
                GET_REG(cur_op, 0).i64 = -GET_REG(cur_op, 2).i64;
                cur_op += 4;
                goto NEXT;
            OP(abs_i): {
                MVMint64 v = GET_REG(cur_op, 2).i64, mask = v >> 63;
                GET_REG(cur_op, 0).i64 = (v + mask) ^ mask;
                cur_op += 4;
                goto NEXT;
            }
            OP(inc_i):
                GET_REG(cur_op, 0).i64++;
                cur_op += 2;
                goto NEXT;
            OP(inc_u):
                GET_REG(cur_op, 0).ui64++;
                cur_op += 2;
                goto NEXT;
            OP(dec_i):
                GET_REG(cur_op, 0).i64--;
                cur_op += 2;
                goto NEXT;
            OP(dec_u):
                GET_REG(cur_op, 0).ui64--;
                cur_op += 2;
                goto NEXT;
            OP(getcode):
                GET_REG(cur_op, 0).o = cu->body.coderefs[GET_UI16(cur_op, 2)];
                cur_op += 4;
                goto NEXT;
            OP(prepargs):
                cur_callsite = MVM_args_prepare(tc, cu, GET_UI16(cur_op, 0));
                cur_op += 2;
                goto NEXT;
            OP(arg_i):
                tc->cur_frame->args[GET_UI16(cur_op, 0)].i64 = GET_REG(cur_op, 2).i64;
                cur_op += 4;
                goto NEXT;
            OP(arg_n):
                tc->cur_frame->args[GET_UI16(cur_op, 0)].n64 = GET_REG(cur_op, 2).n64;
                cur_op += 4;
                goto NEXT;
            OP(arg_s):
                tc->cur_frame->args[GET_UI16(cur_op, 0)].s = GET_REG(cur_op, 2).s;
                cur_op += 4;
                goto NEXT;
            OP(arg_o):
                tc->cur_frame->args[GET_UI16(cur_op, 0)].o = GET_REG(cur_op, 2).o;
                cur_op += 4;
                goto NEXT;
            OP(invoke_v):
                {
                    MVMObject   *code = GET_REG(cur_op, 0).o;
                    MVMRegister *args = tc->cur_frame->args;
                    code = MVM_frame_find_invokee_multi_ok(tc, code, &cur_callsite, args);
                    tc->cur_frame->return_value = NULL;
                    tc->cur_frame->return_type = MVM_RETURN_VOID;
                    cur_op += 2;
                    tc->cur_frame->return_address = cur_op;
                    STABLE(code)->invoke(tc, code, cur_callsite, args);
                }
                goto NEXT;
            OP(invoke_i):
                {
                    MVMObject   *code = GET_REG(cur_op, 2).o;
                    MVMRegister *args = tc->cur_frame->args;
                    code = MVM_frame_find_invokee_multi_ok(tc, code, &cur_callsite, args);
                    tc->cur_frame->return_value = &GET_REG(cur_op, 0);
                    tc->cur_frame->return_type = MVM_RETURN_INT;
                    cur_op += 4;
                    tc->cur_frame->return_address = cur_op;
                    STABLE(code)->invoke(tc, code, cur_callsite, args);
                }
                goto NEXT;
            OP(invoke_n):
                {
                    MVMObject   *code = GET_REG(cur_op, 2).o;
                    MVMRegister *args = tc->cur_frame->args;
                    code = MVM_frame_find_invokee_multi_ok(tc, code, &cur_callsite, args);
                    tc->cur_frame->return_value = &GET_REG(cur_op, 0);
                    tc->cur_frame->return_type = MVM_RETURN_NUM;
                    cur_op += 4;
                    tc->cur_frame->return_address = cur_op;
                    STABLE(code)->invoke(tc, code, cur_callsite, args);
                }
                goto NEXT;
            OP(invoke_s):
                {
                    MVMObject   *code = GET_REG(cur_op, 2).o;
                    MVMRegister *args = tc->cur_frame->args;
                    code = MVM_frame_find_invokee_multi_ok(tc, code, &cur_callsite, args);
                    tc->cur_frame->return_value = &GET_REG(cur_op, 0);
                    tc->cur_frame->return_type = MVM_RETURN_STR;
                    cur_op += 4;
                    tc->cur_frame->return_address = cur_op;
                    STABLE(code)->invoke(tc, code, cur_callsite, args);
                }
                goto NEXT;
            OP(invoke_o):
                {
                    MVMObject   *code = GET_REG(cur_op, 2).o;
                    MVMRegister *args = tc->cur_frame->args;
                    code = MVM_frame_find_invokee_multi_ok(tc, code, &cur_callsite, args);
                    tc->cur_frame->return_value = &GET_REG(cur_op, 0);
                    tc->cur_frame->return_type = MVM_RETURN_OBJ;
                    cur_op += 4;
                    tc->cur_frame->return_address = cur_op;
                    STABLE(code)->invoke(tc, code, cur_callsite, args);
                }
                goto NEXT;
            OP(add_n):
                GET_REG(cur_op, 0).n64 = GET_REG(cur_op, 2).n64 + GET_REG(cur_op, 4).n64;
                cur_op += 6;
                goto NEXT;
            OP(sub_n):
                GET_REG(cur_op, 0).n64 = GET_REG(cur_op, 2).n64 - GET_REG(cur_op, 4).n64;
                cur_op += 6;
                goto NEXT;
            OP(mul_n):
                GET_REG(cur_op, 0).n64 = GET_REG(cur_op, 2).n64 * GET_REG(cur_op, 4).n64;
                cur_op += 6;
                goto NEXT;
            OP(div_n):
                GET_REG(cur_op, 0).n64 = GET_REG(cur_op, 2).n64 / GET_REG(cur_op, 4).n64;
                cur_op += 6;
                goto NEXT;
            OP(mod_n): {
                MVMnum64 a = GET_REG(cur_op, 2).n64;
                MVMnum64 b = GET_REG(cur_op, 4).n64;
                GET_REG(cur_op, 0).n64 = b == 0 ? a : a - b * floor(a / b);
                cur_op += 6;
                goto NEXT;
            }
            OP(neg_n):
                GET_REG(cur_op, 0).n64 = -GET_REG(cur_op, 2).n64;
                cur_op += 4;
                goto NEXT;
            OP(abs_n):
                {
                    MVMnum64 num = GET_REG(cur_op, 2).n64;
                    if (num < 0) num = num * -1;
                    GET_REG(cur_op, 0).n64 = num;
                    cur_op += 4;
                }
                goto NEXT;
            OP(eq_i):
                GET_REG(cur_op, 0).i64 = GET_REG(cur_op, 2).i64 == GET_REG(cur_op, 4).i64;
                cur_op += 6;
                goto NEXT;
            OP(ne_i):
                GET_REG(cur_op, 0).i64 = GET_REG(cur_op, 2).i64 != GET_REG(cur_op, 4).i64;
                cur_op += 6;
                goto NEXT;
            OP(lt_i):
                GET_REG(cur_op, 0).i64 = GET_REG(cur_op, 2).i64 <  GET_REG(cur_op, 4).i64;
                cur_op += 6;
                goto NEXT;
            OP(le_i):
                GET_REG(cur_op, 0).i64 = GET_REG(cur_op, 2).i64 <= GET_REG(cur_op, 4).i64;
                cur_op += 6;
                goto NEXT;
            OP(gt_i):
                GET_REG(cur_op, 0).i64 = GET_REG(cur_op, 2).i64 >  GET_REG(cur_op, 4).i64;
                cur_op += 6;
                goto NEXT;
            OP(ge_i):
                GET_REG(cur_op, 0).i64 = GET_REG(cur_op, 2).i64 >= GET_REG(cur_op, 4).i64;
                cur_op += 6;
                goto NEXT;
            OP(eq_n):
                GET_REG(cur_op, 0).i64 = GET_REG(cur_op, 2).n64 == GET_REG(cur_op, 4).n64;
                cur_op += 6;
                goto NEXT;
            OP(ne_n):
                GET_REG(cur_op, 0).i64 = GET_REG(cur_op, 2).n64 != GET_REG(cur_op, 4).n64;
                cur_op += 6;
                goto NEXT;
            OP(lt_n):
                GET_REG(cur_op, 0).i64 = GET_REG(cur_op, 2).n64 <  GET_REG(cur_op, 4).n64;
                cur_op += 6;
                goto NEXT;
            OP(le_n):
                GET_REG(cur_op, 0).i64 = GET_REG(cur_op, 2).n64 <= GET_REG(cur_op, 4).n64;
                cur_op += 6;
                goto NEXT;
            OP(gt_n):
                GET_REG(cur_op, 0).i64 = GET_REG(cur_op, 2).n64 >  GET_REG(cur_op, 4).n64;
                cur_op += 6;
                goto NEXT;
            OP(ge_n):
                GET_REG(cur_op, 0).i64 = GET_REG(cur_op, 2).n64 >= GET_REG(cur_op, 4).n64;
                cur_op += 6;
                goto NEXT;
            OP(argconst_i):
                tc->cur_frame->args[GET_UI16(cur_op, 0)].i64 = MVM_BC_get_I64(cur_op, 2);
                cur_op += 10;
                goto NEXT;
            OP(argconst_n):
                tc->cur_frame->args[GET_UI16(cur_op, 0)].n64 = MVM_BC_get_N64(cur_op, 2);
                cur_op += 10;
                goto NEXT;
            OP(argconst_s):
                tc->cur_frame->args[GET_UI16(cur_op, 0)].s = cu->body.strings[GET_UI32(cur_op, 2)];
                cur_op += 6;
                goto NEXT;
            OP(checkarity):
                MVM_args_checkarity(tc, &tc->cur_frame->params, GET_UI16(cur_op, 0), GET_UI16(cur_op, 2));
                cur_op += 4;
                goto NEXT;
            OP(param_rp_i):
                GET_REG(cur_op, 0).i64 = MVM_args_get_pos_int(tc, &tc->cur_frame->params,
                    GET_UI16(cur_op, 2), MVM_ARG_REQUIRED).arg.i64;
                cur_op += 4;
                goto NEXT;
            OP(param_rp_n):
                GET_REG(cur_op, 0).n64 = MVM_args_get_pos_num(tc, &tc->cur_frame->params,
                    GET_UI16(cur_op, 2), MVM_ARG_REQUIRED).arg.n64;
                cur_op += 4;
                goto NEXT;
            OP(param_rp_s):
                GET_REG(cur_op, 0).s = MVM_args_get_pos_str(tc, &tc->cur_frame->params,
                    GET_UI16(cur_op, 2), MVM_ARG_REQUIRED).arg.s;
                cur_op += 4;
                goto NEXT;
            OP(param_rp_o):
                GET_REG(cur_op, 0).o = MVM_args_get_pos_obj(tc, &tc->cur_frame->params,
                    GET_UI16(cur_op, 2), MVM_ARG_REQUIRED).arg.o;
                cur_op += 4;
                goto NEXT;
            OP(param_op_i):
            {
                MVMArgInfo param = MVM_args_get_pos_int(tc, &tc->cur_frame->params,
                    GET_UI16(cur_op, 2), MVM_ARG_OPTIONAL);
                if (param.exists) {
                    GET_REG(cur_op, 0).i64 = param.arg.i64;
                    cur_op = bytecode_start + GET_UI32(cur_op, 4);
                }
                else {
                    cur_op += 8;
                }
                goto NEXT;
            }
            OP(param_op_n):
            {
                MVMArgInfo param = MVM_args_get_pos_num(tc, &tc->cur_frame->params,
                    GET_UI16(cur_op, 2), MVM_ARG_OPTIONAL);
                if (param.exists) {
                    GET_REG(cur_op, 0).n64 = param.arg.n64;
                    cur_op = bytecode_start + GET_UI32(cur_op, 4);
                }
                else {
                    cur_op += 8;
                }
                goto NEXT;
            }
            OP(param_op_s):
            {
                MVMArgInfo param = MVM_args_get_pos_str(tc, &tc->cur_frame->params,
                    GET_UI16(cur_op, 2), MVM_ARG_OPTIONAL);
                if (param.exists) {
                    GET_REG(cur_op, 0).s = param.arg.s;
                    cur_op = bytecode_start + GET_UI32(cur_op, 4);
                }
                else {
                    cur_op += 8;
                }
                goto NEXT;
            }
            OP(param_op_o):
            {
                MVMArgInfo param = MVM_args_get_pos_obj(tc, &tc->cur_frame->params,
                    GET_UI16(cur_op, 2), MVM_ARG_OPTIONAL);
                if (param.exists) {
                    GET_REG(cur_op, 0).o = param.arg.o;
                    cur_op = bytecode_start + GET_UI32(cur_op, 4);
                }
                else {
                    cur_op += 8;
                }
                goto NEXT;
            }
            OP(param_rn_i):
                GET_REG(cur_op, 0).i64 = MVM_args_get_named_int(tc, &tc->cur_frame->params,
                    cu->body.strings[GET_UI32(cur_op, 2)], MVM_ARG_REQUIRED).arg.i64;
                cur_op += 6;
                goto NEXT;
            OP(param_rn_n):
                GET_REG(cur_op, 0).n64 = MVM_args_get_named_num(tc, &tc->cur_frame->params,
                    cu->body.strings[GET_UI32(cur_op, 2)], MVM_ARG_REQUIRED).arg.n64;
                cur_op += 6;
                goto NEXT;
            OP(param_rn_s):
                GET_REG(cur_op, 0).s = MVM_args_get_named_str(tc, &tc->cur_frame->params,
                    cu->body.strings[GET_UI32(cur_op, 2)], MVM_ARG_REQUIRED).arg.s;
                cur_op += 6;
                goto NEXT;
            OP(param_rn_o):
                GET_REG(cur_op, 0).o = MVM_args_get_named_obj(tc, &tc->cur_frame->params,
                    cu->body.strings[GET_UI32(cur_op, 2)], MVM_ARG_REQUIRED).arg.o;
                cur_op += 6;
                goto NEXT;
            OP(param_on_i):
            {
                MVMArgInfo param = MVM_args_get_named_int(tc, &tc->cur_frame->params,
                    cu->body.strings[GET_UI32(cur_op, 2)], MVM_ARG_OPTIONAL);
                if (param.exists) {
                    GET_REG(cur_op, 0).i64 = param.arg.i64;
                    cur_op = bytecode_start + GET_UI32(cur_op, 6);
                }
                else {
                    cur_op += 10;
                }
                goto NEXT;
            }
            OP(param_on_n):
            {
                MVMArgInfo param = MVM_args_get_named_num(tc, &tc->cur_frame->params,
                    cu->body.strings[GET_UI32(cur_op, 2)], MVM_ARG_OPTIONAL);
                if (param.exists) {
                    GET_REG(cur_op, 0).n64 = param.arg.n64;
                    cur_op = bytecode_start + GET_UI32(cur_op, 6);
                }
                else {
                    cur_op += 10;
                }
                goto NEXT;
            }
            OP(param_on_s):
            {
                MVMArgInfo param = MVM_args_get_named_str(tc, &tc->cur_frame->params,
                    cu->body.strings[GET_UI32(cur_op, 2)], MVM_ARG_OPTIONAL);
                if (param.exists) {
                    GET_REG(cur_op, 0).s = param.arg.s;
                    cur_op = bytecode_start + GET_UI32(cur_op, 6);
                }
                else {
                    cur_op += 10;
                }
                goto NEXT;
            }
            OP(param_on_o):
            {
                MVMArgInfo param = MVM_args_get_named_obj(tc, &tc->cur_frame->params,
                    cu->body.strings[GET_UI32(cur_op, 2)], MVM_ARG_OPTIONAL);
                if (param.exists) {
                    GET_REG(cur_op, 0).o = param.arg.o;
                    cur_op = bytecode_start + GET_UI32(cur_op, 6);
                }
                else {
                    cur_op += 10;
                }
                goto NEXT;
            }
            OP(coerce_in):
                GET_REG(cur_op, 0).n64 = (MVMnum64)GET_REG(cur_op, 2).i64;
                cur_op += 4;
                goto NEXT;
            OP(coerce_ni):
                GET_REG(cur_op, 0).i64 = (MVMint64)GET_REG(cur_op, 2).n64;
                cur_op += 4;
                goto NEXT;
            OP(band_i):
                GET_REG(cur_op, 0).i64 = GET_REG(cur_op, 2).i64 & GET_REG(cur_op, 4).i64;
                cur_op += 6;
                goto NEXT;
            OP(bor_i):
                GET_REG(cur_op, 0).i64 = GET_REG(cur_op, 2).i64 | GET_REG(cur_op, 4).i64;
                cur_op += 6;
                goto NEXT;
            OP(bxor_i):
                GET_REG(cur_op, 0).i64 = GET_REG(cur_op, 2).i64 ^ GET_REG(cur_op, 4).i64;
                cur_op += 6;
                goto NEXT;
            OP(bnot_i):
                GET_REG(cur_op, 0).i64 = ~GET_REG(cur_op, 2).i64;
                cur_op += 4;
                goto NEXT;
            OP(blshift_i):
                GET_REG(cur_op, 0).i64 = GET_REG(cur_op, 2).i64 << GET_REG(cur_op, 4).i64;
                cur_op += 6;
                goto NEXT;
            OP(brshift_i):
                GET_REG(cur_op, 0).i64 = GET_REG(cur_op, 2).i64 >> GET_REG(cur_op, 4).i64;
                cur_op += 6;
                goto NEXT;
            OP(pow_i): {
                    MVMint64 base = GET_REG(cur_op, 2).i64;
                    MVMint64 exp = GET_REG(cur_op, 4).i64;
                    MVMint64 result = 1;
                    /* "Exponentiation by squaring" */
                    if (exp < 0) {
                        result = 0; /* because 1/base**-exp is between 0 and 1 */
                    }
                    else {
                        while (exp) {
                            if (exp & 1)
                                result *= base;
                            exp >>= 1;
                            base *= base;
                        }
                    }
                    GET_REG(cur_op, 0).i64 = result;
                }
                cur_op += 6;
                goto NEXT;
            OP(pow_n):
                GET_REG(cur_op, 0).n64 = pow(GET_REG(cur_op, 2).n64, GET_REG(cur_op, 4).n64);
                cur_op += 6;
                goto NEXT;
            OP(capturelex):
                MVM_frame_capturelex(tc, GET_REG(cur_op, 0).o);
                cur_op += 2;
                goto NEXT;
            OP(takeclosure):
                GET_REG(cur_op, 0).o = MVM_frame_takeclosure(tc, GET_REG(cur_op, 2).o);
                cur_op += 4;
                goto NEXT;
            OP(jumplist): {
                MVMint64 num_labels = MVM_BC_get_I64(cur_op, 0);
                MVMint64 input = GET_REG(cur_op, 8).i64;
                cur_op += 10;
                /* the goto ops are guaranteed valid/existent by validation.c */
                if (input < 0 || input >= num_labels) { /* implicitly covers num_labels == 0 */
                    /* skip the entire goto list block */
                    cur_op += (6 /* size of each goto op */) * num_labels;
                }
                else { /* delve directly into the selected goto op */
                    cur_op = bytecode_start + GET_UI32(cur_op,
                        input * (6 /* size of each goto op */)
                        + (2 /* size of the goto instruction itself */));
                }
                GC_SYNC_POINT(tc);
                goto NEXT;
            }
            OP(caller): {
                MVMFrame *caller = tc->cur_frame;
                MVMint64 depth = GET_REG(cur_op, 2).i64;

                while (caller && depth-- > 0) /* keep the > 0. */
                    caller = caller->caller;

                GET_REG(cur_op, 0).o = caller ? caller->code_ref : tc->instance->VMNull;

                cur_op += 4;
                goto NEXT;
            }
            OP(getdynlex): {
                GET_REG(cur_op, 0).o = MVM_frame_getdynlex(tc, GET_REG(cur_op, 2).s,
                        tc->cur_frame->caller);
                cur_op += 4;
                goto NEXT;
            }
            OP(binddynlex): {
                MVM_frame_binddynlex(tc, GET_REG(cur_op, 0).s, GET_REG(cur_op, 2).o,
                        tc->cur_frame->caller);
                cur_op += 4;
                goto NEXT;
            }
            OP(coerce_is): {
                GET_REG(cur_op, 0).s = MVM_coerce_i_s(tc, GET_REG(cur_op, 2).i64);
                cur_op += 4;
                goto NEXT;
            }
            OP(coerce_ns): {
                GET_REG(cur_op, 0).s = MVM_coerce_n_s(tc, GET_REG(cur_op, 2).n64);
                cur_op += 4;
                goto NEXT;
            }
            OP(coerce_si):
                GET_REG(cur_op, 0).i64 = MVM_coerce_s_i(tc, GET_REG(cur_op, 2).s);
                cur_op += 4;
                goto NEXT;
            OP(coerce_sn):
                GET_REG(cur_op, 0).n64 = MVM_coerce_s_n(tc, GET_REG(cur_op, 2).s);
                cur_op += 4;
                goto NEXT;
            OP(smrt_numify): {
                /* Increment PC before calling coercer, as it may make
                 * a method call to get the result. */
                MVMObject   *obj = GET_REG(cur_op, 2).o;
                MVMRegister *res = &GET_REG(cur_op, 0);
                cur_op += 4;
                MVM_coerce_smart_numify(tc, obj, res);
                goto NEXT;
            }
            OP(smrt_strify): {
                /* Increment PC before calling coercer, as it may make
                 * a method call to get the result. */
                MVMObject   *obj = GET_REG(cur_op, 2).o;
                MVMRegister *res = &GET_REG(cur_op, 0);
                cur_op += 4;
                MVM_coerce_smart_stringify(tc, obj, res);
                goto NEXT;
            }
            OP(param_sp):
                GET_REG(cur_op, 0).o = MVM_args_slurpy_positional(tc, &tc->cur_frame->params, GET_UI16(cur_op, 2));
                cur_op += 4;
                goto NEXT;
            OP(param_sn):
                GET_REG(cur_op, 0).o = MVM_args_slurpy_named(tc, &tc->cur_frame->params);
                cur_op += 2;
                goto NEXT;
            OP(ifnonnull):
                if (MVM_is_null(tc, GET_REG(cur_op, 0).o))
                    cur_op += 6;
                else
                    cur_op = bytecode_start + GET_UI32(cur_op, 2);
                GC_SYNC_POINT(tc);
                goto NEXT;
            OP(cmp_i): {
                MVMint64 a = GET_REG(cur_op, 2).i64, b = GET_REG(cur_op, 4).i64;
                GET_REG(cur_op, 0).i64 = (a > b) - (a < b);
                cur_op += 6;
                goto NEXT;
            }
            OP(cmp_n): {
                MVMnum64 a = GET_REG(cur_op, 2).n64, b = GET_REG(cur_op, 4).n64;
                GET_REG(cur_op, 0).i64 = (a > b) - (a < b);
                cur_op += 6;
                goto NEXT;
            }
            OP(not_i): {
                GET_REG(cur_op, 0).i64 = GET_REG(cur_op, 2).i64 ? 0 : 1;
                cur_op += 4;
                goto NEXT;
            }
            OP(setlexvalue): {
                MVMObject *code = GET_REG(cur_op, 0).o;
                MVMString *name = cu->body.strings[GET_UI32(cur_op, 2)];
                MVMObject *val  = GET_REG(cur_op, 6).o;
                MVMint16   flag = GET_I16(cur_op, 8);
                if (flag < 0 || flag > 2)
                    MVM_exception_throw_adhoc(tc, "setlexvalue provided with invalid flag");
                if (IS_CONCRETE(code) && REPR(code)->ID == MVM_REPR_ID_MVMCode) {
                    MVMStaticFrame *sf = ((MVMCode *)code)->body.sf;
                    MVMuint8 found = 0;
                    if (!sf->body.fully_deserialized)
                        MVM_bytecode_finish_frame(tc, sf->body.cu, sf, 0);
                    MVM_string_flatten(tc, name);
                    if (sf->body.lexical_names) {
                        MVMLexicalRegistry *entry;
                        MVM_HASH_GET(tc, sf->body.lexical_names, name, entry);
                        if (entry && sf->body.lexical_types[entry->value] == MVM_reg_obj) {
                            MVM_ASSIGN_REF(tc, &(sf->common.header), sf->body.static_env[entry->value].o, val);
                            sf->body.static_env_flags[entry->value] = (MVMuint8)flag;
                            found = 1;
                        }
                    }
                    if (!found)
                        MVM_exception_throw_adhoc(tc, "setstaticlex given invalid lexical name");
                }
                else {
                    MVM_exception_throw_adhoc(tc, "setstaticlex needs a code ref");
                }
                cur_op += 10;
                goto NEXT;
            }
            OP(exception):
                GET_REG(cur_op, 0).o = tc->active_handlers
                    ? tc->active_handlers->ex_obj
                    : tc->instance->VMNull;
                cur_op += 2;
                goto NEXT;
            OP(bindexmessage): {
                MVMObject *ex = GET_REG(cur_op, 0).o;
                if (IS_CONCRETE(ex) && REPR(ex)->ID == MVM_REPR_ID_MVMException) {
                    MVM_ASSIGN_REF(tc, &(ex->header), ((MVMException *)ex)->body.message,
                        GET_REG(cur_op, 2).s);
                }
                else {
                    MVM_exception_throw_adhoc(tc, "bindexmessage needs a VMException");
                }
                cur_op += 4;
                goto NEXT;
            }
            OP(bindexpayload): {
                MVMObject *ex = GET_REG(cur_op, 0).o;
                if (IS_CONCRETE(ex) && REPR(ex)->ID == MVM_REPR_ID_MVMException) {
                    MVM_ASSIGN_REF(tc, &(ex->header), ((MVMException *)ex)->body.payload,
                        GET_REG(cur_op, 2).o);
                }
                else {
                    MVM_exception_throw_adhoc(tc, "bindexpayload needs a VMException");
                }
                cur_op += 4;
                goto NEXT;
            }
            OP(bindexcategory): {
                MVMObject *ex = GET_REG(cur_op, 0).o;
                if (IS_CONCRETE(ex) && REPR(ex)->ID == MVM_REPR_ID_MVMException)
                    ((MVMException *)ex)->body.category = GET_REG(cur_op, 2).i64;
                else
                    MVM_exception_throw_adhoc(tc, "bindexcategory needs a VMException");
                cur_op += 4;
                goto NEXT;
            }
            OP(getexmessage): {
                MVMObject *ex = GET_REG(cur_op, 2).o;
                if (IS_CONCRETE(ex) && REPR(ex)->ID == MVM_REPR_ID_MVMException)
                    GET_REG(cur_op, 0).s = ((MVMException *)ex)->body.message;
                else
                    MVM_exception_throw_adhoc(tc, "getexmessage needs a VMException");
                cur_op += 4;
                goto NEXT;
            }
            OP(getexpayload): {
                MVMObject *ex = GET_REG(cur_op, 2).o;
                if (IS_CONCRETE(ex) && REPR(ex)->ID == MVM_REPR_ID_MVMException)
                    GET_REG(cur_op, 0).o = ((MVMException *)ex)->body.payload;
                else
                    MVM_exception_throw_adhoc(tc, "getexpayload needs a VMException");
                cur_op += 4;
                goto NEXT;
            }
            OP(getexcategory): {
                MVMObject *ex = GET_REG(cur_op, 2).o;
                if (IS_CONCRETE(ex) && REPR(ex)->ID == MVM_REPR_ID_MVMException)
                    GET_REG(cur_op, 0).i64 = ((MVMException *)ex)->body.category;
                else
                    MVM_exception_throw_adhoc(tc, "getexcategory needs a VMException");
                cur_op += 4;
                goto NEXT;
            }
            OP(throwdyn): {
                MVMRegister *rr     = &GET_REG(cur_op, 0);
                MVMObject   *ex_obj = GET_REG(cur_op, 2).o;
                cur_op += 4;
                MVM_exception_throwobj(tc, MVM_EX_THROW_DYN, ex_obj, rr);
                goto NEXT;
            }
            OP(throwlex): {
                MVMRegister *rr     = &GET_REG(cur_op, 0);
                MVMObject   *ex_obj = GET_REG(cur_op, 2).o;
                cur_op += 4;
                MVM_exception_throwobj(tc, MVM_EX_THROW_LEX, ex_obj, rr);
                goto NEXT;
            }
            OP(throwlexotic): {
                MVMRegister *rr     = &GET_REG(cur_op, 0);
                MVMObject   *ex_obj = GET_REG(cur_op, 2).o;
                cur_op += 4;
                MVM_exception_throwobj(tc, MVM_EX_THROW_LEXOTIC, ex_obj, rr);
                goto NEXT;
            }
            OP(throwcatdyn): {
                MVMRegister *rr  = &GET_REG(cur_op, 0);
                MVMuint32    cat = (MVMuint32)MVM_BC_get_I64(cur_op, 2);
                cur_op += 4;
                MVM_exception_throwcat(tc, MVM_EX_THROW_DYN, cat, rr);
                goto NEXT;
            }
            OP(throwcatlex): {
                MVMRegister *rr  = &GET_REG(cur_op, 0);
                MVMuint32    cat = (MVMuint32)MVM_BC_get_I64(cur_op, 2);
                cur_op += 4;
                MVM_exception_throwcat(tc, MVM_EX_THROW_LEX, cat, rr);
                goto NEXT;
            }
            OP(throwcatlexotic): {
                MVMRegister *rr  = &GET_REG(cur_op, 0);
                MVMuint32    cat = (MVMuint32)MVM_BC_get_I64(cur_op, 2);
                cur_op += 4;
                MVM_exception_throwcat(tc, MVM_EX_THROW_LEXOTIC, cat, rr);
                goto NEXT;
            }
            OP(die): {
                MVMRegister  *rr = &GET_REG(cur_op, 0);
                MVMString   *str =  GET_REG(cur_op, 2).s;
                cur_op += 4;
                MVM_exception_die(tc, str, rr);
                goto NEXT;
            }
            OP(takehandlerresult): {
                GET_REG(cur_op, 0).o = tc->last_handler_result
                    ? tc->last_handler_result
                    : tc->instance->VMNull;
                tc->last_handler_result = NULL;
                cur_op += 2;
                goto NEXT;
            }
            OP(newlexotic): {
                GET_REG(cur_op, 0).o = MVM_exception_newlexotic(tc,
                    GET_UI32(cur_op, 2));
                cur_op += 6;
                goto NEXT;
            }
            OP(lexoticresult): {
                MVMObject *lex = GET_REG(cur_op, 2).o;
                if (IS_CONCRETE(lex) && REPR(lex)->ID == MVM_REPR_ID_Lexotic)
                    GET_REG(cur_op, 0).o = ((MVMLexotic *)lex)->body.result;
                else
                    MVM_exception_throw_adhoc(tc, "lexoticresult needs a Lexotic");
                cur_op += 4;
                goto NEXT;
            }
            OP(usecapture):
                GET_REG(cur_op, 0).o = MVM_args_use_capture(tc, tc->cur_frame);
                cur_op += 2;
                goto NEXT;
            OP(savecapture): {
                /* Create a new call capture object. */
                GET_REG(cur_op, 0).o = MVM_args_save_capture(tc, tc->cur_frame);
                cur_op += 2;
                goto NEXT;
            }
            OP(captureposelems): {
                MVMObject *obj = GET_REG(cur_op, 2).o;
                if (IS_CONCRETE(obj) && REPR(obj)->ID == MVM_REPR_ID_MVMCallCapture) {
                    MVMCallCapture *cc = (MVMCallCapture *)obj;
                    GET_REG(cur_op, 0).i64 = cc->body.apc->num_pos;
                }
                else {
                    MVM_exception_throw_adhoc(tc, "captureposelems needs a MVMCallCapture");
                }
                cur_op += 4;
                goto NEXT;
            }
            OP(captureposarg): {
                MVMObject *obj = GET_REG(cur_op, 2).o;
                if (IS_CONCRETE(obj) && REPR(obj)->ID == MVM_REPR_ID_MVMCallCapture) {
                    MVMCallCapture *cc = (MVMCallCapture *)obj;
                    GET_REG(cur_op, 0).o = MVM_args_get_pos_obj(tc, cc->body.apc,
                        (MVMuint32)GET_REG(cur_op, 4).i64, MVM_ARG_REQUIRED).arg.o;
                }
                else {
                    MVM_exception_throw_adhoc(tc, "captureposarg needs a MVMCallCapture");
                }
                cur_op += 6;
                goto NEXT;
            }
            OP(captureposarg_i): {
                MVMObject *obj = GET_REG(cur_op, 2).o;
                if (IS_CONCRETE(obj) && REPR(obj)->ID == MVM_REPR_ID_MVMCallCapture) {
                    MVMCallCapture *cc = (MVMCallCapture *)obj;
                    GET_REG(cur_op, 0).i64 = MVM_args_get_pos_int(tc, cc->body.apc,
                        (MVMuint32)GET_REG(cur_op, 4).i64, MVM_ARG_REQUIRED).arg.i64;
                }
                else {
                    MVM_exception_throw_adhoc(tc, "captureposarg_i needs a MVMCallCapture");
                }
                cur_op += 6;
                goto NEXT;
            }
            OP(captureposarg_n): {
                MVMObject *obj = GET_REG(cur_op, 2).o;
                if (IS_CONCRETE(obj) && REPR(obj)->ID == MVM_REPR_ID_MVMCallCapture) {
                    MVMCallCapture *cc = (MVMCallCapture *)obj;
                    GET_REG(cur_op, 0).n64 = MVM_args_get_pos_num(tc, cc->body.apc,
                        (MVMuint32)GET_REG(cur_op, 4).i64, MVM_ARG_REQUIRED).arg.n64;
                }
                else {
                    MVM_exception_throw_adhoc(tc, "captureposarg_n needs a MVMCallCapture");
                }
                cur_op += 6;
                goto NEXT;
            }
            OP(captureposarg_s): {
                MVMObject *obj = GET_REG(cur_op, 2).o;
                if (IS_CONCRETE(obj) && REPR(obj)->ID == MVM_REPR_ID_MVMCallCapture) {
                    MVMCallCapture *cc = (MVMCallCapture *)obj;
                    GET_REG(cur_op, 0).s = MVM_args_get_pos_str(tc, cc->body.apc,
                        (MVMuint32)GET_REG(cur_op, 4).i64, MVM_ARG_REQUIRED).arg.s;
                }
                else {
                    MVM_exception_throw_adhoc(tc, "captureposarg_s needs a MVMCallCapture");
                }
                cur_op += 6;
                goto NEXT;
            }
            OP(captureposprimspec): {
                MVMObject *obj = GET_REG(cur_op, 2).o;
                MVMint64   i   = GET_REG(cur_op, 4).i64;
                if (IS_CONCRETE(obj) && REPR(obj)->ID == MVM_REPR_ID_MVMCallCapture) {
                    MVMCallCapture *cc = (MVMCallCapture *)obj;
                    if (i >= 0 && i < cc->body.apc->num_pos) {
                        MVMCallsiteEntry *arg_flags = cc->body.apc->arg_flags
                            ? cc->body.apc->arg_flags
                            : cc->body.apc->callsite->arg_flags;
                        switch (arg_flags[i] & MVM_CALLSITE_ARG_MASK) {
                            case MVM_CALLSITE_ARG_INT:
                                GET_REG(cur_op, 0).i64 = MVM_STORAGE_SPEC_BP_INT;
                                break;
                            case MVM_CALLSITE_ARG_NUM:
                                GET_REG(cur_op, 0).i64 = MVM_STORAGE_SPEC_BP_NUM;
                                break;
                            case MVM_CALLSITE_ARG_STR:
                                GET_REG(cur_op, 0).i64 = MVM_STORAGE_SPEC_BP_STR;
                                break;
                            default:
                                GET_REG(cur_op, 0).i64 = MVM_STORAGE_SPEC_BP_NONE;
                                break;
                        }
                    }
                    else {
                        MVM_exception_throw_adhoc(tc,
                            "Bad argument index given to captureposprimspec");
                    }
                }
                else {
                    MVM_exception_throw_adhoc(tc, "captureposprimspec needs a MVMCallCapture");
                }
                cur_op += 6;
                goto NEXT;
            }
            OP(invokewithcapture): {
                MVMObject *cobj = GET_REG(cur_op, 4).o;
                if (IS_CONCRETE(cobj) && REPR(cobj)->ID == MVM_REPR_ID_MVMCallCapture) {
                    MVMObject *code = GET_REG(cur_op, 2).o;
                    MVMCallCapture *cc = (MVMCallCapture *)cobj;
                    code = MVM_frame_find_invokee(tc, code, NULL);
                    tc->cur_frame->return_value = &GET_REG(cur_op, 0);
                    tc->cur_frame->return_type = MVM_RETURN_OBJ;
                    cur_op += 6;
                    tc->cur_frame->return_address = cur_op;
                    STABLE(code)->invoke(tc, code, cc->body.effective_callsite,
                        cc->body.apc->args);
                    goto NEXT;
                }
                else {
                    MVM_exception_throw_adhoc(tc, "invokewithcapture needs a MVMCallCapture");
                }
            }
            OP(multicacheadd):
                GET_REG(cur_op, 0).o = MVM_multi_cache_add(tc, GET_REG(cur_op, 2).o,
                    GET_REG(cur_op, 4).o, GET_REG(cur_op, 6).o);
                cur_op += 8;
                goto NEXT;
            OP(multicachefind):
                GET_REG(cur_op, 0).o = MVM_multi_cache_find(tc, GET_REG(cur_op, 2).o,
                    GET_REG(cur_op, 4).o);
                cur_op += 6;
                goto NEXT;
            OP(lexprimspec): {
                MVMObject *ctx  = GET_REG(cur_op, 2).o;
                MVMString *name = GET_REG(cur_op, 4).s;
                if (REPR(ctx)->ID != MVM_REPR_ID_MVMContext || !IS_CONCRETE(ctx))
                    MVM_exception_throw_adhoc(tc, "lexprimspec needs a context");
                GET_REG(cur_op, 0).i64 = MVM_frame_lexical_primspec(tc,
                    ((MVMContext *)ctx)->body.context, name);
                cur_op += 6;
                goto NEXT;
            }
            OP(ceil_n):{
                GET_REG(cur_op, 0).n64 = ceil(GET_REG(cur_op, 2).n64);
                cur_op += 4;
                goto NEXT;
            }
            OP(floor_n): {
                GET_REG(cur_op, 0).n64 = floor(GET_REG(cur_op, 2).n64);
                cur_op += 4;
                goto NEXT;
            }
            OP(assign): {
                MVMObject *cont  = GET_REG(cur_op, 0).o;
                MVMObject *obj = GET_REG(cur_op, 2).o;
                const MVMContainerSpec *spec = STABLE(cont)->container_spec;
                cur_op += 4;
                if (spec) {
                    spec->store(tc, cont, obj);
                } else {
                    MVM_exception_throw_adhoc(tc, "Cannot assign to an immutable value");
                }
                goto NEXT;
            }
            OP(assignunchecked): {
                MVMObject *cont  = GET_REG(cur_op, 0).o;
                MVMObject *obj = GET_REG(cur_op, 2).o;
                const MVMContainerSpec *spec = STABLE(cont)->container_spec;
                cur_op += 4;
                if (spec) {
                    spec->store_unchecked(tc, cont, obj);
                } else {
                    MVM_exception_throw_adhoc(tc, "Cannot assign to an immutable value");
                }
                goto NEXT;
            }
            OP(objprimspec): {
                MVMObject *type = GET_REG(cur_op, 2).o;
                if (type) {
                    const MVMStorageSpec *ss = REPR(type)->get_storage_spec(tc, STABLE(type));
                    GET_REG(cur_op, 0).i64 = ss->boxed_primitive;
                }
                else {
                    GET_REG(cur_op, 0).i64 = 0;
                }
                cur_op += 4;
                goto NEXT;
            }
            OP(backtracestrings):
                GET_REG(cur_op, 0).o = MVM_exception_backtrace_strings(tc, GET_REG(cur_op, 2).o);
                cur_op += 4;
                goto NEXT;
            OP(masttofile):
                MVM_mast_to_file(tc, GET_REG(cur_op, 0).o,
                    GET_REG(cur_op, 2).o, GET_REG(cur_op, 4).s);
                cur_op += 6;
                goto NEXT;
            OP(masttocu): {
                /* This op will end up returning into the runloop to run
                 * deserialization and load code, so make sure we're done
                 * processing this op really. */
                MVMObject *node = GET_REG(cur_op, 2).o;
                MVMObject *types = GET_REG(cur_op, 4).o;
                MVMRegister *result_reg = &GET_REG(cur_op, 0);
                cur_op += 6;

                /* Set up return (really continuation after load) address
                 * and enter bytecode loading process. */
                tc->cur_frame->return_address = cur_op;
                MVM_mast_to_cu(tc, node, types, result_reg);
                goto NEXT;
            }
            OP(iscompunit): {
                MVMObject *maybe_cu = GET_REG(cur_op, 2).o;
                GET_REG(cur_op, 0).i64 = REPR(maybe_cu)->ID == MVM_REPR_ID_MVMCompUnit;
                cur_op += 4;
                goto NEXT;
            }
            OP(compunitmainline): {
                MVMObject *maybe_cu = GET_REG(cur_op, 2).o;
                if (REPR(maybe_cu)->ID == MVM_REPR_ID_MVMCompUnit) {
                    MVMCompUnit *cu = (MVMCompUnit *)maybe_cu;
                    GET_REG(cur_op, 0).o = cu->body.coderefs[0];
                }
                else {
                    MVM_exception_throw_adhoc(tc, "compunitmainline requires an MVMCompUnit");
                }
                cur_op += 4;
                goto NEXT;
            }
            OP(compunitcodes): {
                MVMObject *     const result = MVM_repr_alloc_init(tc, MVM_hll_current(tc)->slurpy_array_type);
                MVMCompUnit * const maybe_cu = (MVMCompUnit *)GET_REG(cur_op, 2).o;
                if (REPR(maybe_cu)->ID == MVM_REPR_ID_MVMCompUnit) {
                    const MVMuint32 num_frames  = maybe_cu->body.num_frames;
                    MVMObject ** const coderefs = maybe_cu->body.coderefs;
                    MVMuint32 i;

                    for (i = 0; i < num_frames; i++) {
                        MVM_repr_push_o(tc, result, coderefs[i]);
                    }

                    GET_REG(cur_op, 0).o = result;
                }
                else {
                    MVM_exception_throw_adhoc(tc, "compunitcodes requires an MVMCompUnit");
                }
                cur_op += 4;
                goto NEXT;
            }
            OP(sleep): {
                MVM_gc_mark_thread_blocked(tc);
                MVM_platform_sleep((MVMuint64)ceil(GET_REG(cur_op, 0).n64 * 1e9));
                MVM_gc_mark_thread_unblocked(tc);
                cur_op += 2;
                goto NEXT;
            }
            OP(concat_s):
                GET_REG(cur_op, 0).s = MVM_string_concatenate(tc,
                    GET_REG(cur_op, 2).s, GET_REG(cur_op, 4).s);
                cur_op += 6;
                goto NEXT;
            OP(repeat_s):
                GET_REG(cur_op, 0).s = MVM_string_repeat(tc,
                    GET_REG(cur_op, 2).s, GET_REG(cur_op, 4).i64);
                cur_op += 6;
                goto NEXT;
            OP(substr_s):
                GET_REG(cur_op, 0).s = MVM_string_substring(tc,
                    GET_REG(cur_op, 2).s, GET_REG(cur_op, 4).i64,
                    GET_REG(cur_op, 6).i64);
                cur_op += 8;
                goto NEXT;
            OP(index_s):
                GET_REG(cur_op, 0).i64 = MVM_string_index(tc,
                    GET_REG(cur_op, 2).s, GET_REG(cur_op, 4).s, GET_REG(cur_op, 6).i64);
                cur_op += 8;
                goto NEXT;
            OP(graphs_s):
                GET_REG(cur_op, 0).i64 = MVM_string_graphs(tc, GET_REG(cur_op, 2).s);
                cur_op += 4;
                goto NEXT;
            OP(codes_s):
                GET_REG(cur_op, 0).i64 = MVM_string_codes(tc, GET_REG(cur_op, 2).s);
                cur_op += 4;
                goto NEXT;
            OP(eq_s):
                GET_REG(cur_op, 0).i64 = MVM_string_equal(tc,
                    GET_REG(cur_op, 2).s, GET_REG(cur_op, 4).s);
                cur_op += 6;
                goto NEXT;
            OP(ne_s):
                GET_REG(cur_op, 0).i64 = (MVMint64)(MVM_string_equal(tc,
                    GET_REG(cur_op, 2).s, GET_REG(cur_op, 4).s)? 0 : 1);
                cur_op += 6;
                goto NEXT;
            OP(eqat_s):
                GET_REG(cur_op, 0).i64 = MVM_string_equal_at(tc,
                    GET_REG(cur_op, 2).s, GET_REG(cur_op, 4).s,
                    GET_REG(cur_op, 6).i64);
                cur_op += 8;
                goto NEXT;
            OP(haveat_s):
                GET_REG(cur_op, 0).i64 = MVM_string_have_at(tc,
                    GET_REG(cur_op, 2).s, GET_REG(cur_op, 4).i64,
                    GET_REG(cur_op, 6).i64, GET_REG(cur_op, 8).s,
                    GET_REG(cur_op, 10).i64);
                cur_op += 12;
                goto NEXT;
            OP(getcp_s):
                GET_REG(cur_op, 0).i64 = MVM_string_get_grapheme_at(tc,
                    GET_REG(cur_op, 2).s, GET_REG(cur_op, 4).i64);
                cur_op += 6;
                goto NEXT;
            OP(indexcp_s):
                GET_REG(cur_op, 0).i64 = MVM_string_index_of_grapheme(tc,
                    GET_REG(cur_op, 2).s, GET_REG(cur_op, 4).i64);
                cur_op += 6;
                goto NEXT;
            OP(uc):
                GET_REG(cur_op, 0).s = MVM_string_uc(tc,
                    GET_REG(cur_op, 2).s);
                cur_op += 4;
                goto NEXT;
            OP(lc):
                GET_REG(cur_op, 0).s = MVM_string_lc(tc,
                    GET_REG(cur_op, 2).s);
                cur_op += 4;
                goto NEXT;
            OP(tc):
                GET_REG(cur_op, 0).s = MVM_string_tc(tc,
                    GET_REG(cur_op, 2).s);
                cur_op += 4;
                goto NEXT;
            OP(split):
                GET_REG(cur_op, 0).o = MVM_string_split(tc,
                    GET_REG(cur_op, 2).s, GET_REG(cur_op, 4).s);
                cur_op += 6;
                goto NEXT;
            OP(join):
                GET_REG(cur_op, 0).s = MVM_string_join(tc,
                    GET_REG(cur_op, 2).s, GET_REG(cur_op, 4).o);
                cur_op += 6;
                goto NEXT;
            OP(replace):
                GET_REG(cur_op, 0).s = MVM_string_replace(tc,
                    GET_REG(cur_op, 2).s, GET_REG(cur_op, 4).i64, GET_REG(cur_op, 6).i64, GET_REG(cur_op, 8).s);
                cur_op += 10;
                goto NEXT;
            OP(getcpbyname):
                GET_REG(cur_op, 0).i64 = MVM_unicode_lookup_by_name(tc,
                    GET_REG(cur_op, 2).s);
                cur_op += 4;
                goto NEXT;
            OP(indexat):
                /* branches on *failure* to match in the constant string, to save an instruction in regexes */
                if (MVM_string_char_at_in_string(tc, GET_REG(cur_op, 0).s,
                        GET_REG(cur_op, 2).i64, cu->body.strings[GET_UI32(cur_op, 4)]) >= 0)
                    cur_op += 12;
                else
                    cur_op = bytecode_start + GET_UI32(cur_op, 8);
                GC_SYNC_POINT(tc);
                goto NEXT;
            OP(indexnat):
                /* branches on *failure* to match in the constant string, to save an instruction in regexes */
                if (MVM_string_char_at_in_string(tc, GET_REG(cur_op, 0).s,
                        GET_REG(cur_op, 2).i64, cu->body.strings[GET_UI32(cur_op, 4)]) == -1)
                    cur_op += 12;
                else
                    cur_op = bytecode_start + GET_UI32(cur_op, 8);
                GC_SYNC_POINT(tc);
                goto NEXT;
            OP(unipropcode):
                GET_REG(cur_op, 0).i64 = (MVMint64)MVM_unicode_name_to_property_code(tc,
                    GET_REG(cur_op, 2).s);
                cur_op += 4;
                goto NEXT;
            OP(unipvalcode):
                GET_REG(cur_op, 0).i64 = (MVMint64)MVM_unicode_name_to_property_value_code(tc,
                    GET_REG(cur_op, 2).i64, GET_REG(cur_op, 4).s);
                cur_op += 6;
                goto NEXT;
            OP(hasuniprop):
                GET_REG(cur_op, 0).i64 = MVM_string_offset_has_unicode_property_value(tc,
                    GET_REG(cur_op, 2).s, GET_REG(cur_op, 4).i64, GET_REG(cur_op, 6).i64,
                    GET_REG(cur_op, 8).i64);
                cur_op += 10;
                goto NEXT;
            OP(hasunipropc):
                GET_REG(cur_op, 0).i64 = MVM_string_offset_has_unicode_property_value(tc,
                    GET_REG(cur_op, 2).s, GET_REG(cur_op, 4).i64, (MVMint64)GET_UI16(cur_op, 6),
                    (MVMint64)GET_UI16(cur_op, 8));
                cur_op += 10;
                goto NEXT;
            OP(getuniprop_bool):
                GET_REG(cur_op, 0).i64 = MVM_unicode_codepoint_get_property_bool(tc,
                    GET_REG(cur_op, 2).i64, GET_REG(cur_op, 4).i64);
                cur_op += 6;
                goto NEXT;
            OP(getuniprop_int):
                GET_REG(cur_op, 0).i64 = MVM_unicode_codepoint_get_property_int(tc,
                    GET_REG(cur_op, 2).i64, GET_REG(cur_op, 4).i64);
                cur_op += 6;
                goto NEXT;
            OP(getuniprop_str):
                GET_REG(cur_op, 0).s = MVM_unicode_codepoint_get_property_str(tc,
                    GET_REG(cur_op, 2).i64, GET_REG(cur_op, 4).i64);
                cur_op += 6;
                goto NEXT;
            OP(matchuniprop):
                GET_REG(cur_op, 0).i64 = MVM_unicode_codepoint_has_property_value(tc,
                    GET_REG(cur_op, 2).i64, GET_REG(cur_op, 4).i64,
                    GET_REG(cur_op, 6).i64);
                cur_op += 8;
                goto NEXT;
            OP(getuniname): {
                GET_REG(cur_op, 0).s = MVM_unicode_get_name(tc, GET_REG(cur_op, 2).i64);
                cur_op += 4;
                goto NEXT;
            }
            OP(chars):
                GET_REG(cur_op, 0).i64 = MVM_string_graphs(tc, GET_REG(cur_op, 2).s);
                cur_op += 4;
                goto NEXT;
            OP(chr):
                GET_REG(cur_op, 0).s = MVM_string_chr(tc, (MVMCodepoint)GET_REG(cur_op, 2).i64);
                cur_op += 4;
                goto NEXT;
            OP(ordfirst): {
                MVMString *s = GET_REG(cur_op, 2).s;
                MVMGrapheme32 g = MVM_string_get_grapheme_at(tc, s, 0);
                GET_REG(cur_op, 0).i64 = g >= 0 ? g : MVM_nfg_get_synthetic_info(tc, g)->base;
                cur_op += 4;
                goto NEXT;
            }
            OP(ordat): {
                MVMString *s = GET_REG(cur_op, 2).s;
                MVMGrapheme32 g = MVM_string_get_grapheme_at(tc, s, GET_REG(cur_op, 4).i64);
                GET_REG(cur_op, 0).i64 = g >= 0 ? g : MVM_nfg_get_synthetic_info(tc, g)->base;
                cur_op += 6;
                goto NEXT;
            }
            OP(rindexfrom):
                GET_REG(cur_op, 0).i64 = MVM_string_index_from_end(tc,
                    GET_REG(cur_op, 2).s, GET_REG(cur_op, 4).s, GET_REG(cur_op, 6).i64);
                cur_op += 8;
                goto NEXT;
            OP(escape):
                GET_REG(cur_op, 0).s = MVM_string_escape(tc,
                    GET_REG(cur_op, 2).s);
                cur_op += 4;
                goto NEXT;
            OP(flip):
                GET_REG(cur_op, 0).s = MVM_string_flip(tc,
                    GET_REG(cur_op, 2).s);
                cur_op += 4;
                goto NEXT;
            OP(iscclass):
                GET_REG(cur_op, 0).i64 = MVM_string_is_cclass(tc,
                    GET_REG(cur_op, 2).i64, GET_REG(cur_op, 4).s,
                    GET_REG(cur_op, 6).i64);
                cur_op += 8;
                goto NEXT;
            OP(findcclass):
                GET_REG(cur_op, 0).i64 = MVM_string_find_cclass(tc,
                    GET_REG(cur_op, 2).i64, GET_REG(cur_op, 4).s,
                    GET_REG(cur_op, 6).i64, GET_REG(cur_op, 8).i64);
                cur_op += 10;
                goto NEXT;
            OP(findnotcclass):
                GET_REG(cur_op, 0).i64 = MVM_string_find_not_cclass(tc,
                    GET_REG(cur_op, 2).i64, GET_REG(cur_op, 4).s,
                    GET_REG(cur_op, 6).i64, GET_REG(cur_op, 8).i64);
                cur_op += 10;
                goto NEXT;
            OP(nfafromstatelist):
                GET_REG(cur_op, 0).o = MVM_nfa_from_statelist(tc,
                    GET_REG(cur_op, 2).o, GET_REG(cur_op, 4).o);
                cur_op += 6;
                goto NEXT;
            OP(nfarunproto):
                GET_REG(cur_op, 0).o = MVM_nfa_run_proto(tc,
                    GET_REG(cur_op, 2).o, GET_REG(cur_op, 4).s,
                    GET_REG(cur_op, 6).i64);
                cur_op += 8;
                goto NEXT;
            OP(nfarunalt):
                MVM_nfa_run_alt(tc, GET_REG(cur_op, 0).o,
                    GET_REG(cur_op, 2).s, GET_REG(cur_op, 4).i64,
                    GET_REG(cur_op, 6).o, GET_REG(cur_op, 8).o,
                    GET_REG(cur_op, 10).o);
                cur_op += 12;
                goto NEXT;
            OP(flattenropes):
                MVM_string_flatten(tc, GET_REG(cur_op, 0).s);
                cur_op += 2;
                goto NEXT;
            OP(gt_s):
                GET_REG(cur_op, 0).i64 = MVM_string_compare(tc,
                    GET_REG(cur_op, 2).s, GET_REG(cur_op, 4).s) == 1;
                cur_op += 6;
                goto NEXT;
            OP(ge_s):
                GET_REG(cur_op, 0).i64 = MVM_string_compare(tc,
                    GET_REG(cur_op, 2).s, GET_REG(cur_op, 4).s) >= 0;
                cur_op += 6;
                goto NEXT;
            OP(lt_s):
                GET_REG(cur_op, 0).i64 = MVM_string_compare(tc,
                    GET_REG(cur_op, 2).s, GET_REG(cur_op, 4).s) == -1;
                cur_op += 6;
                goto NEXT;
            OP(le_s):
                GET_REG(cur_op, 0).i64 = MVM_string_compare(tc,
                    GET_REG(cur_op, 2).s, GET_REG(cur_op, 4).s) <= 0;
                cur_op += 6;
                goto NEXT;
            OP(cmp_s):
                GET_REG(cur_op, 0).i64 = MVM_string_compare(tc,
                    GET_REG(cur_op, 2).s, GET_REG(cur_op, 4).s);
                cur_op += 6;
                goto NEXT;
            OP(radix):
                GET_REG(cur_op, 0).o = MVM_radix(tc,
                    GET_REG(cur_op, 2).i64, GET_REG(cur_op, 4).s,
                    GET_REG(cur_op, 6).i64, GET_REG(cur_op, 8).i64);
                cur_op += 10;
                goto NEXT;
            OP(eqatic_s):
                GET_REG(cur_op, 0).i64 = MVM_string_equal_at_ignore_case(tc,
                    GET_REG(cur_op, 2).s, GET_REG(cur_op, 4).s,
                    GET_REG(cur_op, 6).i64);
                cur_op += 8;
                goto NEXT;
            OP(sin_n):
                GET_REG(cur_op, 0).n64 = sin(GET_REG(cur_op, 2).n64);
                cur_op += 4;
                goto NEXT;
            OP(asin_n):
                GET_REG(cur_op, 0).n64 = asin(GET_REG(cur_op, 2).n64);
                cur_op += 4;
                goto NEXT;
            OP(cos_n):
                GET_REG(cur_op, 0).n64 = cos(GET_REG(cur_op, 2).n64);
                cur_op += 4;
                goto NEXT;
            OP(acos_n):
                GET_REG(cur_op, 0).n64 = acos(GET_REG(cur_op, 2).n64);
                cur_op += 4;
                goto NEXT;
            OP(tan_n):
                GET_REG(cur_op, 0).n64 = tan(GET_REG(cur_op, 2).n64);
                cur_op += 4;
                goto NEXT;
            OP(atan_n):
                GET_REG(cur_op, 0).n64 = atan(GET_REG(cur_op, 2).n64);
                cur_op += 4;
                goto NEXT;
            OP(atan2_n):
                GET_REG(cur_op, 0).n64 = atan2(GET_REG(cur_op, 2).n64,
                    GET_REG(cur_op, 4).n64);
                cur_op += 6;
                goto NEXT;
            OP(sec_n): /* XXX TODO) handle edge cases */
                GET_REG(cur_op, 0).n64 = 1.0 / cos(GET_REG(cur_op, 2).n64);
                cur_op += 4;
                goto NEXT;
            OP(asec_n): /* XXX TODO) handle edge cases */
                GET_REG(cur_op, 0).n64 = acos(1.0 / GET_REG(cur_op, 2).n64);
                cur_op += 4;
                goto NEXT;
            OP(sinh_n):
                GET_REG(cur_op, 0).n64 = sinh(GET_REG(cur_op, 2).n64);
                cur_op += 4;
                goto NEXT;
            OP(cosh_n):
                GET_REG(cur_op, 0).n64 = cosh(GET_REG(cur_op, 2).n64);
                cur_op += 4;
                goto NEXT;
            OP(tanh_n):
                GET_REG(cur_op, 0).n64 = tanh(GET_REG(cur_op, 2).n64);
                cur_op += 4;
                goto NEXT;
            OP(sech_n): /* XXX TODO) handle edge cases */
                GET_REG(cur_op, 0).n64 = 1.0 / cosh(GET_REG(cur_op, 2).n64);
                cur_op += 4;
                goto NEXT;
            OP(sqrt_n):
                GET_REG(cur_op, 0).n64 = sqrt(GET_REG(cur_op, 2).n64);
                cur_op += 4;
                goto NEXT;
            OP(gcd_i): {
                MVMint64 a = labs(GET_REG(cur_op, 2).i64), b = labs(GET_REG(cur_op, 4).i64), c;
                while ( b != 0 ) {
                    c = a % b; a = b; b = c;
                }
                GET_REG(cur_op, 0).i64 = a;
                cur_op += 6;
                goto NEXT;
            }
            OP(lcm_i): {
                MVMint64 a = GET_REG(cur_op, 2).i64, b = GET_REG(cur_op, 4).i64, c, a_ = a, b_ = b;
                while ( b != 0 ) {
                    c = a % b; a = b; b = c;
                }
                c = a;
                GET_REG(cur_op, 0).i64 = a_ / c * b_;
                cur_op += 6;
                goto NEXT;
            }
            OP(abs_I): {
                MVMObject *   const type = GET_REG(cur_op, 4).o;
                MVMObject * const result = MVM_repr_alloc_init(tc, type);
                MVM_bigint_abs(tc, result, GET_REG(cur_op, 2).o);
                GET_REG(cur_op, 0).o = result;
                cur_op += 6;
                goto NEXT;
            }
            OP(neg_I): {
                MVMObject *   const type = GET_REG(cur_op, 4).o;
                MVMObject * const result = MVM_repr_alloc_init(tc, type);
                MVM_bigint_neg(tc, result, GET_REG(cur_op, 2).o);
                GET_REG(cur_op, 0).o = result;
                cur_op += 6;
                goto NEXT;
            }
            OP(bool_I):
                GET_REG(cur_op, 0).i64 = MVM_bigint_bool(tc, GET_REG(cur_op, 2).o);
                cur_op += 4;
                goto NEXT;
            OP(add_I):
                GET_REG(cur_op, 0).o = MVM_bigint_add(tc, GET_REG(cur_op, 6).o,
                    GET_REG(cur_op, 2).o, GET_REG(cur_op, 4).o);
                cur_op += 8;
                goto NEXT;
            OP(sub_I):
                GET_REG(cur_op, 0).o = MVM_bigint_sub(tc, GET_REG(cur_op, 6).o,
                    GET_REG(cur_op, 2).o, GET_REG(cur_op, 4).o);
                cur_op += 8;
                goto NEXT;
            OP(mul_I):
                GET_REG(cur_op, 0).o = MVM_bigint_mul(tc, GET_REG(cur_op, 6).o,
                    GET_REG(cur_op, 2).o, GET_REG(cur_op, 4).o);
                cur_op += 8;
                goto NEXT;
            OP(div_I): {
                MVMObject *   const type = GET_REG(cur_op, 6).o;
                GET_REG(cur_op, 0).o = MVM_bigint_div(tc, type, GET_REG(cur_op, 2).o, GET_REG(cur_op, 4).o);
                cur_op += 8;
                goto NEXT;
            }
            OP(mod_I): {
                MVMObject *   const type = GET_REG(cur_op, 6).o;
                GET_REG(cur_op, 0).o = MVM_bigint_mod(tc, type, GET_REG(cur_op, 2).o, GET_REG(cur_op, 4).o);
                cur_op += 8;
                goto NEXT;
            }
            OP(expmod_I): {
                MVMObject *   const type = GET_REG(cur_op, 8).o;
                MVMObject * const result = MVM_repr_alloc_init(tc, type);
                MVM_bigint_expmod(tc, result, GET_REG(cur_op, 2).o, GET_REG(cur_op, 4).o, GET_REG(cur_op, 6).o);
                GET_REG(cur_op, 0).o = result;
                cur_op += 10;
                goto NEXT;
            }
            OP(gcd_I): {
                GET_REG(cur_op, 0).o = MVM_bigint_gcd(tc, GET_REG(cur_op, 6).o, GET_REG(cur_op, 2).o, GET_REG(cur_op, 4).o);
                cur_op += 8;
                goto NEXT;
            }
            OP(lcm_I):
                GET_REG(cur_op, 0).o = MVM_bigint_lcm(tc, GET_REG(cur_op, 6).o,
                    GET_REG(cur_op, 2).o, GET_REG(cur_op, 4).o);
                cur_op += 8;
                goto NEXT;
            OP(bor_I): {
                MVMObject *   const type = GET_REG(cur_op, 6).o;
                MVMObject * const result = MVM_repr_alloc_init(tc, type);
                MVM_bigint_or(tc, result, GET_REG(cur_op, 2).o, GET_REG(cur_op, 4).o);
                GET_REG(cur_op, 0).o = result;
                cur_op += 8;
                goto NEXT;
            }
            OP(bxor_I): {
                MVMObject *   const type = GET_REG(cur_op, 6).o;
                MVMObject * const result = MVM_repr_alloc_init(tc, type);
                MVM_bigint_xor(tc, result, GET_REG(cur_op, 2).o, GET_REG(cur_op, 4).o);
                GET_REG(cur_op, 0).o = result;
                cur_op += 8;
                goto NEXT;
            }
            OP(band_I): {
                MVMObject *   const type = GET_REG(cur_op, 6).o;
                MVMObject * const result = MVM_repr_alloc_init(tc, type);
                MVM_bigint_and(tc, result, GET_REG(cur_op, 2).o, GET_REG(cur_op, 4).o);
                GET_REG(cur_op, 0).o = result;
                cur_op += 8;
                goto NEXT;
            }
            OP(bnot_I): {
                MVMObject *   const type = GET_REG(cur_op, 4).o;
                MVMObject * const result = MVM_repr_alloc_init(tc, type);
                MVM_bigint_not(tc, result, GET_REG(cur_op, 2).o);
                GET_REG(cur_op, 0).o = result;
                cur_op += 6;
                goto NEXT;
            }
            OP(blshift_I): {
                MVMObject *   const type = GET_REG(cur_op, 6).o;
                MVMObject * const result = MVM_repr_alloc_init(tc, type);
                MVM_bigint_shl(tc, result, GET_REG(cur_op, 2).o, GET_REG(cur_op, 4).i64);
                GET_REG(cur_op, 0).o = result;
                cur_op += 8;
                goto NEXT;
            }
            OP(brshift_I): {
                MVMObject *   const type = GET_REG(cur_op, 6).o;
                MVMObject * const result = MVM_repr_alloc_init(tc, type);
                MVM_bigint_shr(tc, result, GET_REG(cur_op, 2).o, GET_REG(cur_op, 4).i64);
                GET_REG(cur_op, 0).o = result;
                cur_op += 8;
                goto NEXT;
            }
            OP(pow_I):
                GET_REG(cur_op, 0).o = MVM_bigint_pow(tc, GET_REG(cur_op, 2).o,
                    GET_REG(cur_op, 4).o, GET_REG(cur_op, 6).o, GET_REG(cur_op, 8).o);
                cur_op += 10;
                goto NEXT;
            OP(cmp_I): {
                MVMObject *a = GET_REG(cur_op, 2).o, *b = GET_REG(cur_op, 4).o;
                GET_REG(cur_op, 0).i64 = MVM_bigint_cmp(tc, a, b);
                cur_op += 6;
                goto NEXT;
            }
            OP(eq_I): {
                MVMObject *a = GET_REG(cur_op, 2).o, *b = GET_REG(cur_op, 4).o;
                GET_REG(cur_op, 0).i64 = MP_EQ == MVM_bigint_cmp(tc, a, b);
                cur_op += 6;
                goto NEXT;
            }
            OP(ne_I): {
                MVMObject *a = GET_REG(cur_op, 2).o, *b = GET_REG(cur_op, 4).o;
                GET_REG(cur_op, 0).i64 = MP_EQ != MVM_bigint_cmp(tc, a, b);
                cur_op += 6;
                goto NEXT;
            }
            OP(lt_I): {
                MVMObject *a = GET_REG(cur_op, 2).o, *b = GET_REG(cur_op, 4).o;
                GET_REG(cur_op, 0).i64 = MP_LT == MVM_bigint_cmp(tc, a, b);
                cur_op += 6;
                goto NEXT;
            }
            OP(le_I): {
                MVMObject *a = GET_REG(cur_op, 2).o, *b = GET_REG(cur_op, 4).o;
                GET_REG(cur_op, 0).i64 = MP_GT != MVM_bigint_cmp(tc, a, b);
                cur_op += 6;
                goto NEXT;
            }
            OP(gt_I): {
                MVMObject *a = GET_REG(cur_op, 2).o, *b = GET_REG(cur_op, 4).o;
                GET_REG(cur_op, 0).i64 = MP_GT == MVM_bigint_cmp(tc, a, b);
                cur_op += 6;
                goto NEXT;
            }
            OP(ge_I): {
                MVMObject *a = GET_REG(cur_op, 2).o, *b = GET_REG(cur_op, 4).o;
                GET_REG(cur_op, 0).i64 = MP_LT != MVM_bigint_cmp(tc, a, b);
                cur_op += 6;
                goto NEXT;
            }
            OP(isprime_I): {
                MVMObject *a = GET_REG(cur_op, 2).o;
                MVMint64 b = GET_REG(cur_op, 4).i64;
                GET_REG(cur_op, 0).i64 = MVM_bigint_is_prime(tc, a, b);
                cur_op += 6;
                goto NEXT;
            }
            OP(rand_I): {
                MVMObject * const type = GET_REG(cur_op, 4).o;
                MVMObject *  const rnd = MVM_repr_alloc_init(tc, type);
                MVM_bigint_rand(tc, rnd, GET_REG(cur_op, 2).o);
                GET_REG(cur_op, 0).o = rnd;
                cur_op += 6;
                goto NEXT;
            }
            OP(coerce_nI): {
                MVMObject *   const type = GET_REG(cur_op, 4).o;
                MVMObject * const result = MVM_repr_alloc_init(tc, type);
                MVM_bigint_from_num(tc, result, GET_REG(cur_op, 2).n64);
                GET_REG(cur_op, 0).o = result;
                cur_op += 6;
                goto NEXT;
            }
            OP(coerce_sI): {
                MVMString *s = GET_REG(cur_op, 2).s;
                MVMObject *type = GET_REG(cur_op, 4).o;
                char *buf = MVM_string_ascii_encode(tc, s, NULL);
                MVMObject *a = MVM_repr_alloc_init(tc, type);
                MVM_bigint_from_str(tc, a, buf);
                MVM_free(buf);
                GET_REG(cur_op, 0).o = a;
                cur_op += 6;
                goto NEXT;
            }
            OP(coerce_In): {
                MVMObject *a = GET_REG(cur_op, 2).o;
                GET_REG(cur_op, 0).n64 = MVM_bigint_to_num(tc, a);
                cur_op += 4;
                goto NEXT;
            }
            OP(coerce_Is): {
                GET_REG(cur_op, 0).s = MVM_bigint_to_str(tc, GET_REG(cur_op, 2).o, 10);
                cur_op += 4;
                goto NEXT;
            }
            OP(isbig_I): {
                GET_REG(cur_op, 0).i64 = MVM_bigint_is_big(tc, GET_REG(cur_op, 2).o);
                cur_op += 4;
                goto NEXT;
            }
            OP(base_I): {
                GET_REG(cur_op, 0).s = MVM_bigint_to_str(tc, GET_REG(cur_op, 2).o, GET_REG(cur_op, 4).i64);
                cur_op += 6;
                goto NEXT;
            }
            OP(radix_I):
                GET_REG(cur_op, 0).o = MVM_bigint_radix(tc,
                    GET_REG(cur_op, 2).i64, GET_REG(cur_op, 4).s,
                    GET_REG(cur_op, 6).i64, GET_REG(cur_op, 8).i64, GET_REG(cur_op, 10).o);
                cur_op += 12;
                goto NEXT;
            OP(div_In): {
                MVMObject *a = GET_REG(cur_op, 2).o, *b = GET_REG(cur_op, 4).o;
                GET_REG(cur_op, 0).n64 = MVM_bigint_div_num(tc, a, b);
                cur_op += 6;
                goto NEXT;
            }
            OP(log_n):
                GET_REG(cur_op, 0).n64 = log(GET_REG(cur_op, 2).n64);
                cur_op += 4;
                goto NEXT;
            OP(exp_n):
                GET_REG(cur_op, 0).n64 = exp(GET_REG(cur_op, 2).n64);
                cur_op += 4;
                goto NEXT;
            OP(knowhow):
                GET_REG(cur_op, 0).o = tc->instance->KnowHOW;
                cur_op += 2;
                goto NEXT;
            OP(findmeth): {
                /* Increment PC first, as we may make a method call. */
                MVMRegister *res  = &GET_REG(cur_op, 0);
                MVMObject   *obj  = GET_REG(cur_op, 2).o;
                MVMString   *name = cu->body.strings[GET_UI32(cur_op, 4)];
                cur_op += 8;
                MVM_6model_find_method(tc, obj, name, res);
                goto NEXT;
            }
            OP(findmeth_s):  {
                /* Increment PC first, as we may make a method call. */
                MVMRegister *res  = &GET_REG(cur_op, 0);
                MVMObject   *obj  = GET_REG(cur_op, 2).o;
                MVMString   *name = GET_REG(cur_op, 4).s;
                cur_op += 6;
                MVM_6model_find_method(tc, obj, name, res);
                goto NEXT;
            }
            OP(can): {
                /* Increment PC first, as we may make a method call. */
                MVMRegister *res  = &GET_REG(cur_op, 0);
                MVMObject   *obj  = GET_REG(cur_op, 2).o;
                MVMString   *name = cu->body.strings[GET_UI32(cur_op, 4)];
                cur_op += 8;
                MVM_6model_can_method(tc, obj, name, res);
                goto NEXT;
            }
            OP(can_s): {
                /* Increment PC first, as we may make a method call. */
                MVMRegister *res  = &GET_REG(cur_op, 0);
                MVMObject   *obj  = GET_REG(cur_op, 2).o;
                MVMString   *name = GET_REG(cur_op, 4).s;
                cur_op += 6;
                MVM_6model_can_method(tc, obj, name, res);
                goto NEXT;
            }
            OP(create): {
                /* Ordering here matters. We write the object into the
                 * register before calling initialize. This is because
                 * if initialize allocates, obj may have moved after
                 * we called it. Note that type is never used after
                 * the initial allocate call also. This saves us having
                 * to put things on the temporary stack. The GC will
                 * know to update it in the register if it moved. */
                MVMObject *type = GET_REG(cur_op, 2).o;
                MVMObject *obj  = REPR(type)->allocate(tc, STABLE(type));
                GET_REG(cur_op, 0).o = obj;
                if (REPR(obj)->initialize)
                    REPR(obj)->initialize(tc, STABLE(obj), obj, OBJECT_BODY(obj));
                cur_op += 4;
                goto NEXT;
            }
            OP(gethow):
                GET_REG(cur_op, 0).o = MVM_6model_get_how(tc,
                    STABLE(GET_REG(cur_op, 2).o));
                cur_op += 4;
                goto NEXT;
            OP(getwhat):
                GET_REG(cur_op, 0).o = STABLE(GET_REG(cur_op, 2).o)->WHAT;
                cur_op += 4;
                goto NEXT;
            OP(atkey_i): {
                MVMObject *obj = GET_REG(cur_op, 2).o;
                REPR(obj)->ass_funcs.at_key(tc, STABLE(obj), obj, OBJECT_BODY(obj),
                    (MVMObject *)GET_REG(cur_op, 4).s, &GET_REG(cur_op, 0), MVM_reg_int64);
                cur_op += 6;
                goto NEXT;
            }
            OP(atkey_n): {
                MVMObject *obj = GET_REG(cur_op, 2).o;
                REPR(obj)->ass_funcs.at_key(tc, STABLE(obj), obj, OBJECT_BODY(obj),
                    (MVMObject *)GET_REG(cur_op, 4).s, &GET_REG(cur_op, 0), MVM_reg_num64);
                cur_op += 6;
                goto NEXT;
            }
            OP(atkey_s): {
                MVMObject *obj = GET_REG(cur_op, 2).o;
                REPR(obj)->ass_funcs.at_key(tc, STABLE(obj), obj, OBJECT_BODY(obj),
                    (MVMObject *)GET_REG(cur_op, 4).s, &GET_REG(cur_op, 0), MVM_reg_str);
                cur_op += 6;
                goto NEXT;
            }
            OP(atkey_o): {
                MVMObject *obj = GET_REG(cur_op, 2).o;
                if (IS_CONCRETE(obj))
                    REPR(obj)->ass_funcs.at_key(tc, STABLE(obj), obj, OBJECT_BODY(obj),
                        (MVMObject *)GET_REG(cur_op, 4).s, &GET_REG(cur_op, 0), MVM_reg_obj);
                else
                    GET_REG(cur_op, 0).o = tc->instance->VMNull;
                cur_op += 6;
                goto NEXT;
            }
            OP(bindkey_i): {
                MVMObject *obj = GET_REG(cur_op, 0).o;
                REPR(obj)->ass_funcs.bind_key(tc, STABLE(obj), obj,
                    OBJECT_BODY(obj), (MVMObject *)GET_REG(cur_op, 2).s,
                    GET_REG(cur_op, 4), MVM_reg_int64);
                MVM_SC_WB_OBJ(tc, obj);
                cur_op += 6;
                goto NEXT;
            }
            OP(bindkey_n): {
                MVMObject *obj = GET_REG(cur_op, 0).o;
                REPR(obj)->ass_funcs.bind_key(tc, STABLE(obj), obj,
                    OBJECT_BODY(obj), (MVMObject *)GET_REG(cur_op, 2).s,
                    GET_REG(cur_op, 4), MVM_reg_num64);
                MVM_SC_WB_OBJ(tc, obj);
                cur_op += 6;
                goto NEXT;
            }
            OP(bindkey_s): {
                MVMObject *obj = GET_REG(cur_op, 0).o;
                REPR(obj)->ass_funcs.bind_key(tc, STABLE(obj), obj,
                    OBJECT_BODY(obj), (MVMObject *)GET_REG(cur_op, 2).s,
                    GET_REG(cur_op, 4), MVM_reg_str);
                MVM_SC_WB_OBJ(tc, obj);
                cur_op += 6;
                goto NEXT;
            }
            OP(bindkey_o): {
                MVMObject *obj = GET_REG(cur_op, 0).o;
                REPR(obj)->ass_funcs.bind_key(tc, STABLE(obj), obj,
                    OBJECT_BODY(obj), (MVMObject *)GET_REG(cur_op, 2).s,
                    GET_REG(cur_op, 4), MVM_reg_obj);
                MVM_SC_WB_OBJ(tc, obj);
                cur_op += 6;
                goto NEXT;
            }
            OP(existskey): {
                MVMObject *obj = GET_REG(cur_op, 2).o;
                GET_REG(cur_op, 0).i64 = REPR(obj)->ass_funcs.exists_key(tc,
                    STABLE(obj), obj, OBJECT_BODY(obj),
                    (MVMObject *)GET_REG(cur_op, 4).s);
                cur_op += 6;
                goto NEXT;
            }
            OP(deletekey): {
                MVMObject *obj = GET_REG(cur_op, 0).o;
                REPR(obj)->ass_funcs.delete_key(tc, STABLE(obj), obj,
                    OBJECT_BODY(obj), (MVMObject *)GET_REG(cur_op, 2).s);
                MVM_SC_WB_OBJ(tc, obj);
                cur_op += 4;
                goto NEXT;
            }
            OP(getwhere):
                GET_REG(cur_op, 0).i64 = (MVMint64)GET_REG(cur_op, 2).o;
                cur_op += 4;
                goto NEXT;
            OP(eqaddr):
                GET_REG(cur_op, 0).i64 = GET_REG(cur_op, 2).o == GET_REG(cur_op, 4).o ? 1 : 0;
                cur_op += 6;
                goto NEXT;
            OP(reprname): {
                const MVMREPROps *repr = REPR(GET_REG(cur_op, 2).o);
                GET_REG(cur_op, 0).s = tc->instance->repr_list[repr->ID]->name;
                cur_op += 4;
                goto NEXT;
            }
            OP(isconcrete): {
                MVMObject *obj = GET_REG(cur_op, 2).o;
                GET_REG(cur_op, 0).i64 = obj && IS_CONCRETE(obj) ? 1 : 0;
                cur_op += 4;
                goto NEXT;
            }
            OP(atpos_i): {
                MVMObject *obj = GET_REG(cur_op, 2).o;
                REPR(obj)->pos_funcs.at_pos(tc, STABLE(obj), obj,
                    OBJECT_BODY(obj), GET_REG(cur_op, 4).i64,
                    &GET_REG(cur_op, 0), MVM_reg_int64);
                cur_op += 6;
                goto NEXT;
            }
            OP(atpos_n): {
                MVMObject *obj = GET_REG(cur_op, 2).o;
                REPR(obj)->pos_funcs.at_pos(tc, STABLE(obj), obj,
                    OBJECT_BODY(obj), GET_REG(cur_op, 4).i64,
                    &GET_REG(cur_op, 0), MVM_reg_num64);
                cur_op += 6;
                goto NEXT;
            }
            OP(atpos_s): {
                MVMObject *obj = GET_REG(cur_op, 2).o;
                REPR(obj)->pos_funcs.at_pos(tc, STABLE(obj), obj,
                    OBJECT_BODY(obj), GET_REG(cur_op, 4).i64,
                    &GET_REG(cur_op, 0), MVM_reg_str);
                cur_op += 6;
                goto NEXT;
            }
            OP(atpos_o): {
                MVMObject *obj = GET_REG(cur_op, 2).o;
                if (IS_CONCRETE(obj))
                    REPR(obj)->pos_funcs.at_pos(tc, STABLE(obj), obj,
                        OBJECT_BODY(obj), GET_REG(cur_op, 4).i64,
                        &GET_REG(cur_op, 0), MVM_reg_obj);
                else
                    GET_REG(cur_op, 0).o = tc->instance->VMNull;
                cur_op += 6;
                goto NEXT;
            }
            OP(bindpos_i): {
                MVMObject *obj = GET_REG(cur_op, 0).o;
                REPR(obj)->pos_funcs.bind_pos(tc, STABLE(obj), obj,
                    OBJECT_BODY(obj), GET_REG(cur_op, 2).i64,
                    GET_REG(cur_op, 4), MVM_reg_int64);
                MVM_SC_WB_OBJ(tc, obj);
                cur_op += 6;
                goto NEXT;
            }
            OP(bindpos_n): {
                MVMObject *obj = GET_REG(cur_op, 0).o;
                REPR(obj)->pos_funcs.bind_pos(tc, STABLE(obj), obj,
                    OBJECT_BODY(obj), GET_REG(cur_op, 2).i64,
                    GET_REG(cur_op, 4), MVM_reg_num64);
                MVM_SC_WB_OBJ(tc, obj);
                cur_op += 6;
                goto NEXT;
            }
            OP(bindpos_s): {
                MVMObject *obj = GET_REG(cur_op, 0).o;
                REPR(obj)->pos_funcs.bind_pos(tc, STABLE(obj), obj,
                    OBJECT_BODY(obj), GET_REG(cur_op, 2).i64,
                    GET_REG(cur_op, 4), MVM_reg_str);
                MVM_SC_WB_OBJ(tc, obj);
                cur_op += 6;
                goto NEXT;
            }
            OP(bindpos_o): {
                MVMObject *obj = GET_REG(cur_op, 0).o;
                REPR(obj)->pos_funcs.bind_pos(tc, STABLE(obj), obj,
                    OBJECT_BODY(obj), GET_REG(cur_op, 2).i64,
                    GET_REG(cur_op, 4), MVM_reg_obj);
                MVM_SC_WB_OBJ(tc, obj);
                cur_op += 6;
                goto NEXT;
            }
            OP(push_i): {
                MVMObject *obj = GET_REG(cur_op, 0).o;
                REPR(obj)->pos_funcs.push(tc, STABLE(obj), obj,
                    OBJECT_BODY(obj), GET_REG(cur_op, 2), MVM_reg_int64);
                MVM_SC_WB_OBJ(tc, obj);
                cur_op += 4;
                goto NEXT;
            }
            OP(push_n): {
                MVMObject *obj = GET_REG(cur_op, 0).o;
                REPR(obj)->pos_funcs.push(tc, STABLE(obj), obj,
                    OBJECT_BODY(obj), GET_REG(cur_op, 2), MVM_reg_num64);
                MVM_SC_WB_OBJ(tc, obj);
                cur_op += 4;
                goto NEXT;
            }
            OP(push_s): {
                MVMObject *obj = GET_REG(cur_op, 0).o;
                REPR(obj)->pos_funcs.push(tc, STABLE(obj), obj,
                    OBJECT_BODY(obj), GET_REG(cur_op, 2), MVM_reg_str);
                MVM_SC_WB_OBJ(tc, obj);
                cur_op += 4;
                goto NEXT;
            }
            OP(push_o): {
                MVMObject *obj = GET_REG(cur_op, 0).o;
                REPR(obj)->pos_funcs.push(tc, STABLE(obj), obj,
                    OBJECT_BODY(obj), GET_REG(cur_op, 2), MVM_reg_obj);
                MVM_SC_WB_OBJ(tc, obj);
                cur_op += 4;
                goto NEXT;
            }
            OP(pop_i): {
                MVMObject *obj = GET_REG(cur_op, 2).o;
                REPR(obj)->pos_funcs.pop(tc, STABLE(obj), obj,
                    OBJECT_BODY(obj), &GET_REG(cur_op, 0), MVM_reg_int64);
                cur_op += 4;
                goto NEXT;
            }
            OP(pop_n): {
                MVMObject *obj = GET_REG(cur_op, 2).o;
                REPR(obj)->pos_funcs.pop(tc, STABLE(obj), obj,
                    OBJECT_BODY(obj), &GET_REG(cur_op, 0), MVM_reg_num64);
                cur_op += 4;
                goto NEXT;
            }
            OP(pop_s): {
                MVMObject *obj = GET_REG(cur_op, 2).o;
                REPR(obj)->pos_funcs.pop(tc, STABLE(obj), obj,
                    OBJECT_BODY(obj), &GET_REG(cur_op, 0), MVM_reg_str);
                cur_op += 4;
                goto NEXT;
            }
            OP(pop_o): {
                MVMObject *obj = GET_REG(cur_op, 2).o;
                REPR(obj)->pos_funcs.pop(tc, STABLE(obj), obj,
                    OBJECT_BODY(obj), &GET_REG(cur_op, 0), MVM_reg_obj);
                cur_op += 4;
                goto NEXT;
            }
            OP(unshift_i): {
                MVMObject *obj = GET_REG(cur_op, 0).o;
                REPR(obj)->pos_funcs.unshift(tc, STABLE(obj), obj,
                    OBJECT_BODY(obj), GET_REG(cur_op, 2), MVM_reg_int64);
                cur_op += 4;
                goto NEXT;
            }
            OP(unshift_n): {
                MVMObject *obj = GET_REG(cur_op, 0).o;
                REPR(obj)->pos_funcs.unshift(tc, STABLE(obj), obj,
                    OBJECT_BODY(obj), GET_REG(cur_op, 2), MVM_reg_num64);
                cur_op += 4;
                goto NEXT;
            }
            OP(unshift_s): {
                MVMObject *obj = GET_REG(cur_op, 0).o;
                REPR(obj)->pos_funcs.unshift(tc, STABLE(obj), obj,
                    OBJECT_BODY(obj), GET_REG(cur_op, 2), MVM_reg_str);
                cur_op += 4;
                goto NEXT;
            }
            OP(unshift_o): {
                MVMObject *obj = GET_REG(cur_op, 0).o;
                REPR(obj)->pos_funcs.unshift(tc, STABLE(obj), obj,
                    OBJECT_BODY(obj), GET_REG(cur_op, 2), MVM_reg_obj);
                cur_op += 4;
                goto NEXT;
            }
            OP(shift_i): {
                MVMObject *obj = GET_REG(cur_op, 2).o;
                REPR(obj)->pos_funcs.shift(tc, STABLE(obj), obj,
                    OBJECT_BODY(obj), &GET_REG(cur_op, 0), MVM_reg_int64);
                cur_op += 4;
                goto NEXT;
            }
            OP(shift_n): {
                MVMObject *obj = GET_REG(cur_op, 2).o;
                REPR(obj)->pos_funcs.shift(tc, STABLE(obj), obj,
                    OBJECT_BODY(obj), &GET_REG(cur_op, 0), MVM_reg_num64);
                cur_op += 4;
                goto NEXT;
            }
            OP(shift_s): {
                MVMObject *obj = GET_REG(cur_op, 2).o;
                REPR(obj)->pos_funcs.shift(tc, STABLE(obj), obj,
                    OBJECT_BODY(obj), &GET_REG(cur_op, 0), MVM_reg_str);
                cur_op += 4;
                goto NEXT;
            }
            OP(shift_o): {
                MVMObject *obj = GET_REG(cur_op, 2).o;
                REPR(obj)->pos_funcs.shift(tc, STABLE(obj), obj,
                    OBJECT_BODY(obj), &GET_REG(cur_op, 0), MVM_reg_obj);
                cur_op += 4;
                goto NEXT;
            }
            OP(splice): {
                MVMObject *obj = GET_REG(cur_op, 0).o;
                REPR(obj)->pos_funcs.splice(tc, STABLE(obj), obj,
                    OBJECT_BODY(obj), GET_REG(cur_op, 2).o,
                    GET_REG(cur_op, 4).i64, GET_REG(cur_op, 6).i64);
                cur_op += 8;
                goto NEXT;
            }
            OP(setelemspos): {
                MVMObject *obj = GET_REG(cur_op, 0).o;
                REPR(obj)->pos_funcs.set_elems(tc, STABLE(obj), obj,
                    OBJECT_BODY(obj), GET_REG(cur_op, 2).i64);
                cur_op += 4;
                goto NEXT;
            }
            OP(box_i): {
                MVM_box_int(tc, GET_REG(cur_op, 2).i64, GET_REG(cur_op, 4).o,
                            &GET_REG(cur_op, 0));
                cur_op += 6;
                goto NEXT;
            }
            OP(box_n): {
                MVM_box_num(tc, GET_REG(cur_op, 2).n64, GET_REG(cur_op, 4).o,
                            &GET_REG(cur_op, 0));
                cur_op += 6;
                goto NEXT;
            }
            OP(box_s): {
                 MVM_box_str(tc, GET_REG(cur_op, 2).s, GET_REG(cur_op, 4).o, &GET_REG(cur_op, 0));
                 cur_op += 6;
                 goto NEXT;
            }
            OP(unbox_i): {
                MVMObject *obj = GET_REG(cur_op, 2).o;
                if (!IS_CONCRETE(obj))
                    MVM_exception_throw_adhoc(tc, "Cannot unbox a type object");
                GET_REG(cur_op, 0).i64 = REPR(obj)->box_funcs.get_int(tc,
                    STABLE(obj), obj, OBJECT_BODY(obj));
                cur_op += 4;
                goto NEXT;
            }
            OP(unbox_n): {
                MVMObject *obj = GET_REG(cur_op, 2).o;
                if (!IS_CONCRETE(obj))
                    MVM_exception_throw_adhoc(tc, "Cannot unbox a type object");
                GET_REG(cur_op, 0).n64 = REPR(obj)->box_funcs.get_num(tc,
                    STABLE(obj), obj, OBJECT_BODY(obj));
                cur_op += 4;
                goto NEXT;
            }
            OP(unbox_s): {
                MVMObject *obj = GET_REG(cur_op, 2).o;
                if (!IS_CONCRETE(obj))
                    MVM_exception_throw_adhoc(tc, "Cannot unbox a type object");
                GET_REG(cur_op, 0).s = REPR(obj)->box_funcs.get_str(tc,
                    STABLE(obj), obj, OBJECT_BODY(obj));
                cur_op += 4;
                goto NEXT;
            }
            OP(bindattr_i): {
                MVMObject *obj = GET_REG(cur_op, 0).o;
                if (!IS_CONCRETE(obj))
                    MVM_exception_throw_adhoc(tc, "Cannot bind attributes in a type object");
                REPR(obj)->attr_funcs.bind_attribute(tc,
                    STABLE(obj), obj, OBJECT_BODY(obj),
                    GET_REG(cur_op, 2).o, cu->body.strings[GET_UI32(cur_op, 4)],
                    GET_I16(cur_op, 10), GET_REG(cur_op, 8), MVM_reg_int64);
                MVM_SC_WB_OBJ(tc, obj);
                cur_op += 12;
                goto NEXT;
            }
            OP(bindattr_n): {
                MVMObject *obj = GET_REG(cur_op, 0).o;
                if (!IS_CONCRETE(obj))
                    MVM_exception_throw_adhoc(tc, "Cannot bind attributes in a type object");
                REPR(obj)->attr_funcs.bind_attribute(tc,
                    STABLE(obj), obj, OBJECT_BODY(obj),
                    GET_REG(cur_op, 2).o, cu->body.strings[GET_UI32(cur_op, 4)],
                    GET_I16(cur_op, 10), GET_REG(cur_op, 8), MVM_reg_num64);
                MVM_SC_WB_OBJ(tc, obj);
                cur_op += 12;
                goto NEXT;
            }
            OP(bindattr_s): {
                MVMObject *obj = GET_REG(cur_op, 0).o;
                if (!IS_CONCRETE(obj))
                    MVM_exception_throw_adhoc(tc, "Cannot bind attributes in a type object");
                REPR(obj)->attr_funcs.bind_attribute(tc,
                    STABLE(obj), obj, OBJECT_BODY(obj),
                    GET_REG(cur_op, 2).o, cu->body.strings[GET_UI32(cur_op, 4)],
                    GET_I16(cur_op, 10), GET_REG(cur_op, 8), MVM_reg_str);
                MVM_SC_WB_OBJ(tc, obj);
                cur_op += 12;
                goto NEXT;
            }
            OP(bindattr_o): {
                MVMObject *obj = GET_REG(cur_op, 0).o;
                if (!IS_CONCRETE(obj))
                    MVM_exception_throw_adhoc(tc, "Cannot bind attributes in a type object");
                REPR(obj)->attr_funcs.bind_attribute(tc,
                    STABLE(obj), obj, OBJECT_BODY(obj),
                    GET_REG(cur_op, 2).o, cu->body.strings[GET_UI32(cur_op, 4)],
                    GET_I16(cur_op, 10), GET_REG(cur_op, 8), MVM_reg_obj);
                MVM_SC_WB_OBJ(tc, obj);
                cur_op += 12;
                goto NEXT;
            }
            OP(bindattrs_i): {
                MVMObject *obj = GET_REG(cur_op, 0).o;
                if (!IS_CONCRETE(obj))
                    MVM_exception_throw_adhoc(tc, "Cannot bind attributes in a type object");
                REPR(obj)->attr_funcs.bind_attribute(tc,
                    STABLE(obj), obj, OBJECT_BODY(obj),
                    GET_REG(cur_op, 2).o, GET_REG(cur_op, 4).s,
                    -1, GET_REG(cur_op, 6), MVM_reg_int64);
                MVM_SC_WB_OBJ(tc, obj);
                cur_op += 8;
                goto NEXT;
            }
            OP(bindattrs_n): {
                MVMObject *obj = GET_REG(cur_op, 0).o;
                if (!IS_CONCRETE(obj))
                    MVM_exception_throw_adhoc(tc, "Cannot bind attributes in a type object");
                REPR(obj)->attr_funcs.bind_attribute(tc,
                    STABLE(obj), obj, OBJECT_BODY(obj),
                    GET_REG(cur_op, 2).o, GET_REG(cur_op, 4).s,
                    -1, GET_REG(cur_op, 6), MVM_reg_num64);
                MVM_SC_WB_OBJ(tc, obj);
                cur_op += 8;
                goto NEXT;
            }
            OP(bindattrs_s): {
                MVMObject *obj = GET_REG(cur_op, 0).o;
                if (!IS_CONCRETE(obj))
                    MVM_exception_throw_adhoc(tc, "Cannot bind attributes in a type object");
                REPR(obj)->attr_funcs.bind_attribute(tc,
                    STABLE(obj), obj, OBJECT_BODY(obj),
                    GET_REG(cur_op, 2).o, GET_REG(cur_op, 4).s,
                    -1, GET_REG(cur_op, 6), MVM_reg_str);
                MVM_SC_WB_OBJ(tc, obj);
                cur_op += 8;
                goto NEXT;
            }
            OP(bindattrs_o): {
                MVMObject *obj = GET_REG(cur_op, 0).o;
                if (!IS_CONCRETE(obj))
                    MVM_exception_throw_adhoc(tc, "Cannot bind attributes in a type object");
                REPR(obj)->attr_funcs.bind_attribute(tc,
                    STABLE(obj), obj, OBJECT_BODY(obj),
                    GET_REG(cur_op, 2).o, GET_REG(cur_op, 4).s,
                    -1, GET_REG(cur_op, 6), MVM_reg_obj);
                MVM_SC_WB_OBJ(tc, obj);
                cur_op += 8;
                goto NEXT;
            }
            OP(getattr_i): {
                MVMObject *obj = GET_REG(cur_op, 2).o;
                if (!IS_CONCRETE(obj))
                    MVM_exception_throw_adhoc(tc, "Cannot look up attributes in a type object");
                REPR(obj)->attr_funcs.get_attribute(tc,
                    STABLE(obj), obj, OBJECT_BODY(obj),
                    GET_REG(cur_op, 4).o, cu->body.strings[GET_UI32(cur_op, 6)],
                    GET_I16(cur_op, 10), &GET_REG(cur_op, 0), MVM_reg_int64);
                cur_op += 12;
                goto NEXT;
            }
            OP(getattr_n): {
                MVMObject *obj = GET_REG(cur_op, 2).o;
                if (!IS_CONCRETE(obj))
                    MVM_exception_throw_adhoc(tc, "Cannot look up attributes in a type object");
                REPR(obj)->attr_funcs.get_attribute(tc,
                    STABLE(obj), obj, OBJECT_BODY(obj),
                    GET_REG(cur_op, 4).o, cu->body.strings[GET_UI32(cur_op, 6)],
                    GET_I16(cur_op, 10), &GET_REG(cur_op, 0), MVM_reg_num64);
                cur_op += 12;
                goto NEXT;
            }
            OP(getattr_s): {
                MVMObject *obj = GET_REG(cur_op, 2).o;
                if (!IS_CONCRETE(obj))
                    MVM_exception_throw_adhoc(tc, "Cannot look up attributes in a type object");
                REPR(obj)->attr_funcs.get_attribute(tc,
                    STABLE(obj), obj, OBJECT_BODY(obj),
                    GET_REG(cur_op, 4).o, cu->body.strings[GET_UI32(cur_op, 6)],
                    GET_I16(cur_op, 10), &GET_REG(cur_op, 0), MVM_reg_str);
                cur_op += 12;
                goto NEXT;
            }
            OP(getattr_o): {
                MVMObject *obj = GET_REG(cur_op, 2).o;
                if (!IS_CONCRETE(obj))
                    MVM_exception_throw_adhoc(tc, "Cannot look up attributes in a type object");
                REPR(obj)->attr_funcs.get_attribute(tc,
                    STABLE(obj), obj, OBJECT_BODY(obj),
                    GET_REG(cur_op, 4).o, cu->body.strings[GET_UI32(cur_op, 6)],
                    GET_I16(cur_op, 10), &GET_REG(cur_op, 0), MVM_reg_obj);
                cur_op += 12;
                goto NEXT;
            }
            OP(getattrs_i): {
                MVMObject *obj = GET_REG(cur_op, 2).o;
                if (!IS_CONCRETE(obj))
                    MVM_exception_throw_adhoc(tc, "Cannot look up attributes in a type object");
                REPR(obj)->attr_funcs.get_attribute(tc,
                    STABLE(obj), obj, OBJECT_BODY(obj),
                    GET_REG(cur_op, 4).o, GET_REG(cur_op, 6).s,
                    -1, &GET_REG(cur_op, 0), MVM_reg_int64);
                cur_op += 8;
                goto NEXT;
            }
            OP(getattrs_n): {
                MVMObject *obj = GET_REG(cur_op, 2).o;
                if (!IS_CONCRETE(obj))
                    MVM_exception_throw_adhoc(tc, "Cannot look up attributes in a type object");
                REPR(obj)->attr_funcs.get_attribute(tc,
                    STABLE(obj), obj, OBJECT_BODY(obj),
                    GET_REG(cur_op, 4).o, GET_REG(cur_op, 6).s,
                    -1, &GET_REG(cur_op, 0), MVM_reg_num64);
                cur_op += 8;
                goto NEXT;
            }
            OP(getattrs_s): {
                MVMObject *obj = GET_REG(cur_op, 2).o;
                if (!IS_CONCRETE(obj))
                    MVM_exception_throw_adhoc(tc, "Cannot look up attributes in a type object");
                REPR(obj)->attr_funcs.get_attribute(tc,
                    STABLE(obj), obj, OBJECT_BODY(obj),
                    GET_REG(cur_op, 4).o, GET_REG(cur_op, 6).s,
                    -1, &GET_REG(cur_op, 0), MVM_reg_str);
                cur_op += 8;
                goto NEXT;
            }
            OP(getattrs_o): {
                MVMObject *obj = GET_REG(cur_op, 2).o;
                if (!IS_CONCRETE(obj))
                    MVM_exception_throw_adhoc(tc, "Cannot look up attributes in a type object");
                REPR(obj)->attr_funcs.get_attribute(tc,
                    STABLE(obj), obj, OBJECT_BODY(obj),
                    GET_REG(cur_op, 4).o, GET_REG(cur_op, 6).s,
                    -1, &GET_REG(cur_op, 0), MVM_reg_obj);
                cur_op += 8;
                goto NEXT;
            }
            OP(hintfor): {
                MVMObject *obj = GET_REG(cur_op, 2).o;
                GET_REG(cur_op, 0).i64 = REPR(obj)->attr_funcs.hint_for(tc,
                    STABLE(obj), obj,
                    GET_REG(cur_op, 4).s);
                cur_op += 6;
                goto NEXT;
            }
            OP(isnull): {
                MVMObject *obj = GET_REG(cur_op, 2).o;
                GET_REG(cur_op, 0).i64 = MVM_is_null(tc, obj);
                cur_op += 4;
                goto NEXT;
            }
            OP(knowhowattr):
                GET_REG(cur_op, 0).o = tc->instance->KnowHOWAttribute;
                cur_op += 2;
                goto NEXT;
            OP(iscoderef):
                GET_REG(cur_op, 0).i64 = !GET_REG(cur_op, 2).o ||
                    STABLE(GET_REG(cur_op, 2).o)->invoke == MVM_6model_invoke_default ? 0 : 1;
                cur_op += 4;
                goto NEXT;
            OP(null):
                GET_REG(cur_op, 0).o = tc->instance->VMNull;
                cur_op += 2;
                goto NEXT;
            OP(clone): {
                MVMObject *value = GET_REG(cur_op, 2).o;
                if (IS_CONCRETE(value)) {
                    MVMROOT(tc, value, {
                        MVMObject *cloned = REPR(value)->allocate(tc, STABLE(value));
                        /* Ordering here matters. We write the object into the
                        * register before calling copy_to. This is because
                        * if copy_to allocates, obj may have moved after
                        * we called it. This saves us having to put things on
                        * the temporary stack. The GC will know to update it
                        * in the register if it moved. */
                        GET_REG(cur_op, 0).o = cloned;
                        REPR(value)->copy_to(tc, STABLE(value), OBJECT_BODY(value), cloned, OBJECT_BODY(cloned));
                    });
                }
                else {
                    GET_REG(cur_op, 0).o = value;
                }
                cur_op += 4;
                goto NEXT;
            }
            OP(isnull_s):
                GET_REG(cur_op, 0).i64 = GET_REG(cur_op, 2).s ? 0 : 1;
                cur_op += 4;
                goto NEXT;
            OP(bootint):
                GET_REG(cur_op, 0).o = tc->instance->boot_types.BOOTInt;
                cur_op += 2;
                goto NEXT;
            OP(bootnum):
                GET_REG(cur_op, 0).o = tc->instance->boot_types.BOOTNum;
                cur_op += 2;
                goto NEXT;
            OP(bootstr):
                GET_REG(cur_op, 0).o = tc->instance->boot_types.BOOTStr;
                cur_op += 2;
                goto NEXT;
            OP(bootarray):
                GET_REG(cur_op, 0).o = tc->instance->boot_types.BOOTArray;
                cur_op += 2;
                goto NEXT;
            OP(boothash):
                GET_REG(cur_op, 0).o = tc->instance->boot_types.BOOTHash;
                cur_op += 2;
                goto NEXT;
            OP(sethllconfig):
                MVM_hll_set_config(tc, GET_REG(cur_op, 0).s, GET_REG(cur_op, 2).o);
                cur_op += 4;
                goto NEXT;
            OP(hllboxtype_i):
                GET_REG(cur_op, 0).o = cu->body.hll_config->int_box_type;
                cur_op += 2;
                goto NEXT;
            OP(hllboxtype_n):
                GET_REG(cur_op, 0).o = cu->body.hll_config->num_box_type;
                cur_op += 2;
                goto NEXT;
            OP(hllboxtype_s):
                GET_REG(cur_op, 0).o = cu->body.hll_config->str_box_type;
                cur_op += 2;
                goto NEXT;
            OP(elems): {
                MVMObject *obj = GET_REG(cur_op, 2).o;
                GET_REG(cur_op, 0).i64 = (MVMint64)REPR(obj)->elems(tc, STABLE(obj), obj, OBJECT_BODY(obj));
                cur_op += 4;
                goto NEXT;
            }
            OP(null_s):
                GET_REG(cur_op, 0).s = NULL;
                cur_op += 2;
                goto NEXT;
            OP(newtype): {
                MVMObject *how = GET_REG(cur_op, 2).o;
                MVMString *repr_name = GET_REG(cur_op, 4).s;
                const MVMREPROps *repr = MVM_repr_get_by_name(tc, repr_name);
                GET_REG(cur_op, 0).o = repr->type_object_for(tc, how);
                cur_op += 6;
                goto NEXT;
            }
            OP(isint): {
                MVMObject *obj = GET_REG(cur_op, 2).o;
                GET_REG(cur_op, 0).i64 = obj && REPR(obj)->ID == MVM_REPR_ID_P6int ? 1 : 0;
                cur_op += 4;
                goto NEXT;
            }
            OP(isnum): {
                MVMObject *obj = GET_REG(cur_op, 2).o;
                GET_REG(cur_op, 0).i64 = obj && REPR(obj)->ID == MVM_REPR_ID_P6num ? 1 : 0;
                cur_op += 4;
                goto NEXT;
            }
            OP(isstr): {
                MVMObject *obj = GET_REG(cur_op, 2).o;
                GET_REG(cur_op, 0).i64 = obj && REPR(obj)->ID == MVM_REPR_ID_P6str ? 1 : 0;
                cur_op += 4;
                goto NEXT;
            }
            OP(islist): {
                MVMObject *obj = GET_REG(cur_op, 2).o;
                GET_REG(cur_op, 0).i64 = obj && REPR(obj)->ID == MVM_REPR_ID_MVMArray ? 1 : 0;
                cur_op += 4;
                goto NEXT;
            }
            OP(ishash): {
                MVMObject *obj = GET_REG(cur_op, 2).o;
                GET_REG(cur_op, 0).i64 = obj && REPR(obj)->ID == MVM_REPR_ID_MVMHash ? 1 : 0;
                cur_op += 4;
                goto NEXT;
            }
            OP(iter): {
                GET_REG(cur_op, 0).o = MVM_iter(tc, GET_REG(cur_op, 2).o);
                cur_op += 4;
                goto NEXT;
            }
            OP(iterkey_s): {
                GET_REG(cur_op, 0).s = MVM_iterkey_s(tc, (MVMIter *)GET_REG(cur_op, 2).o);
                cur_op += 4;
                goto NEXT;
            }
            OP(iterval): {
                GET_REG(cur_op, 0).o = MVM_iterval(tc, (MVMIter *)GET_REG(cur_op, 2).o);
                cur_op += 4;
                goto NEXT;
            }
            OP(getcodename): {
                MVMCode *c = (MVMCode *)GET_REG(cur_op, 2).o;
                GET_REG(cur_op, 0).s = c->body.name;
                cur_op += 4;
                goto NEXT;
            }
            OP(composetype): {
                MVMObject *obj = GET_REG(cur_op, 2).o;
                REPR(obj)->compose(tc, STABLE(obj), GET_REG(cur_op, 4).o);
                GET_REG(cur_op, 0).o = GET_REG(cur_op, 2).o;
                cur_op += 6;
                goto NEXT;
            }
            OP(setmethcache): {
                MVMObject *iter = MVM_iter(tc, GET_REG(cur_op, 2).o);
                MVMObject *cache;
                MVMSTable *stable;
                MVMROOT(tc, iter, {
                    cache = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTHash);
                });

                while (MVM_iter_istrue(tc, (MVMIter *)iter)) {
                    MVMRegister result;
                    REPR(iter)->pos_funcs.shift(tc, STABLE(iter), iter,
                        OBJECT_BODY(iter), &result, MVM_reg_obj);
                    MVM_repr_bind_key_o(tc, cache, MVM_iterkey_s(tc, (MVMIter *)iter),
                        MVM_iterval(tc, (MVMIter *)iter));
                }

                stable = STABLE(GET_REG(cur_op, 0).o);
                MVM_ASSIGN_REF(tc, &(stable->header), stable->method_cache, cache);
                stable->method_cache_sc = NULL;
                MVM_SC_WB_ST(tc, stable);

                cur_op += 4;
                goto NEXT;
            }
            OP(setmethcacheauth): {
                MVMObject *obj = GET_REG(cur_op, 0).o;
                MVMint64 new_flags = STABLE(obj)->mode_flags & (~MVM_METHOD_CACHE_AUTHORITATIVE);
                MVMint64 flag = GET_REG(cur_op, 2).i64;
                if (flag != 0)
                    new_flags |= MVM_METHOD_CACHE_AUTHORITATIVE;
                STABLE(obj)->mode_flags = new_flags;
                MVM_SC_WB_ST(tc, STABLE(obj));
                cur_op += 4;
                goto NEXT;
            }
            OP(settypecache): {
                MVMObject *obj    = GET_REG(cur_op, 0).o;
                MVMObject *types  = GET_REG(cur_op, 2).o;
                MVMSTable *st     = STABLE(obj);
                MVMint64 i, elems = REPR(types)->elems(tc, STABLE(types), types, OBJECT_BODY(types));
                MVMObject **cache = MVM_malloc(sizeof(MVMObject *) * elems);
                for (i = 0; i < elems; i++) {
                    MVM_ASSIGN_REF(tc, &(st->header), cache[i], MVM_repr_at_pos_o(tc, types, i));
                }
                /* technically this free isn't thread safe */
                if (st->type_check_cache)
                    MVM_free(st->type_check_cache);
                st->type_check_cache = cache;
                st->type_check_cache_length = (MVMuint16)elems;
                MVM_SC_WB_ST(tc, st);
                cur_op += 4;
                goto NEXT;
            }
            OP(setinvokespec): {
                MVMObject *obj = GET_REG(cur_op, 0).o, *ch = GET_REG(cur_op, 2).o,
                    *invocation_handler = GET_REG(cur_op, 6).o;
                MVMString *name = GET_REG(cur_op, 4).s;
                MVMInvocationSpec *is = MVM_calloc(1, sizeof(MVMInvocationSpec));
                MVMSTable *st = STABLE(obj);
                MVM_ASSIGN_REF(tc, &(st->header), is->class_handle, ch);
                MVM_ASSIGN_REF(tc, &(st->header), is->attr_name, name);
                if (ch && name)
                    is->hint = REPR(ch)->attr_funcs.hint_for(tc, STABLE(ch), ch, name);
                MVM_ASSIGN_REF(tc, &(st->header), is->invocation_handler, invocation_handler);
                /* XXX not thread safe, but this should occur on non-shared objects anyway... */
                if (st->invocation_spec)
                    MVM_free(st->invocation_spec);
                st->invocation_spec = is;
                cur_op += 8;
                goto NEXT;
            }
            OP(isinvokable): {
                MVMSTable *st = STABLE(GET_REG(cur_op, 2).o);
                GET_REG(cur_op, 0).i64 = st->invoke == MVM_6model_invoke_default
                    ? (st->invocation_spec ? 1 : 0)
                    : 1;
                cur_op += 4;
                goto NEXT;
            }
            OP(iscont): {
                MVMObject *obj = GET_REG(cur_op, 2).o;
                GET_REG(cur_op, 0).i64 = MVM_is_null(tc, obj) || STABLE(obj)->container_spec == NULL ? 0 : 1;
                cur_op += 4;
                goto NEXT;
            }
            OP(decont): {
                MVMObject *obj = GET_REG(cur_op, 2).o;
                MVMRegister *r = &GET_REG(cur_op, 0);
                cur_op += 4;
                if (obj && IS_CONCRETE(obj) && STABLE(obj)->container_spec)
                    STABLE(obj)->container_spec->fetch(tc, obj, r);
                else
                    r->o = obj;
                goto NEXT;
            }
            OP(setboolspec): {
                MVMSTable            *st = GET_REG(cur_op, 0).o->st;
                MVMBoolificationSpec *bs = MVM_malloc(sizeof(MVMBoolificationSpec));
                bs->mode = (MVMuint32)GET_REG(cur_op, 2).i64;
                MVM_ASSIGN_REF(tc, &(st->header), bs->method, GET_REG(cur_op, 4).o);
                st->boolification_spec = bs;
                cur_op += 6;
                goto NEXT;
            }
            OP(istrue): {
                /* Increment PC first then call coerce, since it may want to
                 * do an invocation. */
                MVMObject   *obj = GET_REG(cur_op, 2).o;
                MVMRegister *res = &GET_REG(cur_op, 0);
                cur_op += 4;
                MVM_coerce_istrue(tc, obj, res, NULL, NULL, 0);
                goto NEXT;
            }
            OP(isfalse): {
                /* Increment PC first then call coerce, since it may want to
                 * do an invocation. */
                MVMObject   *obj = GET_REG(cur_op, 2).o;
                MVMRegister *res = &GET_REG(cur_op, 0);
                cur_op += 4;
                MVM_coerce_istrue(tc, obj, res, NULL, NULL, 1);
                goto NEXT;
            }
            OP(istrue_s):
                GET_REG(cur_op, 0).i64 = MVM_coerce_istrue_s(tc, GET_REG(cur_op, 2).s);
                cur_op += 4;
                goto NEXT;
            OP(isfalse_s):
                GET_REG(cur_op, 0).i64 = MVM_coerce_istrue_s(tc, GET_REG(cur_op, 2).s) ? 0 : 1;
                cur_op += 4;
                goto NEXT;
            OP(getcodeobj): {
                MVMObject *obj = GET_REG(cur_op, 2).o;
                GET_REG(cur_op, 0).o = MVM_frame_get_code_object(tc, (MVMCode *)obj);
                cur_op += 4;
                goto NEXT;
            }
            OP(setcodeobj): {
                MVMObject *obj = GET_REG(cur_op, 0).o;
                if (REPR(obj)->ID == MVM_REPR_ID_MVMCode) {
                    MVM_ASSIGN_REF(tc, &(obj->header), ((MVMCode *)obj)->body.code_object,
                        GET_REG(cur_op, 2).o);
                }
                else {
                    MVM_exception_throw_adhoc(tc, "setcodeobj needs a code ref");
                }
                cur_op += 4;
                goto NEXT;
            }
            OP(setcodename): {
                MVMObject *obj = GET_REG(cur_op, 0).o;
                if (REPR(obj)->ID == MVM_REPR_ID_MVMCode) {
                    MVM_ASSIGN_REF(tc, &(obj->header), ((MVMCode *)obj)->body.name,
                        GET_REG(cur_op, 2).s);
                }
                else {
                    MVM_exception_throw_adhoc(tc, "setcodename needs a code ref");
                }
                cur_op += 4;
                goto NEXT;
            }
            OP(forceouterctx): {
                MVMObject *obj = GET_REG(cur_op, 0).o, *ctx = GET_REG(cur_op, 2).o;
                MVMFrame *orig;
                MVMFrame *context;
                MVMStaticFrame *sf;
                if (REPR(obj)->ID != MVM_REPR_ID_MVMCode || !IS_CONCRETE(obj)) {
                    MVM_exception_throw_adhoc(tc, "forceouterctx needs a code ref");
                }
                if (REPR(ctx)->ID != MVM_REPR_ID_MVMContext || !IS_CONCRETE(ctx)) {
                    MVM_exception_throw_adhoc(tc, "forceouterctx needs a context");
                }

                orig = ((MVMCode *)obj)->body.outer;
                sf = ((MVMCode *)obj)->body.sf;
                context = ((MVMContext *)ctx)->body.context;

                MVM_ASSIGN_REF(tc, &(((MVMObject *)sf)->header), sf->body.outer, context->static_info);
                if (orig != context) {
                    ((MVMCode *)obj)->body.outer = context;
                    MVM_frame_inc_ref(tc, context);
                    if (orig) {
                        orig = MVM_frame_dec_ref(tc, orig);
                    }
                }
                cur_op += 4;
                goto NEXT;
            }
            OP(getcomp): {
                MVMObject *obj = tc->instance->compiler_registry;
                uv_mutex_lock(&tc->instance->mutex_compiler_registry);
                GET_REG(cur_op, 0).o = MVM_repr_at_key_o(tc, obj, GET_REG(cur_op, 2).s);
                uv_mutex_unlock(&tc->instance->mutex_compiler_registry);
                cur_op += 4;
                goto NEXT;
            }
            OP(bindcomp): {
                MVMObject *obj = tc->instance->compiler_registry;
                uv_mutex_lock(&tc->instance->mutex_compiler_registry);
                REPR(obj)->ass_funcs.bind_key(tc, STABLE(obj), obj, OBJECT_BODY(obj),
                    (MVMObject *)GET_REG(cur_op, 2).s, GET_REG(cur_op, 4), MVM_reg_obj);
                uv_mutex_unlock(&tc->instance->mutex_compiler_registry);
                GET_REG(cur_op, 0).o = GET_REG(cur_op, 4).o;
                cur_op += 6;
                goto NEXT;
            }
            OP(getcurhllsym): {
                MVMString *hll_name = tc->cur_frame->static_info->body.cu->body.hll_name;
                GET_REG(cur_op, 0).o = MVM_hll_sym_get(tc, hll_name, GET_REG(cur_op, 2).s);
                cur_op += 4;
                goto NEXT;
            }
            OP(bindcurhllsym): {
                MVMObject *syms = tc->instance->hll_syms, *hash;
                MVMString *hll_name = tc->cur_frame->static_info->body.cu->body.hll_name;
                uv_mutex_lock(&tc->instance->mutex_hll_syms);
                hash = MVM_repr_at_key_o(tc, syms, hll_name);
                if (MVM_is_null(tc, hash)) {
                    hash = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTHash);
                    /* must re-get syms in case it moved */
                    syms = tc->instance->hll_syms;
                    hll_name = tc->cur_frame->static_info->body.cu->body.hll_name;
                    MVM_repr_bind_key_o(tc, syms, hll_name, hash);
                }
                MVM_repr_bind_key_o(tc, hash, GET_REG(cur_op, 2).s, GET_REG(cur_op, 4).o);
                GET_REG(cur_op, 0).o = GET_REG(cur_op, 4).o;
                uv_mutex_unlock(&tc->instance->mutex_hll_syms);
                cur_op += 6;
                goto NEXT;
            }
            OP(getwho): {
                MVMObject *who = STABLE(GET_REG(cur_op, 2).o)->WHO;
                GET_REG(cur_op, 0).o = who ? who : tc->instance->VMNull;
                cur_op += 4;
                goto NEXT;
            }
            OP(setwho): {
                MVMSTable *st = STABLE(GET_REG(cur_op, 2).o);
                MVM_ASSIGN_REF(tc, &(st->header), st->WHO, GET_REG(cur_op, 4).o);
                GET_REG(cur_op, 0).o = GET_REG(cur_op, 2).o;
                cur_op += 6;
                goto NEXT;
            }
            OP(rebless):
                if (!REPR(GET_REG(cur_op, 2).o)->change_type) {
                    MVM_exception_throw_adhoc(tc, "This REPR cannot change type");
                }
                REPR(GET_REG(cur_op, 2).o)->change_type(tc, GET_REG(cur_op, 2).o, GET_REG(cur_op, 4).o);
                GET_REG(cur_op, 0).o = GET_REG(cur_op, 2).o;
                MVM_SC_WB_OBJ(tc, GET_REG(cur_op, 0).o);
                cur_op += 6;
                MVM_spesh_deopt_all(tc);
                goto NEXT;
            OP(istype): {
                /* Increment PC first, as we may make a method call. */
                MVMRegister *res  = &GET_REG(cur_op, 0);
                MVMObject   *obj  = GET_REG(cur_op, 2).o;
                MVMObject   *type = GET_REG(cur_op, 4).o;
                cur_op += 6;
                MVM_6model_istype(tc, obj, type, res);
                goto NEXT;
            }
            OP(ctx): {
                MVMObject *ctx = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTContext);
                ((MVMContext *)ctx)->body.context = MVM_frame_inc_ref(tc, tc->cur_frame);
                GET_REG(cur_op, 0).o = ctx;
                cur_op += 2;
                goto NEXT;
            }
            OP(ctxouter): {
                MVMObject *this_ctx = GET_REG(cur_op, 2).o, *ctx;
                MVMFrame *frame;
                if (!IS_CONCRETE(this_ctx) || REPR(this_ctx)->ID != MVM_REPR_ID_MVMContext) {
                    MVM_exception_throw_adhoc(tc, "ctxouter needs an MVMContext");
                }
                if ((frame = ((MVMContext *)this_ctx)->body.context->outer)) {
                    ctx = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTContext);
                    ((MVMContext *)ctx)->body.context = MVM_frame_inc_ref(tc, frame);
                    GET_REG(cur_op, 0).o = ctx;
                }
                else {
                    GET_REG(cur_op, 0).o = tc->instance->VMNull;
                }
                cur_op += 4;
                goto NEXT;
            }
            OP(ctxcaller): {
                MVMObject *this_ctx = GET_REG(cur_op, 2).o, *ctx = NULL;
                MVMFrame *frame;
                if (!IS_CONCRETE(this_ctx) || REPR(this_ctx)->ID != MVM_REPR_ID_MVMContext) {
                    MVM_exception_throw_adhoc(tc, "ctxcaller needs an MVMContext");
                }
                if ((frame = ((MVMContext *)this_ctx)->body.context->caller)) {
                    ctx = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTContext);
                    ((MVMContext *)ctx)->body.context = MVM_frame_inc_ref(tc, frame);
                }
                GET_REG(cur_op, 0).o = ctx ? ctx : tc->instance->VMNull;
                cur_op += 4;
                goto NEXT;
            }
            OP(ctxlexpad): {
                MVMObject *this_ctx = GET_REG(cur_op, 2).o;
                if (!IS_CONCRETE(this_ctx) || REPR(this_ctx)->ID != MVM_REPR_ID_MVMContext) {
                    MVM_exception_throw_adhoc(tc, "ctxlexpad needs an MVMContext");
                }
                GET_REG(cur_op, 0).o = this_ctx;
                cur_op += 4;
                goto NEXT;
            }
            OP(curcode):
                GET_REG(cur_op, 0).o = tc->cur_frame->code_ref;
                cur_op += 2;
                goto NEXT;
            OP(callercode): {
                GET_REG(cur_op, 0).o = tc->cur_frame->caller
                    ? tc->cur_frame->caller->code_ref
                    : tc->instance->VMNull;
                cur_op += 2;
                goto NEXT;
            }
            OP(bootintarray):
                GET_REG(cur_op, 0).o = tc->instance->boot_types.BOOTIntArray;
                cur_op += 2;
                goto NEXT;
            OP(bootnumarray):
                GET_REG(cur_op, 0).o = tc->instance->boot_types.BOOTNumArray;
                cur_op += 2;
                goto NEXT;
            OP(bootstrarray):
                GET_REG(cur_op, 0).o = tc->instance->boot_types.BOOTStrArray;
                cur_op += 2;
                goto NEXT;
            OP(hlllist):
                GET_REG(cur_op, 0).o = cu->body.hll_config->slurpy_array_type;
                cur_op += 2;
                goto NEXT;
            OP(hllhash):
                GET_REG(cur_op, 0).o = cu->body.hll_config->slurpy_hash_type;
                cur_op += 2;
                goto NEXT;
            OP(attrinited): {
                MVMObject *obj = GET_REG(cur_op, 2).o;
                if (!IS_CONCRETE(obj))
                    MVM_exception_throw_adhoc(tc, "Cannot look up attributes in a type object");
                GET_REG(cur_op, 0).i64 = REPR(obj)->attr_funcs.is_attribute_initialized(tc,
                    STABLE(obj), OBJECT_BODY(obj),
                    GET_REG(cur_op, 4).o, GET_REG(cur_op, 6).s, MVM_NO_HINT);
                cur_op += 8;
                goto NEXT;
            }
            OP(setcontspec): {
                MVMSTable *st   = STABLE(GET_REG(cur_op, 0).o);
                MVMString *name = GET_REG(cur_op, 2).s;
                const MVMContainerConfigurer *cc = MVM_6model_get_container_config(tc, name);
                if (cc == NULL)
                    MVM_exception_throw_adhoc(tc, "Cannot use unknown container spec %s",
                        MVM_string_utf8_encode_C_string(tc, name));
                if (st->container_spec)
                    MVM_exception_throw_adhoc(tc,
                        "Cannot change a type's container specification");

                cc->set_container_spec(tc, st);
                cc->configure_container_spec(tc, st, GET_REG(cur_op, 4).o);
                cur_op += 6;
                goto NEXT;
            }
            OP(existspos): {
                MVMObject * const obj = GET_REG(cur_op, 2).o;
                GET_REG(cur_op, 0).i64 = REPR(obj)->pos_funcs.exists_pos(tc,
                    STABLE(obj), obj, OBJECT_BODY(obj), GET_REG(cur_op, 4).i64);
                cur_op += 6;
                goto NEXT;
            }
            OP(gethllsym):
                GET_REG(cur_op, 0).o = MVM_hll_sym_get(tc,
                    GET_REG(cur_op, 2).s, GET_REG(cur_op, 4).s);
                cur_op += 6;
                goto NEXT;
            OP(freshcoderef): {
                MVMObject * const cr = GET_REG(cur_op, 2).o;
                MVMCode *ncr;
                if (REPR(cr)->ID != MVM_REPR_ID_MVMCode)
                    MVM_exception_throw_adhoc(tc, "freshcoderef requires a coderef");
                ncr = (MVMCode *)(GET_REG(cur_op, 0).o = MVM_repr_clone(tc, cr));
                MVMROOT(tc, ncr, {
                    MVMStaticFrame *nsf;
                    if (!ncr->body.sf->body.fully_deserialized)
                        MVM_bytecode_finish_frame(tc, ncr->body.sf->body.cu, ncr->body.sf, 0);
                    nsf = (MVMStaticFrame *)MVM_repr_clone(tc,
                        (MVMObject *)ncr->body.sf);
                    MVM_ASSIGN_REF(tc, &(ncr->common.header), ncr->body.sf, nsf);
                    MVM_ASSIGN_REF(tc, &(ncr->common.header), ncr->body.sf->body.static_code, ncr);
                });
                cur_op += 4;
                goto NEXT;
            }
            OP(markcodestatic): {
                MVMObject * const cr = GET_REG(cur_op, 0).o;
                if (REPR(cr)->ID != MVM_REPR_ID_MVMCode)
                    MVM_exception_throw_adhoc(tc, "markcodestatic requires a coderef");
                ((MVMCode *)cr)->body.is_static = 1;
                cur_op += 2;
                goto NEXT;
            }
            OP(markcodestub): {
                MVMObject * const cr = GET_REG(cur_op, 0).o;
                if (REPR(cr)->ID != MVM_REPR_ID_MVMCode)
                    MVM_exception_throw_adhoc(tc, "markcodestub requires a coderef");
                ((MVMCode *)cr)->body.is_compiler_stub = 1;
                cur_op += 2;
                goto NEXT;
            }
            OP(getstaticcode): {
                MVMObject * const cr = GET_REG(cur_op, 2).o;
                if (REPR(cr)->ID != MVM_REPR_ID_MVMCode)
                    MVM_exception_throw_adhoc(tc, "getstaticcode requires a static coderef");
                GET_REG(cur_op, 0).o = (MVMObject *)((MVMCode *)cr)->body.sf->body.static_code;
                cur_op += 4;
                goto NEXT;
            }
            OP(getcodecuid): {
                MVMObject * const cr = GET_REG(cur_op, 2).o;
                if (REPR(cr)->ID != MVM_REPR_ID_MVMCode)
                    MVM_exception_throw_adhoc(tc, "getcodecuid requires a static coderef");
                GET_REG(cur_op, 0).s = ((MVMCode *)cr)->body.sf->body.cuuid;
                cur_op += 4;
                goto NEXT;
            }
            OP(copy_f):
                MVM_file_copy(tc, GET_REG(cur_op, 0).s, GET_REG(cur_op, 2).s);
                cur_op += 4;
                goto NEXT;
            OP(append_f):
                MVM_exception_throw_adhoc(tc, "append is not supported");
                goto NEXT;
            OP(rename_f):
                MVM_file_rename(tc, GET_REG(cur_op, 0).s, GET_REG(cur_op, 2).s);
                cur_op += 4;
                goto NEXT;
            OP(delete_f):
                MVM_file_delete(tc, GET_REG(cur_op, 0).s);
                cur_op += 2;
                goto NEXT;
            OP(chmod_f):
                MVM_file_chmod(tc, GET_REG(cur_op, 0).s, GET_REG(cur_op, 2).i64);
                cur_op += 4;
                goto NEXT;
            OP(exists_f):
                GET_REG(cur_op, 0).i64 = MVM_file_exists(tc, GET_REG(cur_op, 2).s, 0);
                cur_op += 4;
                goto NEXT;
            OP(mkdir):
                MVM_dir_mkdir(tc, GET_REG(cur_op, 0).s, GET_REG(cur_op, 2).i64);
                cur_op += 4;
                goto NEXT;
            OP(rmdir):
                MVM_dir_rmdir(tc, GET_REG(cur_op, 0).s);
                cur_op += 2;
                goto NEXT;
            OP(open_dir):
                GET_REG(cur_op, 0).o = MVM_dir_open(tc, GET_REG(cur_op, 2).s);
                cur_op += 4;
                goto NEXT;
            OP(read_dir):
                GET_REG(cur_op, 0).s = MVM_dir_read(tc, GET_REG(cur_op, 2).o);
                cur_op += 4;
                goto NEXT;
            OP(close_dir):
                MVM_dir_close(tc, GET_REG(cur_op, 0).o);
                cur_op += 2;
                goto NEXT;
            OP(open_fh):
                GET_REG(cur_op, 0).o = MVM_file_open_fh(tc, GET_REG(cur_op, 2).s, GET_REG(cur_op, 4).s);
                cur_op += 6;
                goto NEXT;
            OP(close_fh):
                MVM_io_close(tc, GET_REG(cur_op, 0).o);
                cur_op += 2;
                goto NEXT;
            OP(read_fhs):
                GET_REG(cur_op, 0).s = MVM_io_read_string(tc, GET_REG(cur_op, 2).o,
                    GET_REG(cur_op, 4).i64);
                cur_op += 6;
                goto NEXT;
            OP(slurp):
                GET_REG(cur_op, 0).s = MVM_file_slurp(tc,
                    GET_REG(cur_op, 2).s, GET_REG(cur_op, 4).s);
                cur_op += 6;
                goto NEXT;
            OP(spew):
                MVM_file_spew(tc, GET_REG(cur_op, 0).s, GET_REG(cur_op, 2).s, GET_REG(cur_op, 4).s);
                cur_op += 6;
                goto NEXT;
            OP(write_fhs):
                GET_REG(cur_op, 0).i64 = MVM_io_write_string(tc, GET_REG(cur_op, 2).o, GET_REG(cur_op, 4).s, 0);
                cur_op += 6;
                goto NEXT;
            OP(seek_fh):
                MVM_io_seek(tc, GET_REG(cur_op, 0).o, GET_REG(cur_op, 2).i64,
                    GET_REG(cur_op, 4).i64);
                cur_op += 6;
                goto NEXT;
            OP(lock_fh):
                GET_REG(cur_op, 0).i64 = MVM_io_lock(tc, GET_REG(cur_op, 2).o,
                    GET_REG(cur_op, 4).i64);
                cur_op += 6;
                goto NEXT;
            OP(unlock_fh):
                MVM_io_unlock(tc, GET_REG(cur_op, 0).o);
                cur_op += 2;
                goto NEXT;
            OP(sync_fh):
                MVM_io_flush(tc, GET_REG(cur_op, 0).o);
                cur_op += 2;
                goto NEXT;
            OP(trunc_fh):
                MVM_io_truncate(tc, GET_REG(cur_op, 0).o, GET_REG(cur_op, 2).i64);
                cur_op += 4;
                goto NEXT;
            OP(eof_fh):
                GET_REG(cur_op, 0).i64 = MVM_io_eof(tc, GET_REG(cur_op, 2).o);
                cur_op += 4;
                goto NEXT;
            OP(getstdin):
                if (MVM_is_null(tc, tc->instance->stdin_handle))
                    MVM_exception_throw_adhoc(tc, "STDIN filehandle was never initialized");
                GET_REG(cur_op, 0).o = tc->instance->stdin_handle;
                cur_op += 2;
                goto NEXT;
            OP(getstdout):
                if (MVM_is_null(tc, tc->instance->stdout_handle))
                    MVM_exception_throw_adhoc(tc, "STDOUT filehandle was never initialized");
                GET_REG(cur_op, 0).o = tc->instance->stdout_handle;
                cur_op += 2;
                goto NEXT;
            OP(getstderr):
                if (MVM_is_null(tc, tc->instance->stderr_handle))
                    MVM_exception_throw_adhoc(tc, "STDERR filehandle was never initialized");
                GET_REG(cur_op, 0).o = tc->instance->stderr_handle;
                cur_op += 2;
                goto NEXT;
            OP(connect_sk):
                MVM_io_connect(tc, GET_REG(cur_op, 0).o,
                    GET_REG(cur_op, 2).s, GET_REG(cur_op, 4).i64);
                cur_op += 6;
                goto NEXT;
            OP(socket):
                GET_REG(cur_op, 0).o = MVM_io_socket_create(tc, GET_REG(cur_op, 2).i64);
                cur_op += 4;
                goto NEXT;
            OP(bind_sk):
                MVM_io_bind(tc, GET_REG(cur_op, 0).o,
                    GET_REG(cur_op, 2).s, GET_REG(cur_op, 4).i64);
                cur_op += 6;
                goto NEXT;
            OP(setinputlinesep_fh):
                MVM_io_set_separator(tc, GET_REG(cur_op, 0).o, GET_REG(cur_op, 2).s);
                cur_op += 4;
                goto NEXT;
            OP(accept_sk):
                GET_REG(cur_op, 0).o = MVM_io_accept(tc, GET_REG(cur_op, 2).o);
                cur_op += 4;
                goto NEXT;
            OP(decodetocodes):
            OP(encodefromcodes):
                MVM_exception_throw_adhoc(tc, "NYI");
            OP(setencoding):
                MVM_io_set_encoding(tc, GET_REG(cur_op, 0).o, GET_REG(cur_op, 2).s);
                cur_op += 4;
                goto NEXT;
            OP(print):
                MVM_string_print(tc, GET_REG(cur_op, 0).s);
                cur_op += 2;
                goto NEXT;
            OP(say):
                MVM_string_say(tc, GET_REG(cur_op, 0).s);
                cur_op += 2;
                goto NEXT;
            OP(readall_fh):
                GET_REG(cur_op, 0).s = MVM_io_slurp(tc, GET_REG(cur_op, 2).o);
                cur_op += 4;
                goto NEXT;
            OP(tell_fh):
                GET_REG(cur_op, 0).i64 = MVM_io_tell(tc, GET_REG(cur_op, 2).o);
                cur_op += 4;
                goto NEXT;
            OP(stat):
                GET_REG(cur_op, 0).i64 = MVM_file_stat(tc, GET_REG(cur_op, 2).s, GET_REG(cur_op, 4).i64, 0);
                cur_op += 6;
                goto NEXT;
            OP(readline_fh):
                GET_REG(cur_op, 0).s = MVM_io_readline(tc, GET_REG(cur_op, 2).o);
                cur_op += 4;
                goto NEXT;
            OP(readlineint_fh):
                cur_op += 6;
                goto NEXT;
            OP(chdir):
                MVM_dir_chdir(tc, GET_REG(cur_op, 0).s);
                cur_op += 2;
                goto NEXT;
            OP(rand_i):
                GET_REG(cur_op, 0).i64 = MVM_proc_rand_i(tc);
                cur_op += 2;
                goto NEXT;
            OP(rand_n):
                GET_REG(cur_op, 0).n64 = MVM_proc_rand_n(tc);
                cur_op += 2;
                goto NEXT;
            OP(srand):
                MVM_proc_seed(tc, GET_REG(cur_op, 0).i64);
                cur_op += 2;
                goto NEXT;
            OP(time_i):
                GET_REG(cur_op, 0).i64 = MVM_proc_time_i(tc);
                cur_op += 2;
                goto NEXT;
            OP(clargs):
                GET_REG(cur_op, 0).o = MVM_proc_clargs(tc);
                cur_op += 2;
                goto NEXT;
            OP(newthread):
                GET_REG(cur_op, 0).o = MVM_thread_new(tc, GET_REG(cur_op, 2).o,
                    GET_REG(cur_op, 4).i64);
                cur_op += 6;
                goto NEXT;
            OP(threadjoin):
                MVM_thread_join(tc, GET_REG(cur_op, 0).o);
                cur_op += 2;
                goto NEXT;
            OP(time_n):
                GET_REG(cur_op, 0).n64 = MVM_proc_time_n(tc);
                cur_op += 2;
                goto NEXT;
            OP(exit): {
                MVMint64 exit_code = GET_REG(cur_op, 0).i64;
                exit(exit_code);
            }
            OP(loadbytecode): {
                /* This op will end up returning into the runloop to run
                 * deserialization and load code, so make sure we're done
                 * processing this op really. */
                MVMString *filename = GET_REG(cur_op, 2).s;
                GET_REG(cur_op, 0).s = filename;
                cur_op += 4;

                /* Set up return (really continuation after load) address
                 * and enter bytecode loading process. */
                tc->cur_frame->return_address = cur_op;
                MVM_load_bytecode(tc, filename);
                goto NEXT;
            }
            OP(getenvhash):
                GET_REG(cur_op, 0).o = MVM_proc_getenvhash(tc);
                cur_op += 2;
                goto NEXT;
            OP(shell):
                GET_REG(cur_op, 0).i64 = MVM_proc_shell(tc, GET_REG(cur_op, 2).s,
                    GET_REG(cur_op, 4).s, GET_REG(cur_op, 6).o);
                cur_op += 8;
                goto NEXT;
            OP(cwd):
                GET_REG(cur_op, 0).s = MVM_dir_cwd(tc);
                cur_op += 2;
                goto NEXT;
            OP(sha1):
                GET_REG(cur_op, 0).s = MVM_sha1(tc,
                    GET_REG(cur_op, 2).s);
                cur_op += 4;
                goto NEXT;
            OP(createsc):
                GET_REG(cur_op, 0).o = MVM_sc_create(tc,
                    GET_REG(cur_op, 2).s);
                cur_op += 4;
                goto NEXT;
            OP(scsetobj): {
                MVMObject *sc  = GET_REG(cur_op, 0).o;
                MVMObject *obj = GET_REG(cur_op, 4).o;
                if (REPR(sc)->ID != MVM_REPR_ID_SCRef)
                    MVM_exception_throw_adhoc(tc,
                        "Must provide an SCRef operand to scsetobj");
                MVM_sc_set_object(tc, (MVMSerializationContext *)sc,
                    GET_REG(cur_op, 2).i64, obj);
                if (MVM_sc_get_stable_sc(tc, STABLE(obj)) == NULL) {
                    /* Need to claim the SC also; typical case for new type objects. */
                    MVMSTable *st = STABLE(obj);
                    MVM_sc_set_stable_sc(tc, st, (MVMSerializationContext *)sc);
                    MVM_sc_push_stable(tc, (MVMSerializationContext *)sc, st);
                }
                cur_op += 6;
                goto NEXT;
            }
            OP(scsetcode): {
                MVMObject *sc   = GET_REG(cur_op, 0).o;
                MVMObject *code = GET_REG(cur_op, 4).o;
                if (REPR(sc)->ID != MVM_REPR_ID_SCRef)
                    MVM_exception_throw_adhoc(tc,
                        "Must provide an SCRef operand to scsetcode");
                MVM_sc_set_code(tc, (MVMSerializationContext *)sc,
                    GET_REG(cur_op, 2).i64, code);
                MVM_sc_set_obj_sc(tc, code, (MVMSerializationContext *)sc);
                cur_op += 6;
                goto NEXT;
            }
            OP(scgetobj): {
                MVMObject *sc = GET_REG(cur_op, 2).o;
                if (REPR(sc)->ID != MVM_REPR_ID_SCRef)
                    MVM_exception_throw_adhoc(tc,
                        "Must provide an SCRef operand to scgetobj");
                GET_REG(cur_op, 0).o = MVM_sc_get_object(tc,
                    (MVMSerializationContext *)sc, GET_REG(cur_op, 4).i64);
                cur_op += 6;
                goto NEXT;
            }
            OP(scgethandle): {
                MVMObject *sc = GET_REG(cur_op, 2).o;
                if (REPR(sc)->ID != MVM_REPR_ID_SCRef)
                    MVM_exception_throw_adhoc(tc,
                        "Must provide an SCRef operand to scgethandle");
                GET_REG(cur_op, 0).s = MVM_sc_get_handle(tc,
                    (MVMSerializationContext *)sc);
                cur_op += 4;
                goto NEXT;
            }
            OP(scgetobjidx): {
                MVMObject *sc = GET_REG(cur_op, 2).o;
                if (REPR(sc)->ID != MVM_REPR_ID_SCRef)
                    MVM_exception_throw_adhoc(tc,
                        "Must provide an SCRef operand to scgetobjidx");
                GET_REG(cur_op, 0).i64 = MVM_sc_find_object_idx(tc,
                    (MVMSerializationContext *)sc, GET_REG(cur_op, 4).o);
                cur_op += 6;
                goto NEXT;
            }
            OP(scsetdesc): {
                MVMObject *sc   = GET_REG(cur_op, 0).o;
                MVMString *desc = GET_REG(cur_op, 2).s;
                if (REPR(sc)->ID != MVM_REPR_ID_SCRef)
                    MVM_exception_throw_adhoc(tc,
                        "Must provide an SCRef operand to scsetdesc");
                MVM_sc_set_description(tc, (MVMSerializationContext *)sc, desc);
                cur_op += 4;
                goto NEXT;
            }
            OP(scobjcount): {
                MVMObject *sc = GET_REG(cur_op, 2).o;
                if (REPR(sc)->ID != MVM_REPR_ID_SCRef)
                    MVM_exception_throw_adhoc(tc,
                        "Must provide an SCRef operand to scobjcount");
                GET_REG(cur_op, 0).i64 = MVM_sc_get_object_count(tc,
                    (MVMSerializationContext *)sc);
                cur_op += 4;
                goto NEXT;
            }
            OP(setobjsc): {
                MVMObject *obj = GET_REG(cur_op, 0).o;
                MVMObject *sc  = GET_REG(cur_op, 2).o;
                if (REPR(sc)->ID != MVM_REPR_ID_SCRef)
                    MVM_exception_throw_adhoc(tc,
                        "Must provide an SCRef operand to setobjsc");
                MVM_sc_set_obj_sc(tc, obj, (MVMSerializationContext *)sc);
                cur_op += 4;
                goto NEXT;
            }
            OP(getobjsc):
                GET_REG(cur_op, 0).o = (MVMObject *)MVM_sc_get_obj_sc(tc, GET_REG(cur_op, 2).o);
                cur_op += 4;
                goto NEXT;
            OP(serialize): {
                MVMObject *sc = GET_REG(cur_op, 2).o;
                MVMObject *obj = GET_REG(cur_op, 4).o;
                if (REPR(sc)->ID != MVM_REPR_ID_SCRef)
                    MVM_exception_throw_adhoc(tc,
                        "Must provide an SCRef operand to serialize");
                GET_REG(cur_op, 0).s = MVM_serialization_serialize(tc, (MVMSerializationContext *)sc, obj);
                cur_op += 6;
                goto NEXT;
            }
            OP(deserialize): {
                MVMString *blob = GET_REG(cur_op, 0).s;
                MVMObject *sc   = GET_REG(cur_op, 2).o;
                MVMObject *sh   = GET_REG(cur_op, 4).o;
                MVMObject *cr   = GET_REG(cur_op, 6).o;
                MVMObject *conf = GET_REG(cur_op, 8).o;
                if (REPR(sc)->ID != MVM_REPR_ID_SCRef)
                    MVM_exception_throw_adhoc(tc,
                        "Must provide an SCRef operand to deserialize");
                MVM_serialization_deserialize(tc, (MVMSerializationContext *)sc,
                    sh, cr, conf, blob);
                cur_op += 10;
                goto NEXT;
            }
            OP(wval): {
                MVMint16 dep = GET_I16(cur_op, 2);
                MVMint16 idx = GET_I16(cur_op, 4);
                GET_REG(cur_op, 0).o = MVM_sc_get_sc_object(tc, cu, dep, idx);
                cur_op += 6;
                goto NEXT;
            }
            OP(wval_wide): {
                MVMint16 dep = GET_I16(cur_op, 2);
                MVMint64 idx = MVM_BC_get_I64(cur_op, 4);
                GET_REG(cur_op, 0).o = MVM_sc_get_sc_object(tc, cu, dep, idx);
                cur_op += 12;
                goto NEXT;
            }
            OP(scwbdisable):
                GET_REG(cur_op, 0).i64 = ++tc->sc_wb_disable_depth;
                cur_op += 2;
                goto NEXT;
            OP(scwbenable):
                GET_REG(cur_op, 0).i64 = --tc->sc_wb_disable_depth;
                cur_op += 2;
                goto NEXT;
            OP(pushcompsc): {
                MVMObject * const sc  = GET_REG(cur_op, 0).o;
                if (REPR(sc)->ID != MVM_REPR_ID_SCRef)
                    MVM_exception_throw_adhoc(tc, "Can only push an SCRef with pushcompsc");
                if (MVM_is_null(tc, tc->compiling_scs)) {
                    MVMROOT(tc, sc, {
                        tc->compiling_scs = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTArray);
                    });
                }
                MVM_repr_unshift_o(tc, tc->compiling_scs, sc);
                cur_op += 2;
                goto NEXT;
            }
            OP(popcompsc): {
                MVMObject * const scs = tc->compiling_scs;
                if (MVM_is_null(tc, scs) || MVM_repr_elems(tc, scs) == 0)
                    MVM_exception_throw_adhoc(tc, "No current compiling SC");
                MVM_repr_shift_o(tc, tc->compiling_scs);
                cur_op += 2;
                goto NEXT;
            }
            OP(scgetdesc): {
                MVMObject *sc = GET_REG(cur_op, 2).o;
                if (REPR(sc)->ID != MVM_REPR_ID_SCRef)
                    MVM_exception_throw_adhoc(tc,
                        "Must provide an SCRef operand to scgetdesc");
                GET_REG(cur_op, 0).s = MVM_sc_get_description(tc,
                    (MVMSerializationContext *)sc);
                cur_op += 4;
                goto NEXT;
            }
            OP(rethrow): {
                MVM_exception_throwobj(tc, MVM_EX_THROW_DYN, GET_REG(cur_op, 0).o, NULL);
                goto NEXT;
            }
            OP(resume):
                /* Expect that resume will set the PC, so don't update cur_op
                 * here. */
                MVM_exception_resume(tc, GET_REG(cur_op, 0).o);
                goto NEXT;
            OP(settypehll):
                STABLE(GET_REG(cur_op, 0).o)->hll_owner = MVM_hll_get_config_for(tc,
                    GET_REG(cur_op, 2).s);
                cur_op += 4;
                goto NEXT;
            OP(settypehllrole):
                STABLE(GET_REG(cur_op, 0).o)->hll_role = GET_REG(cur_op, 2).i64;
                cur_op += 4;
                goto NEXT;
            OP(usecompileehllconfig):
                MVM_hll_enter_compilee_mode(tc);
                goto NEXT;
            OP(usecompilerhllconfig):
                MVM_hll_leave_compilee_mode(tc);
                goto NEXT;
            OP(encode):
                MVM_string_encode_to_buf(tc, GET_REG(cur_op, 2).s,
                    GET_REG(cur_op, 4).s, GET_REG(cur_op, 6).o);
                GET_REG(cur_op, 0).o = GET_REG(cur_op, 6).o;
                cur_op += 8;
                goto NEXT;
            OP(decode):
                GET_REG(cur_op, 0).s = MVM_string_decode_from_buf(tc,
                    GET_REG(cur_op, 2).o, GET_REG(cur_op, 4).s);
                cur_op += 6;
                goto NEXT;
            OP(bindhllsym): {
                MVMObject *syms     = tc->instance->hll_syms;
                MVMString *hll_name = GET_REG(cur_op, 0).s;
                MVMObject *hash;
                uv_mutex_lock(&tc->instance->mutex_hll_syms);
                hash = MVM_repr_at_key_o(tc, syms, hll_name);
                if (MVM_is_null(tc, hash)) {
                    hash = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTHash);
                    /* must re-get syms and HLL name in case it moved */
                    syms = tc->instance->hll_syms;
                    hll_name = GET_REG(cur_op, 0).s;
                    MVM_repr_bind_key_o(tc, syms, hll_name, hash);
                }
                MVM_repr_bind_key_o(tc, hash, GET_REG(cur_op, 2).s, GET_REG(cur_op, 4).o);
                uv_mutex_unlock(&tc->instance->mutex_hll_syms);
                cur_op += 6;
                goto NEXT;
            }
            OP(hllize): {
                /* Increment PC before mapping, as it may invoke. */
                MVMRegister *res_reg = &GET_REG(cur_op, 0);
                MVMObject   *mapee   = GET_REG(cur_op, 2).o;
                cur_op += 4;
                MVM_hll_map(tc, mapee, MVM_hll_current(tc), res_reg);
                goto NEXT;
            }
            OP(hllizefor): {
                /* Increment PC before mapping, as it may invoke. */
                MVMRegister *res_reg = &GET_REG(cur_op, 0);
                MVMObject   *mapee   = GET_REG(cur_op, 2).o;
                MVMString   *hll     = GET_REG(cur_op, 4).s;
                cur_op += 6;
                MVM_hll_map(tc, mapee, MVM_hll_get_config_for(tc, hll), res_reg);
                goto NEXT;
            }
            OP(loadlib): {
                MVMString *name = GET_REG(cur_op, 0).s;
                MVMString *path = GET_REG(cur_op, 2).s;
                MVM_dll_load(tc, name, path);
                cur_op += 4;
                goto NEXT;
            }
            OP(freelib): {
                MVMString *name = GET_REG(cur_op, 0).s;
                MVM_dll_free(tc, name);
                cur_op += 2;
                goto NEXT;
            }
            OP(findsym): {
                MVMString *lib = GET_REG(cur_op, 2).s;
                MVMString *sym = GET_REG(cur_op, 4).s;
                MVMObject *obj = MVM_dll_find_symbol(tc, lib, sym);
                if (MVM_is_null(tc, obj))
                    MVM_exception_throw_adhoc(tc, "symbol not found in DLL");

                GET_REG(cur_op, 0).o = obj;
                cur_op += 6;
                goto NEXT;
            }
            OP(dropsym): {
                MVM_dll_drop_symbol(tc, GET_REG(cur_op, 0).o);
                cur_op += 2;
                goto NEXT;
            }
            OP(loadext): {
                MVMString *lib = GET_REG(cur_op, 0).s;
                MVMString *ext = GET_REG(cur_op, 2).s;
                MVM_ext_load(tc, lib, ext);
                cur_op += 4;
                goto NEXT;
            }
            OP(settypecheckmode): {
                MVMSTable *st = STABLE(GET_REG(cur_op, 0).o);
                st->mode_flags = GET_REG(cur_op, 2).i64 |
                    (st->mode_flags & (~MVM_TYPE_CHECK_CACHE_FLAG_MASK));
                MVM_SC_WB_ST(tc, st);
                cur_op += 4;
                goto NEXT;
            }
            OP(setdispatcher):
                tc->cur_dispatcher = GET_REG(cur_op, 0).o;
                cur_op += 2;
                goto NEXT;
            OP(takedispatcher):
                GET_REG(cur_op, 0).o = tc->cur_dispatcher ? tc->cur_dispatcher : tc->instance->VMNull;
                tc->cur_dispatcher = NULL;
                cur_op += 2;
                goto NEXT;
            OP(captureexistsnamed): {
                MVMObject *obj = GET_REG(cur_op, 2).o;
                if (IS_CONCRETE(obj) && REPR(obj)->ID == MVM_REPR_ID_MVMCallCapture) {
                    MVMCallCapture *cc = (MVMCallCapture *)obj;
                    GET_REG(cur_op, 0).i64 = MVM_args_has_named(tc, cc->body.apc,
                        GET_REG(cur_op, 4).s);
                }
                else {
                    MVM_exception_throw_adhoc(tc, "captureexistsnamed needs a MVMCallCapture");
                }
                cur_op += 6;
                goto NEXT;
            }
            OP(capturehasnameds): {
                MVMObject *obj = GET_REG(cur_op, 2).o;
                if (IS_CONCRETE(obj) && REPR(obj)->ID == MVM_REPR_ID_MVMCallCapture) {
                    /* If positionals count doesn't match arg count, we must
                     * have some named args. */
                    MVMCallCapture *cc = (MVMCallCapture *)obj;
                    GET_REG(cur_op, 0).i64 = cc->body.apc->arg_count != cc->body.apc->num_pos;
                }
                else {
                    MVM_exception_throw_adhoc(tc, "capturehasnameds needs a MVMCallCapture");
                }
                cur_op += 4;
                goto NEXT;
            }
            OP(backendconfig):
                GET_REG(cur_op, 0).o = MVM_backend_config(tc);
                cur_op += 2;
                goto NEXT;
            OP(getlexouter): {
                GET_REG(cur_op, 0).o = MVM_frame_find_lexical_by_name_outer(tc,
                    GET_REG(cur_op, 2).s);
                cur_op += 4;
                goto NEXT;
            }
            OP(getlexrel): {
                MVMObject *ctx  = GET_REG(cur_op, 2).o;
                MVMRegister *r;
                if (REPR(ctx)->ID != MVM_REPR_ID_MVMContext || !IS_CONCRETE(ctx))
                    MVM_exception_throw_adhoc(tc, "getlexrel needs a context");
                r = MVM_frame_find_lexical_by_name_rel(tc,
                    GET_REG(cur_op, 4).s, ((MVMContext *)ctx)->body.context);
                GET_REG(cur_op, 0).o = r ? r->o : NULL;
                cur_op += 6;
                goto NEXT;
            }
            OP(getlexreldyn): {
                MVMObject *ctx  = GET_REG(cur_op, 2).o;
                if (REPR(ctx)->ID != MVM_REPR_ID_MVMContext || !IS_CONCRETE(ctx))
                    MVM_exception_throw_adhoc(tc, "getlexreldyn needs a context");
                GET_REG(cur_op, 0).o = MVM_frame_getdynlex(tc, GET_REG(cur_op, 4).s,
                        ((MVMContext *)ctx)->body.context);
                cur_op += 6;
                goto NEXT;
            }
            OP(getlexrelcaller): {
                MVMObject   *ctx  = GET_REG(cur_op, 2).o;
                MVMRegister *res;
                if (REPR(ctx)->ID != MVM_REPR_ID_MVMContext || !IS_CONCRETE(ctx))
                    MVM_exception_throw_adhoc(tc, "getlexrelcaller needs a context");
                res = MVM_frame_find_lexical_by_name_rel_caller(tc, GET_REG(cur_op, 4).s,
                    ((MVMContext *)ctx)->body.context);
                GET_REG(cur_op, 0).o = res ? res->o : tc->instance->VMNull;
                cur_op += 6;
                goto NEXT;
            }
            OP(getlexcaller): {
                MVMRegister *res = MVM_frame_find_lexical_by_name_rel_caller(tc,
                    GET_REG(cur_op, 2).s, tc->cur_frame->caller);
                GET_REG(cur_op, 0).o = res ? res->o : tc->instance->VMNull;
                cur_op += 4;
                goto NEXT;
            }
            OP(bitand_s):
                GET_REG(cur_op, 0).s = MVM_string_bitand(tc,
                    GET_REG(cur_op, 2).s, GET_REG(cur_op, 4).s);
                cur_op += 6;
                goto NEXT;
            OP(bitor_s):
                GET_REG(cur_op, 0).s = MVM_string_bitor(tc,
                    GET_REG(cur_op, 2).s, GET_REG(cur_op, 4).s);
                cur_op += 6;
                goto NEXT;
            OP(bitxor_s):
                GET_REG(cur_op, 0).s = MVM_string_bitxor(tc,
                    GET_REG(cur_op, 2).s, GET_REG(cur_op, 4).s);
                cur_op += 6;
                goto NEXT;
            OP(isnanorinf):
                GET_REG(cur_op, 0).i64 = MVM_num_isnanorinf(tc, GET_REG(cur_op, 2).n64);
                cur_op += 4;
                goto NEXT;
            OP(inf):
                GET_REG(cur_op, 0).n64 = MVM_num_posinf(tc);
                cur_op += 2;
                goto NEXT;
            OP(neginf):
                GET_REG(cur_op, 0).n64 = MVM_num_neginf(tc);
                cur_op += 2;
                goto NEXT;
            OP(nan):
                GET_REG(cur_op, 0).n64 = MVM_num_nan(tc);
                cur_op += 2;
                goto NEXT;
            OP(getpid):
                GET_REG(cur_op, 0).i64 = MVM_proc_getpid(tc);
                cur_op += 2;
                goto NEXT;
            OP(spawn):
                GET_REG(cur_op, 0).i64 = MVM_proc_spawn(tc, GET_REG(cur_op, 2).o,
                    GET_REG(cur_op, 4).s, GET_REG(cur_op, 6).o);
                cur_op += 8;
                goto NEXT;
            OP(filereadable):
                GET_REG(cur_op, 0).i64 = MVM_file_isreadable(tc, GET_REG(cur_op, 2).s,0);
                cur_op += 4;
                goto NEXT;
            OP(filewritable):
                GET_REG(cur_op, 0).i64 = MVM_file_iswritable(tc, GET_REG(cur_op, 2).s,0);
                cur_op += 4;
                goto NEXT;
            OP(fileexecutable):
                GET_REG(cur_op, 0).i64 = MVM_file_isexecutable(tc, GET_REG(cur_op, 2).s,0);
                cur_op += 4;
                goto NEXT;
            OP(say_fhs):
                GET_REG(cur_op, 0).i64 = MVM_io_write_string(tc, GET_REG(cur_op, 2).o, GET_REG(cur_op, 4).s, 1);
                cur_op += 6;
                goto NEXT;
            OP(capturenamedshash): {
                MVMObject *obj = GET_REG(cur_op, 2).o;
                if (IS_CONCRETE(obj) && REPR(obj)->ID == MVM_REPR_ID_MVMCallCapture) {
                    MVMCallCapture *cc = (MVMCallCapture *)obj;
                    GET_REG(cur_op, 0).o = MVM_args_slurpy_named(tc, cc->body.apc);
                }
                else {
                    MVM_exception_throw_adhoc(tc, "capturehasnameds needs a MVMCallCapture");
                }
                cur_op += 4;
                goto NEXT;
            }
            OP(read_fhb):
                MVM_io_read_bytes(tc, GET_REG(cur_op, 0).o, GET_REG(cur_op, 2).o,
                    GET_REG(cur_op, 4).i64);
                cur_op += 6;
                goto NEXT;
            OP(write_fhb):
                MVM_io_write_bytes(tc, GET_REG(cur_op, 0).o, GET_REG(cur_op, 2).o);
                cur_op += 4;
                goto NEXT;
            OP(newexception):
                GET_REG(cur_op, 0).o = (MVMObject *)MVM_repr_alloc_init(tc,
                    tc->instance->boot_types.BOOTException);
                cur_op += 2;
                goto NEXT;
            OP(openpipe):
                GET_REG(cur_op, 0).o = MVM_file_openpipe(tc, GET_REG(cur_op, 2).s, GET_REG(cur_op, 4).s, GET_REG(cur_op, 6).o, GET_REG(cur_op, 8).s);
                cur_op += 10;
                goto NEXT;
            OP(backtrace):
                GET_REG(cur_op, 0).o = MVM_exception_backtrace(tc, GET_REG(cur_op, 2).o);
                cur_op += 4;
                goto NEXT;
            OP(symlink):
                MVM_file_symlink(tc, GET_REG(cur_op, 0).s, GET_REG(cur_op, 2).s);
                cur_op += 4;
                goto NEXT;
            OP(link):
                MVM_file_link(tc, GET_REG(cur_op, 0).s, GET_REG(cur_op, 2).s);
                cur_op += 4;
                goto NEXT;
            OP(gethostname):
                GET_REG(cur_op, 0).s = MVM_io_get_hostname(tc);
                cur_op += 2;
                goto NEXT;
            OP(exreturnafterunwind): {
                MVMObject *ex = GET_REG(cur_op, 0).o;
                if (IS_CONCRETE(ex) && REPR(ex)->ID == MVM_REPR_ID_MVMException)
                    ((MVMException *)ex)->body.return_after_unwind = 1;
                else
                    MVM_exception_throw_adhoc(tc, "exreturnafterunwind needs a VMException");
                cur_op += 2;
                goto NEXT;
            }
            OP(continuationclone): {
                MVMObject *cont = GET_REG(cur_op, 2).o;
                if (REPR(cont)->ID == MVM_REPR_ID_MVMContinuation)
                    GET_REG(cur_op, 0).o = (MVMObject *)MVM_continuation_clone(tc,
                        (MVMContinuation *)cont);
                else
                    MVM_exception_throw_adhoc(tc, "continuationclone expects an MVMContinuation");
                cur_op += 4;
                goto NEXT;
            }
            OP(continuationreset): {
                MVMRegister *res  = &GET_REG(cur_op, 0);
                MVMObject   *tag  = GET_REG(cur_op, 2).o;
                MVMObject   *code = GET_REG(cur_op, 4).o;
                cur_op += 6;
                MVM_continuation_reset(tc, tag, code, res);
                goto NEXT;
            }
            OP(continuationcontrol): {
                MVMRegister *res     = &GET_REG(cur_op, 0);
                MVMint64     protect = GET_REG(cur_op, 2).i64;
                MVMObject   *tag     = GET_REG(cur_op, 4).o;
                MVMObject   *code    = GET_REG(cur_op, 6).o;
                cur_op += 8;
                MVM_continuation_control(tc, protect, tag, code, res);
                goto NEXT;
            }
            OP(continuationinvoke): {
                MVMRegister *res  = &GET_REG(cur_op, 0);
                MVMObject   *cont = GET_REG(cur_op, 2).o;
                MVMObject   *code = GET_REG(cur_op, 4).o;
                cur_op += 6;
                if (REPR(cont)->ID == MVM_REPR_ID_MVMContinuation)
                    MVM_continuation_invoke(tc, (MVMContinuation *)cont, code, res);
                else
                    MVM_exception_throw_adhoc(tc, "continuationinvoke expects an MVMContinuation");
                goto NEXT;
            }
            OP(randscale_n):
                GET_REG(cur_op, 0).n64 = MVM_proc_rand_n(tc) * GET_REG(cur_op, 2).n64;
                cur_op += 4;
                goto NEXT;
            OP(uniisblock):
                GET_REG(cur_op, 0).i64 = (MVMint64)MVM_unicode_is_in_block(tc,
                    GET_REG(cur_op, 2).s, GET_REG(cur_op, 4).i64, GET_REG(cur_op, 6).s);
                cur_op += 8;
                goto NEXT;
            OP(assertparamcheck): {
                MVMint64 ok = GET_REG(cur_op, 0).i64;
                cur_op += 2;
                if (!ok)
                    MVM_args_bind_failed(tc);
                goto NEXT;
            }
            OP(paramnamesused): {
                MVMArgProcContext *ctx = &tc->cur_frame->params;
                if (ctx->callsite->num_pos != ctx->callsite->arg_count)
                    MVM_args_assert_nameds_used(tc, ctx);
                goto NEXT;
            }
            OP(nativecallbuild):
                MVM_nativecall_build(tc, GET_REG(cur_op, 0).o, GET_REG(cur_op, 2).s,
                    GET_REG(cur_op, 4).s, GET_REG(cur_op, 6).s,
                    GET_REG(cur_op, 8).o, GET_REG(cur_op, 10).o);
                cur_op += 12;
                goto NEXT;
            OP(nativecallinvoke):
                GET_REG(cur_op, 0).o = MVM_nativecall_invoke(tc, GET_REG(cur_op, 2).o,
                    GET_REG(cur_op, 4).o, GET_REG(cur_op, 6).o);
                cur_op += 8;
                goto NEXT;
            OP(nativecallrefresh):
                MVM_nativecall_refresh(tc, GET_REG(cur_op, 0).o);
                cur_op += 2;
                goto NEXT;
            OP(threadrun):
                MVM_thread_run(tc, GET_REG(cur_op, 0).o);
                cur_op += 2;
                goto NEXT;
            OP(threadid):
                GET_REG(cur_op, 0).i64 = MVM_thread_id(tc, GET_REG(cur_op, 2).o);
                cur_op += 4;
                goto NEXT;
            OP(threadyield):
                MVM_thread_yield(tc);
                goto NEXT;
            OP(currentthread):
                GET_REG(cur_op, 0).o = MVM_thread_current(tc);
                cur_op += 2;
                goto NEXT;
            OP(lock): {
                MVMObject *lock = GET_REG(cur_op, 0).o;
                if (REPR(lock)->ID == MVM_REPR_ID_ReentrantMutex && IS_CONCRETE(lock))
                    MVM_reentrantmutex_lock(tc, (MVMReentrantMutex *)lock);
                else
                    MVM_exception_throw_adhoc(tc,
                        "lock requires a concrete object with REPR ReentrantMutex");
                cur_op += 2;
                goto NEXT;
            }
            OP(unlock): {
                MVMObject *lock = GET_REG(cur_op, 0).o;
                if (REPR(lock)->ID == MVM_REPR_ID_ReentrantMutex && IS_CONCRETE(lock))
                    MVM_reentrantmutex_unlock(tc, (MVMReentrantMutex *)lock);
                else
                    MVM_exception_throw_adhoc(tc,
                        "lock requires a concrete object with REPR ReentrantMutex");
                cur_op += 2;
                goto NEXT;
            }
            OP(semacquire): {
                MVMObject *sem = GET_REG(cur_op, 0).o;
                if (REPR(sem)->ID == MVM_REPR_ID_Semaphore && IS_CONCRETE(sem))
                    MVM_semaphore_acquire(tc, (MVMSemaphore *)sem);
                else
                    MVM_exception_throw_adhoc(tc,
                        "semacquire requires a concrete object with REPR Semaphore");
                cur_op += 2;
                goto NEXT;
            }
            OP(semtryacquire): {
                MVMObject *sem = GET_REG(cur_op, 2).o;
                if (REPR(sem)->ID == MVM_REPR_ID_Semaphore && IS_CONCRETE(sem))
                    GET_REG(cur_op, 0).i64 = MVM_semaphore_tryacquire(tc,
                        (MVMSemaphore *)sem);
                else
                    MVM_exception_throw_adhoc(tc,
                        "semtryacquire requires a concrete object with REPR Semaphore");
                cur_op += 4;
                goto NEXT;
            }
            OP(semrelease): {
                MVMObject *sem = GET_REG(cur_op, 0).o;
                if (REPR(sem)->ID == MVM_REPR_ID_Semaphore && IS_CONCRETE(sem))
                    MVM_semaphore_release(tc, (MVMSemaphore *)sem);
                else
                    MVM_exception_throw_adhoc(tc,
                        "semrelease requires a concrete object with REPR Semaphore");
                cur_op += 2;
                goto NEXT;
            }
            OP(getlockcondvar): {
                MVMObject *lock = GET_REG(cur_op, 2).o;
                if (REPR(lock)->ID == MVM_REPR_ID_ReentrantMutex && IS_CONCRETE(lock))
                    GET_REG(cur_op, 0).o = MVM_conditionvariable_from_lock(tc,
                        (MVMReentrantMutex *)lock, GET_REG(cur_op, 4).o);
                else
                    MVM_exception_throw_adhoc(tc,
                        "getlockcondvar requires a concrete object with REPR ReentrantMutex");
                cur_op += 6;
                goto NEXT;
            }
            OP(condwait): {
                MVMObject *cv = GET_REG(cur_op, 0).o;
                if (REPR(cv)->ID == MVM_REPR_ID_ConditionVariable && IS_CONCRETE(cv))
                    MVM_conditionvariable_wait(tc, (MVMConditionVariable *)cv);
                else
                    MVM_exception_throw_adhoc(tc,
                        "condwait requires a concrete object with REPR ConditionVariable");
                cur_op += 2;
                goto NEXT;
            }
            OP(condsignalone): {
                MVMObject *cv = GET_REG(cur_op, 0).o;
                if (REPR(cv)->ID == MVM_REPR_ID_ConditionVariable && IS_CONCRETE(cv))
                    MVM_conditionvariable_signal_one(tc, (MVMConditionVariable *)cv);
                else
                    MVM_exception_throw_adhoc(tc,
                        "condsignalone requires a concrete object with REPR ConditionVariable");
                cur_op += 2;
                goto NEXT;
            }
            OP(condsignalall): {
                MVMObject *cv = GET_REG(cur_op, 0).o;
                if (REPR(cv)->ID == MVM_REPR_ID_ConditionVariable && IS_CONCRETE(cv))
                    MVM_conditionvariable_signal_all(tc, (MVMConditionVariable *)cv);
                else
                    MVM_exception_throw_adhoc(tc,
                        "condsignalall requires a concrete object with REPR ConditionVariable");
                cur_op += 2;
                goto NEXT;
            }
            OP(queuepoll): {
                MVMObject *queue = GET_REG(cur_op, 2).o;
                if (REPR(queue)->ID == MVM_REPR_ID_ConcBlockingQueue && IS_CONCRETE(queue))
                    GET_REG(cur_op, 0).o = MVM_concblockingqueue_poll(tc,
                        (MVMConcBlockingQueue *)queue);
                else
                    MVM_exception_throw_adhoc(tc,
                        "queuepoll requires a concrete object with REPR ConcBlockingQueue");
                cur_op += 4;
                goto NEXT;
            }
            OP(setmultispec): {
                MVMObject *obj        = GET_REG(cur_op, 0).o;
                MVMObject *ch         = GET_REG(cur_op, 2).o;
                MVMString *valid_attr = GET_REG(cur_op, 4).s;
                MVMString *cache_attr = GET_REG(cur_op, 6).s;
                MVMSTable *st         = STABLE(obj);
                MVMInvocationSpec *is = st->invocation_spec;
                if (!is)
                    MVM_exception_throw_adhoc(tc,
                        "Can only use setmultispec after setinvokespec");
                MVM_ASSIGN_REF(tc, &(st->header), is->md_class_handle, ch);
                MVM_ASSIGN_REF(tc, &(st->header), is->md_valid_attr_name, valid_attr);
                MVM_ASSIGN_REF(tc, &(st->header), is->md_cache_attr_name, cache_attr);
                is->md_valid_hint = REPR(ch)->attr_funcs.hint_for(tc, STABLE(ch), ch,
                    valid_attr);
                is->md_cache_hint = REPR(ch)->attr_funcs.hint_for(tc, STABLE(ch), ch,
                    cache_attr);
                cur_op += 8;
                goto NEXT;
            }
            OP(ctxouterskipthunks): {
                MVMObject *this_ctx = GET_REG(cur_op, 2).o, *ctx;
                MVMFrame *frame;
                if (!IS_CONCRETE(this_ctx) || REPR(this_ctx)->ID != MVM_REPR_ID_MVMContext) {
                    MVM_exception_throw_adhoc(tc, "ctxouter needs an MVMContext");
                }
                frame = ((MVMContext *)this_ctx)->body.context->outer;
                while (frame && frame->static_info->body.is_thunk)
                    frame = frame->caller;
                if (frame) {
                    ctx = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTContext);
                    ((MVMContext *)ctx)->body.context = MVM_frame_inc_ref(tc, frame);
                    GET_REG(cur_op, 0).o = ctx;
                }
                else {
                    GET_REG(cur_op, 0).o = tc->instance->VMNull;
                }
                cur_op += 4;
                goto NEXT;
            }
            OP(ctxcallerskipthunks): {
                MVMObject *this_ctx = GET_REG(cur_op, 2).o, *ctx = NULL;
                MVMFrame *frame;
                if (!IS_CONCRETE(this_ctx) || REPR(this_ctx)->ID != MVM_REPR_ID_MVMContext) {
                    MVM_exception_throw_adhoc(tc, "ctxcaller needs an MVMContext");
                }
                frame = ((MVMContext *)this_ctx)->body.context->caller;
                while (frame && frame->static_info->body.is_thunk)
                    frame = frame->caller;
                if (frame) {
                    ctx = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTContext);
                    ((MVMContext *)ctx)->body.context = MVM_frame_inc_ref(tc, frame);
                }
                GET_REG(cur_op, 0).o = ctx ? ctx : tc->instance->VMNull;
                cur_op += 4;
                goto NEXT;
            }
            OP(timer):
                GET_REG(cur_op, 0).o = MVM_io_timer_create(tc, GET_REG(cur_op, 2).o,
                    GET_REG(cur_op, 4).o, GET_REG(cur_op, 6).i64,
                    GET_REG(cur_op, 8).i64, GET_REG(cur_op, 10).o);
                cur_op += 12;
                goto NEXT;
            OP(cancel):
                MVM_io_eventloop_cancel_work(tc, GET_REG(cur_op, 0).o);
                cur_op += 2;
                goto NEXT;
            OP(signal):
                GET_REG(cur_op, 0).o = MVM_io_signal_handle(tc, GET_REG(cur_op, 2).o,
                    GET_REG(cur_op, 4).o, GET_REG(cur_op, 6).i64, GET_REG(cur_op, 8).o);
                cur_op += 10;
                goto NEXT;
            OP(watchfile):
                GET_REG(cur_op, 0).o = MVM_io_file_watch(tc, GET_REG(cur_op, 2).o,
                    GET_REG(cur_op, 4).o, GET_REG(cur_op, 6).s, GET_REG(cur_op, 8).o);
                cur_op += 10;
                goto NEXT;
            OP(asyncconnect):
                GET_REG(cur_op, 0).o = MVM_io_socket_connect_async(tc,
                    GET_REG(cur_op, 2).o, GET_REG(cur_op, 4).o, GET_REG(cur_op, 6).s,
                    GET_REG(cur_op, 8).i64, GET_REG(cur_op, 10).o);
                cur_op += 12;
                goto NEXT;
            OP(asynclisten):
                GET_REG(cur_op, 0).o = MVM_io_socket_listen_async(tc,
                    GET_REG(cur_op, 2).o, GET_REG(cur_op, 4).o, GET_REG(cur_op, 6).s,
                    GET_REG(cur_op, 8).i64, GET_REG(cur_op, 10).o);
                cur_op += 12;
                goto NEXT;
            OP(asyncwritestr):
                GET_REG(cur_op, 0).o = MVM_io_write_string_async(tc, GET_REG(cur_op, 2).o,
                    GET_REG(cur_op, 4).o, GET_REG(cur_op, 6).o, GET_REG(cur_op, 8).s,
                    GET_REG(cur_op, 10).o);
                cur_op += 12;
                goto NEXT;
            OP(asyncwritebytes):
                GET_REG(cur_op, 0).o = MVM_io_write_bytes_async(tc, GET_REG(cur_op, 2).o,
                    GET_REG(cur_op, 4).o, GET_REG(cur_op, 6).o, GET_REG(cur_op, 8).o,
                    GET_REG(cur_op, 10).o);
                cur_op += 12;
                goto NEXT;
            OP(asyncreadchars):
                GET_REG(cur_op, 0).o = MVM_io_read_chars_async(tc, GET_REG(cur_op, 2).o,
                    GET_REG(cur_op, 4).o, GET_REG(cur_op, 6).o, GET_REG(cur_op, 8).o);
                cur_op += 10;
                goto NEXT;
            OP(asyncreadbytes):
                GET_REG(cur_op, 0).o = MVM_io_read_bytes_async(tc, GET_REG(cur_op, 2).o,
                    GET_REG(cur_op, 4).o, GET_REG(cur_op, 6).o, GET_REG(cur_op, 8).o,
                    GET_REG(cur_op, 10).o);
                cur_op += 12;
                goto NEXT;
            OP(getlexstatic_o):
            OP(getlexperinvtype_o): {
                MVMRegister *found = MVM_frame_find_lexical_by_name(tc,
                    GET_REG(cur_op, 2).s, MVM_reg_obj);
                GET_REG(cur_op, 0).o = found ? found->o : tc->instance->VMNull;
                cur_op += 4;
                goto NEXT;
            }
            OP(execname):
                GET_REG(cur_op, 0).s = MVM_executable_name(tc);
                cur_op += 2;
                goto NEXT;
            OP(const_i64_16):
                GET_REG(cur_op, 0).i64 = GET_I16(cur_op, 2);
                cur_op += 4;
                goto NEXT;
            OP(const_i64_32):
                GET_REG(cur_op, 0).i64 = GET_I32(cur_op, 2);
                cur_op += 6;
                goto NEXT;
            OP(isnonnull): {
                MVMObject *obj = GET_REG(cur_op, 2).o;
                GET_REG(cur_op, 0).i64 = !MVM_is_null(tc, obj);
                cur_op += 4;
                goto NEXT;
            }
            OP(param_rn2_i): {
                MVMArgInfo param = MVM_args_get_named_int(tc, &tc->cur_frame->params,
                    cu->body.strings[GET_UI32(cur_op, 2)], MVM_ARG_OPTIONAL);
                if (param.exists)
                    GET_REG(cur_op, 0).i64 = param.arg.i64;
                else
                    GET_REG(cur_op, 0).i64 = MVM_args_get_named_int(tc, &tc->cur_frame->params,
                        cu->body.strings[GET_UI32(cur_op, 6)], MVM_ARG_REQUIRED).arg.i64;
                cur_op += 10;
                goto NEXT;
            }
            OP(param_rn2_n): {
                MVMArgInfo param = MVM_args_get_named_num(tc, &tc->cur_frame->params,
                    cu->body.strings[GET_UI32(cur_op, 2)], MVM_ARG_OPTIONAL);
                if (param.exists)
                    GET_REG(cur_op, 0).n64 = param.arg.n64;
                else
                    GET_REG(cur_op, 0).n64 = MVM_args_get_named_num(tc, &tc->cur_frame->params,
                        cu->body.strings[GET_UI32(cur_op, 6)], MVM_ARG_REQUIRED).arg.n64;
                cur_op += 10;
                goto NEXT;
            }
            OP(param_rn2_s): {
                MVMArgInfo param = MVM_args_get_named_str(tc, &tc->cur_frame->params,
                    cu->body.strings[GET_UI32(cur_op, 2)], MVM_ARG_OPTIONAL);
                if (param.exists)
                    GET_REG(cur_op, 0).s = param.arg.s;
                else
                    GET_REG(cur_op, 0).s = MVM_args_get_named_str(tc, &tc->cur_frame->params,
                        cu->body.strings[GET_UI32(cur_op, 6)], MVM_ARG_REQUIRED).arg.s;
                cur_op += 10;
                goto NEXT;
            }
            OP(param_rn2_o): {
                MVMArgInfo param = MVM_args_get_named_obj(tc, &tc->cur_frame->params,
                    cu->body.strings[GET_UI32(cur_op, 2)], MVM_ARG_OPTIONAL);
                if (param.exists)
                    GET_REG(cur_op, 0).o = param.arg.o;
                else
                    GET_REG(cur_op, 0).o = MVM_args_get_named_obj(tc, &tc->cur_frame->params,
                        cu->body.strings[GET_UI32(cur_op, 6)], MVM_ARG_REQUIRED).arg.o;
                cur_op += 10;
                goto NEXT;
            }
            OP(param_on2_i): {
                MVMArgInfo param = MVM_args_get_named_int(tc, &tc->cur_frame->params,
                    cu->body.strings[GET_UI32(cur_op, 2)], MVM_ARG_OPTIONAL);
                if (!param.exists)
                    param = MVM_args_get_named_int(tc, &tc->cur_frame->params,
                        cu->body.strings[GET_UI32(cur_op, 6)], MVM_ARG_OPTIONAL);
                if (param.exists) {
                    GET_REG(cur_op, 0).i64 = param.arg.i64;
                    cur_op = bytecode_start + GET_UI32(cur_op, 10);
                }
                else {
                    cur_op += 14;
                }
                goto NEXT;
            }
            OP(param_on2_n): {
                MVMArgInfo param = MVM_args_get_named_num(tc, &tc->cur_frame->params,
                    cu->body.strings[GET_UI32(cur_op, 2)], MVM_ARG_OPTIONAL);
                if (!param.exists)
                    param = MVM_args_get_named_num(tc, &tc->cur_frame->params,
                        cu->body.strings[GET_UI32(cur_op, 6)], MVM_ARG_OPTIONAL);
                if (param.exists) {
                    GET_REG(cur_op, 0).n64 = param.arg.n64;
                    cur_op = bytecode_start + GET_UI32(cur_op, 10);
                }
                else {
                    cur_op += 14;
                }
                goto NEXT;
            }
            OP(param_on2_s): {
                MVMArgInfo param = MVM_args_get_named_str(tc, &tc->cur_frame->params,
                    cu->body.strings[GET_UI32(cur_op, 2)], MVM_ARG_OPTIONAL);
                if (!param.exists)
                    param = MVM_args_get_named_str(tc, &tc->cur_frame->params,
                        cu->body.strings[GET_UI32(cur_op, 6)], MVM_ARG_OPTIONAL);
                if (param.exists) {
                    GET_REG(cur_op, 0).s = param.arg.s;
                    cur_op = bytecode_start + GET_UI32(cur_op, 10);
                }
                else {
                    cur_op += 14;
                }
                goto NEXT;
            }
            OP(param_on2_o): {
                MVMArgInfo param = MVM_args_get_named_obj(tc, &tc->cur_frame->params,
                    cu->body.strings[GET_UI32(cur_op, 2)], MVM_ARG_OPTIONAL);
                if (!param.exists)
                    param = MVM_args_get_named_obj(tc, &tc->cur_frame->params,
                        cu->body.strings[GET_UI32(cur_op, 6)], MVM_ARG_OPTIONAL);
                if (param.exists) {
                    GET_REG(cur_op, 0).o = param.arg.o;
                    cur_op = bytecode_start + GET_UI32(cur_op, 10);
                }
                else {
                    cur_op += 14;
                }
                goto NEXT;
            }
            OP(osrpoint):
                if (++(tc->cur_frame->osr_counter) == MVM_OSR_THRESHOLD)
                    MVM_spesh_osr(tc);
                goto NEXT;
            OP(nativecallcast):
                GET_REG(cur_op, 0).o = MVM_nativecall_cast(tc, GET_REG(cur_op, 2).o,
                    GET_REG(cur_op, 4).o, GET_REG(cur_op, 6).o);
                cur_op += 8;
                goto NEXT;
            OP(spawnprocasync):
                GET_REG(cur_op, 0).o = MVM_proc_spawn_async(tc, GET_REG(cur_op, 2).o,
                    GET_REG(cur_op, 4).o, GET_REG(cur_op, 6).s,
                    GET_REG(cur_op, 8).o, GET_REG(cur_op, 10).o);
                cur_op += 12;
                goto NEXT;
            OP(killprocasync):
                MVM_proc_kill_async(tc, GET_REG(cur_op, 0).o, GET_REG(cur_op, 2).i64);
                cur_op += 4;
                goto NEXT;
            OP(startprofile):
                MVM_profile_start(tc, GET_REG(cur_op, 0).o);
                cur_op += 2;
                goto NEXT;
            OP(endprofile):
                GET_REG(cur_op, 0).o = MVM_profile_end(tc);
                cur_op += 2;
                goto NEXT;
            OP(objectid):
                GET_REG(cur_op, 0).i64 = (MVMint64)MVM_gc_object_id(tc, GET_REG(cur_op, 2).o);
                cur_op += 4;
                goto NEXT;
            OP(settypefinalize):
                MVM_gc_finalize_set(tc, GET_REG(cur_op, 0).o, GET_REG(cur_op, 2).i64);
                cur_op += 4;
                goto NEXT;
            OP(force_gc):
                MVM_gc_enter_from_allocator(tc);
                goto NEXT;
            OP(nativecallglobal):
                GET_REG(cur_op, 0).o = MVM_nativecall_global(tc, GET_REG(cur_op, 2).s,
                    GET_REG(cur_op, 4).s, GET_REG(cur_op, 6).o, GET_REG(cur_op, 8).o);
                cur_op += 10;
                goto NEXT;
            OP(close_fhi):
                GET_REG(cur_op, 0).i64 = MVM_io_close(tc, GET_REG(cur_op, 2).o);
                cur_op += 4;
                goto NEXT;
            OP(setparameterizer):
                MVM_6model_parametric_setup(tc, GET_REG(cur_op, 0).o, GET_REG(cur_op, 2).o);
                cur_op += 4;
                goto NEXT;
            OP(parameterizetype): {
                MVMObject   *type   = GET_REG(cur_op, 2).o;
                MVMObject   *params = GET_REG(cur_op, 4).o;
                MVMRegister *result = &(GET_REG(cur_op, 0));
                cur_op += 6;
                MVM_6model_parametric_parameterize(tc, type, params, result);
                goto NEXT;
            }
            OP(typeparameterized):
                GET_REG(cur_op, 0).o = MVM_6model_parametric_type_parameterized(tc, GET_REG(cur_op, 2).o);
                cur_op += 4;
                goto NEXT;
            OP(typeparameters):
                GET_REG(cur_op, 0).o = MVM_6model_parametric_type_parameters(tc, GET_REG(cur_op, 2).o);
                cur_op += 4;
                goto NEXT;
            OP(typeparameterat):
                GET_REG(cur_op, 0).o = MVM_6model_parametric_type_parameter_at(tc,
                    GET_REG(cur_op, 2).o, GET_REG(cur_op, 4).i64);
                cur_op += 6;
                goto NEXT;
            OP(readlink):
                GET_REG(cur_op, 0).s = MVM_file_readlink(tc, GET_REG(cur_op, 2).s);
                cur_op += 4;
                goto NEXT;
            OP(lstat):
                GET_REG(cur_op, 0).i64 = MVM_file_stat(tc, GET_REG(cur_op, 2).s, GET_REG(cur_op, 4).i64, 1);
                cur_op += 6;
                goto NEXT;
            OP(iscont_i):
                GET_REG(cur_op, 0).i64 = MVM_6model_container_iscont_i(tc,
                    GET_REG(cur_op, 2).o);
                cur_op += 4;
                goto NEXT;
            OP(iscont_n):
                GET_REG(cur_op, 0).i64 = MVM_6model_container_iscont_n(tc,
                    GET_REG(cur_op, 2).o);
                cur_op += 4;
                goto NEXT;
            OP(iscont_s):
                GET_REG(cur_op, 0).i64 = MVM_6model_container_iscont_s(tc,
                    GET_REG(cur_op, 2).o);
                cur_op += 4;
                goto NEXT;
            OP(assign_i): {
                MVMObject *cont  = GET_REG(cur_op, 0).o;
                MVMint64   value = GET_REG(cur_op, 2).i64;
                cur_op += 4;
                MVM_6model_container_assign_i(tc, cont, value);
                goto NEXT;
            }
            OP(assign_n): {
                MVMObject *cont  = GET_REG(cur_op, 0).o;
                MVMnum64   value = GET_REG(cur_op, 2).n64;
                cur_op += 4;
                MVM_6model_container_assign_n(tc, cont, value);
                goto NEXT;
            }
            OP(assign_s): {
                MVMObject *cont  = GET_REG(cur_op, 0).o;
                MVMString *value = GET_REG(cur_op, 2).s;
                cur_op += 4;
                MVM_6model_container_assign_s(tc, cont, value);
                goto NEXT;
            }
            OP(decont_i): {
                MVMObject *obj = GET_REG(cur_op, 2).o;
                MVMRegister *r = &GET_REG(cur_op, 0);
                cur_op += 4;
                MVM_6model_container_decont_i(tc, obj, r);
                goto NEXT;
            }
            OP(decont_n): {
                MVMObject *obj = GET_REG(cur_op, 2).o;
                MVMRegister *r = &GET_REG(cur_op, 0);
                cur_op += 4;
                MVM_6model_container_decont_n(tc, obj, r);
                goto NEXT;
            }
            OP(decont_s): {
                MVMObject *obj = GET_REG(cur_op, 2).o;
                MVMRegister *r = &GET_REG(cur_op, 0);
                cur_op += 4;
                MVM_6model_container_decont_s(tc, obj, r);
                goto NEXT;
            }
            OP(getregref_i):
                GET_REG(cur_op, 0).o = MVM_nativeref_reg_i(tc, tc->cur_frame,
                    &GET_REG(cur_op, 2));
                cur_op += 4;
                goto NEXT;
            OP(getregref_n):
                GET_REG(cur_op, 0).o = MVM_nativeref_reg_n(tc, tc->cur_frame,
                    &GET_REG(cur_op, 2));
                cur_op += 4;
                goto NEXT;
            OP(getregref_s):
                GET_REG(cur_op, 0).o = MVM_nativeref_reg_s(tc, tc->cur_frame,
                    &GET_REG(cur_op, 2));
                cur_op += 4;
                goto NEXT;
            OP(getlexref_i):
                GET_REG(cur_op, 0).o = MVM_nativeref_lex_i(tc,
                    GET_UI16(cur_op, 4), GET_UI16(cur_op, 2));
                cur_op += 6;
                goto NEXT;
            OP(getlexref_n):
                GET_REG(cur_op, 0).o = MVM_nativeref_lex_n(tc,
                    GET_UI16(cur_op, 4), GET_UI16(cur_op, 2));
                cur_op += 6;
                goto NEXT;
            OP(getlexref_s):
                GET_REG(cur_op, 0).o = MVM_nativeref_lex_s(tc,
                    GET_UI16(cur_op, 4), GET_UI16(cur_op, 2));
                cur_op += 6;
                goto NEXT;
            OP(getlexref_ni):
                GET_REG(cur_op, 0).o = MVM_nativeref_lex_name_i(tc,
                    cu->body.strings[GET_UI32(cur_op, 2)]);
                cur_op += 6;
                goto NEXT;
            OP(getlexref_nn):
                GET_REG(cur_op, 0).o = MVM_nativeref_lex_name_n(tc,
                    cu->body.strings[GET_UI32(cur_op, 2)]);
                cur_op += 6;
                goto NEXT;
            OP(getlexref_ns):
                GET_REG(cur_op, 0).o = MVM_nativeref_lex_name_s(tc,
                    cu->body.strings[GET_UI32(cur_op, 2)]);
                cur_op += 6;
                goto NEXT;
            OP(atposref_i):
                GET_REG(cur_op, 0).o = MVM_nativeref_pos_i(tc,
                    GET_REG(cur_op, 2).o, GET_REG(cur_op, 4).i64);
                cur_op += 6;
                goto NEXT;
            OP(atposref_n):
                GET_REG(cur_op, 0).o = MVM_nativeref_pos_n(tc,
                    GET_REG(cur_op, 2).o, GET_REG(cur_op, 4).i64);
                cur_op += 6;
                goto NEXT;
            OP(atposref_s):
                GET_REG(cur_op, 0).o = MVM_nativeref_pos_s(tc,
                    GET_REG(cur_op, 2).o, GET_REG(cur_op, 4).i64);
                cur_op += 6;
                goto NEXT;
            OP(getattrref_i):
                GET_REG(cur_op, 0).o = MVM_nativeref_attr_i(tc,
                    GET_REG(cur_op, 2).o, GET_REG(cur_op, 4).o,
                    cu->body.strings[GET_UI32(cur_op, 6)]);
                cur_op += 12;
                goto NEXT;
            OP(getattrref_n):
                GET_REG(cur_op, 0).o = MVM_nativeref_attr_n(tc,
                    GET_REG(cur_op, 2).o, GET_REG(cur_op, 4).o,
                    cu->body.strings[GET_UI32(cur_op, 6)]);
                cur_op += 12;
                goto NEXT;
            OP(getattrref_s):
                GET_REG(cur_op, 0).o = MVM_nativeref_attr_s(tc,
                    GET_REG(cur_op, 2).o, GET_REG(cur_op, 4).o,
                    cu->body.strings[GET_UI32(cur_op, 6)]);
                cur_op += 12;
                goto NEXT;
            OP(getattrsref_i):
                GET_REG(cur_op, 0).o = MVM_nativeref_attr_i(tc,
                    GET_REG(cur_op, 2).o, GET_REG(cur_op, 4).o,
                    GET_REG(cur_op, 6).s);
                cur_op += 8;
                goto NEXT;
            OP(getattrsref_n):
                GET_REG(cur_op, 0).o = MVM_nativeref_attr_n(tc,
                    GET_REG(cur_op, 2).o, GET_REG(cur_op, 4).o,
                    GET_REG(cur_op, 6).s);
                cur_op += 8;
                goto NEXT;
            OP(getattrsref_s):
                GET_REG(cur_op, 0).o = MVM_nativeref_attr_s(tc,
                    GET_REG(cur_op, 2).o, GET_REG(cur_op, 4).o,
                    GET_REG(cur_op, 6).s);
                cur_op += 8;
                goto NEXT;
            OP(nativecallsizeof):
                GET_REG(cur_op, 0).i64 = MVM_nativecall_sizeof(tc, GET_REG(cur_op, 2).o);
                cur_op += 4;
                goto NEXT;
            OP(encodenorm):
                MVM_exception_throw_adhoc(tc, "NYI");
            OP(normalizecodes):
                MVM_unicode_normalize_codepoints(tc, GET_REG(cur_op, 0).o, GET_REG(cur_op, 4).o,
                    MVN_unicode_normalizer_form(tc, GET_REG(cur_op, 2).i64));
                cur_op += 6;
                goto NEXT;
            OP(strfromcodes):
                GET_REG(cur_op, 0).s = MVM_unicode_codepoints_to_nfg_string(tc,
                    GET_REG(cur_op, 2).o);
                cur_op += 4;
                goto NEXT;
            OP(strtocodes):
                MVM_unicode_string_to_codepoints(tc, GET_REG(cur_op, 0).s,
                    MVN_unicode_normalizer_form(tc, GET_REG(cur_op, 2).i64),
                    GET_REG(cur_op, 4).o);
                cur_op += 6;
                goto NEXT;
            OP(getcodelocation):
                GET_REG(cur_op, 0).o = MVM_code_location(tc, GET_REG(cur_op, 2).o);
                cur_op += 4;
                goto NEXT;
            OP(eqatim_s):
                if (MVM_string_graphs(tc, GET_REG(cur_op, 2).s) <= GET_REG(cur_op, 6).i64)
                    GET_REG(cur_op, 0).i64 = 0;
                else
                    GET_REG(cur_op, 0).i64 = MVM_string_ord_basechar_at(tc, GET_REG(cur_op, 2).s, GET_REG(cur_op, 6).i64)
                                          == MVM_string_ord_basechar_at(tc, GET_REG(cur_op, 4).s, 0)
                                           ? 1 : 0;
                cur_op += 8;
                goto NEXT;
            OP(ordbaseat):
                GET_REG(cur_op, 0).i64 = MVM_string_ord_basechar_at(tc, GET_REG(cur_op, 2).s, GET_REG(cur_op, 4).i64);
                cur_op += 6;
                goto NEXT;
            OP(neverrepossess):
                MVM_6model_never_repossess(tc, GET_REG(cur_op, 0).o);
                cur_op += 2;
                goto NEXT;
            OP(sp_log):
                if (tc->cur_frame->spesh_log_idx >= 0) {
                    MVM_ASSIGN_REF(tc, &(tc->cur_frame->static_info->common.header),
                        tc->cur_frame->spesh_log_slots[
                            GET_UI16(cur_op, 2) * MVM_SPESH_LOG_RUNS + tc->cur_frame->spesh_log_idx
                        ],
                        GET_REG(cur_op, 0).o);
                }
                cur_op += 4;
                goto NEXT;
            OP(sp_osrfinalize): {
                MVMSpeshCandidate *cand = tc->cur_frame->spesh_cand;
                if (cand) {
                    tc->cur_frame->spesh_log_idx = cand->log_enter_idx;
                    cand->log_enter_idx++;
                    if (cand->log_enter_idx >= MVM_SPESH_LOG_RUNS)
                        MVM_spesh_osr_finalize(tc);
                }
                goto NEXT;
            }
            OP(sp_guardconc): {
                MVMObject *check = GET_REG(cur_op, 0).o;
                MVMSTable *want  = (MVMSTable *)tc->cur_frame
                    ->effective_spesh_slots[GET_UI16(cur_op, 2)];
                cur_op += 4;
                if (!check || !IS_CONCRETE(check) || STABLE(check) != want)
                    MVM_spesh_deopt_one(tc);
                goto NEXT;
            }
            OP(sp_guardtype): {
                MVMObject *check = GET_REG(cur_op, 0).o;
                MVMSTable *want  = (MVMSTable *)tc->cur_frame
                    ->effective_spesh_slots[GET_UI16(cur_op, 2)];
                cur_op += 4;
                if (!check || IS_CONCRETE(check) || STABLE(check) != want)
                    MVM_spesh_deopt_one(tc);
                goto NEXT;
            }
            OP(sp_guardcontconc): {
                MVMint32   ok     = 0;
                MVMObject *check  = GET_REG(cur_op, 0).o;
                MVMSTable *want_c = (MVMSTable *)tc->cur_frame
                    ->effective_spesh_slots[GET_UI16(cur_op, 2)];
                MVMSTable *want_v = (MVMSTable *)tc->cur_frame
                    ->effective_spesh_slots[GET_UI16(cur_op, 4)];
                cur_op += 6;
                if (check && IS_CONCRETE(check) && STABLE(check) == want_c) {
                    MVMContainerSpec const *contspec = STABLE(check)->container_spec;
                    MVMRegister r;
                    contspec->fetch(tc, check, &r);
                    if (r.o && IS_CONCRETE(r.o) && STABLE(r.o) == want_v)
                        ok = 1;
                }
                if (!ok)
                    MVM_spesh_deopt_one(tc);
                goto NEXT;
            }
            OP(sp_guardconttype): {
                MVMint32   ok     = 0;
                MVMObject *check  = GET_REG(cur_op, 0).o;
                MVMSTable *want_c = (MVMSTable *)tc->cur_frame
                    ->effective_spesh_slots[GET_UI16(cur_op, 2)];
                MVMSTable *want_v = (MVMSTable *)tc->cur_frame
                    ->effective_spesh_slots[GET_UI16(cur_op, 4)];
                cur_op += 6;
                if (check && IS_CONCRETE(check) && STABLE(check) == want_c) {
                    MVMContainerSpec const *contspec = STABLE(check)->container_spec;
                    MVMRegister r;
                    contspec->fetch(tc, check, &r);
                    if (r.o && !IS_CONCRETE(r.o) && STABLE(r.o) == want_v)
                        ok = 1;
                }
                if (!ok)
                    MVM_spesh_deopt_one(tc);
                goto NEXT;
            }
            OP(sp_getarg_o):
                GET_REG(cur_op, 0).o = tc->cur_frame->params.args[GET_UI16(cur_op, 2)].o;
                cur_op += 4;
                goto NEXT;
            OP(sp_getarg_i):
                GET_REG(cur_op, 0).i64 = tc->cur_frame->params.args[GET_UI16(cur_op, 2)].i64;
                cur_op += 4;
                goto NEXT;
            OP(sp_getarg_n):
                GET_REG(cur_op, 0).n64 = tc->cur_frame->params.args[GET_UI16(cur_op, 2)].n64;
                cur_op += 4;
                goto NEXT;
            OP(sp_getarg_s):
                GET_REG(cur_op, 0).s = tc->cur_frame->params.args[GET_UI16(cur_op, 2)].s;
                cur_op += 4;
                goto NEXT;
            OP(sp_fastinvoke_v): {
                MVMCode     *code       = (MVMCode *)GET_REG(cur_op, 0).o;
                MVMRegister *args       = tc->cur_frame->args;
                MVMint32     spesh_cand = GET_UI16(cur_op, 2);
                tc->cur_frame->return_value = NULL;
                tc->cur_frame->return_type  = MVM_RETURN_VOID;
                cur_op += 4;
                tc->cur_frame->return_address = cur_op;
                MVM_frame_invoke(tc, code->body.sf, cur_callsite, args,
                    code->body.outer, (MVMObject *)code, spesh_cand);
                goto NEXT;
            }
            OP(sp_fastinvoke_i): {
                MVMCode     *code       = (MVMCode *)GET_REG(cur_op, 2).o;
                MVMRegister *args       = tc->cur_frame->args;
                MVMint32     spesh_cand = GET_UI16(cur_op, 4);
                tc->cur_frame->return_value = &GET_REG(cur_op, 0);
                tc->cur_frame->return_type  = MVM_RETURN_INT;
                cur_op += 6;
                tc->cur_frame->return_address = cur_op;
                MVM_frame_invoke(tc, code->body.sf, cur_callsite, args,
                    code->body.outer, (MVMObject *)code, spesh_cand);
                goto NEXT;
            }
            OP(sp_fastinvoke_n): {
                MVMCode     *code       = (MVMCode *)GET_REG(cur_op, 2).o;
                MVMRegister *args       = tc->cur_frame->args;
                MVMint32     spesh_cand = GET_UI16(cur_op, 4);
                tc->cur_frame->return_value = &GET_REG(cur_op, 0);
                tc->cur_frame->return_type  = MVM_RETURN_NUM;
                cur_op += 6;
                tc->cur_frame->return_address = cur_op;
                MVM_frame_invoke(tc, code->body.sf, cur_callsite, args,
                    code->body.outer, (MVMObject *)code, spesh_cand);
                goto NEXT;
            }
            OP(sp_fastinvoke_s): {
                MVMCode     *code       = (MVMCode *)GET_REG(cur_op, 2).o;
                MVMRegister *args       = tc->cur_frame->args;
                MVMint32     spesh_cand = GET_UI16(cur_op, 4);
                tc->cur_frame->return_value = &GET_REG(cur_op, 0);
                tc->cur_frame->return_type  = MVM_RETURN_STR;
                cur_op += 6;
                tc->cur_frame->return_address = cur_op;
                MVM_frame_invoke(tc, code->body.sf, cur_callsite, args,
                    code->body.outer, (MVMObject *)code, spesh_cand);
                goto NEXT;
            }
            OP(sp_fastinvoke_o): {
                MVMCode     *code       = (MVMCode *)GET_REG(cur_op, 2).o;
                MVMRegister *args       = tc->cur_frame->args;
                MVMint32     spesh_cand = GET_UI16(cur_op, 4);
                tc->cur_frame->return_value = &GET_REG(cur_op, 0);
                tc->cur_frame->return_type  = MVM_RETURN_OBJ;
                cur_op += 6;
                tc->cur_frame->return_address = cur_op;
                MVM_frame_invoke(tc, code->body.sf, cur_callsite, args,
                    code->body.outer, (MVMObject *)code, spesh_cand);
                goto NEXT;
            }
            OP(sp_namedarg_used):
                tc->cur_frame->params.named_used[GET_UI16(cur_op, 0)] = 1;
                cur_op += 2;
                goto NEXT;
            OP(sp_getspeshslot):
                GET_REG(cur_op, 0).o = (MVMObject *)tc->cur_frame
                    ->effective_spesh_slots[GET_UI16(cur_op, 2)];
                cur_op += 4;
                goto NEXT;
            OP(sp_findmeth): {
                /* Obtain object and cache index; see if we get a match. */
                MVMObject *obj = GET_REG(cur_op, 2).o;
                MVMuint16  idx = GET_UI16(cur_op, 8);
                if ((MVMSTable *)tc->cur_frame->effective_spesh_slots[idx] == STABLE(obj)) {
                    GET_REG(cur_op, 0).o = (MVMObject *)tc->cur_frame->effective_spesh_slots[idx + 1];
                    cur_op += 10;
                }
                else {
                    /* May invoke, so pre-increment op counter */
                    MVMString *name = cu->body.strings[GET_UI32(cur_op, 4)];
                    MVMRegister *res = &GET_REG(cur_op, 0);
                    cur_op += 10;
                    MVM_6model_find_method_spesh(tc, obj, name, idx, res);

                }
                goto NEXT;
            }
            OP(sp_fastcreate): {
                /* Assume we're in normal code, so doing a nursery allocation.
                 * Also, that there is no initialize. */
                MVMuint16 size       = GET_UI16(cur_op, 2);
                MVMObject *obj       = MVM_gc_allocate_zeroed(tc, size);
                obj->st              = (MVMSTable *)tc->cur_frame->effective_spesh_slots[GET_UI16(cur_op, 4)];
                obj->header.size     = size;
                obj->header.owner    = tc->thread_id;
                GET_REG(cur_op, 0).o = obj;
                cur_op += 6;
                goto NEXT;
            }
            OP(sp_get_i):
                GET_REG(cur_op, 0).i64 = *((MVMint64 *)((char *)&GET_REG(cur_op, 2) + GET_UI16(cur_op, 4)));
                cur_op += 6;
                goto NEXT;
            OP(sp_get_n):
                GET_REG(cur_op, 0).n64 = *((MVMnum64 *)((char *)&GET_REG(cur_op, 2) + GET_UI16(cur_op, 4)));
                cur_op += 6;
                goto NEXT;
            OP(sp_get_s):
                GET_REG(cur_op, 0).s = ((MVMString *)((char *)&GET_REG(cur_op, 2) + GET_UI16(cur_op, 4)));
                cur_op += 6;
                goto NEXT;
            OP(sp_get_o):
            OP(sp_bind_o):
            OP(sp_bind_i):
            OP(sp_bind_n):
            OP(sp_bind_s):
                MVM_exception_throw_adhoc(tc, "Unimplemented spesh ops hit");
            OP(sp_p6oget_o): {
                MVMObject *o     = GET_REG(cur_op, 2).o;
                char      *data  = MVM_p6opaque_real_data(tc, OBJECT_BODY(o));
                MVMObject *val   = *((MVMObject **)(data + GET_UI16(cur_op, 4)));
                GET_REG(cur_op, 0).o = val ? val : tc->instance->VMNull;
                cur_op += 6;
                goto NEXT;
            }
            OP(sp_p6ogetvt_o): {
                MVMObject *o     = GET_REG(cur_op, 2).o;
                char      *data  = MVM_p6opaque_real_data(tc, OBJECT_BODY(o));
                MVMObject *val   = *((MVMObject **)(data + GET_UI16(cur_op, 4)));
                if (!val) {
                    val = (MVMObject *)tc->cur_frame->effective_spesh_slots[GET_UI16(cur_op, 6)];
                    MVM_ASSIGN_REF(tc, &(o->header), *((MVMObject **)(data + GET_UI16(cur_op, 4))), val);
                }
                GET_REG(cur_op, 0).o = val;
                cur_op += 8;
                goto NEXT;
            }
            OP(sp_p6ogetvc_o): {
                MVMObject *o     = GET_REG(cur_op, 2).o;
                char      *data  = MVM_p6opaque_real_data(tc, OBJECT_BODY(o));
                MVMObject *val   = *((MVMObject **)(data + GET_UI16(cur_op, 4)));
                if (!val) {
                    /* Clone might allocate, so re-fetch things after it. */
                    val  = MVM_repr_clone(tc, (MVMObject *)tc->cur_frame->effective_spesh_slots[GET_UI16(cur_op, 6)]);
                    o    = GET_REG(cur_op, 2).o;
                    data = MVM_p6opaque_real_data(tc, OBJECT_BODY(o));
                    MVM_ASSIGN_REF(tc, &(o->header), *((MVMObject **)(data + GET_UI16(cur_op, 4))), val);
                }
                GET_REG(cur_op, 0).o = val;
                cur_op += 8;
                goto NEXT;
            }
            OP(sp_p6oget_i): {
                MVMObject *o     = GET_REG(cur_op, 2).o;
                char      *data  = MVM_p6opaque_real_data(tc, OBJECT_BODY(o));
				GET_REG(cur_op, 0).i64 = *((MVMint64 *)(data + GET_UI16(cur_op, 4)));
                cur_op += 6;
                goto NEXT;
            }
            OP(sp_p6oget_n): {
                MVMObject *o     = GET_REG(cur_op, 2).o;
                char      *data  = MVM_p6opaque_real_data(tc, OBJECT_BODY(o));
                GET_REG(cur_op, 0).n64 = *((MVMnum64 *)(data + GET_UI16(cur_op, 4)));
                cur_op += 6;
                goto NEXT;
            }
            OP(sp_p6oget_s): {
                MVMObject *o     = GET_REG(cur_op, 2).o;
                char      *data  = MVM_p6opaque_real_data(tc, OBJECT_BODY(o));
                GET_REG(cur_op, 0).s = *((MVMString **)(data + GET_UI16(cur_op, 4)));
                cur_op += 6;
                goto NEXT;
            }
            OP(sp_p6obind_o): {
                MVMObject *o     = GET_REG(cur_op, 0).o;
                MVMObject *value = GET_REG(cur_op, 4).o;
                char      *data  = MVM_p6opaque_real_data(tc, OBJECT_BODY(o));
                MVM_ASSIGN_REF(tc, &(o->header), *((MVMObject **)(data + GET_UI16(cur_op, 2))), value);
                cur_op += 6;
                goto NEXT;
            }
            OP(sp_p6obind_i): {
                MVMObject *o     = GET_REG(cur_op, 0).o;
                char      *data  = MVM_p6opaque_real_data(tc, OBJECT_BODY(o));
                *((MVMint64 *)(data + GET_UI16(cur_op, 2))) = GET_REG(cur_op, 4).i64;
                cur_op += 6;
                goto NEXT;
            }
            OP(sp_p6obind_n): {
                MVMObject *o     = GET_REG(cur_op, 0).o;
                char      *data  = MVM_p6opaque_real_data(tc, OBJECT_BODY(o));
                *((MVMnum64 *)(data + GET_UI16(cur_op, 2))) = GET_REG(cur_op, 4).n64;
                cur_op += 6;
                goto NEXT;
            }
            OP(sp_p6obind_s): {
                MVMObject *o     = GET_REG(cur_op, 0).o;
                char      *data  = MVM_p6opaque_real_data(tc, OBJECT_BODY(o));
                MVM_ASSIGN_REF(tc, &(o->header), *((MVMString **)(data + GET_UI16(cur_op, 2))),
                    GET_REG(cur_op, 4).s);
                cur_op += 6;
                goto NEXT;
            }
            OP(sp_jit_enter): {
                if (tc->cur_frame->spesh_cand->jitcode == NULL) {
                    MVM_exception_throw_adhoc(tc, "Try to enter NULL jitcode");
                }
                // trampoline back to this opcode
                cur_op -= 2;
                if (MVM_jit_enter_code(tc, cu, tc->cur_frame->spesh_cand->jitcode)) {
                    if (MVM_frame_try_return(tc) == 0)
                        goto return_label;
                }
                goto NEXT;
            }
            OP(sp_boolify_iter): {
                GET_REG(cur_op, 0).i64 = MVM_iter_istrue(tc, (MVMIter*)GET_REG(cur_op, 2).o);
                cur_op += 4;
                goto NEXT;
            }
            OP(sp_boolify_iter_arr): {
                MVMIter *iter = (MVMIter *)GET_REG(cur_op, 2).o;

                GET_REG(cur_op, 0).i64 = iter->body.array_state.index + 1 < iter->body.array_state.limit ? 1 : 0;

                cur_op += 4;
                goto NEXT;
            }
            OP(sp_boolify_iter_hash): {
                MVMIter *iter = (MVMIter *)GET_REG(cur_op, 2).o;

                GET_REG(cur_op, 0).i64 = iter->body.hash_state.next != NULL ? 1 : 0;

                cur_op += 4;
                goto NEXT;
            }
            OP(prof_enter):
                MVM_profile_log_enter(tc, tc->cur_frame->static_info,
                    MVM_PROFILE_ENTER_NORMAL);
                goto NEXT;
            OP(prof_enterspesh):
                MVM_profile_log_enter(tc, tc->cur_frame->static_info,
                    MVM_PROFILE_ENTER_SPESH);
                goto NEXT;
            OP(prof_enterinline):
                MVM_profile_log_enter(tc,
                    (MVMStaticFrame *)tc->cur_frame->effective_spesh_slots[GET_UI16(cur_op, 0)],
                    MVM_PROFILE_ENTER_SPESH_INLINE);
                cur_op += 2;
                goto NEXT;
            OP(prof_exit):
                MVM_profile_log_exit(tc);
                goto NEXT;
            OP(prof_allocated):
                MVM_profile_log_allocated(tc, GET_REG(cur_op, 0).o);
                cur_op += 2;
                goto NEXT;
#if MVM_CGOTO
            OP_CALL_EXTOP: {
                /* Bounds checking? Never heard of that. */
                MVMFrame *frame_before = tc->cur_frame;
                MVMExtOpRecord *record = &cu->body.extops[op - MVM_OP_EXT_BASE];
                record->func(tc, cur_op);
                if (tc->cur_frame == frame_before)
                    cur_op += record->operand_bytes;
                goto NEXT;
            }
#else
            default: {
                if (op >= MVM_OP_EXT_BASE
                        && (op - MVM_OP_EXT_BASE) < cu->body.num_extops) {
                    MVMFrame *frame_before = tc->cur_frame;
                    MVMExtOpRecord *record =
                            &cu->body.extops[op - MVM_OP_EXT_BASE];
                    record->func(tc, cur_op);
                    if (tc->cur_frame == frame_before)
                        cur_op += record->operand_bytes;
                    goto NEXT;
                }

                MVM_panic(MVM_exitcode_invalidopcode, "Invalid opcode executed (corrupt bytecode stream?) opcode %u", op);
            }
#endif
        }
    }

    return_label:
    /* Need to clear these pointer pointers since they may be rooted
     * by some GC procedure. */
    tc->interp_cur_op         = NULL;
    tc->interp_bytecode_start = NULL;
    tc->interp_reg_base       = NULL;
    tc->interp_cu             = NULL;
    MVM_barrier();
}

void MVM_interp_enable_tracing() {
    tracing_enabled = 1;
}
