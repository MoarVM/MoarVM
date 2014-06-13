/*
** This file has been pre-processed with DynASM.
** http://luajit.org/dynasm.html
** DynASM version 1.3.0, DynASM x64 version 1.3.0
** DO NOT EDIT! The original file is in "src/jit/x86_64.dasc".
*/

#line 1 "src/jit/x86_64.dasc"
#include "moar.h"
#include <dasm_proto.h>
#include <dasm_x86.h>
#include "emit.h"

//|.arch x64
#if DASM_VERSION != 10300
#error "Version mismatch between DynASM and included encoding engine"
#endif
#line 7 "src/jit/x86_64.dasc"
//|.actionlist actions
static const unsigned char actions[237] = {
  85,72,137,229,255,65,86,83,65,84,65,85,255,73,137,252,254,72,139,158,233,
  76,139,166,233,76,139,174,233,255,248,10,255,65,93,65,92,91,65,94,255,72,
  137,252,236,93,195,255,72,199,131,233,237,255,72,139,131,233,72,3,131,233,
  72,137,131,233,255,72,139,189,233,255,72,139,181,233,255,72,139,149,233,255,
  72,139,141,233,255,76,139,133,233,255,76,139,141,233,255,76,137,252,247,255,
  76,137,252,246,255,76,137,252,242,255,76,137,252,241,255,77,137,252,240,255,
  77,137,252,241,255,73,139,190,233,255,73,139,182,233,255,73,139,150,233,255,
  73,139,142,233,255,77,139,134,233,255,77,139,142,233,255,72,139,187,233,255,
  72,139,179,233,255,72,139,147,233,255,72,139,139,233,255,76,139,131,233,255,
  76,139,139,233,255,72,199,199,237,255,72,199,198,237,255,72,199,194,237,255,
  72,199,193,237,255,73,199,192,237,255,73,199,193,237,255,73,186,237,237,65,
  252,255,210,255,252,233,244,10,255,252,233,245,255,249,255
};

#line 8 "src/jit/x86_64.dasc"

/* Static allocation of relevant types to registers. I pick
 * callee-save registers for efficiency. It is likely we'll be calling
 * quite a C functions, and this saves us the trouble of storing
 * them. Moreover, C compilers preferentially do not use callee-saved
 * registers, and so in most cases, these won't be touched at all. */
//|.type TC, MVMThreadContext, r14
#define Dt1(_V) (int)(ptrdiff_t)&(((MVMThreadContext *)0)_V)
#line 15 "src/jit/x86_64.dasc"
//|.type FRAME, MVMFrame
#define Dt2(_V) (int)(ptrdiff_t)&(((MVMFrame *)0)_V)
#line 16 "src/jit/x86_64.dasc"
/* 'alternative base pointer. I'll be using this often, so picking rbx
 * here rather than the extended registers will likely lead to smaller
 * bytecode */
//|.type WORK, MVMRegister, rbx
#define Dt3(_V) (int)(ptrdiff_t)&(((MVMRegister *)0)_V)
#line 20 "src/jit/x86_64.dasc"
//|.type ARGS, MVMRegister, r12
#define Dt4(_V) (int)(ptrdiff_t)&(((MVMRegister *)0)_V)
#line 21 "src/jit/x86_64.dasc"
//|.type ENV,  MVMRegister, r13
#define Dt5(_V) (int)(ptrdiff_t)&(((MVMRegister *)0)_V)
#line 22 "src/jit/x86_64.dasc"

//|.macro saveregs
//| push TC; push WORK; push ARGS; push ENV
//|.endmacro

//|.macro restoreregs
//| pop ENV; pop ARGS; pop WORK; pop TC
//|.endmacro


//|.section code
#define DASM_SECTION_CODE	0
#define DASM_MAXSECTION		1
#line 33 "src/jit/x86_64.dasc"
//|.globals JIT_LABEL_
enum {
  JIT_LABEL_exit,
  JIT_LABEL__MAX
};
#line 34 "src/jit/x86_64.dasc"

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

//|.define ARG1, rdi
//|.define ARG2, rsi
//|.define ARG3, rdx
//|.define ARG4, rcx
//|.define ARG5, r8
//|.define ARG6, r9

/* Special register for the function to be invoked */
//|.define FUNCTION, r10

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
//||    case 4:
//|         mov ARG5, val
//||        break;
//||    case 5:
//|         mov ARG6, val
//||        break;
//||    default:
//||        MVM_exception_throw_adhoc(tc, "Can't JIT more than 6 arguments");
//||}
//|.endmacro



/* A function prologue is always the same in x86 / x64, becuase
 * we do not provide variable arguments, instead arguments are provided
 * via a frame. All JIT entry points receive prologue */
void MVM_jit_emit_prologue(MVMThreadContext *tc, dasm_State **Dst) {
    /* Setup stack */
    //| push rbp
    //| mov rbp, rsp
    dasm_put(Dst, 0);
#line 94 "src/jit/x86_64.dasc"
    /* save callee-save registers */
    //| saveregs
    dasm_put(Dst, 5);
#line 96 "src/jit/x86_64.dasc"
    /* setup special frame variables */
    //| mov TC,   ARG1
    //| mov WORK, FRAME:ARG2->work
    //| mov ARGS, FRAME:ARG2->args
    //| mov ENV,  FRAME:ARG2->env
    dasm_put(Dst, 13, Dt2(->work), Dt2(->args), Dt2(->env));
#line 101 "src/jit/x86_64.dasc"
}

/* And a function epilogue is also always the same */
void MVM_jit_emit_epilogue(MVMThreadContext *tc, dasm_State **Dst) {
    //| ->exit:
    dasm_put(Dst, 30);
#line 106 "src/jit/x86_64.dasc"
    /* restore callee-save registers */
    //| restoreregs
    dasm_put(Dst, 33);
#line 108 "src/jit/x86_64.dasc"
    /* Restore stack */
    //| mov rsp, rbp
    //| pop rbp
    //| ret
    dasm_put(Dst, 41);
#line 112 "src/jit/x86_64.dasc"
}

/* compile per instruction, can't really do any better yet */
void MVM_jit_emit_primitive(MVMThreadContext *tc, MVMJitPrimitive * prim,
                            dasm_State **Dst) {
    MVMSpeshIns *ins = prim->ins;
    fprintf(stderr, "original opcode: <%s>\n", ins->info->name);
    switch (ins->info->opcode) {
    case MVM_OP_const_i64_16: {
        MVMint32 reg = ins->operands[0].reg.orig;
        /* Upgrade to 64 bit */
        MVMint64 val = (MVMint64)ins->operands[1].lit_i16;
        fprintf(stderr, "Emit store %d reg %d\n", val, reg);
        //| mov WORK[reg], qword val
        dasm_put(Dst, 48, Dt3([reg]), val);
#line 126 "src/jit/x86_64.dasc"
        break;
    }
    case MVM_OP_const_i64: {
        MVMint32 reg = ins->operands[0].reg.orig;
        MVMint64 val = ins->operands[1].lit_i64;
        //| mov WORK[reg], qword val
        dasm_put(Dst, 48, Dt3([reg]), val);
#line 132 "src/jit/x86_64.dasc"
        break;
    }
    case MVM_OP_const_s: {
         MVMint32 reg = ins->operands[0].reg.i;
         MVMint32 idx = ins->operands[1].lit_str_idx;
         break;
    }
    case MVM_OP_add_i: {
        /* a = b + c */
        MVMint32 reg_a = ins->operands[0].reg.orig;
        MVMint32 reg_b = ins->operands[1].reg.orig;
        MVMint32 reg_c = ins->operands[2].reg.orig;
        fprintf(stderr, "Emit add r%d = r%d  + r%d\n", reg_a, reg_b, reg_c);
        //| mov rax, WORK[reg_b]
        //| add rax, WORK[reg_c]
        //| mov WORK[reg_a], rax
        dasm_put(Dst, 54, Dt3([reg_b]), Dt3([reg_c]), Dt3([reg_a]));
#line 148 "src/jit/x86_64.dasc"
        break;
    }
    default:
        MVM_exception_throw_adhoc(tc, "Can't JIT opcode");
    }
}

void MVM_jit_emit_call_c(MVMThreadContext *tc, MVMJitCallC * call_spec,
                         dasm_State **Dst) {
    int i;
    MVMJitAddr *args = call_spec->args;
    fprintf(stderr, "Emitting c call\n");
    if (call_spec->has_vargs) {
        MVM_exception_throw_adhoc(tc, "JIT can't handle varargs yet");
    }
    /* first, add arguments */
    for (i = 0; i < call_spec->num_args; i++) {
        switch (args[i].base) {
        case MVM_JIT_ADDR_STACK: /* unlikely to use this now, though */
            fprintf(stderr, "Emit load stack offset arg %d\n", args[i].idx);
            //| addarg i, [rbp-args[i].idx]
            switch(i) {
                case 0:
            dasm_put(Dst, 67, -args[i].idx);
                    break;
                case 1:
            dasm_put(Dst, 72, -args[i].idx);
                    break;
                case 2:
            dasm_put(Dst, 77, -args[i].idx);
                    break;
                case 3:
            dasm_put(Dst, 82, -args[i].idx);
                    break;
                case 4:
            dasm_put(Dst, 87, -args[i].idx);
                    break;
                case 5:
            dasm_put(Dst, 92, -args[i].idx);
                    break;
                default:
                    MVM_exception_throw_adhoc(tc, "Can't JIT more than 6 arguments");
            }
#line 169 "src/jit/x86_64.dasc"
            break;
        case MVM_JIT_ADDR_INTERP:
            switch (args[i].idx) {
            case MVM_JIT_INTERP_TC:
                //| addarg i, TC
                switch(i) {
                    case 0:
                dasm_put(Dst, 97);
                        break;
                    case 1:
                dasm_put(Dst, 102);
                        break;
                    case 2:
                dasm_put(Dst, 107);
                        break;
                    case 3:
                dasm_put(Dst, 112);
                        break;
                    case 4:
                dasm_put(Dst, 117);
                        break;
                    case 5:
                dasm_put(Dst, 122);
                        break;
                    default:
                        MVM_exception_throw_adhoc(tc, "Can't JIT more than 6 arguments");
                }
#line 174 "src/jit/x86_64.dasc"
                 break;
            case MVM_JIT_INTERP_FRAME:
                //| addarg i, TC->cur_frame
                switch(i) {
                    case 0:
                dasm_put(Dst, 127, Dt1(->cur_frame));
                        break;
                    case 1:
                dasm_put(Dst, 132, Dt1(->cur_frame));
                        break;
                    case 2:
                dasm_put(Dst, 137, Dt1(->cur_frame));
                        break;
                    case 3:
                dasm_put(Dst, 142, Dt1(->cur_frame));
                        break;
                    case 4:
                dasm_put(Dst, 147, Dt1(->cur_frame));
                        break;
                    case 5:
                dasm_put(Dst, 152, Dt1(->cur_frame));
                        break;
                    default:
                        MVM_exception_throw_adhoc(tc, "Can't JIT more than 6 arguments");
                }
#line 177 "src/jit/x86_64.dasc"
                break;
            }
            break;
        case MVM_JIT_ADDR_REG:
            fprintf(stderr, "Emit work offset arg %d\n", args[i].idx);
            //| addarg i, WORK[args[i].idx]
            switch(i) {
                case 0:
            dasm_put(Dst, 157, Dt3([args[i].idx]));
                    break;
                case 1:
            dasm_put(Dst, 162, Dt3([args[i].idx]));
                    break;
                case 2:
            dasm_put(Dst, 167, Dt3([args[i].idx]));
                    break;
                case 3:
            dasm_put(Dst, 172, Dt3([args[i].idx]));
                    break;
                case 4:
            dasm_put(Dst, 177, Dt3([args[i].idx]));
                    break;
                case 5:
            dasm_put(Dst, 182, Dt3([args[i].idx]));
                    break;
                default:
                    MVM_exception_throw_adhoc(tc, "Can't JIT more than 6 arguments");
            }
#line 183 "src/jit/x86_64.dasc"
            break;
        case MVM_JIT_ADDR_LITERAL:
            fprintf(stderr, "Emit constant arg %d\n", args[i].idx);
            //| addarg i, args[i].idx
            switch(i) {
                case 0:
            dasm_put(Dst, 187, args[i].idx);
                    break;
                case 1:
            dasm_put(Dst, 192, args[i].idx);
                    break;
                case 2:
            dasm_put(Dst, 197, args[i].idx);
                    break;
                case 3:
            dasm_put(Dst, 202, args[i].idx);
                    break;
                case 4:
            dasm_put(Dst, 207, args[i].idx);
                    break;
                case 5:
            dasm_put(Dst, 212, args[i].idx);
                    break;
                default:
                    MVM_exception_throw_adhoc(tc, "Can't JIT more than 6 arguments");
            }
#line 187 "src/jit/x86_64.dasc"
            break;

        }
    }
    /* Emit the call. I think we should be able to do something smarter than
     * store the constant into the bytecode, like a data segment. But I'm
     * not sure. */
    //| mov64 FUNCTION, (uintptr_t)call_spec->func_ptr
    //| call FUNCTION
    dasm_put(Dst, 217, (unsigned int)((uintptr_t)call_spec->func_ptr), (unsigned int)(((uintptr_t)call_spec->func_ptr)>>32));
#line 196 "src/jit/x86_64.dasc"
    /* No need to do anything here, our work registers are callee-saved :-) */
}

void MVM_jit_emit_branch(MVMThreadContext *tc, MVMJitBranch * branch,
                         dasm_State **Dst) {
    if (branch->destination == MVM_JIT_BRANCH_EXIT) {
        //| jmp ->exit
        dasm_put(Dst, 226);
#line 203 "src/jit/x86_64.dasc"
    } else {
        //| jmp =>(branch->destination)
        dasm_put(Dst, 231, (branch->destination));
#line 205 "src/jit/x86_64.dasc"
    }
}

void MVM_jit_emit_label(MVMThreadContext *tc, MVMint32 label,
                        dasm_State **Dst) {
    //| =>(label):
    dasm_put(Dst, 235, (label));
#line 211 "src/jit/x86_64.dasc"
}
