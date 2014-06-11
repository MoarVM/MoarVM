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
static const unsigned char actions[177] = {
  195,255,85,72,137,229,72,129,252,236,239,255,72,137,125,252,248,72,137,117,
  252,240,73,137,252,246,77,139,190,233,255,72,137,252,236,93,195,255,73,199,
  135,233,237,255,73,139,135,233,73,3,135,233,73,137,135,233,255,72,139,189,
  233,255,72,139,181,233,255,72,139,149,233,255,72,139,141,233,255,76,139,133,
  233,255,76,139,141,233,255,73,139,191,233,255,73,139,183,233,255,73,139,151,
  233,255,73,139,143,233,255,77,139,135,233,255,77,139,143,233,255,72,199,199,
  237,255,72,199,198,237,255,72,199,194,237,255,72,199,193,237,255,73,199,192,
  237,255,73,199,193,237,255,73,186,237,237,65,252,255,210,255,76,139,117,252,
  240,77,139,190,233,255,252,233,244,10,255,252,233,245,255,249,255
};

#line 8 "src/jit/x86_64.dasc"


const unsigned char * MVM_jit_actions(void) {
    return actions;
}

#define JIT_FRAME_SIZE 16

/* Type maps for work and frame registers. This is fragile. */
//|.type WORK, MVMRegister, r15
#define Dt1(_V) (int)(ptrdiff_t)&(((MVMRegister *)0)_V)
#line 18 "src/jit/x86_64.dasc"
//|.type FRAME, MVMFrame, r14
#define Dt2(_V) (int)(ptrdiff_t)&(((MVMFrame *)0)_V)
#line 19 "src/jit/x86_64.dasc"

//|.macro addarg, i, val
//||switch(i) {
//||    case 0:
//|         mov rdi, val
//||        break;
//||    case 1:
//|         mov rsi, val
//||        break;
//||    case 2:
//|         mov rdx, val
//||        break;
//||    case 3:
//|         mov rcx, val
//||        break;
//||    case 4:
//|         mov r8, val
//||        break;
//||    case 5:
//|         mov r9, val
//||        break;
//||    default:
//||        MVM_exception_throw_adhoc(tc, "Can't JIT more than 6 arguments");
//||}
//|.endmacro



//|.macro setup, loc
//| mov FRAME, loc
//| mov WORK, FRAME->work
//|.endmacro



/* A function prologue is always the same in x86 / x64, becuase
 * we do not provide variable arguments, instead arguments are provided
 * via a frame. All JIT entry points receive prologue */
void MVM_jit_emit_prologue(MVMThreadContext *tc, dasm_State **Dst) {
    /* set up our C call frame, i.e. allocate stack space*/
    //| ret
    dasm_put(Dst, 0);
#line 60 "src/jit/x86_64.dasc"

    //| push rbp
    //| mov rbp, rsp
    //| sub rsp, JIT_FRAME_SIZE
    dasm_put(Dst, 2, JIT_FRAME_SIZE);
#line 64 "src/jit/x86_64.dasc"

    //| mov [rbp-8], rdi              // thread context
    //| mov [rbp-16], rsi             // mvm frame
    //| setup rsi                     // setup our work registers
    dasm_put(Dst, 12, Dt2(->work));
#line 68 "src/jit/x86_64.dasc"

}

/* And a function epilogue is also always the same */
void MVM_jit_emit_epilogue(MVMThreadContext *tc, dasm_State **Dst) {
    //| mov rsp, rbp
    //| pop rbp
    //| ret
    dasm_put(Dst, 31);
#line 76 "src/jit/x86_64.dasc"
}

/* compile per instruction, can't really do any better yet */
void MVM_jit_emit_primitive(MVMThreadContext *tc, MVMJitPrimitive * prim,
                            dasm_State **Dst) {
    MVMSpeshIns *ins = prim->ins;
    switch (ins->info->opcode) {
    case MVM_OP_const_i64_16: {
        MVMint32 reg = ins->operands[0].reg.i;
        /* Upgrade to 64 bit */
        MVMint64 val = (MVMint64)ins->operands[1].lit_i16;
        //| mov WORK[reg], qword val
        dasm_put(Dst, 38, Dt1([reg]), val);
#line 88 "src/jit/x86_64.dasc"
        break;
    }
    case MVM_OP_const_i64: {
        MVMint32 reg = ins->operands[0].reg.i;
        MVMint64 val = ins->operands[1].lit_i64;
        //| mov WORK[reg], qword val
        dasm_put(Dst, 38, Dt1([reg]), val);
#line 94 "src/jit/x86_64.dasc"
        break;
    }
    case MVM_OP_add_i: {
        /* a = b + c */
        MVMint32 reg_a = ins->operands[0].reg.i;
        MVMint32 reg_b = ins->operands[1].reg.i;
        MVMint32 reg_c = ins->operands[2].reg.i;
        //| mov rax, WORK[reg_b]
        //| add rax, WORK[reg_c]
        //| mov WORK[reg_a], rax
        dasm_put(Dst, 44, Dt1([reg_b]), Dt1([reg_c]), Dt1([reg_a]));
#line 104 "src/jit/x86_64.dasc"
        break;
    }
    default:
        MVM_exception_throw_adhoc(tc, "Can't JIT opcode");
    }
}

void MVM_jit_emit_call_c(MVMThreadContext *tc, MVMJitCallC * call_spec,
                         dasm_State **Dst) {
    int i;
    MVMJitCallArg *args = call_spec->args;
    if (call_spec->has_vargs) {
        MVM_exception_throw_adhoc(tc, "JIT can't handle varargs yet");
    }
    /* first, add arguments */
    for (i = 0; i < call_spec->num_args; i++) {
        switch (args[i].base) {
        case MVM_JIT_ARG_STACK:
            //| addarg i, [rbp-args[i].offset]
            switch(i) {
                case 0:
            dasm_put(Dst, 57, -args[i].offset);
                    break;
                case 1:
            dasm_put(Dst, 62, -args[i].offset);
                    break;
                case 2:
            dasm_put(Dst, 67, -args[i].offset);
                    break;
                case 3:
            dasm_put(Dst, 72, -args[i].offset);
                    break;
                case 4:
            dasm_put(Dst, 77, -args[i].offset);
                    break;
                case 5:
            dasm_put(Dst, 82, -args[i].offset);
                    break;
                default:
                    MVM_exception_throw_adhoc(tc, "Can't JIT more than 6 arguments");
            }
#line 123 "src/jit/x86_64.dasc"
            break;
        case MVM_JIT_ARG_REG:
            //| addarg i, WORK[args[i].offset]
            switch(i) {
                case 0:
            dasm_put(Dst, 87, Dt1([args[i].offset]));
                    break;
                case 1:
            dasm_put(Dst, 92, Dt1([args[i].offset]));
                    break;
                case 2:
            dasm_put(Dst, 97, Dt1([args[i].offset]));
                    break;
                case 3:
            dasm_put(Dst, 102, Dt1([args[i].offset]));
                    break;
                case 4:
            dasm_put(Dst, 107, Dt1([args[i].offset]));
                    break;
                case 5:
            dasm_put(Dst, 112, Dt1([args[i].offset]));
                    break;
                default:
                    MVM_exception_throw_adhoc(tc, "Can't JIT more than 6 arguments");
            }
#line 126 "src/jit/x86_64.dasc"
            break;
        case MVM_JIT_ARG_CONST:
            //| addarg i, args[i].offset
            switch(i) {
                case 0:
            dasm_put(Dst, 117, args[i].offset);
                    break;
                case 1:
            dasm_put(Dst, 122, args[i].offset);
                    break;
                case 2:
            dasm_put(Dst, 127, args[i].offset);
                    break;
                case 3:
            dasm_put(Dst, 132, args[i].offset);
                    break;
                case 4:
            dasm_put(Dst, 137, args[i].offset);
                    break;
                case 5:
            dasm_put(Dst, 142, args[i].offset);
                    break;
                default:
                    MVM_exception_throw_adhoc(tc, "Can't JIT more than 6 arguments");
            }
#line 129 "src/jit/x86_64.dasc"
            break;
        }
    }
    //| mov64 r10, (uintptr_t)call_spec->func_ptr
    //| call r10
    dasm_put(Dst, 147, (unsigned int)((uintptr_t)call_spec->func_ptr), (unsigned int)(((uintptr_t)call_spec->func_ptr)>>32));
#line 134 "src/jit/x86_64.dasc"
    /* Restore the work register pointer */
    //| setup [rbp-16]
    dasm_put(Dst, 156, Dt2(->work));
#line 136 "src/jit/x86_64.dasc"
}

void MVM_jit_emit_branch(MVMThreadContext *tc, MVMJitBranch * branch,
                         dasm_State **Dst) {
    if (branch->destination == MVM_JIT_BRANCH_EXIT) {
        //| jmp ->jit_exit
        dasm_put(Dst, 166);
#line 142 "src/jit/x86_64.dasc"
    } else {
        //| jmp =>(branch->destination)
        dasm_put(Dst, 171, (branch->destination));
#line 144 "src/jit/x86_64.dasc"
    }
}

void MVM_jit_emit_label(MVMThreadContext *tc, MVMint32 label,
                        dasm_State **Dst) {
    //| =>(label):
    dasm_put(Dst, 175, (label));
#line 150 "src/jit/x86_64.dasc"
}
