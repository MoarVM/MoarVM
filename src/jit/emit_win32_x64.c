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
static const unsigned char actions[1055] = {
  85,72,137,229,255,65,86,83,65,84,65,85,255,73,137,206,73,137,213,73,139,142,
  233,72,139,153,233,76,139,161,233,255,248,10,72,199,192,0,0,0,0,248,11,255,
  65,93,65,92,91,65,94,255,72,137,252,236,93,195,255,72,185,237,237,72,137,
  139,233,255,72,199,131,233,237,255,73,139,141,233,72,139,137,233,72,137,139,
  233,255,73,139,142,233,72,139,137,233,72,137,139,233,255,72,139,139,233,72,
  139,137,233,255,73,139,142,233,255,72,139,145,233,255,72,139,146,233,255,
  72,131,252,250,0,15,133,244,247,255,205,3,82,81,255,76,137,252,241,72,139,
  84,36,8,73,199,192,237,73,186,237,237,72,131,252,236,32,65,252,255,210,72,
  131,196,32,255,72,131,196,16,255,72,137,194,248,1,255,72,137,147,233,255,
  72,139,137,233,72,139,147,233,72,137,145,233,255,73,139,140,253,36,233,72,
  137,139,233,255,72,139,139,233,72,131,185,233,0,15,133,244,247,72,141,145,
  233,252,233,244,248,248,1,72,139,145,233,72,141,146,233,248,2,255,76,139,
  2,73,131,252,248,0,255,15,133,244,249,255,77,139,134,233,77,139,128,233,248,
  3,255,76,139,2,255,73,131,252,248,0,15,133,244,250,77,139,134,233,77,139,
  128,233,77,139,128,233,102,252,247,129,233,236,15,149,208,73,131,252,248,
  0,15,149,212,132,196,15,149,208,102,65,252,247,128,233,236,15,148,212,132,
  196,15,132,244,249,82,65,80,76,137,252,241,72,139,147,233,73,186,237,237,
  72,131,252,236,32,65,252,255,210,72,131,196,32,65,88,90,248,3,255,76,137,
  2,248,4,255,76,139,2,73,131,252,248,0,15,133,244,250,255,81,82,76,137,252,
  241,73,139,150,233,72,139,146,233,72,139,146,233,73,186,237,237,72,131,252,
  236,32,65,252,255,210,72,131,196,32,90,89,255,73,137,192,102,252,247,129,
  233,236,15,149,208,73,131,252,248,0,15,149,212,132,196,15,149,208,102,65,
  252,247,128,233,236,15,148,212,132,196,15,132,244,249,82,65,80,76,137,252,
  241,72,139,147,233,73,186,237,237,72,131,252,236,32,65,252,255,210,72,131,
  196,32,65,88,90,248,3,76,137,2,255,76,137,131,233,255,72,139,139,233,72,139,
  147,233,76,141,129,233,73,131,184,233,0,15,132,244,247,77,139,128,233,248,
  1,255,102,252,247,129,233,236,15,149,208,72,131,252,250,0,15,149,212,132,
  196,15,149,208,102,252,247,130,233,236,15,148,212,132,196,15,132,244,248,
  82,65,80,76,137,252,241,72,139,147,233,73,186,237,237,72,131,252,236,32,65,
  252,255,210,72,131,196,32,65,88,90,248,2,255,73,137,144,233,255,72,139,139,
  233,72,137,139,233,255,72,139,131,233,255,72,3,131,233,255,72,43,131,233,
  255,72,15,175,131,233,255,72,153,72,252,247,187,233,255,72,137,131,233,255,
  72,252,255,131,233,255,72,252,255,139,233,255,252,242,15,16,131,233,255,252,
  242,15,88,131,233,255,252,242,15,92,131,233,255,252,242,15,89,131,233,255,
  252,242,15,94,131,233,255,252,242,15,17,131,233,255,252,242,72,15,42,131,
  233,252,242,15,17,131,233,255,252,242,72,15,44,131,233,72,137,131,233,255,
  72,59,131,233,255,15,148,208,255,15,149,208,255,15,156,208,255,15,158,208,
  255,15,159,208,255,15,157,208,255,72,15,182,192,72,137,131,233,255,72,139,
  141,233,255,72,139,149,233,255,76,139,133,233,255,76,139,141,233,255,76,137,
  252,241,255,76,137,252,242,255,77,137,252,240,255,77,137,252,241,255,73,139,
  150,233,255,77,139,134,233,255,77,139,142,233,255,76,137,252,233,255,76,137,
  252,234,255,77,137,232,255,77,137,252,233,255,72,139,139,233,255,72,139,147,
  233,255,76,139,131,233,255,76,139,139,233,255,252,242,15,16,139,233,255,252,
  242,15,16,147,233,255,252,242,15,16,155,233,255,72,199,193,237,255,72,199,
  194,237,255,73,199,192,237,255,73,199,193,237,255,72,139,8,72,137,139,233,
  255,72,139,139,233,72,137,8,255,252,233,244,10,255,252,233,245,255,72,139,
  131,233,72,133,192,15,133,245,255,72,139,131,233,72,133,192,15,132,245,255,
  72,139,139,233,73,139,150,233,72,139,146,233,72,139,146,233,255,72,131,252,
  249,0,15,132,244,247,255,102,252,247,129,233,236,255,72,59,145,233,15,133,
  244,247,255,102,252,247,129,233,236,15,133,244,247,255,252,233,244,248,248,
  1,255,76,137,252,241,72,199,194,237,73,199,192,237,73,186,237,237,72,131,
  252,236,32,65,252,255,210,72,131,196,32,255,72,199,192,237,252,233,244,11,
  248,2,255
};

#line 8 "src/jit/emit_x64.dasc"
//|.section code
#define DASM_SECTION_CODE	0
#define DASM_MAXSECTION		1
#line 9 "src/jit/emit_x64.dasc"
//|.globals JIT_LABEL_
enum {
  JIT_LABEL_exit,
  JIT_LABEL_out,
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
//|.type OBJECTPTR, MVMObject*
#define DtC(_V) (int)(ptrdiff_t)&(((MVMObject* *)0)_V)
#line 24 "src/jit/emit_x64.dasc"
//|.type U16, MVMuint16
#define DtD(_V) (int)(ptrdiff_t)&(((MVMuint16 *)0)_V)
#line 25 "src/jit/emit_x64.dasc"
//|.type U32, MVMuint32
#define DtE(_V) (int)(ptrdiff_t)&(((MVMuint32 *)0)_V)
#line 26 "src/jit/emit_x64.dasc"
//|.type U64, MVMuint64
#define DtF(_V) (int)(ptrdiff_t)&(((MVMuint64 *)0)_V)
#line 27 "src/jit/emit_x64.dasc"



/* Static allocation of relevant types to registers. I pick
 * callee-save registers for efficiency. It is likely we'll be calling
 * quite a C functions, and this saves us the trouble of storing
 * them. Moreover, C compilers preferentially do not use callee-saved
 * registers, and so in most cases, these won't be touched at all. */
//|.type TC, MVMThreadContext, r14
#define Dt10(_V) (int)(ptrdiff_t)&(((MVMThreadContext *)0)_V)
#line 36 "src/jit/emit_x64.dasc"
/* Alternative base pointer. I'll be using this often, so picking rbx
 * here rather than the extended registers will lead to smaller
 * bytecode */
//|.type WORK, MVMRegister, rbx
#define Dt11(_V) (int)(ptrdiff_t)&(((MVMRegister *)0)_V)
#line 40 "src/jit/emit_x64.dasc"
//|.type ARGS, MVMRegister, r12
#define Dt12(_V) (int)(ptrdiff_t)&(((MVMRegister *)0)_V)
#line 41 "src/jit/emit_x64.dasc"
//|.type CU, MVMCompUnit, r13
#define Dt13(_V) (int)(ptrdiff_t)&(((MVMCompUnit *)0)_V)
#line 42 "src/jit/emit_x64.dasc"


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

//|.macro check_wb, root, ref;
//| test word COLLECTABLE:root->flags, MVM_CF_SECOND_GEN;
//| setnz al;
//| cmp ref, 0x0;
//| setne ah;
//| test ah, al;
//| setnz al;
//| test word COLLECTABLE:ref->flags, MVM_CF_SECOND_GEN;
//| setz ah;
//| test ah, al;
//|.endmacro;

//|.macro hit_wb, obj
//| mov ARG1, TC;
//| mov ARG2, obj;
//| callp &MVM_gc_write_barrier_hit;
//|.endmacro

//|.macro get_spesh_slot, reg, idx;
//| mov reg, TC->cur_frame;
//| mov reg, FRAME:reg->effective_spesh_slots;
//| mov reg, OBJECTPTR:reg[idx];
//|.endmacro

//|.macro get_vmnull, reg
//| mov reg, TC->instance;
//| mov reg, MVMINSTANCE:reg->VMNull;
//|.endmacro

//|.macro is_type_object, reg
//| test word OBJECT:reg->header.flags, MVM_CF_TYPE_OBJECT
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
#line 226 "src/jit/emit_x64.dasc"
    /* save callee-save registers */
    //| saveregs;
    dasm_put(Dst, 5);
#line 228 "src/jit/emit_x64.dasc"
    /* setup special frame variables */
    //| mov TC,   ARG1;
    //| mov CU,   ARG2;
    //| mov TMP1, TC->cur_frame;
    //| mov WORK, FRAME:TMP1->work;
    //| mov ARGS, FRAME:TMP1->params.args;
    dasm_put(Dst, 13, Dt10(->cur_frame), Dt1(->work), Dt1(->params.args));
#line 234 "src/jit/emit_x64.dasc"
}

/* And a function epilogue is also always the same */
void MVM_jit_emit_epilogue(MVMThreadContext *tc, MVMJitGraph *jg,
                           dasm_State **Dst) {
    //| ->exit:
    //| mov RV, 0;
    //| ->out:
    dasm_put(Dst, 32);
#line 242 "src/jit/emit_x64.dasc"
    /* restore callee-save registers */
    //| restoreregs;
    dasm_put(Dst, 44);
#line 244 "src/jit/emit_x64.dasc"
    /* Restore stack */
    //| mov rsp, rbp;
    //| pop rbp;
    //| ret;
    dasm_put(Dst, 52);
#line 248 "src/jit/emit_x64.dasc"
}

static MVMuint64 try_emit_gen2_ref(MVMThreadContext *tc, MVMJitGraph *jg,
                                   MVMObject *obj, MVMint16 reg, 
                                   dasm_State **Dst) {
    if (!(obj->header.flags & MVM_CF_SECOND_GEN))
        return 0;
    //| mov64 TMP1, (uintptr_t)obj;
    //| mov WORK[reg], TMP1;
    dasm_put(Dst, 59, (unsigned int)((uintptr_t)obj), (unsigned int)(((uintptr_t)obj)>>32), Dt11([reg]));
#line 257 "src/jit/emit_x64.dasc"
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
        dasm_put(Dst, 68, Dt11([reg]), val);
#line 276 "src/jit/emit_x64.dasc"
        break;
    }
    case MVM_OP_const_i64: {
        MVMint32 reg = ins->operands[0].reg.orig;
        MVMint64 val = ins->operands[1].lit_i64;
        //| mov64 TMP1, val;
        //| mov WORK[reg], TMP1;
        dasm_put(Dst, 59, (unsigned int)(val), (unsigned int)((val)>>32), Dt11([reg]));
#line 283 "src/jit/emit_x64.dasc"
        break;
    }
    case MVM_OP_const_n64: {
        MVM_jit_log(tc, "store const %f\n", ins->operands[1].lit_n64);
        MVMint16 reg = ins->operands[0].reg.orig;
        MVMint64 valbytes = ins->operands[1].lit_i64;
        //| mov64 TMP1, valbytes;
        //| mov WORK[reg], TMP1;
        dasm_put(Dst, 59, (unsigned int)(valbytes), (unsigned int)((valbytes)>>32), Dt11([reg]));
#line 291 "src/jit/emit_x64.dasc"
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
             dasm_put(Dst, 74, Dt13(->body.strings), DtB([idx]), Dt11([reg]));
#line 302 "src/jit/emit_x64.dasc"
         }
         break;
    }
    case MVM_OP_null: {
        MVMint16 reg = ins->operands[0].reg.orig;
        //| mov TMP1, TC->instance;
        //| mov TMP1, MVMINSTANCE:TMP1->VMNull;
        //| mov WORK[reg], TMP1;
        dasm_put(Dst, 87, Dt10(->instance), Dt7(->VMNull), Dt11([reg]));
#line 310 "src/jit/emit_x64.dasc"
        break;
    }
    case MVM_OP_gethow:
    case MVM_OP_getwhat: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint16 obj = ins->operands[0].reg.orig;
        //| mov TMP1, WORK[obj];
        //| mov TMP1, OBJECT:TMP1->st;
        dasm_put(Dst, 100, Dt11([obj]), Dt8(->st));
#line 318 "src/jit/emit_x64.dasc"
        if (op == MVM_OP_gethow) {
            //| mov TMP1, STABLE:TMP1->HOW;
            dasm_put(Dst, 104, DtA(->HOW));
#line 320 "src/jit/emit_x64.dasc"
        } else {
            //| mov TMP1, STABLE:TMP1->WHAT;
            dasm_put(Dst, 104, DtA(->WHAT));
#line 322 "src/jit/emit_x64.dasc"
        }
        //| mov WORK[dst], TMP1;
        dasm_put(Dst, 63, Dt11([dst]));
#line 324 "src/jit/emit_x64.dasc"
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
        dasm_put(Dst, 109, Dt10(->cur_frame));
#line 334 "src/jit/emit_x64.dasc"
        for (i = 0; i < out; i++) {
            /* I'm going to skip compiling the check whether the outer
             * node really exists, because if the code has run N times
             * correctly, then the outer frame must have existed then,
             * and since this chain is static, it should still exist now.
             * If it doesn't exist, that means we crash. */
            //| mov TMP1, FRAME:TMP1->outer;
            dasm_put(Dst, 104, Dt1(->outer));
#line 341 "src/jit/emit_x64.dasc"
            sf = sf->body.outer;
        }
        /* get array of lexicals */
        //| mov TMP2, FRAME:TMP1->env;
        dasm_put(Dst, 114, Dt1(->env));
#line 345 "src/jit/emit_x64.dasc"
        /* read value */
        //| mov TMP2, REGISTER:TMP2[idx];
        dasm_put(Dst, 119, Dt2([idx]));
#line 347 "src/jit/emit_x64.dasc"
        lexical_types = (jg->spesh->lexical_types ? jg->spesh->lexical_types :
                         sf->body.lexical_types);
        MVM_jit_log(tc, "Lexical type of register: %d\n", lexical_types[idx]);
        if (lexical_types[idx] == MVM_reg_obj) {
            MVM_jit_log(tc, "Emit lex vifivy check\n");
            /* NB: this code path hasn't been checked. */
            /* if it is zero, check if we need to auto-vivify */        
            //| cmp TMP2, 0;
            //| jne >1; 
            dasm_put(Dst, 124);
#line 356 "src/jit/emit_x64.dasc"
            /* save frame and value */
            //| int 3; // debug instruction - i haven't tested this path, and it seems hard to trigger
            //| push TMP2;
            //| push TMP1;
            dasm_put(Dst, 134);
#line 360 "src/jit/emit_x64.dasc"
            /* setup args */
            //| mov ARG1, TC;
            //| mov ARG2, [rsp+8]; // the frame, which i just pushed
            //| mov ARG3, idx;
            //| callp &MVM_frame_vivify_lexical;
            dasm_put(Dst, 139, idx, (unsigned int)((uintptr_t)&MVM_frame_vivify_lexical), (unsigned int)(((uintptr_t)&MVM_frame_vivify_lexical)>>32));
#line 365 "src/jit/emit_x64.dasc"
            /* restore stack pointer */
            //| add rsp, 16;
            dasm_put(Dst, 170);
#line 367 "src/jit/emit_x64.dasc"
            /* use return value for the result */
            //| mov TMP2, RV;
            //|1:
            dasm_put(Dst, 175);
#line 370 "src/jit/emit_x64.dasc"
        } 
        /* store the value */
        //| mov WORK[dst], TMP2;
        dasm_put(Dst, 181, Dt11([dst]));
#line 373 "src/jit/emit_x64.dasc"
        break;
    }
    case MVM_OP_bindlex: {
        MVMint16 idx = ins->operands[0].lex.idx;
        MVMint16 out = ins->operands[0].lex.outers;
        MVMint16 src = ins->operands[1].reg.orig;
        MVMint16 i;
        //| mov TMP1, TC->cur_frame;
        dasm_put(Dst, 109, Dt10(->cur_frame));
#line 381 "src/jit/emit_x64.dasc"
        for (i = 0; i < out; i++) {
            //| mov TMP1, FRAME:TMP1->outer;
            dasm_put(Dst, 104, Dt1(->outer));
#line 383 "src/jit/emit_x64.dasc"
        }
        //| mov TMP1, FRAME:TMP1->env;
        //| mov TMP2, WORK[src];
        //| mov REGISTER:TMP1[idx], TMP2;
        dasm_put(Dst, 186, Dt1(->env), Dt11([src]), Dt2([idx]));
#line 387 "src/jit/emit_x64.dasc"
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
        dasm_put(Dst, 199, Dt12([idx]), Dt11([reg]));
#line 397 "src/jit/emit_x64.dasc"
        break;
    }
    case MVM_OP_sp_p6oget_i:
    case MVM_OP_sp_p6oget_n:
    case MVM_OP_sp_p6oget_s:
    case MVM_OP_sp_p6oget_o:
    case MVM_OP_sp_p6ogetvc_o:
    case MVM_OP_sp_p6ogetvt_o: {
        MVMint16 dst    = ins->operands[0].reg.orig;
        MVMint16 obj    = ins->operands[1].reg.orig;
        MVMint16 offset = ins->operands[2].lit_i16;
        MVMint16 body   = offsetof(MVMP6opaque, body);
        //| mov TMP1, WORK[obj];
        //| cmp aword P6OPAQUE:TMP1->body.replaced, 0;
        //| jne >1;
        //| lea TMP2, [TMP1 + (offset + body)];
        //| jmp >2;
        //|1:
        //| mov TMP2, P6OPAQUE:TMP1->body.replaced;
        //| lea TMP2, [TMP2 + offset];
        //|2:
        dasm_put(Dst, 210, Dt11([obj]), Dt5(->body.replaced), (offset + body), Dt5(->body.replaced), offset);
#line 418 "src/jit/emit_x64.dasc"
        /* TMP1 now contains address of item */
        if (op == MVM_OP_sp_p6oget_o) {
            //| mov TMP3, [TMP2];
            //| cmp TMP3, 0;
            dasm_put(Dst, 244);
#line 422 "src/jit/emit_x64.dasc"
            /* Check if object doesn't point to NULL */
            //| jne >3;
            dasm_put(Dst, 253);
#line 424 "src/jit/emit_x64.dasc"
            /* Otherwise load VMNull */ 
            //| get_vmnull TMP3;
            //|3:
            dasm_put(Dst, 258, Dt10(->instance), Dt7(->VMNull));
#line 427 "src/jit/emit_x64.dasc"
        } else if (op == MVM_OP_sp_p6ogetvt_o) {
            /* vivify as type object */ 
            MVMint16 spesh_idx = ins->operands[3].lit_i16;
            //| mov TMP3, [TMP2];
            dasm_put(Dst, 269);
#line 431 "src/jit/emit_x64.dasc"
            /*
            //| cmp TMP3, 0;
            //| jne >4;
            //| get_spesh_slot TMP3, spesh_idx;
            //| check_wb TMP1, TMP3;
            //| jz >3;
            //| push TMP2; // address
            //| push TMP3; // value
            //| hit_wb WORK[obj]; // write barrier for header 
            //| pop TMP3;
            //| pop TMP2;
            //|3:
            dasm_put(Dst, 273, Dt10(->cur_frame), Dt1(->effective_spesh_slots), DtC([spesh_idx]), Dt9(->flags), MVM_CF_SECOND_GEN, Dt9(->flags), MVM_CF_SECOND_GEN, Dt11([obj]), (unsigned int)((uintptr_t)&MVM_gc_write_barrier_hit), (unsigned int)(((uintptr_t)&MVM_gc_write_barrier_hit)>>32));
#line 443 "src/jit/emit_x64.dasc"

            //| mov [TMP2], TMP3;
            //|4:
            dasm_put(Dst, 366);
#line 446 "src/jit/emit_x64.dasc"
            */
        } else if (op == MVM_OP_sp_p6ogetvc_o) {
            MVMint16 spesh_idx = ins->operands[3].lit_i16;
            //| mov TMP3, [TMP2];
            //| cmp TMP3, 0;
            //| jne >4;
            dasm_put(Dst, 372);
#line 452 "src/jit/emit_x64.dasc"
            /* vivify as clone */
            //| push TMP1;
            //| push TMP2;
            //| mov ARG1, TC; 
            //| get_spesh_slot ARG2, spesh_idx;
            //| callp &MVM_repr_clone;
            //| pop TMP2;
            //| pop TMP1;
            dasm_put(Dst, 385, Dt10(->cur_frame), Dt1(->effective_spesh_slots), DtC([spesh_idx]), (unsigned int)((uintptr_t)&MVM_repr_clone), (unsigned int)(((uintptr_t)&MVM_repr_clone)>>32));
#line 460 "src/jit/emit_x64.dasc"
            /* assign with write barrier */
            //| mov TMP3, RV;
            //| check_wb TMP1, TMP3;
            //| jz >3;
            //| push TMP2;
            //| push TMP3;
            //| hit_wb WORK[obj];
            //| pop TMP3;
            //| pop TMP2;
            //|3: 
            //| mov [TMP2], TMP3;
            dasm_put(Dst, 423, Dt9(->flags), MVM_CF_SECOND_GEN, Dt9(->flags), MVM_CF_SECOND_GEN, Dt11([obj]), (unsigned int)((uintptr_t)&MVM_gc_write_barrier_hit), (unsigned int)(((uintptr_t)&MVM_gc_write_barrier_hit)>>32));
#line 471 "src/jit/emit_x64.dasc"
            /* done */
            //|4:
            dasm_put(Dst, 369);
#line 473 "src/jit/emit_x64.dasc"
        } else {
            /* the regular case */
            //| mov TMP3, [TMP2];
            dasm_put(Dst, 269);
#line 476 "src/jit/emit_x64.dasc"
        }
        /* store in local register */
        //| mov WORK[dst], TMP3;
        dasm_put(Dst, 501, Dt11([dst]));
#line 479 "src/jit/emit_x64.dasc"
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
        dasm_put(Dst, 506, Dt11([obj]), Dt11([val]), Dt5(->body), Dt6(->replaced), Dt6(->replaced));
#line 495 "src/jit/emit_x64.dasc"
        if (op == MVM_OP_sp_p6obind_o) {
            /* check if we should hit write barrier */
            //| check_wb TMP1, TMP2;
            //| jz >2;
            //| push TMP2; // store value
            //| push TMP3; // store body pointer
            //| hit_wb WORK[obj]
            //| pop TMP3; // restore body pointer
            //| pop TMP2; // restore value
            //|2: // done
            dasm_put(Dst, 534, Dt9(->flags), MVM_CF_SECOND_GEN, Dt9(->flags), MVM_CF_SECOND_GEN, Dt11([obj]), (unsigned int)((uintptr_t)&MVM_gc_write_barrier_hit), (unsigned int)(((uintptr_t)&MVM_gc_write_barrier_hit)>>32));
#line 505 "src/jit/emit_x64.dasc"
        }
        //| mov [TMP3+offset], TMP2; // store value into body
        dasm_put(Dst, 605, offset);
#line 507 "src/jit/emit_x64.dasc"
        break;
    }
    case MVM_OP_getwhere:
    case MVM_OP_set: {
         MVMint32 reg1 = ins->operands[0].reg.orig;
         MVMint32 reg2 = ins->operands[1].reg.orig;
         //| mov TMP1, WORK[reg2];
         //| mov WORK[reg1], TMP1;
         dasm_put(Dst, 610, Dt11([reg2]), Dt11([reg1]));
#line 515 "src/jit/emit_x64.dasc"
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
        dasm_put(Dst, 619, Dt11([reg_b]));
#line 526 "src/jit/emit_x64.dasc"
        switch(ins->info->opcode) {
        case MVM_OP_add_i:
            //| add rax, WORK[reg_c];
            dasm_put(Dst, 624, Dt11([reg_c]));
#line 529 "src/jit/emit_x64.dasc"
            break;
        case MVM_OP_sub_i:
            //| sub rax, WORK[reg_c];
            dasm_put(Dst, 629, Dt11([reg_c]));
#line 532 "src/jit/emit_x64.dasc"
            break;
        case MVM_OP_mul_i:
            //| imul rax, WORK[reg_c];
            dasm_put(Dst, 634, Dt11([reg_c]));
#line 535 "src/jit/emit_x64.dasc"
            break;
        case MVM_OP_div_i:
        case MVM_OP_mod_i:
            // Convert Quadword to Octoword, i.e. use rax:rdx as one
            // single 16 byte register
            //| cqo; 
            //| idiv qword WORK[reg_c];
            dasm_put(Dst, 640, Dt11([reg_c]));
#line 542 "src/jit/emit_x64.dasc"
            break;
        }
        if (ins->info->opcode == MVM_OP_mod_i) {
            // result of modula is stored in rdx
            //| mov WORK[reg_a], rdx;
            dasm_put(Dst, 181, Dt11([reg_a]));
#line 547 "src/jit/emit_x64.dasc"
        } else {
            // all others in rax
            //| mov WORK[reg_a], rax;
            dasm_put(Dst, 648, Dt11([reg_a]));
#line 550 "src/jit/emit_x64.dasc"
        }
        break;
    }
    case MVM_OP_inc_i: {
         MVMint32 reg = ins->operands[0].reg.orig;
         //| inc qword WORK[reg];
         dasm_put(Dst, 653, Dt11([reg]));
#line 556 "src/jit/emit_x64.dasc"
         break;
    }
    case MVM_OP_dec_i: {
        MVMint32 reg = ins->operands[0].reg.orig;
        //| dec qword WORK[reg];
        dasm_put(Dst, 659, Dt11([reg]));
#line 561 "src/jit/emit_x64.dasc"
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
        dasm_put(Dst, 665, Dt11([reg_b]));
#line 573 "src/jit/emit_x64.dasc"
        switch(ins->info->opcode) {
        case MVM_OP_add_n:
            //| addsd xmm0, qword WORK[reg_c];
            dasm_put(Dst, 672, Dt11([reg_c]));
#line 576 "src/jit/emit_x64.dasc"
            break;
        case MVM_OP_sub_n:
            //| subsd xmm0, qword WORK[reg_c];
            dasm_put(Dst, 679, Dt11([reg_c]));
#line 579 "src/jit/emit_x64.dasc"
            break;
        case MVM_OP_mul_n:
            //| mulsd xmm0, qword WORK[reg_c];
            dasm_put(Dst, 686, Dt11([reg_c]));
#line 582 "src/jit/emit_x64.dasc"
            break;
        case MVM_OP_div_n:
            //| divsd xmm0, qword WORK[reg_c];
            dasm_put(Dst, 693, Dt11([reg_c]));
#line 585 "src/jit/emit_x64.dasc"
            break;
        }
        //| movsd qword WORK[reg_a], xmm0;
        dasm_put(Dst, 700, Dt11([reg_a]));
#line 588 "src/jit/emit_x64.dasc"
        break;
    }
    case MVM_OP_coerce_in: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint16 src = ins->operands[1].reg.orig;
        /* convert simple integer to double precision */
        //| cvtsi2sd xmm0, qword WORK[src];
        //| movsd qword WORK[dst], xmm0;
        dasm_put(Dst, 707, Dt11([src]), Dt11([dst]));
#line 596 "src/jit/emit_x64.dasc"
        break;
    }
    case MVM_OP_coerce_ni: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint16 src = ins->operands[1].reg.orig;
        /* convert double precision to simple intege */
        //| cvttsd2si rax, qword WORK[src];
        //| mov WORK[dst], rax;
        dasm_put(Dst, 721, Dt11([src]), Dt11([dst]));
#line 604 "src/jit/emit_x64.dasc"
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
        dasm_put(Dst, 619, Dt11([reg_b]));
#line 617 "src/jit/emit_x64.dasc"
        /* comparison result in the setting bits in the rflags register */
        //| cmp rax, WORK[reg_c];
        dasm_put(Dst, 733, Dt11([reg_c]));
#line 619 "src/jit/emit_x64.dasc"
        /* copy the right comparison bit to the lower byte of the rax register */
        switch(ins->info->opcode) {
        case MVM_OP_eqaddr:
        case MVM_OP_eq_i:
            //| sete al;
            dasm_put(Dst, 738);
#line 624 "src/jit/emit_x64.dasc"
            break;
        case MVM_OP_ne_i:
            //| setne al;
            dasm_put(Dst, 742);
#line 627 "src/jit/emit_x64.dasc"
            break;
        case MVM_OP_lt_i:
            //| setl al;
            dasm_put(Dst, 746);
#line 630 "src/jit/emit_x64.dasc"
            break;
        case MVM_OP_le_i:
            //| setle al;
            dasm_put(Dst, 750);
#line 633 "src/jit/emit_x64.dasc"
            break;
        case MVM_OP_gt_i:
            //| setg al;
            dasm_put(Dst, 754);
#line 636 "src/jit/emit_x64.dasc"
            break;
        case MVM_OP_ge_i:
            //| setge al;
            dasm_put(Dst, 758);
#line 639 "src/jit/emit_x64.dasc"
            break;
        }
        /* zero extend al (lower byte) to rax (whole register) */
        //| movzx rax, al;
        //| mov WORK[reg_a], rax; 
        dasm_put(Dst, 762, Dt11([reg_a]));
#line 644 "src/jit/emit_x64.dasc"
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
            dasm_put(Dst, 771, -args[i].idx);
                    break;
                case 1:
            dasm_put(Dst, 776, -args[i].idx);
                    break;
                case 2:
            dasm_put(Dst, 781, -args[i].idx);
                    break;
                case 3:
            dasm_put(Dst, 786, -args[i].idx);
                    break;
                default:
                    MVM_exception_throw_adhoc(tc, "Can't JIT more than %d arguments", i);
            }
#line 664 "src/jit/emit_x64.dasc"
            break;
        case MVM_JIT_ADDR_INTERP:
            MVM_jit_log(tc, "emit interp call arg %d %d \n", i, args[i].idx);
            switch (args[i].idx) {
            case MVM_JIT_INTERP_TC:
                //| addarg i, TC;
                switch(i) {
                    case 0:
                dasm_put(Dst, 791);
                        break;
                    case 1:
                dasm_put(Dst, 796);
                        break;
                    case 2:
                dasm_put(Dst, 801);
                        break;
                    case 3:
                dasm_put(Dst, 806);
                        break;
                    default:
                        MVM_exception_throw_adhoc(tc, "Can't JIT more than %d arguments", i);
                }
#line 670 "src/jit/emit_x64.dasc"
                 break;
            case MVM_JIT_INTERP_FRAME:
                //| addarg i, TC->cur_frame;
                switch(i) {
                    case 0:
                dasm_put(Dst, 109, Dt10(->cur_frame));
                        break;
                    case 1:
                dasm_put(Dst, 811, Dt10(->cur_frame));
                        break;
                    case 2:
                dasm_put(Dst, 816, Dt10(->cur_frame));
                        break;
                    case 3:
                dasm_put(Dst, 821, Dt10(->cur_frame));
                        break;
                    default:
                        MVM_exception_throw_adhoc(tc, "Can't JIT more than %d arguments", i);
                }
#line 673 "src/jit/emit_x64.dasc"
                break;
            case MVM_JIT_INTERP_CU:
                //| addarg i, CU;
                switch(i) {
                    case 0:
                dasm_put(Dst, 826);
                        break;
                    case 1:
                dasm_put(Dst, 831);
                        break;
                    case 2:
                dasm_put(Dst, 836);
                        break;
                    case 3:
                dasm_put(Dst, 840);
                        break;
                    default:
                        MVM_exception_throw_adhoc(tc, "Can't JIT more than %d arguments", i);
                }
#line 676 "src/jit/emit_x64.dasc"
                break;
            }
            break;
        case MVM_JIT_ADDR_REG:
            //| addarg i, WORK[args[i].idx];
            switch(i) {
                case 0:
            dasm_put(Dst, 845, Dt11([args[i].idx]));
                    break;
                case 1:
            dasm_put(Dst, 850, Dt11([args[i].idx]));
                    break;
                case 2:
            dasm_put(Dst, 855, Dt11([args[i].idx]));
                    break;
                case 3:
            dasm_put(Dst, 860, Dt11([args[i].idx]));
                    break;
                default:
                    MVM_exception_throw_adhoc(tc, "Can't JIT more than %d arguments", i);
            }
#line 681 "src/jit/emit_x64.dasc"
            break;
        case MVM_JIT_ADDR_REG_F:
            //| addarg_f i, WORK[args[i].idx];
            switch(i) {
                case 0:
            dasm_put(Dst, 665, Dt11([args[i].idx]));
                    break;
                case 1:
            dasm_put(Dst, 865, Dt11([args[i].idx]));
                    break;
                case 2:
            dasm_put(Dst, 872, Dt11([args[i].idx]));
                    break;
                case 3:
            dasm_put(Dst, 879, Dt11([args[i].idx]));
                    break;
                default:
                    MVM_exception_throw_adhoc(tc, "Can't JIT more than %d arguments", i);
            }
#line 684 "src/jit/emit_x64.dasc"
            break;
        case MVM_JIT_ADDR_LITERAL:
            //| addarg i, args[i].idx;
            switch(i) {
                case 0:
            dasm_put(Dst, 886, args[i].idx);
                    break;
                case 1:
            dasm_put(Dst, 891, args[i].idx);
                    break;
                case 2:
            dasm_put(Dst, 896, args[i].idx);
                    break;
                case 3:
            dasm_put(Dst, 901, args[i].idx);
                    break;
                default:
                    MVM_exception_throw_adhoc(tc, "Can't JIT more than %d arguments", i);
            }
#line 687 "src/jit/emit_x64.dasc"
            break;
        }
    }
    /* Emit the call. I think we should be able to do something smarter than
     * store the constant into the bytecode, like a data segment. But I'm
     * not sure. */
    //| callp call_spec->func_ptr;
    dasm_put(Dst, 152, (unsigned int)((uintptr_t)call_spec->func_ptr), (unsigned int)(((uintptr_t)call_spec->func_ptr)>>32));
#line 694 "src/jit/emit_x64.dasc"
    /* right, now determine what to do with the return value */
    switch(call_spec->rv_mode) {
    case MVM_JIT_RV_VOID:
        break;
    case MVM_JIT_RV_INT:
    case MVM_JIT_RV_PTR:
        //| mov WORK[call_spec->rv_idx], RV;
        dasm_put(Dst, 648, Dt11([call_spec->rv_idx]));
#line 701 "src/jit/emit_x64.dasc"
        break;
    case MVM_JIT_RV_NUM:
        //| movsd qword WORK[call_spec->rv_idx], RVF;
        dasm_put(Dst, 700, Dt11([call_spec->rv_idx]));
#line 704 "src/jit/emit_x64.dasc"
        break;
    case MVM_JIT_RV_DEREF:
        //| mov TMP1, [RV];
        //| mov WORK[call_spec->rv_idx], TMP1;
        dasm_put(Dst, 906, Dt11([call_spec->rv_idx]));
#line 708 "src/jit/emit_x64.dasc"
        break;
    case MVM_JIT_RV_ADDR:
        /* store local at address */
        //| mov TMP1, WORK[call_spec->rv_idx];
        //| mov [RV], TMP1;
        dasm_put(Dst, 914, Dt11([call_spec->rv_idx]));
#line 713 "src/jit/emit_x64.dasc"
        break;
    }
}

void MVM_jit_emit_branch(MVMThreadContext *tc, MVMJitGraph *jg,
                         MVMJitBranch * branch, dasm_State **Dst) {
    MVMSpeshIns *ins = branch->ins;
    MVMint32 name = branch->dest.name;
    if (ins == NULL || ins->info->opcode == MVM_OP_goto) {
        MVM_jit_log(tc, "emit jump to label %d\n", name);
        if (name == MVM_JIT_BRANCH_EXIT) {
            //| jmp ->exit
            dasm_put(Dst, 922);
#line 725 "src/jit/emit_x64.dasc"
        } else {
            //| jmp =>(name)
            dasm_put(Dst, 927, (name));
#line 727 "src/jit/emit_x64.dasc"
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
            dasm_put(Dst, 931, Dt11([reg]), (name));
#line 737 "src/jit/emit_x64.dasc"
            break;
        case MVM_OP_unless_i:
            //| mov rax, WORK[reg];
            //| test rax, rax;
            //| jz =>(name);
            dasm_put(Dst, 942, Dt11([reg]), (name));
#line 742 "src/jit/emit_x64.dasc"
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
    dasm_put(Dst, 256, (label->name));
#line 753 "src/jit/emit_x64.dasc"
}

void MVM_jit_emit_guard(MVMThreadContext *tc, MVMJitGraph *jg,
                        MVMJitGuard *guard, dasm_State **Dst) {
    MVMint16 op        = guard->ins->info->opcode;
    MVMint16 obj       = guard->ins->operands[0].lit_i16;
    MVMint16 spesh_idx = guard->ins->operands[1].lit_i16;
    MVM_jit_log(tc, "emit guard <%s>\n", guard->ins->info->name);
    /* load object and spesh slot value */
    //| mov TMP1, WORK[obj];
    //| get_spesh_slot TMP2, spesh_idx;
    dasm_put(Dst, 953, Dt11([obj]), Dt10(->cur_frame), Dt1(->effective_spesh_slots), DtC([spesh_idx]));
#line 764 "src/jit/emit_x64.dasc"
    if (op == MVM_OP_sp_guardtype) {
        /* object in queston should be a type object, so it shouldn't
         * be zero, should not be concrete, and the STABLE should be
         * equal to the value in the spesh slot */
        /* check for null */
        //| cmp TMP1, 0;
        //| je >1;
        dasm_put(Dst, 970);
#line 771 "src/jit/emit_x64.dasc"
        /* check if type object (not concrete) */
        //| is_type_object TMP1;
        dasm_put(Dst, 980, Dt8(->header.flags), MVM_CF_TYPE_OBJECT);
#line 773 "src/jit/emit_x64.dasc"
        /* if zero, this is a concrete object, and we should deopt */
        //| jz >1;
        dasm_put(Dst, 975);
#line 775 "src/jit/emit_x64.dasc"
        /* get stable and compare */
        //| cmp TMP2, OBJECT:TMP1->st;
        //| jne >1;
        dasm_put(Dst, 987, Dt8(->st));
#line 778 "src/jit/emit_x64.dasc"
        /* we're good, no need to deopt */
    } else if (op == MVM_OP_sp_guardconc) {
        /* object should be a non-null concrete (non-type) object */
        //| cmp TMP1, 0;
        //| je >1;
        dasm_put(Dst, 970);
#line 783 "src/jit/emit_x64.dasc"
        /* shouldn't be type object */
        //| is_type_object TMP1;
        //| jnz >1;
        dasm_put(Dst, 996, Dt8(->header.flags), MVM_CF_TYPE_OBJECT);
#line 786 "src/jit/emit_x64.dasc"
        /* should have our stable */
        //| cmp TMP2, OBJECT:TMP1->st;
        //| jne >1;
        dasm_put(Dst, 987, Dt8(->st));
#line 789 "src/jit/emit_x64.dasc"
    }
    /* if we're here, we didn't jump to deopt, so skip it */
    //| jmp >2;
    //|1:
    dasm_put(Dst, 1007);
#line 793 "src/jit/emit_x64.dasc"
    /* emit deopt */
    //| mov ARG1, TC;
    //| mov ARG2, guard->deopt_offset;
    //| mov ARG3, guard->deopt_target;
    //| callp &MVM_spesh_deopt_one_direct;
    dasm_put(Dst, 1014, guard->deopt_offset, guard->deopt_target, (unsigned int)((uintptr_t)&MVM_spesh_deopt_one_direct), (unsigned int)(((uintptr_t)&MVM_spesh_deopt_one_direct)>>32));
#line 798 "src/jit/emit_x64.dasc"
    /* tell jit driver we're deopting */
    //| mov RV, MVM_JIT_CTRL_DEOPT
    //| jmp ->out;
    //|2:
    dasm_put(Dst, 1044, MVM_JIT_CTRL_DEOPT);
#line 802 "src/jit/emit_x64.dasc"
}
