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
static const unsigned char actions[629] = {
  85,72,137,229,255,65,86,83,65,84,65,85,255,73,137,252,254,72,139,158,233,
  76,139,166,233,76,139,174,233,255,248,10,255,65,93,65,92,91,65,94,255,72,
  137,252,236,93,195,255,72,199,131,233,237,255,72,185,237,237,72,137,139,233,
  255,73,139,140,253,36,233,72,137,139,233,255,72,139,139,233,72,139,137,233,
  72,131,185,233,0,255,15,132,244,247,72,139,137,233,255,248,1,255,72,131,252,
  249,0,255,15,133,244,248,255,73,139,142,233,72,139,137,233,248,2,255,72,139,
  139,233,72,139,147,233,76,141,129,233,73,131,184,233,0,15,132,244,247,77,
  139,128,233,248,1,255,102,252,247,129,233,236,15,132,244,248,72,131,252,250,
  0,15,132,244,248,255,102,252,247,130,233,236,15,133,244,248,82,65,80,76,137,
  252,247,255,72,139,179,233,73,186,237,237,65,252,255,210,65,88,90,248,2,255,
  73,137,144,233,255,72,139,139,233,72,137,139,233,255,72,139,131,233,255,72,
  3,131,233,255,72,43,131,233,255,72,15,175,131,233,255,72,153,72,252,247,187,
  233,255,72,137,147,233,255,72,137,131,233,255,72,252,255,131,233,255,72,252,
  255,139,233,255,252,242,15,16,131,233,255,252,242,15,88,131,233,255,252,242,
  15,92,131,233,255,252,242,15,89,131,233,255,252,242,15,94,131,233,255,252,
  242,15,17,131,233,255,252,242,72,15,42,131,233,252,242,15,17,131,233,255,
  252,242,72,15,44,131,233,72,137,131,233,255,72,59,131,233,255,15,148,208,
  255,15,149,208,255,15,156,208,255,15,158,208,255,15,159,208,255,15,157,208,
  255,72,15,182,192,72,137,131,233,255,72,139,189,233,255,72,139,181,233,255,
  72,139,149,233,255,72,139,141,233,255,76,139,133,233,255,76,139,141,233,255,
  76,137,252,246,255,76,137,252,242,255,76,137,252,241,255,77,137,252,240,255,
  77,137,252,241,255,73,139,190,233,255,73,139,182,233,255,73,139,150,233,255,
  73,139,142,233,255,77,139,134,233,255,77,139,142,233,255,72,139,187,233,255,
  72,139,179,233,255,72,139,147,233,255,72,139,139,233,255,76,139,131,233,255,
  76,139,139,233,255,252,242,15,16,139,233,255,252,242,15,16,147,233,255,252,
  242,15,16,155,233,255,252,242,15,16,163,233,255,252,242,15,16,171,233,255,
  252,242,15,16,179,233,255,252,242,15,16,187,233,255,72,199,199,237,255,72,
  199,198,237,255,72,199,194,237,255,72,199,193,237,255,73,199,192,237,255,
  73,199,193,237,255,73,186,237,237,65,252,255,210,255,252,233,244,10,255,252,
  233,245,255,72,139,131,233,72,133,192,15,133,245,255,72,139,131,233,72,133,
  192,15,132,245,255,249,255,72,139,8,72,137,139,233,255,72,139,139,233,72,
  137,8,255
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
//|.type P6OPAQUE, MVMP6opaque
#define Dt7(_V) (int)(ptrdiff_t)&(((MVMP6opaque *)0)_V)
#line 26 "src/jit/emit_x64.dasc"
//|.type P6OBODY, MVMP6opaqueBody
#define Dt8(_V) (int)(ptrdiff_t)&(((MVMP6opaqueBody *)0)_V)
#line 27 "src/jit/emit_x64.dasc"
//|.type MVMINSTANCE, MVMInstance
#define Dt9(_V) (int)(ptrdiff_t)&(((MVMInstance *)0)_V)
#line 28 "src/jit/emit_x64.dasc"
//|.type OBJECT, MVMObject
#define DtA(_V) (int)(ptrdiff_t)&(((MVMObject *)0)_V)
#line 29 "src/jit/emit_x64.dasc"
//|.type COLLECTABLE, MVMCollectable
#define DtB(_V) (int)(ptrdiff_t)&(((MVMCollectable *)0)_V)
#line 30 "src/jit/emit_x64.dasc"


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

/* C call argument registers for floating point */
//|.if WIN32
//|.define ARG1F, xmm0
//|.define ARG2F, xmm1
//|.define ARG3F, xmm2
//|.define ARG4F, xmm3
//|.else
//|.define ARG1F, xmm0
//|.define ARG2F, xmm1
//|.define ARG3F, xmm2
//|.define ARG4F, xmm3
//|.define ARG5F, xmm4
//|.define ARG6F, xmm5
//|.define ARG7F, xmm6
//|.define ARG8F, xmm7
//|.endif

/* Special register for the function to be invoked 
 * (chosen because it isn't involved in argument passing
 *  and volatile) */
//|.define FUNCTION, r10
/* all-purpose temporary registers */
//|.define TMP1, rcx
//|.define TMP2, rdx
//|.define TMP3, r8
//|.define TMP4, r9
//|.define TMP5, r10
//|.define TMP6, r11

/* return value */
//|.define RV, rax
//|.define RVF, xmm0

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

//|.macro addarg_f, i, val
//||switch(i) {
//||    case 0:
//|         movsd ARG1F, qword val
//||        break;
//||    case 1:
//|         movsd ARG2F, qword val
//||        break;
//||    case 2:
//|         movsd ARG3F, qword val
//||        break;
//||    case 3:
//|         movsd ARG4F, qword val
//||        break;
//|.if not WIN32
//||    case 4:
//|         movsd ARG5F, qword val
//||        break;
//||    case 5:
//|         movsd ARG6F, qword val
//||        break;
//||    case 6:
//|         movsd ARG7F, qword val
//||        break;
//||    case 7:
//|         movsd ARG8F, qword val
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
#line 182 "src/jit/emit_x64.dasc"
    /* save callee-save registers */
    //| saveregs
    dasm_put(Dst, 5);
#line 184 "src/jit/emit_x64.dasc"
    /* setup special frame variables */
    //| mov TC,   ARG1
    //| mov WORK, FRAME:ARG2->work
    //| mov ARGS, FRAME:ARG2->params.args
    //| mov ENV,  FRAME:ARG2->env
    dasm_put(Dst, 13, Dt2(->work), Dt2(->params.args), Dt2(->env));
#line 189 "src/jit/emit_x64.dasc"
}

/* And a function epilogue is also always the same */
void MVM_jit_emit_epilogue(MVMThreadContext *tc, MVMJitGraph *jg,
                           dasm_State **Dst) {
    //| ->exit:
    dasm_put(Dst, 30);
#line 195 "src/jit/emit_x64.dasc"
    /* restore callee-save registers */
    //| restoreregs
    dasm_put(Dst, 33);
#line 197 "src/jit/emit_x64.dasc"
    /* Restore stack */
    //| mov rsp, rbp
    //| pop rbp
    //| ret
    dasm_put(Dst, 41);
#line 201 "src/jit/emit_x64.dasc"
}

/* compile per instruction, can't really do any better yet */
void MVM_jit_emit_primitive(MVMThreadContext *tc, MVMJitGraph *jg,
                            MVMJitPrimitive * prim, dasm_State **Dst) {
    MVMSpeshIns *ins = prim->ins;
    MVMuint16 op = ins->info->opcode;
    MVM_jit_log(tc, "emit opcode: <%s>\n", ins->info->name);
    /* Quite a few of these opcodes are copies. Ultimately, I want to
     * move copies to their own node (MVMJitCopy or such), and reduce
     * the number of copies (and thereby increase the efficiency), but
     * currently that isn't really feasible. */
    switch (op) {
    case MVM_OP_const_i64_16: {
        MVMint32 reg = ins->operands[0].reg.orig;
        /* Upgrade to 64 bit */
        MVMint64 val = (MVMint64)ins->operands[1].lit_i16;
        //| mov WORK[reg], qword val;
        dasm_put(Dst, 48, Dt4([reg]), val);
#line 219 "src/jit/emit_x64.dasc"
        break;
    }
    case MVM_OP_const_i64: {
        MVMint32 reg = ins->operands[0].reg.orig;
        MVMint64 val = ins->operands[1].lit_i64;
        //| mov64 TMP1, val;
        //| mov WORK[reg], TMP1;
        dasm_put(Dst, 54, (unsigned int)(val), (unsigned int)((val)>>32), Dt4([reg]));
#line 226 "src/jit/emit_x64.dasc"
        break;
    }
    case MVM_OP_const_n64: {
        MVM_jit_log(tc, "store const %f\n", ins->operands[1].lit_n64);
        MVMint16 reg = ins->operands[0].reg.orig;
        MVMint64 valbytes = ins->operands[1].lit_i64;
        //| mov64 TMP1, valbytes;
        //| mov WORK[reg], TMP1;
        dasm_put(Dst, 54, (unsigned int)(valbytes), (unsigned int)((valbytes)>>32), Dt4([reg]));
#line 234 "src/jit/emit_x64.dasc"
        break;
    }
    case MVM_OP_sp_getarg_o:
    case MVM_OP_sp_getarg_n:
    case MVM_OP_sp_getarg_s:
    case MVM_OP_sp_getarg_i: {
        MVMint32 reg = ins->operands[0].reg.orig;
        MVMuint16 idx = ins->operands[1].callsite_idx;
        //| mov TMP1, ARGS[idx];
        //| mov WORK[reg], TMP1;
        dasm_put(Dst, 63, Dt5([idx]), Dt4([reg]));
#line 244 "src/jit/emit_x64.dasc"
        break;
    }
    case MVM_OP_sp_p6oget_i:
    case MVM_OP_sp_p6oget_n:
    case MVM_OP_sp_p6oget_s:
    case MVM_OP_sp_p6oget_o: {
        MVMint16 dst    = ins->operands[0].reg.orig;
        MVMint16 obj    = ins->operands[1].reg.orig;
        MVMint16 offset = ins->operands[2].callsite_idx;
        //| mov TMP1, WORK[obj];
        //| mov TMP1, P6OPAQUE:TMP1->body;
        //| cmp qword P6OBODY:TMP1->replaced, 0;
        dasm_put(Dst, 74, Dt4([obj]), Dt7(->body), Dt8(->replaced));
#line 256 "src/jit/emit_x64.dasc"
        /* if not zero then load replacement data pointer */
        //| je >1;
        //| mov TMP1, P6OBODY:TMP1->replaced;
        dasm_put(Dst, 88, Dt8(->replaced));
#line 259 "src/jit/emit_x64.dasc"
        /* otherwise do nothing (i.e. the body is our data pointer) */
        //|1:
        dasm_put(Dst, 97);
#line 261 "src/jit/emit_x64.dasc"
        /* load our value */
        //| mov TMP1, [TMP1 + offset];
        dasm_put(Dst, 92, offset);
#line 263 "src/jit/emit_x64.dasc"
        if (op == MVM_OP_sp_p6oget_o) {
            /* transform null object pointers to VMNull */
            //| cmp TMP1, 0;
            dasm_put(Dst, 100);
#line 266 "src/jit/emit_x64.dasc"
            /* not-null? done */
            //| jne >2;
            dasm_put(Dst, 106);
#line 268 "src/jit/emit_x64.dasc"
            /* store VMNull instead */
            //| mov TMP1, TC->instance;
            //| mov TMP1, MVMINSTANCE:TMP1->VMNull;
            //|2:
            dasm_put(Dst, 111, Dt1(->instance), Dt9(->VMNull));
#line 272 "src/jit/emit_x64.dasc"
        }
        //| mov WORK[dst], TMP1;
        dasm_put(Dst, 58, Dt4([dst]));
#line 274 "src/jit/emit_x64.dasc"
        break;
    }
    case MVM_OP_sp_p6obind_i:
    case MVM_OP_sp_p6obind_n:
    case MVM_OP_sp_p6obind_o:
    case MVM_OP_sp_p6obind_s: {
        MVMint16 obj    = ins->operands[0].reg.orig;
        MVMint16 offset = ins->operands[1].callsite_idx;
        MVMint16 val    = ins->operands[2].reg.orig;
        //| mov TMP1, WORK[obj];            // object
        //| mov TMP2, WORK[val];            // value
        //| lea TMP3, P6OPAQUE:TMP1->body;  // body
        //| cmp qword P6OBODY:TMP3->replaced, 0; 
        //| je >1;
        //| mov TMP3, P6OBODY:TMP3->replaced; // replaced object body
        //|1:
        dasm_put(Dst, 122, Dt4([obj]), Dt4([val]), Dt7(->body), Dt8(->replaced), Dt8(->replaced));
#line 290 "src/jit/emit_x64.dasc"
        if (op == MVM_OP_sp_p6obind_o) {
            /* this is an assembly-version of MVM_gc_write_barrier
             * TMP1 is root, TMP2 is referenced value */
            // is our root second gen?
            //| test word COLLECTABLE:TMP1->flags, MVM_CF_SECOND_GEN; 
            //| jz >2; // if not, skip
            //| cmp TMP2, 0; // is value non-null? (should be)
            //| je >2; // if not, skip
            dasm_put(Dst, 150, DtB(->flags), MVM_CF_SECOND_GEN);
#line 298 "src/jit/emit_x64.dasc"
            // is the reference second gen?
            //| test word COLLECTABLE:TMP2->flags, MVM_CF_SECOND_GEN; 
            //| jnz >2;  // if so, skip
            //| push TMP2; // store value
            //| push TMP3; // store body pointer
            //| mov ARG1, TC;  // set tc as first argument
            dasm_put(Dst, 170, DtB(->flags), MVM_CF_SECOND_GEN);
#line 304 "src/jit/emit_x64.dasc"
            // NB, c call arguments arguments clobber our temporary
            // space (depending on ABI), so I reload the work object
            // from register space 
            //| mov ARG2, WORK[obj]; // object as second
            //| callp &MVM_gc_write_barrier_hit; // call our function
            //| pop TMP3; // restore body pointer
            //| pop TMP2; // restore value
            //|2: // done
            dasm_put(Dst, 188, Dt4([obj]), (unsigned int)((uintptr_t)&MVM_gc_write_barrier_hit), (unsigned int)(((uintptr_t)&MVM_gc_write_barrier_hit)>>32));
#line 312 "src/jit/emit_x64.dasc"
        }
        //| mov [TMP3+offset], TMP2; // store value into body
        dasm_put(Dst, 206, offset);
#line 314 "src/jit/emit_x64.dasc"
        break;
    }
    case MVM_OP_set: {
         MVMint32 reg1 = ins->operands[0].reg.orig;
         MVMint32 reg2 = ins->operands[1].reg.orig;
         //| mov TMP1, WORK[reg2]
         //| mov WORK[reg1], TMP1
         dasm_put(Dst, 211, Dt4([reg2]), Dt4([reg1]));
#line 321 "src/jit/emit_x64.dasc"
         break;
    }
    case MVM_OP_const_s: {
         MVMint32 reg = ins->operands[0].reg.orig;
         MVMint32 idx = ins->operands[1].lit_str_idx;
         MVMStaticFrame *sf = jg->spesh->sf;
         MVMString * s = sf->body.cu->body.strings[idx];
         // TODO fixme
         //| mov64 TMP1, (uintptr_t)s
         //| mov WORK[reg], TMP1
         dasm_put(Dst, 54, (unsigned int)((uintptr_t)s), (unsigned int)(((uintptr_t)s)>>32), Dt4([reg]));
#line 331 "src/jit/emit_x64.dasc"
         break;
    }
    case MVM_OP_add_i:
    case MVM_OP_sub_i:
    case MVM_OP_mul_i:
    case MVM_OP_div_i:
    case MVM_OP_mod_i: {
        MVMint32 reg_a = ins->operands[0].reg.orig;
        MVMint32 reg_b = ins->operands[1].reg.orig;
        MVMint32 reg_c = ins->operands[2].reg.orig;
        //| mov rax, WORK[reg_b];
        dasm_put(Dst, 220, Dt4([reg_b]));
#line 342 "src/jit/emit_x64.dasc"
        switch(ins->info->opcode) {
        case MVM_OP_add_i:
            //| add rax, WORK[reg_c];
            dasm_put(Dst, 225, Dt4([reg_c]));
#line 345 "src/jit/emit_x64.dasc"
            break;
        case MVM_OP_sub_i:
            //| sub rax, WORK[reg_c];
            dasm_put(Dst, 230, Dt4([reg_c]));
#line 348 "src/jit/emit_x64.dasc"
            break;
        case MVM_OP_mul_i:
            //| imul rax, WORK[reg_c];
            dasm_put(Dst, 235, Dt4([reg_c]));
#line 351 "src/jit/emit_x64.dasc"
            break;
        case MVM_OP_div_i:
        case MVM_OP_mod_i:
            // Convert Quadword to Octoword, i.e. use rax:rdx as one
            // single 16 byte register
            //| cqo; 
            //| idiv qword WORK[reg_c];
            dasm_put(Dst, 241, Dt4([reg_c]));
#line 358 "src/jit/emit_x64.dasc"
            break;
        }
        if (ins->info->opcode == MVM_OP_mod_i) {
            // result of modula is stored in rdx
            //| mov WORK[reg_a], rdx;
            dasm_put(Dst, 249, Dt4([reg_a]));
#line 363 "src/jit/emit_x64.dasc"
        } else {
            // all others in rax
            //| mov WORK[reg_a], rax;
            dasm_put(Dst, 254, Dt4([reg_a]));
#line 366 "src/jit/emit_x64.dasc"
        }
        break;
    }
    case MVM_OP_inc_i: {
         MVMint32 reg = ins->operands[0].reg.orig;
         //| inc qword WORK[reg];
         dasm_put(Dst, 259, Dt4([reg]));
#line 372 "src/jit/emit_x64.dasc"
         break;
    }
    case MVM_OP_dec_i: {
        MVMint32 reg = ins->operands[0].reg.orig;
        //| dec qword WORK[reg];
        dasm_put(Dst, 265, Dt4([reg]));
#line 377 "src/jit/emit_x64.dasc"
        break;
    }
    case MVM_OP_add_n:
    case MVM_OP_sub_n:
    case MVM_OP_mul_n:
    case MVM_OP_div_n: {
        MVMint16 reg_a = ins->operands[0].reg.orig;
        MVMint16 reg_b = ins->operands[1].reg.orig;
        MVMint16 reg_c = ins->operands[2].reg.orig;
        /* Copying data to xmm (floating point) registers requires
         * a special move instruction */
        //| movsd xmm0, qword WORK[reg_b];
        dasm_put(Dst, 271, Dt4([reg_b]));
#line 389 "src/jit/emit_x64.dasc"
        switch(ins->info->opcode) {
        case MVM_OP_add_n:
            //| addsd xmm0, qword WORK[reg_c];
            dasm_put(Dst, 278, Dt4([reg_c]));
#line 392 "src/jit/emit_x64.dasc"
            break;
        case MVM_OP_sub_n:
            //| subsd xmm0, qword WORK[reg_c];
            dasm_put(Dst, 285, Dt4([reg_c]));
#line 395 "src/jit/emit_x64.dasc"
            break;
        case MVM_OP_mul_n:
            //| mulsd xmm0, qword WORK[reg_c];
            dasm_put(Dst, 292, Dt4([reg_c]));
#line 398 "src/jit/emit_x64.dasc"
            break;
        case MVM_OP_div_n:
            //| divsd xmm0, qword WORK[reg_c];
            dasm_put(Dst, 299, Dt4([reg_c]));
#line 401 "src/jit/emit_x64.dasc"
            break;
        }
        //| movsd qword WORK[reg_a], xmm0;
        dasm_put(Dst, 306, Dt4([reg_a]));
#line 404 "src/jit/emit_x64.dasc"
        break;
    }
    case MVM_OP_coerce_in: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint16 src = ins->operands[1].reg.orig;
        /* convert simple integer to double precision */
        //| cvtsi2sd xmm0, qword WORK[src];
        //| movsd qword WORK[dst], xmm0;
        dasm_put(Dst, 313, Dt4([src]), Dt4([dst]));
#line 412 "src/jit/emit_x64.dasc"
        break;
    }
    case MVM_OP_coerce_ni: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint16 src = ins->operands[1].reg.orig;
        /* convert double precision to simple intege */
        //| cvttsd2si rax, qword WORK[src];
        //| mov WORK[dst], rax;
        dasm_put(Dst, 327, Dt4([src]), Dt4([dst]));
#line 420 "src/jit/emit_x64.dasc"
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
        //| mov rax, WORK[reg_b];
        dasm_put(Dst, 220, Dt4([reg_b]));
#line 432 "src/jit/emit_x64.dasc"
        /* comparison result in the setting bits in the rflags register */
        //| cmp rax, WORK[reg_c];
        dasm_put(Dst, 339, Dt4([reg_c]));
#line 434 "src/jit/emit_x64.dasc"
        /* copy the right comparison bit to the lower byte of the rax register */
        switch(ins->info->opcode) {
        case MVM_OP_eq_i:
            //| sete al;
            dasm_put(Dst, 344);
#line 438 "src/jit/emit_x64.dasc"
            break;
        case MVM_OP_ne_i:
            //| setne al;
            dasm_put(Dst, 348);
#line 441 "src/jit/emit_x64.dasc"
            break;
        case MVM_OP_lt_i:
            //| setl al;
            dasm_put(Dst, 352);
#line 444 "src/jit/emit_x64.dasc"
            break;
        case MVM_OP_le_i:
            //| setle al;
            dasm_put(Dst, 356);
#line 447 "src/jit/emit_x64.dasc"
            break;
        case MVM_OP_gt_i:
            //| setg al;
            dasm_put(Dst, 360);
#line 450 "src/jit/emit_x64.dasc"
            break;
        case MVM_OP_ge_i:
            //| setge al;
            dasm_put(Dst, 364);
#line 453 "src/jit/emit_x64.dasc"
            break;
        }
        /* zero extend al (lower byte) to rax (whole register) */
        //| movzx rax, al;
        //| mov WORK[reg_a], rax; 
        dasm_put(Dst, 368, Dt4([reg_a]));
#line 458 "src/jit/emit_x64.dasc"
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
            dasm_put(Dst, 377, -args[i].idx);
                    break;
                case 1:
            dasm_put(Dst, 382, -args[i].idx);
                    break;
                case 2:
            dasm_put(Dst, 387, -args[i].idx);
                    break;
                case 3:
            dasm_put(Dst, 392, -args[i].idx);
                    break;
                case 4:
            dasm_put(Dst, 397, -args[i].idx);
                    break;
                case 5:
            dasm_put(Dst, 402, -args[i].idx);
                    break;
                default:
                    MVM_exception_throw_adhoc(tc, "Can't JIT more than %d arguments", i);
            }
#line 478 "src/jit/emit_x64.dasc"
            break;
        case MVM_JIT_ADDR_INTERP:
            switch (args[i].idx) {
            case MVM_JIT_INTERP_TC:
                //| addarg i, TC;
                switch(i) {
                    case 0:
                dasm_put(Dst, 183);
                        break;
                    case 1:
                dasm_put(Dst, 407);
                        break;
                    case 2:
                dasm_put(Dst, 412);
                        break;
                    case 3:
                dasm_put(Dst, 417);
                        break;
                    case 4:
                dasm_put(Dst, 422);
                        break;
                    case 5:
                dasm_put(Dst, 427);
                        break;
                    default:
                        MVM_exception_throw_adhoc(tc, "Can't JIT more than %d arguments", i);
                }
#line 483 "src/jit/emit_x64.dasc"
                 break;
            case MVM_JIT_INTERP_FRAME:
                //| addarg i, TC->cur_frame;
                switch(i) {
                    case 0:
                dasm_put(Dst, 432, Dt1(->cur_frame));
                        break;
                    case 1:
                dasm_put(Dst, 437, Dt1(->cur_frame));
                        break;
                    case 2:
                dasm_put(Dst, 442, Dt1(->cur_frame));
                        break;
                    case 3:
                dasm_put(Dst, 447, Dt1(->cur_frame));
                        break;
                    case 4:
                dasm_put(Dst, 452, Dt1(->cur_frame));
                        break;
                    case 5:
                dasm_put(Dst, 457, Dt1(->cur_frame));
                        break;
                    default:
                        MVM_exception_throw_adhoc(tc, "Can't JIT more than %d arguments", i);
                }
#line 486 "src/jit/emit_x64.dasc"
                break;
            }
            break;
        case MVM_JIT_ADDR_REG:
            //| addarg i, WORK[args[i].idx];
            switch(i) {
                case 0:
            dasm_put(Dst, 462, Dt4([args[i].idx]));
                    break;
                case 1:
            dasm_put(Dst, 467, Dt4([args[i].idx]));
                    break;
                case 2:
            dasm_put(Dst, 472, Dt4([args[i].idx]));
                    break;
                case 3:
            dasm_put(Dst, 477, Dt4([args[i].idx]));
                    break;
                case 4:
            dasm_put(Dst, 482, Dt4([args[i].idx]));
                    break;
                case 5:
            dasm_put(Dst, 487, Dt4([args[i].idx]));
                    break;
                default:
                    MVM_exception_throw_adhoc(tc, "Can't JIT more than %d arguments", i);
            }
#line 491 "src/jit/emit_x64.dasc"
            break;
        case MVM_JIT_ADDR_REG_F:
            //| addarg_f i, WORK[args[i].idx];
            switch(i) {
                case 0:
            dasm_put(Dst, 271, Dt4([args[i].idx]));
                    break;
                case 1:
            dasm_put(Dst, 492, Dt4([args[i].idx]));
                    break;
                case 2:
            dasm_put(Dst, 499, Dt4([args[i].idx]));
                    break;
                case 3:
            dasm_put(Dst, 506, Dt4([args[i].idx]));
                    break;
                case 4:
            dasm_put(Dst, 513, Dt4([args[i].idx]));
                    break;
                case 5:
            dasm_put(Dst, 520, Dt4([args[i].idx]));
                    break;
                case 6:
            dasm_put(Dst, 527, Dt4([args[i].idx]));
                    break;
                case 7:
            dasm_put(Dst, 534, Dt4([args[i].idx]));
                    break;
                default:
                    MVM_exception_throw_adhoc(tc, "Can't JIT more than %d arguments", i);
            }
#line 494 "src/jit/emit_x64.dasc"
            break;
        case MVM_JIT_ADDR_LITERAL:
            //| addarg i, args[i].idx;
            switch(i) {
                case 0:
            dasm_put(Dst, 541, args[i].idx);
                    break;
                case 1:
            dasm_put(Dst, 546, args[i].idx);
                    break;
                case 2:
            dasm_put(Dst, 551, args[i].idx);
                    break;
                case 3:
            dasm_put(Dst, 556, args[i].idx);
                    break;
                case 4:
            dasm_put(Dst, 561, args[i].idx);
                    break;
                case 5:
            dasm_put(Dst, 566, args[i].idx);
                    break;
                default:
                    MVM_exception_throw_adhoc(tc, "Can't JIT more than %d arguments", i);
            }
#line 497 "src/jit/emit_x64.dasc"
            break;
        }
    }
    /* Emit the call. I think we should be able to do something smarter than
     * store the constant into the bytecode, like a data segment. But I'm
     * not sure. */
     //| callp call_spec->func_ptr
     dasm_put(Dst, 571, (unsigned int)((uintptr_t)call_spec->func_ptr), (unsigned int)(((uintptr_t)call_spec->func_ptr)>>32));
#line 504 "src/jit/emit_x64.dasc"
}

void MVM_jit_emit_branch(MVMThreadContext *tc, MVMJitGraph *jg,
                         MVMJitBranch * branch, dasm_State **Dst) {
    MVMSpeshIns *ins = branch->ins;
    MVMint32 name = branch->dest.name;
    if (ins == NULL || ins->info->opcode == MVM_OP_goto) {
        MVM_jit_log(tc, "emit jump to label %d\n", name);
        if (name == MVM_JIT_BRANCH_EXIT) {
            //| jmp ->exit
            dasm_put(Dst, 580);
#line 514 "src/jit/emit_x64.dasc"
        } else {
            //| jmp =>(name)
            dasm_put(Dst, 585, (name));
#line 516 "src/jit/emit_x64.dasc"
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
            dasm_put(Dst, 589, Dt4([reg]), (name));
#line 526 "src/jit/emit_x64.dasc"
            break;
        case MVM_OP_unless_i:
            //| mov rax, WORK[reg];
            //| test rax, rax;
            //| jz =>(name);
            dasm_put(Dst, 600, Dt4([reg]), (name));
#line 531 "src/jit/emit_x64.dasc"
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
    dasm_put(Dst, 611, (label->name));
#line 542 "src/jit/emit_x64.dasc"
}

void MVM_jit_emit_rvh(MVMThreadContext *tc, MVMJitGraph *jg,
                      MVMJitRVH *rvh, dasm_State **Dst) {
    switch(rvh->mode) {
    case MVM_JIT_RV_VAL_TO_REG:
        //| mov WORK[rvh->addr.idx], RV;
        dasm_put(Dst, 254, Dt4([rvh->addr.idx]));
#line 549 "src/jit/emit_x64.dasc"
        break;
    case MVM_JIT_RV_VAL_TO_REG_F:
        //| movsd qword WORK[rvh->addr.idx], RVF;
        dasm_put(Dst, 306, Dt4([rvh->addr.idx]));
#line 552 "src/jit/emit_x64.dasc"
        break;
    case MVM_JIT_RV_REF_TO_REG:
        //| mov TMP1, [RV]; // maybe add an offset?
        //| mov WORK[rvh->addr.idx], TMP1;
        dasm_put(Dst, 613, Dt4([rvh->addr.idx]));
#line 556 "src/jit/emit_x64.dasc"
        break;
    case MVM_JIT_RV_REG_TO_PTR:
        //| mov TMP1, WORK[rvh->addr.idx];
        //| mov [RV], TMP1;
        dasm_put(Dst, 621, Dt4([rvh->addr.idx]));
#line 560 "src/jit/emit_x64.dasc"
        break;
    }
}
