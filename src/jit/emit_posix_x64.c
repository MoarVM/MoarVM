/*
** This file has been pre-processed with DynASM.
** http://luajit.org/dynasm.html
** DynASM version 1.3.0, DynASM x64 version 1.3.0
** DO NOT EDIT! The original file is in "src/jit/emit_x64.dasc".
*/

#line 1 "src/jit/emit_x64.dasc"
#include "moar.h"
#include <dasm_proto.h>
#include <dasm_x86.h>
#include "emit.h"

//|.arch x64
#if DASM_VERSION != 10300
#error "Version mismatch between DynASM and included encoding engine"
#endif
#line 7 "src/jit/emit_x64.dasc"
//|.actionlist actions
static const unsigned char actions[371] = {
  85,72,137,229,255,65,86,83,65,84,65,85,255,73,137,252,254,72,139,158,233,
  76,139,166,233,76,139,174,233,255,248,10,255,65,93,65,92,91,65,94,255,72,
  137,252,236,93,195,255,72,199,131,233,237,255,73,187,237,237,76,137,155,233,
  255,77,139,156,253,36,233,76,137,155,233,255,76,139,155,233,76,137,155,233,
  255,72,139,131,233,72,3,131,233,72,137,131,233,255,72,139,131,233,72,43,131,
  233,72,137,131,233,255,72,252,255,131,233,255,72,252,255,139,233,255,72,139,
  131,233,72,59,131,233,255,15,148,208,255,15,149,208,255,15,156,208,255,15,
  158,208,255,15,159,208,255,15,157,208,255,72,15,182,192,72,137,131,233,255,
  72,139,189,233,255,72,139,181,233,255,72,139,149,233,255,72,139,141,233,255,
  76,139,133,233,255,76,139,141,233,255,76,137,252,247,255,76,137,252,246,255,
  76,137,252,242,255,76,137,252,241,255,77,137,252,240,255,77,137,252,241,255,
  73,139,190,233,255,73,139,182,233,255,73,139,150,233,255,73,139,142,233,255,
  77,139,134,233,255,77,139,142,233,255,72,139,187,233,255,72,139,179,233,255,
  72,139,147,233,255,72,139,139,233,255,76,139,131,233,255,76,139,139,233,255,
  72,199,199,237,255,72,199,198,237,255,72,199,194,237,255,72,199,193,237,255,
  73,199,192,237,255,73,199,193,237,255,73,186,237,237,65,252,255,210,255,252,
  233,244,10,255,252,233,245,255,72,139,131,233,72,133,192,15,133,245,255,72,
  139,131,233,72,133,192,15,132,245,255,249,255,76,139,24,76,137,155,233,255,
  76,139,155,233,76,137,24,255
};

#line 8 "src/jit/emit_x64.dasc"
//|.section code
#define DASM_SECTION_CODE	0
#define DASM_MAXSECTION		1
#line 9 "src/jit/emit_x64.dasc"
//|.globals JIT_LABEL_
enum {
  JIT_LABEL_exit,
  JIT_LABEL__MAX
};
#line 10 "src/jit/emit_x64.dasc"

/* Static allocation of relevant types to registers. I pick
 * callee-save registers for efficiency. It is likely we'll be calling
 * quite a C functions, and this saves us the trouble of storing
 * them. Moreover, C compilers preferentially do not use callee-saved
 * registers, and so in most cases, these won't be touched at all. */
//|.type TC, MVMThreadContext, r14
#define Dt1(_V) (int)(ptrdiff_t)&(((MVMThreadContext *)0)_V)
#line 17 "src/jit/emit_x64.dasc"
//|.type FRAME, MVMFrame
#define Dt2(_V) (int)(ptrdiff_t)&(((MVMFrame *)0)_V)
#line 18 "src/jit/emit_x64.dasc"
//|.type ARGCTX, MVMArgProcContext
#define Dt3(_V) (int)(ptrdiff_t)&(((MVMArgProcContext *)0)_V)
#line 19 "src/jit/emit_x64.dasc"
/* 'alternative base pointer. I'll be using this often, so picking rbx
 * here rather than the extended registers will likely lead to smaller
 * bytecode */
//|.type WORK, MVMRegister, rbx
#define Dt4(_V) (int)(ptrdiff_t)&(((MVMRegister *)0)_V)
#line 23 "src/jit/emit_x64.dasc"
//|.type ARGS, MVMRegister, r12
#define Dt5(_V) (int)(ptrdiff_t)&(((MVMRegister *)0)_V)
#line 24 "src/jit/emit_x64.dasc"
//|.type ENV,  MVMRegister, r13
#define Dt6(_V) (int)(ptrdiff_t)&(((MVMRegister *)0)_V)
#line 25 "src/jit/emit_x64.dasc"

//|.macro saveregs
//| push TC; push WORK; push ARGS; push ENV
//|.endmacro

//|.macro restoreregs
//| pop ENV; pop ARGS; pop WORK; pop TC
//|.endmacro


const MVMint32 MVM_jit_support(void) {
    return 1;
}

const unsigned char * MVM_jit_actions(void) {
    return actions;
}

const unsigned int MVM_jit_num_globals(void) {
    return JIT_LABEL__MAX;
}


/* C Call argument registers */
//|.if WIN32
//|.define ARG1, rcx
//|.define ARG2, rdx
//|.define ARG3, r8
//|.define ARG4, r9
//|.else
//|.define ARG1, rdi
//|.define ARG2, rsi
//|.define ARG3, rdx
//|.define ARG4, rcx
//|.define ARG5, r8
//|.define ARG6, r9
//|.endif

/* Special register for the function to be invoked */
//|.define FUNCTION, r10;
/* all-purpose temporary register */
//|.define TMP, r11;
/* return value */
//|.define RV, rax


//|.macro callp, funcptr
//| mov64 FUNCTION, (uintptr_t)funcptr
//|.if WIN32
//| sub rsp, 32
//| call FUNCTION
//| add rsp, 32
//|.else
//| call FUNCTION
//|.endif
//|.endmacro

//|.macro addarg, i, val
//||switch(i) {
//||    case 0:
//|         mov ARG1, val
//||        break;
//||    case 1:
//|         mov ARG2, val
//||        break;
//||    case 2:
//|         mov ARG3, val
//||        break;
//||    case 3:
//|         mov ARG4, val
//||        break;
//|.if not WIN32
//||    case 4:
//|         mov ARG5, val
//||        break;
//||    case 5:
//|         mov ARG6, val
//||        break;
//|.endif
//||    default:
//||        MVM_exception_throw_adhoc(tc, "Can't JIT more than %d arguments", i);
//||}
//|.endmacro


/* A function prologue is always the same in x86 / x64, becuase
 * we do not provide variable arguments, instead arguments are provided
 * via a frame. All JIT entry points receive a prologue. */
void MVM_jit_emit_prologue(MVMThreadContext *tc, MVMJitGraph *jg,
                           dasm_State **Dst) {
    /* Setup stack */
    //| push rbp
    //| mov rbp, rsp
    dasm_put(Dst, 0);
#line 118 "src/jit/emit_x64.dasc"
    /* save callee-save registers */
    //| saveregs
    dasm_put(Dst, 5);
#line 120 "src/jit/emit_x64.dasc"
    /* setup special frame variables */
    //| mov TC,   ARG1
    //| mov WORK, FRAME:ARG2->work
    //| mov ARGS, FRAME:ARG2->params.args
    //| mov ENV,  FRAME:ARG2->env
    dasm_put(Dst, 13, Dt2(->work), Dt2(->params.args), Dt2(->env));
#line 125 "src/jit/emit_x64.dasc"
}

/* And a function epilogue is also always the same */
void MVM_jit_emit_epilogue(MVMThreadContext *tc, MVMJitGraph *jg,
                           dasm_State **Dst) {
    //| ->exit:
    dasm_put(Dst, 30);
#line 131 "src/jit/emit_x64.dasc"
    /* restore callee-save registers */
    //| restoreregs
    dasm_put(Dst, 33);
#line 133 "src/jit/emit_x64.dasc"
    /* Restore stack */
    //| mov rsp, rbp
    //| pop rbp
    //| ret
    dasm_put(Dst, 41);
#line 137 "src/jit/emit_x64.dasc"
}

/* compile per instruction, can't really do any better yet */
void MVM_jit_emit_primitive(MVMThreadContext *tc, MVMJitGraph *jg,
                            MVMJitPrimitive * prim, dasm_State **Dst) {
    MVMSpeshIns *ins = prim->ins;
    MVM_jit_log(tc, "emit opcode: <%s>\n", ins->info->name);
    /* Quite a few of these opcodes are copies. Ultimately, I want to
     * move copies to their own node (MVMJitCopy or such), and reduce
     * the number of copies (and thereby increase the efficiency), but
     * currently that isn't really feasible. */
    switch (ins->info->opcode) {
    case MVM_OP_const_i64_16: {
        MVMint32 reg = ins->operands[0].reg.orig;
        /* Upgrade to 64 bit */
        MVMint64 val = (MVMint64)ins->operands[1].lit_i16;
        //| mov WORK[reg], qword val
        dasm_put(Dst, 48, Dt4([reg]), val);
#line 154 "src/jit/emit_x64.dasc"
        break;
    }
    case MVM_OP_const_i64: {
        MVMint32 reg = ins->operands[0].reg.orig;
        MVMint64 val = ins->operands[1].lit_i64;
        //| mov64 TMP, val
        //| mov WORK[reg], TMP
        dasm_put(Dst, 54, (unsigned int)(val), (unsigned int)((val)>>32), Dt4([reg]));
#line 161 "src/jit/emit_x64.dasc"
        break;
    }
    case MVM_OP_sp_getarg_o:
    case MVM_OP_sp_getarg_n:
    case MVM_OP_sp_getarg_s:
    case MVM_OP_sp_getarg_i: {
        MVMint32 reg = ins->operands[0].reg.orig;
        MVMuint16 idx = ins->operands[1].callsite_idx;
        //| mov TMP, ARGS[idx]
        //| mov WORK[reg], TMP
        dasm_put(Dst, 63, Dt5([idx]), Dt4([reg]));
#line 171 "src/jit/emit_x64.dasc"
        break;
    }
    case MVM_OP_set: {
         MVMint32 reg1 = ins->operands[0].reg.orig;
         MVMint32 reg2 = ins->operands[1].reg.orig;
         //| mov TMP, WORK[reg2]
         //| mov WORK[reg1], TMP
         dasm_put(Dst, 74, Dt4([reg2]), Dt4([reg1]));
#line 178 "src/jit/emit_x64.dasc"
         break;
    }
    case MVM_OP_const_s: {
         MVMint32 reg = ins->operands[0].reg.orig;
         MVMint32 idx = ins->operands[1].lit_str_idx;
         MVMStaticFrame *sf = jg->spesh->sf;
         MVMString * s = sf->body.cu->body.strings[idx];
         // TODO fixme
         //| mov64 TMP, (uintptr_t)s
         //| mov WORK[reg], TMP
         dasm_put(Dst, 54, (unsigned int)((uintptr_t)s), (unsigned int)(((uintptr_t)s)>>32), Dt4([reg]));
#line 188 "src/jit/emit_x64.dasc"
         break;
    }
    case MVM_OP_add_i: {
        /* a = b + c */
        MVMint32 reg_a = ins->operands[0].reg.orig;
        MVMint32 reg_b = ins->operands[1].reg.orig;
        MVMint32 reg_c = ins->operands[2].reg.orig;
        //| mov rax, WORK[reg_b]
        //| add rax, WORK[reg_c]
        //| mov WORK[reg_a], rax
        dasm_put(Dst, 83, Dt4([reg_b]), Dt4([reg_c]), Dt4([reg_a]));
#line 198 "src/jit/emit_x64.dasc"
        break;
    }
    case MVM_OP_sub_i: {
        MVMint32 reg_a = ins->operands[0].reg.orig;
        MVMint32 reg_b = ins->operands[1].reg.orig;
        MVMint32 reg_c = ins->operands[2].reg.orig;
        //| mov rax, WORK[reg_b]
        //| sub rax, WORK[reg_c]
        //| mov WORK[reg_a], rax
        dasm_put(Dst, 96, Dt4([reg_b]), Dt4([reg_c]), Dt4([reg_a]));
#line 207 "src/jit/emit_x64.dasc"
        break;
    }
    case MVM_OP_inc_i: {
         MVMint32 reg = ins->operands[0].reg.orig;
         //| inc qword WORK[reg]
         dasm_put(Dst, 109, Dt4([reg]));
#line 212 "src/jit/emit_x64.dasc"
         break;
    }
    case MVM_OP_dec_i: {
        MVMint32 reg = ins->operands[0].reg.orig;
        //| dec qword WORK[reg]
        dasm_put(Dst, 115, Dt4([reg]));
#line 217 "src/jit/emit_x64.dasc"
        break;
    }
    case MVM_OP_eq_i:
    case MVM_OP_ne_i:
    case MVM_OP_lt_i:
    case MVM_OP_le_i:
    case MVM_OP_gt_i:
    case MVM_OP_ge_i: {
        MVMint32 reg_a = ins->operands[0].reg.orig;
        MVMint32 reg_b = ins->operands[1].reg.orig;
        MVMint32 reg_c = ins->operands[2].reg.orig;
        //| mov rax, WORK[reg_b]
        //| cmp rax, WORK[reg_c]
        dasm_put(Dst, 121, Dt4([reg_b]), Dt4([reg_c]));
#line 230 "src/jit/emit_x64.dasc"
        /* copy the right comparison bit */
        switch(ins->info->opcode) {
        case MVM_OP_eq_i:
            //| sete al
            dasm_put(Dst, 130);
#line 234 "src/jit/emit_x64.dasc"
            break;
        case MVM_OP_ne_i:
            //| setne al
            dasm_put(Dst, 134);
#line 237 "src/jit/emit_x64.dasc"
            break;
        case MVM_OP_lt_i:
            //| setl al
            dasm_put(Dst, 138);
#line 240 "src/jit/emit_x64.dasc"
            break;
        case MVM_OP_le_i:
            //| setle al
            dasm_put(Dst, 142);
#line 243 "src/jit/emit_x64.dasc"
            break;
        case MVM_OP_gt_i:
            //| setg al
            dasm_put(Dst, 146);
#line 246 "src/jit/emit_x64.dasc"
            break;
        case MVM_OP_ge_i:
            //| setge al
            dasm_put(Dst, 150);
#line 249 "src/jit/emit_x64.dasc"
            break;
        }
        //| movzx rax, al        // zero extend al (byte) to rax (quadword)
        //| mov WORK[reg_a], rax // store in mvm register
        dasm_put(Dst, 154, Dt4([reg_a]));
#line 253 "src/jit/emit_x64.dasc"
        break;
    }
    default:
        MVM_exception_throw_adhoc(tc, "Can't JIT opcode");
    }
}

void MVM_jit_emit_call_c(MVMThreadContext *tc, MVMJitGraph *jg,
                         MVMJitCallC * call_spec, dasm_State **Dst) {
    int i;
    MVMJitAddr *args = call_spec->args;
    MVM_jit_log(tc, "emit c call <%d args>\n", call_spec->num_args);
    if (call_spec->has_vargs) {
        MVM_exception_throw_adhoc(tc, "JIT can't handle varargs yet");
    }
    /* first, add arguments */
    for (i = 0; i < call_spec->num_args; i++) {
        switch (args[i].base) {
        case MVM_JIT_ADDR_STACK: /* unlikely to use this now, though */
            //| addarg i, [rbp-args[i].idx];
            switch(i) {
                case 0:
            dasm_put(Dst, 163, -args[i].idx);
                    break;
                case 1:
            dasm_put(Dst, 168, -args[i].idx);
                    break;
                case 2:
            dasm_put(Dst, 173, -args[i].idx);
                    break;
                case 3:
            dasm_put(Dst, 178, -args[i].idx);
                    break;
                case 4:
            dasm_put(Dst, 183, -args[i].idx);
                    break;
                case 5:
            dasm_put(Dst, 188, -args[i].idx);
                    break;
                default:
                    MVM_exception_throw_adhoc(tc, "Can't JIT more than %d arguments", i);
            }
#line 273 "src/jit/emit_x64.dasc"
            break;
        case MVM_JIT_ADDR_INTERP:
            switch (args[i].idx) {
            case MVM_JIT_INTERP_TC:
                //| addarg i, TC;
                switch(i) {
                    case 0:
                dasm_put(Dst, 193);
                        break;
                    case 1:
                dasm_put(Dst, 198);
                        break;
                    case 2:
                dasm_put(Dst, 203);
                        break;
                    case 3:
                dasm_put(Dst, 208);
                        break;
                    case 4:
                dasm_put(Dst, 213);
                        break;
                    case 5:
                dasm_put(Dst, 218);
                        break;
                    default:
                        MVM_exception_throw_adhoc(tc, "Can't JIT more than %d arguments", i);
                }
#line 278 "src/jit/emit_x64.dasc"
                 break;
            case MVM_JIT_INTERP_FRAME:
                //| addarg i, TC->cur_frame;
                switch(i) {
                    case 0:
                dasm_put(Dst, 223, Dt1(->cur_frame));
                        break;
                    case 1:
                dasm_put(Dst, 228, Dt1(->cur_frame));
                        break;
                    case 2:
                dasm_put(Dst, 233, Dt1(->cur_frame));
                        break;
                    case 3:
                dasm_put(Dst, 238, Dt1(->cur_frame));
                        break;
                    case 4:
                dasm_put(Dst, 243, Dt1(->cur_frame));
                        break;
                    case 5:
                dasm_put(Dst, 248, Dt1(->cur_frame));
                        break;
                    default:
                        MVM_exception_throw_adhoc(tc, "Can't JIT more than %d arguments", i);
                }
#line 281 "src/jit/emit_x64.dasc"
                break;
            }
            break;
        case MVM_JIT_ADDR_REG:
            //| addarg i, WORK[args[i].idx];
            switch(i) {
                case 0:
            dasm_put(Dst, 253, Dt4([args[i].idx]));
                    break;
                case 1:
            dasm_put(Dst, 258, Dt4([args[i].idx]));
                    break;
                case 2:
            dasm_put(Dst, 263, Dt4([args[i].idx]));
                    break;
                case 3:
            dasm_put(Dst, 268, Dt4([args[i].idx]));
                    break;
                case 4:
            dasm_put(Dst, 273, Dt4([args[i].idx]));
                    break;
                case 5:
            dasm_put(Dst, 278, Dt4([args[i].idx]));
                    break;
                default:
                    MVM_exception_throw_adhoc(tc, "Can't JIT more than %d arguments", i);
            }
#line 286 "src/jit/emit_x64.dasc"
            break;
        case MVM_JIT_ADDR_LITERAL:
            //| addarg i, args[i].idx;
            switch(i) {
                case 0:
            dasm_put(Dst, 283, args[i].idx);
                    break;
                case 1:
            dasm_put(Dst, 288, args[i].idx);
                    break;
                case 2:
            dasm_put(Dst, 293, args[i].idx);
                    break;
                case 3:
            dasm_put(Dst, 298, args[i].idx);
                    break;
                case 4:
            dasm_put(Dst, 303, args[i].idx);
                    break;
                case 5:
            dasm_put(Dst, 308, args[i].idx);
                    break;
                default:
                    MVM_exception_throw_adhoc(tc, "Can't JIT more than %d arguments", i);
            }
#line 289 "src/jit/emit_x64.dasc"
            break;
        }
    }
    /* Emit the call. I think we should be able to do something smarter than
     * store the constant into the bytecode, like a data segment. But I'm
     * not sure. */
     //| callp call_spec->func_ptr
     dasm_put(Dst, 313, (unsigned int)((uintptr_t)call_spec->func_ptr), (unsigned int)(((uintptr_t)call_spec->func_ptr)>>32));
#line 296 "src/jit/emit_x64.dasc"
}

void MVM_jit_emit_branch(MVMThreadContext *tc, MVMJitGraph *jg,
                         MVMJitBranch * branch, dasm_State **Dst) {
    MVMSpeshIns *ins = branch->ins;
    MVMint32 name = branch->dest.name;
    if (ins == NULL || ins->info->opcode == MVM_OP_goto) {
        MVM_jit_log(tc, "emit jump to label %d\n", name);
        if (name == MVM_JIT_BRANCH_EXIT) {
            //| jmp ->exit
            dasm_put(Dst, 322);
#line 306 "src/jit/emit_x64.dasc"
        } else {
            //| jmp =>(name)
            dasm_put(Dst, 327, (name));
#line 308 "src/jit/emit_x64.dasc"
        }
    } else {
        MVMint16 reg = ins->operands[0].reg.orig;
        MVM_jit_log(tc, "emit branch <%s> to label %d\n",
                    ins->info->name, name);
        switch(ins->info->opcode) {
        case MVM_OP_if_i:
            //| mov rax, WORK[reg];
            //| test rax, rax;
            //| jnz =>(name); // jump to dynamic label
            dasm_put(Dst, 331, Dt4([reg]), (name));
#line 318 "src/jit/emit_x64.dasc"
            break;
        case MVM_OP_unless_i:
            //| mov rax, WORK[reg];
            //| test rax, rax;
            //| jz =>(name);
            dasm_put(Dst, 342, Dt4([reg]), (name));
#line 323 "src/jit/emit_x64.dasc"
            break;
        default:
            MVM_exception_throw_adhoc(tc, "JIT: Can't handle conditional <%s>",
                                      ins->info->name);
        }
    }
}

void MVM_jit_emit_label(MVMThreadContext *tc, MVMJitGraph *jg,
                        MVMJitLabel *label, dasm_State **Dst) {
    //| =>(label->name):
    dasm_put(Dst, 353, (label->name));
#line 334 "src/jit/emit_x64.dasc"
}

void MVM_jit_emit_rvh(MVMThreadContext *tc, MVMJitGraph *jg,
                      MVMJitRVH *rvh, dasm_State **Dst) {
    switch(rvh->mode) {
    case MVM_JIT_RV_VAL_TO_REG:
        //| mov WORK[rvh->addr.idx], RV;
        dasm_put(Dst, 91, Dt4([rvh->addr.idx]));
#line 341 "src/jit/emit_x64.dasc"
        break;
    case MVM_JIT_RV_REF_TO_REG:
        //| mov TMP, [RV]; // maybe add an offset?
        //| mov WORK[rvh->addr.idx], TMP;
        dasm_put(Dst, 355, Dt4([rvh->addr.idx]));
#line 345 "src/jit/emit_x64.dasc"
        break;
    case MVM_JIT_RV_REG_TO_PTR:
        //| mov TMP, WORK[rvh->addr.idx];
        //| mov [RV], TMP;
        dasm_put(Dst, 363, Dt4([rvh->addr.idx]));
#line 349 "src/jit/emit_x64.dasc"
        break;
    }
}
