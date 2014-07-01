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
static const unsigned char actions[752] = {
  85,72,137,229,255,65,86,83,65,84,65,85,255,73,137,252,254,73,137,252,245,
  73,139,142,233,72,139,153,233,76,139,161,233,255,248,10,255,65,93,65,92,91,
  65,94,255,72,137,252,236,93,195,255,72,185,237,237,72,137,139,233,255,72,
  199,131,233,237,255,73,139,141,233,72,139,137,233,72,137,139,233,255,73,139,
  142,233,72,139,137,233,72,137,139,233,255,72,139,139,233,72,139,137,233,255,
  73,139,142,233,255,72,139,145,233,255,72,139,146,233,255,72,133,210,15,133,
  244,247,255,82,81,255,76,137,252,247,72,139,116,36,8,72,199,194,237,73,186,
  237,237,65,252,255,210,255,72,131,196,16,255,72,137,194,248,1,255,72,137,
  147,233,255,72,139,137,233,72,139,147,233,72,137,145,233,255,73,139,140,253,
  36,233,72,137,139,233,255,72,139,139,233,72,141,137,233,72,131,185,233,0,
  255,15,132,244,247,72,139,137,233,255,72,131,252,249,0,255,15,133,244,248,
  255,73,139,142,233,72,139,137,233,248,2,255,72,139,139,233,72,139,147,233,
  76,141,129,233,73,131,184,233,0,15,132,244,247,77,139,128,233,248,1,255,102,
  252,247,129,233,236,15,132,244,248,72,131,252,250,0,15,132,244,248,255,102,
  252,247,130,233,236,15,133,244,248,82,65,80,76,137,252,247,255,72,139,179,
  233,73,186,237,237,65,252,255,210,65,88,90,248,2,255,73,137,144,233,255,72,
  139,139,233,72,137,139,233,255,72,139,131,233,255,72,3,131,233,255,72,43,
  131,233,255,72,15,175,131,233,255,72,153,72,252,247,187,233,255,72,137,131,
  233,255,72,252,255,131,233,255,72,252,255,139,233,255,252,242,15,16,131,233,
  255,252,242,15,88,131,233,255,252,242,15,92,131,233,255,252,242,15,89,131,
  233,255,252,242,15,94,131,233,255,252,242,15,17,131,233,255,252,242,72,15,
  42,131,233,252,242,15,17,131,233,255,252,242,72,15,44,131,233,72,137,131,
  233,255,72,59,131,233,255,15,148,208,255,15,149,208,255,15,156,208,255,15,
  158,208,255,15,159,208,255,15,157,208,255,72,15,182,192,72,137,131,233,255,
  72,139,189,233,255,72,139,181,233,255,72,139,149,233,255,72,139,141,233,255,
  76,139,133,233,255,76,139,141,233,255,76,137,252,246,255,76,137,252,242,255,
  76,137,252,241,255,77,137,252,240,255,77,137,252,241,255,73,139,190,233,255,
  73,139,182,233,255,73,139,150,233,255,77,139,134,233,255,77,139,142,233,255,
  76,137,252,239,255,76,137,252,238,255,76,137,252,234,255,76,137,252,233,255,
  77,137,232,255,77,137,252,233,255,72,139,187,233,255,72,139,179,233,255,72,
  139,147,233,255,72,139,139,233,255,76,139,131,233,255,76,139,139,233,255,
  252,242,15,16,139,233,255,252,242,15,16,147,233,255,252,242,15,16,155,233,
  255,252,242,15,16,163,233,255,252,242,15,16,171,233,255,252,242,15,16,179,
  233,255,252,242,15,16,187,233,255,72,199,199,237,255,72,199,198,237,255,72,
  199,194,237,255,72,199,193,237,255,73,199,192,237,255,73,199,193,237,255,
  252,233,244,10,255,252,233,245,255,72,139,131,233,72,133,192,15,133,245,255,
  72,139,131,233,72,133,192,15,132,245,255,249,255,72,139,8,72,137,139,233,
  255,72,139,139,233,72,137,8,255
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

/* type declarations */
//|.type FRAME, MVMFrame
#define Dt1(_V) (int)(ptrdiff_t)&(((MVMFrame *)0)_V)
#line 13 "src/jit/emit_x64.dasc"
//|.type REGISTER, MVMRegister
#define Dt2(_V) (int)(ptrdiff_t)&(((MVMRegister *)0)_V)
#line 14 "src/jit/emit_x64.dasc"
//|.type ARGCTX, MVMArgProcContext
#define Dt3(_V) (int)(ptrdiff_t)&(((MVMArgProcContext *)0)_V)
#line 15 "src/jit/emit_x64.dasc"
//|.type STATICFRAME, MVMStaticFrame
#define Dt4(_V) (int)(ptrdiff_t)&(((MVMStaticFrame *)0)_V)
#line 16 "src/jit/emit_x64.dasc"
//|.type P6OPAQUE, MVMP6opaque
#define Dt5(_V) (int)(ptrdiff_t)&(((MVMP6opaque *)0)_V)
#line 17 "src/jit/emit_x64.dasc"
//|.type P6OBODY, MVMP6opaqueBody
#define Dt6(_V) (int)(ptrdiff_t)&(((MVMP6opaqueBody *)0)_V)
#line 18 "src/jit/emit_x64.dasc"
//|.type MVMINSTANCE, MVMInstance
#define Dt7(_V) (int)(ptrdiff_t)&(((MVMInstance *)0)_V)
#line 19 "src/jit/emit_x64.dasc"
//|.type OBJECT, MVMObject
#define Dt8(_V) (int)(ptrdiff_t)&(((MVMObject *)0)_V)
#line 20 "src/jit/emit_x64.dasc"
//|.type COLLECTABLE, MVMCollectable
#define Dt9(_V) (int)(ptrdiff_t)&(((MVMCollectable *)0)_V)
#line 21 "src/jit/emit_x64.dasc"
//|.type STABLE, MVMSTable
#define DtA(_V) (int)(ptrdiff_t)&(((MVMSTable *)0)_V)
#line 22 "src/jit/emit_x64.dasc"
//|.type STRING, MVMString*
#define DtB(_V) (int)(ptrdiff_t)&(((MVMString* *)0)_V)
#line 23 "src/jit/emit_x64.dasc"
//|.type U16, MVMuint16
#define DtC(_V) (int)(ptrdiff_t)&(((MVMuint16 *)0)_V)
#line 24 "src/jit/emit_x64.dasc"
//|.type U32, MVMuint32
#define DtD(_V) (int)(ptrdiff_t)&(((MVMuint32 *)0)_V)
#line 25 "src/jit/emit_x64.dasc"
//|.type U64, MVMuint64
#define DtE(_V) (int)(ptrdiff_t)&(((MVMuint64 *)0)_V)
#line 26 "src/jit/emit_x64.dasc"



/* Static allocation of relevant types to registers. I pick
 * callee-save registers for efficiency. It is likely we'll be calling
 * quite a C functions, and this saves us the trouble of storing
 * them. Moreover, C compilers preferentially do not use callee-saved
 * registers, and so in most cases, these won't be touched at all. */
//|.type TC, MVMThreadContext, r14
#define DtF(_V) (int)(ptrdiff_t)&(((MVMThreadContext *)0)_V)
#line 35 "src/jit/emit_x64.dasc"
/* Alternative base pointer. I'll be using this often, so picking rbx
 * here rather than the extended registers will lead to smaller
 * bytecode */
//|.type WORK, MVMRegister, rbx
#define Dt10(_V) (int)(ptrdiff_t)&(((MVMRegister *)0)_V)
#line 39 "src/jit/emit_x64.dasc"
//|.type ARGS, MVMRegister, r12
#define Dt11(_V) (int)(ptrdiff_t)&(((MVMRegister *)0)_V)
#line 40 "src/jit/emit_x64.dasc"
//|.type CU, MVMCompUnit, r13
#define Dt12(_V) (int)(ptrdiff_t)&(((MVMCompUnit *)0)_V)
#line 41 "src/jit/emit_x64.dasc"


//|.macro saveregs
//| push TC; push WORK; push ARGS; push CU;
//|.endmacro

//|.macro restoreregs
//| pop CU; pop ARGS; pop WORK; pop TC
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
    //| push rbp;
    //| mov rbp, rsp;
    dasm_put(Dst, 0);
#line 192 "src/jit/emit_x64.dasc"
    /* save callee-save registers */
    //| saveregs;
    dasm_put(Dst, 5);
#line 194 "src/jit/emit_x64.dasc"
    /* setup special frame variables */
    //| mov TC,   ARG1;
    //| mov CU,   ARG2;
    //| mov TMP1, TC->cur_frame;
    //| mov WORK, FRAME:TMP1->work;
    //| mov ARGS, FRAME:TMP1->params.args;
    dasm_put(Dst, 13, DtF(->cur_frame), Dt1(->work), Dt1(->params.args));
#line 200 "src/jit/emit_x64.dasc"
}

/* And a function epilogue is also always the same */
void MVM_jit_emit_epilogue(MVMThreadContext *tc, MVMJitGraph *jg,
                           dasm_State **Dst) {
    //| ->exit:
    dasm_put(Dst, 34);
#line 206 "src/jit/emit_x64.dasc"
    /* restore callee-save registers */
    //| restoreregs;
    dasm_put(Dst, 37);
#line 208 "src/jit/emit_x64.dasc"
    /* Restore stack */
    //| mov rsp, rbp;
    //| pop rbp;
    //| ret;
    dasm_put(Dst, 45);
#line 212 "src/jit/emit_x64.dasc"
}

static MVMuint64 try_emit_gen2_ref(MVMThreadContext *tc, MVMJitGraph *jg,
                                   MVMObject *obj, MVMint16 reg, 
                                   dasm_State **Dst) {
    if (!(obj->header.flags & MVM_CF_SECOND_GEN))
        return 0;
    //| mov64 TMP1, (uintptr_t)obj;
    //| mov WORK[reg], TMP1;
    dasm_put(Dst, 52, (unsigned int)((uintptr_t)obj), (unsigned int)(((uintptr_t)obj)>>32), Dt10([reg]));
#line 221 "src/jit/emit_x64.dasc"
    return 1;
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
        dasm_put(Dst, 61, Dt10([reg]), val);
#line 240 "src/jit/emit_x64.dasc"
        break;
    }
    case MVM_OP_const_i64: {
        MVMint32 reg = ins->operands[0].reg.orig;
        MVMint64 val = ins->operands[1].lit_i64;
        //| mov64 TMP1, val;
        //| mov WORK[reg], TMP1;
        dasm_put(Dst, 52, (unsigned int)(val), (unsigned int)((val)>>32), Dt10([reg]));
#line 247 "src/jit/emit_x64.dasc"
        break;
    }
    case MVM_OP_const_n64: {
        MVM_jit_log(tc, "store const %f\n", ins->operands[1].lit_n64);
        MVMint16 reg = ins->operands[0].reg.orig;
        MVMint64 valbytes = ins->operands[1].lit_i64;
        //| mov64 TMP1, valbytes;
        //| mov WORK[reg], TMP1;
        dasm_put(Dst, 52, (unsigned int)(valbytes), (unsigned int)((valbytes)>>32), Dt10([reg]));
#line 255 "src/jit/emit_x64.dasc"
        break;
    }
    case MVM_OP_const_s: {
         MVMint16 reg = ins->operands[0].reg.orig;
         MVMuint32 idx = ins->operands[1].lit_str_idx;
         MVMStaticFrame *sf = jg->spesh->sf;
         MVMString * s = sf->body.cu->body.strings[idx];
         if (!try_emit_gen2_ref(tc, jg, (MVMObject*)s, reg, Dst)) {
             //| mov TMP1, CU->body.strings; // get strings array
             //| mov TMP1, STRING:TMP1[idx];
             //| mov WORK[reg], TMP1;
             dasm_put(Dst, 67, Dt12(->body.strings), DtB([idx]), Dt10([reg]));
#line 266 "src/jit/emit_x64.dasc"
         }
         break;
    }
    case MVM_OP_null: {
        MVMint16 reg = ins->operands[0].reg.orig;
        //| mov TMP1, TC->instance;
        //| mov TMP1, MVMINSTANCE:TMP1->VMNull;
        //| mov WORK[reg], TMP1;
        dasm_put(Dst, 80, DtF(->instance), Dt7(->VMNull), Dt10([reg]));
#line 274 "src/jit/emit_x64.dasc"
        break;
    }
    case MVM_OP_gethow:
    case MVM_OP_getwhat: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint16 obj = ins->operands[0].reg.orig;
        //| mov TMP1, WORK[obj];
        //| mov TMP1, OBJECT:TMP1->st;
        dasm_put(Dst, 93, Dt10([obj]), Dt8(->st));
#line 282 "src/jit/emit_x64.dasc"
        if (op == MVM_OP_gethow) {
            //| mov TMP1, STABLE:TMP1->HOW;
            dasm_put(Dst, 97, DtA(->HOW));
#line 284 "src/jit/emit_x64.dasc"
        } else {
            //| mov TMP1, STABLE:TMP1->WHAT;
            dasm_put(Dst, 97, DtA(->WHAT));
#line 286 "src/jit/emit_x64.dasc"
        }
        //| mov WORK[dst], TMP1;
        dasm_put(Dst, 56, Dt10([dst]));
#line 288 "src/jit/emit_x64.dasc"
        break;
    }
    case MVM_OP_getlex: {
        MVMint16 *lexical_types;
        MVMStaticFrame * sf = jg->spesh->sf;
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint16 idx = ins->operands[1].lex.idx;
        MVMint16 out = ins->operands[1].lex.outers;
        MVMint16 i;
        //| mov TMP1, TC->cur_frame;
        dasm_put(Dst, 102, DtF(->cur_frame));
#line 298 "src/jit/emit_x64.dasc"
        for (i = 0; i < out; i++) {
            /* I'm going to skip compiling the check whether the outer
             * node really exists, because if the code has run N times
             * correctly, then the outer frame must have existed then,
             * and since this chain is static, it should still exist now.
             * If it doesn't exist, that means we crash. */
            //| mov TMP1, FRAME:TMP1->outer;
            dasm_put(Dst, 97, Dt1(->outer));
#line 305 "src/jit/emit_x64.dasc"
            sf = sf->body.outer;
        }
        /* get array of lexicals */
        //| mov TMP2, FRAME:TMP1->env;
        dasm_put(Dst, 107, Dt1(->env));
#line 309 "src/jit/emit_x64.dasc"
        /* read value */
        //| mov TMP2, REGISTER:TMP2[idx];
        dasm_put(Dst, 112, Dt2([idx]));
#line 311 "src/jit/emit_x64.dasc"
        lexical_types = (jg->spesh->lexical_types ? jg->spesh->lexical_types :
                         sf->body.lexical_types);
        if (lexical_types[idx] == MVM_reg_obj) {
           /* NB: this code path hasn't been checked. */
            /* if it is zero, check if we need to auto-vivify */        
            //| test TMP2, TMP2;
            //| jnz >1; 
            dasm_put(Dst, 117);
#line 318 "src/jit/emit_x64.dasc"
            /* save frame and value */
            //| push TMP2;
            //| push TMP1;
            dasm_put(Dst, 125);
#line 321 "src/jit/emit_x64.dasc"
            /* setup args */
            //| mov ARG1, TC;
            //| mov ARG2, [rsp+8]; // the frame, which i just pushed
            //| mov ARG3, idx;
            //| callp &MVM_frame_vivify_lexical;
            dasm_put(Dst, 128, idx, (unsigned int)((uintptr_t)&MVM_frame_vivify_lexical), (unsigned int)(((uintptr_t)&MVM_frame_vivify_lexical)>>32));
#line 326 "src/jit/emit_x64.dasc"
            /* restore stack pointer */
            //| add rsp, 16;
            dasm_put(Dst, 150);
#line 328 "src/jit/emit_x64.dasc"
            /* use return value for the result */
            //| mov TMP2, RV;
            //|1:
            dasm_put(Dst, 155);
#line 331 "src/jit/emit_x64.dasc"
        } 
        /* store the value */
        //| mov WORK[dst], TMP2;
        dasm_put(Dst, 161, Dt10([dst]));
#line 334 "src/jit/emit_x64.dasc"
        break;
    }
    case MVM_OP_bindlex: {
        MVMint16 idx = ins->operands[0].lex.idx;
        MVMint16 out = ins->operands[0].lex.outers;
        MVMint16 src = ins->operands[1].reg.orig;
        MVMint16 i;
        //| mov TMP1, TC->cur_frame;
        dasm_put(Dst, 102, DtF(->cur_frame));
#line 342 "src/jit/emit_x64.dasc"
        for (i = 0; i < out; i++) {
            //| mov TMP1, FRAME:TMP1->outer;
            dasm_put(Dst, 97, Dt1(->outer));
#line 344 "src/jit/emit_x64.dasc"
        }
        //| mov TMP1, FRAME:TMP1->env;
        //| mov TMP2, WORK[src];
        //| mov REGISTER:TMP1[idx], TMP2;
        dasm_put(Dst, 166, Dt1(->env), Dt10([src]), Dt2([idx]));
#line 348 "src/jit/emit_x64.dasc"
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
        dasm_put(Dst, 179, Dt11([idx]), Dt10([reg]));
#line 358 "src/jit/emit_x64.dasc"
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
        //| lea TMP1, P6OPAQUE:TMP1->body;
        //| cmp qword P6OBODY:TMP1->replaced, 0;
        dasm_put(Dst, 190, Dt10([obj]), Dt5(->body), Dt6(->replaced));
#line 370 "src/jit/emit_x64.dasc"
        /* if not zero then load replacement data pointer */
        //| je >1;
        //| mov TMP1, P6OBODY:TMP1->replaced;
        dasm_put(Dst, 204, Dt6(->replaced));
#line 373 "src/jit/emit_x64.dasc"
        /* otherwise do nothing (i.e. the body is our data pointer) */
        //|1:
        dasm_put(Dst, 158);
#line 375 "src/jit/emit_x64.dasc"
        /* load our value */
        //| mov TMP1, [TMP1 + offset];
        dasm_put(Dst, 97, offset);
#line 377 "src/jit/emit_x64.dasc"
        if (op == MVM_OP_sp_p6oget_o) {
            /* transform null object pointers to VMNull */
            //| cmp TMP1, 0;
            dasm_put(Dst, 213);
#line 380 "src/jit/emit_x64.dasc"
            /* not-null? done */
            //| jne >2;
            dasm_put(Dst, 219);
#line 382 "src/jit/emit_x64.dasc"
            /* store VMNull instead */
            //| mov TMP1, TC->instance;
            //| mov TMP1, MVMINSTANCE:TMP1->VMNull;
            //|2:
            dasm_put(Dst, 224, DtF(->instance), Dt7(->VMNull));
#line 386 "src/jit/emit_x64.dasc"
        }
        //| mov WORK[dst], TMP1;
        dasm_put(Dst, 56, Dt10([dst]));
#line 388 "src/jit/emit_x64.dasc"
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
        dasm_put(Dst, 235, Dt10([obj]), Dt10([val]), Dt5(->body), Dt6(->replaced), Dt6(->replaced));
#line 404 "src/jit/emit_x64.dasc"
        if (op == MVM_OP_sp_p6obind_o) {
            /* this is an assembly-version of MVM_gc_write_barrier
             * TMP1 is root, TMP2 is referenced value. And yes, this
             * should be factored out */
            // is our root second gen?
            //| test word COLLECTABLE:TMP1->flags, MVM_CF_SECOND_GEN; 
            //| jz >2; // if not, skip
            //| cmp TMP2, 0; // is value non-null? (should be)
            //| je >2; // if not, skip
            dasm_put(Dst, 263, Dt9(->flags), MVM_CF_SECOND_GEN);
#line 413 "src/jit/emit_x64.dasc"
            // is the reference second gen?
            //| test word COLLECTABLE:TMP2->flags, MVM_CF_SECOND_GEN; 
            //| jnz >2;  // if so, skip
            //| push TMP2; // store value
            //| push TMP3; // store body pointer
            //| mov ARG1, TC;  // set tc as first argument
            dasm_put(Dst, 283, Dt9(->flags), MVM_CF_SECOND_GEN);
#line 419 "src/jit/emit_x64.dasc"
            // NB, c call arguments arguments clobber our temporary
            // space (depending on ABI), so I reload the work object
            // from register space 
            //| mov ARG2, WORK[obj]; // object as second
            //| callp &MVM_gc_write_barrier_hit; // call our function
            //| pop TMP3; // restore body pointer
            //| pop TMP2; // restore value
            //|2: // done
            dasm_put(Dst, 301, Dt10([obj]), (unsigned int)((uintptr_t)&MVM_gc_write_barrier_hit), (unsigned int)(((uintptr_t)&MVM_gc_write_barrier_hit)>>32));
#line 427 "src/jit/emit_x64.dasc"
        }
        //| mov [TMP3+offset], TMP2; // store value into body
        dasm_put(Dst, 319, offset);
#line 429 "src/jit/emit_x64.dasc"
        break;
    }
    case MVM_OP_getwhere:
    case MVM_OP_set: {
         MVMint32 reg1 = ins->operands[0].reg.orig;
         MVMint32 reg2 = ins->operands[1].reg.orig;
         //| mov TMP1, WORK[reg2];
         //| mov WORK[reg1], TMP1;
         dasm_put(Dst, 324, Dt10([reg2]), Dt10([reg1]));
#line 437 "src/jit/emit_x64.dasc"
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
        dasm_put(Dst, 333, Dt10([reg_b]));
#line 448 "src/jit/emit_x64.dasc"
        switch(ins->info->opcode) {
        case MVM_OP_add_i:
            //| add rax, WORK[reg_c];
            dasm_put(Dst, 338, Dt10([reg_c]));
#line 451 "src/jit/emit_x64.dasc"
            break;
        case MVM_OP_sub_i:
            //| sub rax, WORK[reg_c];
            dasm_put(Dst, 343, Dt10([reg_c]));
#line 454 "src/jit/emit_x64.dasc"
            break;
        case MVM_OP_mul_i:
            //| imul rax, WORK[reg_c];
            dasm_put(Dst, 348, Dt10([reg_c]));
#line 457 "src/jit/emit_x64.dasc"
            break;
        case MVM_OP_div_i:
        case MVM_OP_mod_i:
            // Convert Quadword to Octoword, i.e. use rax:rdx as one
            // single 16 byte register
            //| cqo; 
            //| idiv qword WORK[reg_c];
            dasm_put(Dst, 354, Dt10([reg_c]));
#line 464 "src/jit/emit_x64.dasc"
            break;
        }
        if (ins->info->opcode == MVM_OP_mod_i) {
            // result of modula is stored in rdx
            //| mov WORK[reg_a], rdx;
            dasm_put(Dst, 161, Dt10([reg_a]));
#line 469 "src/jit/emit_x64.dasc"
        } else {
            // all others in rax
            //| mov WORK[reg_a], rax;
            dasm_put(Dst, 362, Dt10([reg_a]));
#line 472 "src/jit/emit_x64.dasc"
        }
        break;
    }
    case MVM_OP_inc_i: {
         MVMint32 reg = ins->operands[0].reg.orig;
         //| inc qword WORK[reg];
         dasm_put(Dst, 367, Dt10([reg]));
#line 478 "src/jit/emit_x64.dasc"
         break;
    }
    case MVM_OP_dec_i: {
        MVMint32 reg = ins->operands[0].reg.orig;
        //| dec qword WORK[reg];
        dasm_put(Dst, 373, Dt10([reg]));
#line 483 "src/jit/emit_x64.dasc"
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
        dasm_put(Dst, 379, Dt10([reg_b]));
#line 495 "src/jit/emit_x64.dasc"
        switch(ins->info->opcode) {
        case MVM_OP_add_n:
            //| addsd xmm0, qword WORK[reg_c];
            dasm_put(Dst, 386, Dt10([reg_c]));
#line 498 "src/jit/emit_x64.dasc"
            break;
        case MVM_OP_sub_n:
            //| subsd xmm0, qword WORK[reg_c];
            dasm_put(Dst, 393, Dt10([reg_c]));
#line 501 "src/jit/emit_x64.dasc"
            break;
        case MVM_OP_mul_n:
            //| mulsd xmm0, qword WORK[reg_c];
            dasm_put(Dst, 400, Dt10([reg_c]));
#line 504 "src/jit/emit_x64.dasc"
            break;
        case MVM_OP_div_n:
            //| divsd xmm0, qword WORK[reg_c];
            dasm_put(Dst, 407, Dt10([reg_c]));
#line 507 "src/jit/emit_x64.dasc"
            break;
        }
        //| movsd qword WORK[reg_a], xmm0;
        dasm_put(Dst, 414, Dt10([reg_a]));
#line 510 "src/jit/emit_x64.dasc"
        break;
    }
    case MVM_OP_coerce_in: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint16 src = ins->operands[1].reg.orig;
        /* convert simple integer to double precision */
        //| cvtsi2sd xmm0, qword WORK[src];
        //| movsd qword WORK[dst], xmm0;
        dasm_put(Dst, 421, Dt10([src]), Dt10([dst]));
#line 518 "src/jit/emit_x64.dasc"
        break;
    }
    case MVM_OP_coerce_ni: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint16 src = ins->operands[1].reg.orig;
        /* convert double precision to simple intege */
        //| cvttsd2si rax, qword WORK[src];
        //| mov WORK[dst], rax;
        dasm_put(Dst, 435, Dt10([src]), Dt10([dst]));
#line 526 "src/jit/emit_x64.dasc"
        break;
    }
    case MVM_OP_eq_i:
    case MVM_OP_eqaddr:
    case MVM_OP_ne_i:
    case MVM_OP_lt_i:
    case MVM_OP_le_i:
    case MVM_OP_gt_i:
    case MVM_OP_ge_i: {
        MVMint32 reg_a = ins->operands[0].reg.orig;
        MVMint32 reg_b = ins->operands[1].reg.orig;
        MVMint32 reg_c = ins->operands[2].reg.orig;
        //| mov rax, WORK[reg_b];
        dasm_put(Dst, 333, Dt10([reg_b]));
#line 539 "src/jit/emit_x64.dasc"
        /* comparison result in the setting bits in the rflags register */
        //| cmp rax, WORK[reg_c];
        dasm_put(Dst, 447, Dt10([reg_c]));
#line 541 "src/jit/emit_x64.dasc"
        /* copy the right comparison bit to the lower byte of the rax register */
        switch(ins->info->opcode) {
        case MVM_OP_eqaddr:
        case MVM_OP_eq_i:
            //| sete al;
            dasm_put(Dst, 452);
#line 546 "src/jit/emit_x64.dasc"
            break;
        case MVM_OP_ne_i:
            //| setne al;
            dasm_put(Dst, 456);
#line 549 "src/jit/emit_x64.dasc"
            break;
        case MVM_OP_lt_i:
            //| setl al;
            dasm_put(Dst, 460);
#line 552 "src/jit/emit_x64.dasc"
            break;
        case MVM_OP_le_i:
            //| setle al;
            dasm_put(Dst, 464);
#line 555 "src/jit/emit_x64.dasc"
            break;
        case MVM_OP_gt_i:
            //| setg al;
            dasm_put(Dst, 468);
#line 558 "src/jit/emit_x64.dasc"
            break;
        case MVM_OP_ge_i:
            //| setge al;
            dasm_put(Dst, 472);
#line 561 "src/jit/emit_x64.dasc"
            break;
        }
        /* zero extend al (lower byte) to rax (whole register) */
        //| movzx rax, al;
        //| mov WORK[reg_a], rax; 
        dasm_put(Dst, 476, Dt10([reg_a]));
#line 566 "src/jit/emit_x64.dasc"
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
            dasm_put(Dst, 485, -args[i].idx);
                    break;
                case 1:
            dasm_put(Dst, 490, -args[i].idx);
                    break;
                case 2:
            dasm_put(Dst, 495, -args[i].idx);
                    break;
                case 3:
            dasm_put(Dst, 500, -args[i].idx);
                    break;
                case 4:
            dasm_put(Dst, 505, -args[i].idx);
                    break;
                case 5:
            dasm_put(Dst, 510, -args[i].idx);
                    break;
                default:
                    MVM_exception_throw_adhoc(tc, "Can't JIT more than %d arguments", i);
            }
#line 586 "src/jit/emit_x64.dasc"
            break;
        case MVM_JIT_ADDR_INTERP:
            MVM_jit_log(tc, "emit interp call arg %d %d \n", i, args[i].idx);
            switch (args[i].idx) {
            case MVM_JIT_INTERP_TC:
                //| addarg i, TC;
                switch(i) {
                    case 0:
                dasm_put(Dst, 296);
                        break;
                    case 1:
                dasm_put(Dst, 515);
                        break;
                    case 2:
                dasm_put(Dst, 520);
                        break;
                    case 3:
                dasm_put(Dst, 525);
                        break;
                    case 4:
                dasm_put(Dst, 530);
                        break;
                    case 5:
                dasm_put(Dst, 535);
                        break;
                    default:
                        MVM_exception_throw_adhoc(tc, "Can't JIT more than %d arguments", i);
                }
#line 592 "src/jit/emit_x64.dasc"
                 break;
            case MVM_JIT_INTERP_FRAME:
                //| addarg i, TC->cur_frame;
                switch(i) {
                    case 0:
                dasm_put(Dst, 540, DtF(->cur_frame));
                        break;
                    case 1:
                dasm_put(Dst, 545, DtF(->cur_frame));
                        break;
                    case 2:
                dasm_put(Dst, 550, DtF(->cur_frame));
                        break;
                    case 3:
                dasm_put(Dst, 102, DtF(->cur_frame));
                        break;
                    case 4:
                dasm_put(Dst, 555, DtF(->cur_frame));
                        break;
                    case 5:
                dasm_put(Dst, 560, DtF(->cur_frame));
                        break;
                    default:
                        MVM_exception_throw_adhoc(tc, "Can't JIT more than %d arguments", i);
                }
#line 595 "src/jit/emit_x64.dasc"
                break;
            case MVM_JIT_INTERP_CU:
                //| addarg i, CU;
                switch(i) {
                    case 0:
                dasm_put(Dst, 565);
                        break;
                    case 1:
                dasm_put(Dst, 570);
                        break;
                    case 2:
                dasm_put(Dst, 575);
                        break;
                    case 3:
                dasm_put(Dst, 580);
                        break;
                    case 4:
                dasm_put(Dst, 585);
                        break;
                    case 5:
                dasm_put(Dst, 589);
                        break;
                    default:
                        MVM_exception_throw_adhoc(tc, "Can't JIT more than %d arguments", i);
                }
#line 598 "src/jit/emit_x64.dasc"
                break;
            }
            break;
        case MVM_JIT_ADDR_REG:
            //| addarg i, WORK[args[i].idx];
            switch(i) {
                case 0:
            dasm_put(Dst, 594, Dt10([args[i].idx]));
                    break;
                case 1:
            dasm_put(Dst, 599, Dt10([args[i].idx]));
                    break;
                case 2:
            dasm_put(Dst, 604, Dt10([args[i].idx]));
                    break;
                case 3:
            dasm_put(Dst, 609, Dt10([args[i].idx]));
                    break;
                case 4:
            dasm_put(Dst, 614, Dt10([args[i].idx]));
                    break;
                case 5:
            dasm_put(Dst, 619, Dt10([args[i].idx]));
                    break;
                default:
                    MVM_exception_throw_adhoc(tc, "Can't JIT more than %d arguments", i);
            }
#line 603 "src/jit/emit_x64.dasc"
            break;
        case MVM_JIT_ADDR_REG_F:
            //| addarg_f i, WORK[args[i].idx];
            switch(i) {
                case 0:
            dasm_put(Dst, 379, Dt10([args[i].idx]));
                    break;
                case 1:
            dasm_put(Dst, 624, Dt10([args[i].idx]));
                    break;
                case 2:
            dasm_put(Dst, 631, Dt10([args[i].idx]));
                    break;
                case 3:
            dasm_put(Dst, 638, Dt10([args[i].idx]));
                    break;
                case 4:
            dasm_put(Dst, 645, Dt10([args[i].idx]));
                    break;
                case 5:
            dasm_put(Dst, 652, Dt10([args[i].idx]));
                    break;
                case 6:
            dasm_put(Dst, 659, Dt10([args[i].idx]));
                    break;
                case 7:
            dasm_put(Dst, 666, Dt10([args[i].idx]));
                    break;
                default:
                    MVM_exception_throw_adhoc(tc, "Can't JIT more than %d arguments", i);
            }
#line 606 "src/jit/emit_x64.dasc"
            break;
        case MVM_JIT_ADDR_LITERAL:
            //| addarg i, args[i].idx;
            switch(i) {
                case 0:
            dasm_put(Dst, 673, args[i].idx);
                    break;
                case 1:
            dasm_put(Dst, 678, args[i].idx);
                    break;
                case 2:
            dasm_put(Dst, 683, args[i].idx);
                    break;
                case 3:
            dasm_put(Dst, 688, args[i].idx);
                    break;
                case 4:
            dasm_put(Dst, 693, args[i].idx);
                    break;
                case 5:
            dasm_put(Dst, 698, args[i].idx);
                    break;
                default:
                    MVM_exception_throw_adhoc(tc, "Can't JIT more than %d arguments", i);
            }
#line 609 "src/jit/emit_x64.dasc"
            break;
        }
    }
    /* Emit the call. I think we should be able to do something smarter than
     * store the constant into the bytecode, like a data segment. But I'm
     * not sure. */
     //| callp call_spec->func_ptr
     dasm_put(Dst, 141, (unsigned int)((uintptr_t)call_spec->func_ptr), (unsigned int)(((uintptr_t)call_spec->func_ptr)>>32));
#line 616 "src/jit/emit_x64.dasc"
}

void MVM_jit_emit_branch(MVMThreadContext *tc, MVMJitGraph *jg,
                         MVMJitBranch * branch, dasm_State **Dst) {
    MVMSpeshIns *ins = branch->ins;
    MVMint32 name = branch->dest.name;
    if (ins == NULL || ins->info->opcode == MVM_OP_goto) {
        MVM_jit_log(tc, "emit jump to label %d\n", name);
        if (name == MVM_JIT_BRANCH_EXIT) {
            //| jmp ->exit
            dasm_put(Dst, 703);
#line 626 "src/jit/emit_x64.dasc"
        } else {
            //| jmp =>(name)
            dasm_put(Dst, 708, (name));
#line 628 "src/jit/emit_x64.dasc"
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
            dasm_put(Dst, 712, Dt10([reg]), (name));
#line 638 "src/jit/emit_x64.dasc"
            break;
        case MVM_OP_unless_i:
            //| mov rax, WORK[reg];
            //| test rax, rax;
            //| jz =>(name);
            dasm_put(Dst, 723, Dt10([reg]), (name));
#line 643 "src/jit/emit_x64.dasc"
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
    dasm_put(Dst, 734, (label->name));
#line 654 "src/jit/emit_x64.dasc"
}

void MVM_jit_emit_rvh(MVMThreadContext *tc, MVMJitGraph *jg,
                      MVMJitRVH *rvh, dasm_State **Dst) {
    switch(rvh->mode) {
    case MVM_JIT_RV_VAL_TO_REG:
        //| mov WORK[rvh->addr.idx], RV;
        dasm_put(Dst, 362, Dt10([rvh->addr.idx]));
#line 661 "src/jit/emit_x64.dasc"
        break;
    case MVM_JIT_RV_VAL_TO_REG_F:
        //| movsd qword WORK[rvh->addr.idx], RVF;
        dasm_put(Dst, 414, Dt10([rvh->addr.idx]));
#line 664 "src/jit/emit_x64.dasc"
        break;
    case MVM_JIT_RV_REF_TO_REG:
        //| mov TMP1, [RV]; // maybe add an offset?
        //| mov WORK[rvh->addr.idx], TMP1;
        dasm_put(Dst, 736, Dt10([rvh->addr.idx]));
#line 668 "src/jit/emit_x64.dasc"
        break;
    case MVM_JIT_RV_REG_TO_PTR:
        //| mov TMP1, WORK[rvh->addr.idx];
        //| mov [RV], TMP1;
        dasm_put(Dst, 744, Dt10([rvh->addr.idx]));
#line 672 "src/jit/emit_x64.dasc"
        break;
    }
}
