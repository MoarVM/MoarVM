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
static const unsigned char actions[1700] = {
  85,72,137,229,255,65,86,83,65,84,65,85,255,73,137,206,73,137,213,77,139,166,
  233,73,139,156,253,36,233,255,65,252,255,224,255,248,10,72,199,192,0,0,0,
  0,248,11,255,65,93,65,92,91,65,94,255,72,137,252,236,93,195,255,72,185,237,
  237,72,137,139,233,255,72,199,131,233,237,255,73,139,141,233,72,139,137,233,
  72,137,139,233,255,73,139,142,233,72,139,137,233,72,137,139,233,255,72,139,
  139,233,72,139,137,233,255,77,137,227,255,77,139,155,233,255,77,139,147,233,
  255,77,139,146,233,255,73,131,252,250,0,15,133,244,247,255,76,137,252,241,
  76,137,218,73,199,192,237,73,186,237,237,72,131,252,236,32,65,252,255,210,
  72,131,196,32,255,73,137,194,248,1,255,76,137,147,233,255,76,137,225,255,
  72,139,137,233,72,139,147,233,72,137,145,233,255,73,139,140,253,36,233,72,
  139,137,233,72,137,139,233,255,72,139,139,233,72,131,185,233,0,15,133,244,
  247,72,141,145,233,252,233,244,248,248,1,72,139,145,233,72,141,146,233,248,
  2,255,76,139,2,73,131,252,248,0,255,15,133,244,249,255,77,139,134,233,77,
  139,128,233,248,3,255,76,139,2,255,73,131,252,248,0,15,133,244,250,255,77,
  139,132,253,36,233,77,139,128,233,255,102,252,247,129,233,236,15,149,208,
  73,131,252,248,0,15,149,212,132,196,15,149,208,102,65,252,247,128,233,236,
  15,148,212,132,196,15,132,244,249,82,65,80,76,137,252,241,72,139,147,233,
  73,186,237,237,72,131,252,236,32,65,252,255,210,72,131,196,32,65,88,90,248,
  3,255,76,137,2,248,4,255,76,139,2,73,131,252,248,0,15,133,244,250,255,81,
  82,76,137,252,241,73,139,148,253,36,233,72,139,146,233,73,186,237,237,72,
  131,252,236,32,65,252,255,210,72,131,196,32,90,89,255,73,137,192,102,252,
  247,129,233,236,15,149,208,73,131,252,248,0,15,149,212,132,196,15,149,208,
  102,65,252,247,128,233,236,15,148,212,132,196,15,132,244,249,82,65,80,76,
  137,252,241,72,139,147,233,73,186,237,237,72,131,252,236,32,65,252,255,210,
  72,131,196,32,65,88,90,248,3,76,137,2,255,76,137,131,233,255,72,139,139,233,
  72,139,147,233,76,141,129,233,73,131,184,233,0,15,132,244,247,77,139,128,
  233,248,1,255,102,252,247,129,233,236,15,149,208,72,131,252,250,0,15,149,
  212,132,196,15,149,208,102,252,247,130,233,236,15,148,212,132,196,15,132,
  244,248,82,65,80,76,137,252,241,72,139,147,233,73,186,237,237,72,131,252,
  236,32,65,252,255,210,72,131,196,32,65,88,90,248,2,255,73,137,144,233,255,
  72,139,139,233,72,137,139,233,255,72,139,139,233,73,137,142,233,255,73,139,
  142,233,72,137,139,233,73,199,134,233,237,255,73,139,141,233,255,72,199,131,
  233,0,0,0,0,255,72,199,194,0,0,0,0,72,131,187,233,0,15,133,244,247,72,199,
  194,1,0,0,0,248,1,72,137,147,233,255,72,139,131,233,255,72,3,131,233,255,
  72,43,131,233,255,72,15,175,131,233,255,72,153,72,252,247,187,233,255,72,
  137,131,233,255,72,252,255,131,233,255,72,252,255,139,233,255,252,242,15,
  16,131,233,255,252,242,15,88,131,233,255,252,242,15,92,131,233,255,252,242,
  15,89,131,233,255,252,242,15,94,131,233,255,252,242,15,17,131,233,255,252,
  242,72,15,42,131,233,252,242,15,17,131,233,255,252,242,72,15,44,131,233,72,
  137,131,233,255,72,59,131,233,255,15,148,208,255,15,149,208,255,15,156,208,
  255,15,158,208,255,15,159,208,255,15,157,208,255,72,15,182,192,72,137,131,
  233,255,76,137,252,241,72,199,194,237,73,186,237,237,72,131,252,236,32,65,
  252,255,210,72,131,196,32,73,139,140,253,36,233,72,139,137,233,72,137,136,
  233,102,199,128,233,236,65,139,142,233,137,136,233,72,137,131,233,255,76,
  139,147,233,77,133,210,255,15,132,244,247,102,65,252,247,130,233,236,255,
  15,133,244,247,77,139,154,233,77,139,155,233,77,133,219,255,15,132,244,247,
  76,137,252,241,76,137,210,76,141,131,233,77,139,147,233,72,131,252,236,32,
  65,252,255,210,72,131,196,32,252,233,244,248,248,1,255,76,137,147,233,248,
  2,255,73,139,140,253,36,233,198,129,233,1,255,76,137,252,241,255,76,137,252,
  242,255,77,137,252,240,255,77,137,252,241,255,77,137,252,243,65,83,255,76,
  137,226,255,77,137,224,255,77,137,225,255,77,137,227,65,83,255,76,137,252,
  233,255,76,137,252,234,255,77,137,232,255,77,137,252,233,255,77,137,252,235,
  65,83,255,77,141,156,253,36,233,255,76,137,217,255,76,137,218,255,77,137,
  216,255,77,137,217,255,77,137,219,65,83,255,72,139,139,233,255,72,139,147,
  233,255,76,139,131,233,255,76,139,139,233,255,76,139,155,233,65,83,255,102,
  72,15,110,131,233,255,102,72,15,110,139,233,255,102,72,15,110,147,233,255,
  102,72,15,110,155,233,255,76,141,155,233,255,77,139,157,233,77,139,155,233,
  255,72,199,193,237,255,72,199,194,237,255,73,199,192,237,255,73,199,193,237,
  255,73,199,195,237,65,83,255,73,187,237,237,255,102,73,15,110,195,255,102,
  73,15,110,203,255,102,73,15,110,211,255,102,73,15,110,219,255,72,139,8,72,
  137,139,233,255,72,139,139,233,72,137,8,255,73,131,190,233,0,15,132,244,247,
  76,137,252,241,73,186,237,237,72,131,252,236,32,65,252,255,210,72,131,196,
  32,248,1,255,252,233,244,10,255,252,233,245,255,72,139,131,233,72,133,192,
  15,133,245,255,72,139,131,233,72,133,192,15,132,245,255,72,139,139,233,72,
  133,201,15,132,244,247,73,139,150,233,72,139,146,233,72,57,209,15,132,244,
  247,252,233,245,248,1,255,72,139,139,233,73,139,148,253,36,233,72,139,146,
  233,255,72,131,252,249,0,15,132,244,247,255,102,252,247,129,233,236,255,72,
  59,145,233,15,133,244,247,255,102,252,247,129,233,236,15,133,244,247,255,
  76,137,252,241,72,199,194,237,73,199,192,237,73,186,237,237,72,131,252,236,
  32,65,252,255,210,72,131,196,32,255,72,199,192,237,252,233,244,11,248,2,255,
  76,137,252,241,76,137,252,234,73,199,192,237,73,186,237,237,72,131,252,236,
  32,65,252,255,210,72,131,196,32,73,137,195,255,77,139,148,253,36,233,255,
  76,139,139,233,77,137,138,233,255,73,185,237,237,77,137,138,233,255,77,139,
  141,233,77,139,137,233,77,137,138,233,255,65,199,132,253,36,233,237,255,73,
  199,132,253,36,233,237,255,72,141,147,233,73,137,148,253,36,233,255,73,139,
  150,233,72,139,18,73,137,148,253,36,233,255,72,141,21,245,73,137,148,253,
  36,233,255,65,82,65,83,255,76,137,252,241,72,139,147,233,73,137,224,77,137,
  209,73,186,237,237,72,131,252,236,32,65,252,255,210,72,131,196,32,255,65,
  91,65,90,255,76,137,252,241,72,137,194,77,137,216,77,137,209,255,76,139,144,
  233,77,139,146,233,72,131,252,236,32,65,252,255,210,72,131,196,32,255,76,
  137,252,241,72,139,147,233,77,137,216,73,199,193,237,73,186,237,237,72,131,
  252,236,32,65,252,255,210,72,131,196,32,255,72,199,192,1,0,0,0,252,233,244,
  11,255,77,59,166,233,15,132,244,247,72,141,13,244,247,73,137,140,253,36,233,
  72,199,192,1,0,0,0,252,233,244,11,248,1,255,205,3,255
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
//|.type REGISTER, MVMRegister
#define Dt1(_V) (int)(ptrdiff_t)&(((MVMRegister *)0)_V)
#line 13 "src/jit/emit_x64.dasc"
//|.type ARGCTX, MVMArgProcContext
#define Dt2(_V) (int)(ptrdiff_t)&(((MVMArgProcContext *)0)_V)
#line 14 "src/jit/emit_x64.dasc"
//|.type STATICFRAME, MVMStaticFrame
#define Dt3(_V) (int)(ptrdiff_t)&(((MVMStaticFrame *)0)_V)
#line 15 "src/jit/emit_x64.dasc"
//|.type P6OPAQUE, MVMP6opaque
#define Dt4(_V) (int)(ptrdiff_t)&(((MVMP6opaque *)0)_V)
#line 16 "src/jit/emit_x64.dasc"
//|.type P6OBODY, MVMP6opaqueBody
#define Dt5(_V) (int)(ptrdiff_t)&(((MVMP6opaqueBody *)0)_V)
#line 17 "src/jit/emit_x64.dasc"
//|.type MVMINSTANCE, MVMInstance
#define Dt6(_V) (int)(ptrdiff_t)&(((MVMInstance *)0)_V)
#line 18 "src/jit/emit_x64.dasc"
//|.type OBJECT, MVMObject
#define Dt7(_V) (int)(ptrdiff_t)&(((MVMObject *)0)_V)
#line 19 "src/jit/emit_x64.dasc"
//|.type COLLECTABLE, MVMCollectable
#define Dt8(_V) (int)(ptrdiff_t)&(((MVMCollectable *)0)_V)
#line 20 "src/jit/emit_x64.dasc"
//|.type STABLE, MVMSTable
#define Dt9(_V) (int)(ptrdiff_t)&(((MVMSTable *)0)_V)
#line 21 "src/jit/emit_x64.dasc"
//|.type STRING, MVMString*
#define DtA(_V) (int)(ptrdiff_t)&(((MVMString* *)0)_V)
#line 22 "src/jit/emit_x64.dasc"
//|.type OBJECTPTR, MVMObject*
#define DtB(_V) (int)(ptrdiff_t)&(((MVMObject* *)0)_V)
#line 23 "src/jit/emit_x64.dasc"
//|.type CONTAINERSPEC, MVMContainerSpec
#define DtC(_V) (int)(ptrdiff_t)&(((MVMContainerSpec *)0)_V)
#line 24 "src/jit/emit_x64.dasc"
//|.type HLLCONFIG, MVMHLLConfig;
#define DtD(_V) (int)(ptrdiff_t)&(((MVMHLLConfig *)0)_V)
#line 25 "src/jit/emit_x64.dasc"
//|.type U8, MVMuint8
#define DtE(_V) (int)(ptrdiff_t)&(((MVMuint8 *)0)_V)
#line 26 "src/jit/emit_x64.dasc"
//|.type U16, MVMuint16
#define DtF(_V) (int)(ptrdiff_t)&(((MVMuint16 *)0)_V)
#line 27 "src/jit/emit_x64.dasc"
//|.type U32, MVMuint32
#define Dt10(_V) (int)(ptrdiff_t)&(((MVMuint32 *)0)_V)
#line 28 "src/jit/emit_x64.dasc"
//|.type U64, MVMuint64
#define Dt11(_V) (int)(ptrdiff_t)&(((MVMuint64 *)0)_V)
#line 29 "src/jit/emit_x64.dasc"



/* Static allocation of relevant types to registers. I pick
 * callee-save registers for efficiency. It is likely we'll be calling
 * quite a C functions, and this saves us the trouble of storing
 * them. Moreover, C compilers preferentially do not use callee-saved
 * registers, and so in most cases, these won't be touched at all. */
//|.type TC, MVMThreadContext, r14
#define Dt12(_V) (int)(ptrdiff_t)&(((MVMThreadContext *)0)_V)
#line 38 "src/jit/emit_x64.dasc"
/* Alternative base pointer. I'll be using this often, so picking rbx
 * here rather than the extended registers will lead to smaller
 * bytecode */
//|.type WORK, MVMRegister, rbx
#define Dt13(_V) (int)(ptrdiff_t)&(((MVMRegister *)0)_V)
#line 42 "src/jit/emit_x64.dasc"
//|.type FRAME, MVMFrame, r12
#define Dt14(_V) (int)(ptrdiff_t)&(((MVMFrame *)0)_V)
#line 43 "src/jit/emit_x64.dasc"
//|.type CU, MVMCompUnit, r13
#define Dt15(_V) (int)(ptrdiff_t)&(((MVMCompUnit *)0)_V)
#line 44 "src/jit/emit_x64.dasc"


//|.macro saveregs
//| push TC; push WORK; push FRAME; push CU;
//|.endmacro

//|.macro restoreregs
//| pop CU; pop FRAME; pop WORK; pop TC
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
/* same, but 32 bits wide */
//|.define TMP1d, ecx
//|.define TMP2d, edx
//|.define TMP3d, r8d
//|.define TMP4d, r9d
//|.define TMP5d, r10d
//|.define TMP6d, r11d

/* return value */
//|.define RV, rax
//|.define RVF, xmm0

//|.macro callr, reg
//|.if WIN32
//| sub rsp, 32
//| call reg
//| add rsp, 32
//|.else
//| call reg
//|.endif
//|.endmacro

//|.macro callp, funcptr
//| mov64 FUNCTION, (uintptr_t)funcptr
//| callr FUNCTION
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
//| mov reg, FRAME->effective_spesh_slots;
//| mov reg, OBJECTPTR:reg[idx];
//|.endmacro


//|.macro get_vmnull, reg
//| mov reg, TC->instance;
//| mov reg, MVMINSTANCE:reg->VMNull;
//|.endmacro

//|.macro get_cur_op, reg
//| mov reg, TC->interp_cur_op
//| mov reg, [reg]
//|.endmacro

//|.macro get_string, reg, idx
//| mov reg, CU->body.strings;
//| mov reg, STRING:reg[idx];
//|.endmacro

//|.macro is_type_object, reg
//| test word OBJECT:reg->header.flags, MVM_CF_TYPE_OBJECT
//|.endmacro

//|.macro gc_sync_point
//| cmp qword TC->gc_status, 0;
//| je >1;
//| mov ARG1, TC;
//| callp &MVM_gc_enter_from_interrupt;
//|1:
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
#line 198 "src/jit/emit_x64.dasc"
    /* save callee-save registers */
    //| saveregs;
    dasm_put(Dst, 5);
#line 200 "src/jit/emit_x64.dasc"
    /* setup special frame variables */
    //| mov TC,   ARG1;
    //| mov CU,   ARG2;
    //| mov FRAME, TC->cur_frame;
    //| mov WORK, FRAME->work;
    dasm_put(Dst, 13, Dt12(->cur_frame), Dt14(->work));
#line 205 "src/jit/emit_x64.dasc"
    /* ARG3 contains our 'entry label' */
    //| jmp ARG3
    dasm_put(Dst, 30);
#line 207 "src/jit/emit_x64.dasc"

}

/* And a function epilogue is also always the same */
void MVM_jit_emit_epilogue(MVMThreadContext *tc, MVMJitGraph *jg,
                           dasm_State **Dst) {
    //| ->exit:
    //| mov RV, 0;
    //| ->out:
    dasm_put(Dst, 35);
#line 216 "src/jit/emit_x64.dasc"
    /* restore callee-save registers */
    //| restoreregs;
    dasm_put(Dst, 47);
#line 218 "src/jit/emit_x64.dasc"
    /* Restore stack */
    //| mov rsp, rbp;
    //| pop rbp;
    //| ret;
    dasm_put(Dst, 55);
#line 222 "src/jit/emit_x64.dasc"
}

static MVMuint64 try_emit_gen2_ref(MVMThreadContext *tc, MVMJitGraph *jg,
                                   MVMObject *obj, MVMint16 reg, 
                                   dasm_State **Dst) {
    if (!(obj->header.flags & MVM_CF_SECOND_GEN))
        return 0;
    //| mov64 TMP1, (uintptr_t)obj;
    //| mov WORK[reg], TMP1;
    dasm_put(Dst, 62, (unsigned int)((uintptr_t)obj), (unsigned int)(((uintptr_t)obj)>>32), Dt13([reg]));
#line 231 "src/jit/emit_x64.dasc"
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
        dasm_put(Dst, 71, Dt13([reg]), val);
#line 250 "src/jit/emit_x64.dasc"
        break;
    }
    case MVM_OP_const_i64: {
        MVMint32 reg = ins->operands[0].reg.orig;
        MVMint64 val = ins->operands[1].lit_i64;
        //| mov64 TMP1, val;
        //| mov WORK[reg], TMP1;
        dasm_put(Dst, 62, (unsigned int)(val), (unsigned int)((val)>>32), Dt13([reg]));
#line 257 "src/jit/emit_x64.dasc"
        break;
    }
    case MVM_OP_const_n64: {
        MVMint16 reg = ins->operands[0].reg.orig;
        MVMint64 valbytes = ins->operands[1].lit_i64;
        MVM_jit_log(tc, "store const %f\n", ins->operands[1].lit_n64);
        //| mov64 TMP1, valbytes;
        //| mov WORK[reg], TMP1;
        dasm_put(Dst, 62, (unsigned int)(valbytes), (unsigned int)((valbytes)>>32), Dt13([reg]));
#line 265 "src/jit/emit_x64.dasc"
        break;
    }
    case MVM_OP_const_s: {
         MVMint16 reg = ins->operands[0].reg.orig;
         MVMuint32 idx = ins->operands[1].lit_str_idx;
         MVMStaticFrame *sf = jg->sg->sf;
         MVMString * s = sf->body.cu->body.strings[idx];
         if (!try_emit_gen2_ref(tc, jg, (MVMObject*)s, reg, Dst)) {
             //| get_string TMP1, idx;
             //| mov WORK[reg], TMP1;
             dasm_put(Dst, 77, Dt15(->body.strings), DtA([idx]), Dt13([reg]));
#line 275 "src/jit/emit_x64.dasc"
         }
         break;
    }
    case MVM_OP_null: {
        MVMint16 reg = ins->operands[0].reg.orig;
        //| get_vmnull TMP1;
        //| mov WORK[reg], TMP1;
        dasm_put(Dst, 90, Dt12(->instance), Dt6(->VMNull), Dt13([reg]));
#line 282 "src/jit/emit_x64.dasc"
        break;
    }
    case MVM_OP_gethow:
    case MVM_OP_getwhat:
    case MVM_OP_getwho: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint16 obj = ins->operands[0].reg.orig;
        //| mov TMP1, WORK[obj];
        //| mov TMP1, OBJECT:TMP1->st;
        dasm_put(Dst, 103, Dt13([obj]), Dt7(->st));
#line 291 "src/jit/emit_x64.dasc"
        if (op == MVM_OP_gethow) {
            //| mov TMP1, STABLE:TMP1->HOW;
            dasm_put(Dst, 107, Dt9(->HOW));
#line 293 "src/jit/emit_x64.dasc"
        } else if (op == MVM_OP_getwho) {
            //| mov TMP1, STABLE:TMP1->WHO;
            dasm_put(Dst, 107, Dt9(->WHO));
#line 295 "src/jit/emit_x64.dasc"
        } else {
            //| mov TMP1, STABLE:TMP1->WHAT;
            dasm_put(Dst, 107, Dt9(->WHAT));
#line 297 "src/jit/emit_x64.dasc"
        }
        //| mov WORK[dst], TMP1;
        dasm_put(Dst, 66, Dt13([dst]));
#line 299 "src/jit/emit_x64.dasc"
        break;
    }
    case MVM_OP_getlex: {
        MVMint16 *lexical_types;
        MVMStaticFrame * sf = jg->sg->sf;
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint16 idx = ins->operands[1].lex.idx;
        MVMint16 out = ins->operands[1].lex.outers;
        MVMint16 i;
        //| mov TMP6, FRAME;
        dasm_put(Dst, 112);
#line 309 "src/jit/emit_x64.dasc"
        for (i = 0; i < out; i++) {
            /* I'm going to skip compiling the check whether the outer
             * node really exists, because if the code has run N times
             * correctly, then the outer frame must have existed then,
             * and since this chain is static, it should still exist
             * now.  If it doesn't exist, that means we crash.
             *
             * NB: inlining /might/ make this all wrong! But, if that
             * happens, the interpreter will panic even without JIT */
            //| mov TMP6, FRAME:TMP6->outer;
            dasm_put(Dst, 116, Dt14(->outer));
#line 319 "src/jit/emit_x64.dasc"
            sf = sf->body.outer;
        }
        /* get array of lexicals */
        //| mov TMP5, FRAME:TMP6->env;
        dasm_put(Dst, 121, Dt14(->env));
#line 323 "src/jit/emit_x64.dasc"
        /* read value */
        //| mov TMP5, REGISTER:TMP5[idx];
        dasm_put(Dst, 126, Dt1([idx]));
#line 325 "src/jit/emit_x64.dasc"
        /* it seems that if at runtime, if the outer frame has been inlined,
         * this /could/ be wrong. But if that is so, the interpreted instruction
         * would also be wrong, because it'd refer to the wrong lexical. */
        lexical_types = (!out && jg->sg->lexical_types ?
                         jg->sg->lexical_types :
                         sf->body.lexical_types);
        MVM_jit_log(tc, "Lexical type of register: %d\n", lexical_types[idx]);
        if (lexical_types[idx] == MVM_reg_obj) {
            MVM_jit_log(tc, "Emit lex vifivy check\n");
            /* NB: this code path hasn't been checked. */
            /* if it is zero, check if we need to auto-vivify */        
            //| cmp TMP5, 0;
            //| jne >1; 
            dasm_put(Dst, 131);
#line 338 "src/jit/emit_x64.dasc"
            /* setup args */
            //| mov ARG1, TC;
            //| mov ARG2, TMP6;
            //| mov ARG3, idx;
            //| callp &MVM_frame_vivify_lexical;
            dasm_put(Dst, 141, idx, (unsigned int)((uintptr_t)&MVM_frame_vivify_lexical), (unsigned int)(((uintptr_t)&MVM_frame_vivify_lexical)>>32));
#line 343 "src/jit/emit_x64.dasc"
            /* use return value for the result */
            //| mov TMP5, RV;
            //|1:
            dasm_put(Dst, 170);
#line 346 "src/jit/emit_x64.dasc"
        } 
        /* store the value */
        //| mov WORK[dst], TMP5;
        dasm_put(Dst, 176, Dt13([dst]));
#line 349 "src/jit/emit_x64.dasc"
        break;
    }
    case MVM_OP_bindlex: {
        MVMint16 idx = ins->operands[0].lex.idx;
        MVMint16 out = ins->operands[0].lex.outers;
        MVMint16 src = ins->operands[1].reg.orig;
        MVMint16 i;
        //| mov TMP1, FRAME;
        dasm_put(Dst, 181);
#line 357 "src/jit/emit_x64.dasc"
        for (i = 0; i < out; i++) {
            //| mov TMP1, FRAME:TMP1->outer;
            dasm_put(Dst, 107, Dt14(->outer));
#line 359 "src/jit/emit_x64.dasc"
        }
        //| mov TMP1, FRAME:TMP1->env;
        //| mov TMP2, WORK[src];
        //| mov REGISTER:TMP1[idx], TMP2;
        dasm_put(Dst, 185, Dt14(->env), Dt13([src]), Dt1([idx]));
#line 363 "src/jit/emit_x64.dasc"
        break;
    }
    case MVM_OP_sp_getarg_o:
    case MVM_OP_sp_getarg_n:
    case MVM_OP_sp_getarg_s:
    case MVM_OP_sp_getarg_i: {
        MVMint32 reg = ins->operands[0].reg.orig;
        MVMuint16 idx = ins->operands[1].callsite_idx;
        //| mov TMP1, FRAME->params.args;
        //| mov TMP1, REGISTER:TMP1[idx];
        //| mov WORK[reg], TMP1;
        dasm_put(Dst, 198, Dt14(->params.args), Dt1([idx]), Dt13([reg]));
#line 374 "src/jit/emit_x64.dasc"
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
        dasm_put(Dst, 213, Dt13([obj]), Dt4(->body.replaced), (offset + body), Dt4(->body.replaced), offset);
#line 395 "src/jit/emit_x64.dasc"
        /* TMP1 now contains address of item */
        if (op == MVM_OP_sp_p6oget_o) {
            //| mov TMP3, [TMP2];
            //| cmp TMP3, 0;
            dasm_put(Dst, 247);
#line 399 "src/jit/emit_x64.dasc"
            /* Check if object doesn't point to NULL */
            //| jne >3;
            dasm_put(Dst, 256);
#line 401 "src/jit/emit_x64.dasc"
            /* Otherwise load VMNull */ 
            //| get_vmnull TMP3;
            //|3:
            dasm_put(Dst, 261, Dt12(->instance), Dt6(->VMNull));
#line 404 "src/jit/emit_x64.dasc"
        } else if (op == MVM_OP_sp_p6ogetvt_o) {
            /* vivify as type object */ 
            MVMint16 spesh_idx = ins->operands[3].lit_i16;
            //| mov TMP3, [TMP2];
            dasm_put(Dst, 272);
#line 408 "src/jit/emit_x64.dasc"
            /* check for null */
            //| cmp TMP3, 0;
            //| jne >4;
            dasm_put(Dst, 276);
#line 411 "src/jit/emit_x64.dasc"
            /* if null, vivify as type object from spesh slot */
            //| get_spesh_slot TMP3, spesh_idx;
            dasm_put(Dst, 286, Dt14(->effective_spesh_slots), DtB([spesh_idx]));
#line 413 "src/jit/emit_x64.dasc"
            /* need to hit write barrier? */
            //| check_wb TMP1, TMP3;
            //| jz >3;
            //| push TMP2; // address
            //| push TMP3; // value
            //| hit_wb WORK[obj]; // write barrier for header 
            //| pop TMP3;
            //| pop TMP2;
            //|3:
            dasm_put(Dst, 297, Dt8(->flags), MVM_CF_SECOND_GEN, Dt8(->flags), MVM_CF_SECOND_GEN, Dt13([obj]), (unsigned int)((uintptr_t)&MVM_gc_write_barrier_hit), (unsigned int)(((uintptr_t)&MVM_gc_write_barrier_hit)>>32));
#line 422 "src/jit/emit_x64.dasc"
            /* store vivified type value in memory location */
            //| mov [TMP2], TMP3;
            //|4:
            dasm_put(Dst, 369);
#line 425 "src/jit/emit_x64.dasc"
        } else if (op == MVM_OP_sp_p6ogetvc_o) {
            MVMint16 spesh_idx = ins->operands[3].lit_i16;
            //| mov TMP3, [TMP2];
            //| cmp TMP3, 0;
            //| jne >4;
            dasm_put(Dst, 375);
#line 430 "src/jit/emit_x64.dasc"
            /* vivify as clone */
            //| push TMP1;
            //| push TMP2;
            //| mov ARG1, TC; 
            //| get_spesh_slot ARG2, spesh_idx;
            //| callp &MVM_repr_clone;
            //| pop TMP2;
            //| pop TMP1;
            dasm_put(Dst, 388, Dt14(->effective_spesh_slots), DtB([spesh_idx]), (unsigned int)((uintptr_t)&MVM_repr_clone), (unsigned int)(((uintptr_t)&MVM_repr_clone)>>32));
#line 438 "src/jit/emit_x64.dasc"
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
            dasm_put(Dst, 424, Dt8(->flags), MVM_CF_SECOND_GEN, Dt8(->flags), MVM_CF_SECOND_GEN, Dt13([obj]), (unsigned int)((uintptr_t)&MVM_gc_write_barrier_hit), (unsigned int)(((uintptr_t)&MVM_gc_write_barrier_hit)>>32));
#line 449 "src/jit/emit_x64.dasc"
            /* done */
            //|4:
            dasm_put(Dst, 372);
#line 451 "src/jit/emit_x64.dasc"
        } else {
            /* the regular case */
            //| mov TMP3, [TMP2];
            dasm_put(Dst, 272);
#line 454 "src/jit/emit_x64.dasc"
        }
        /* store in local register */
        //| mov WORK[dst], TMP3;
        dasm_put(Dst, 502, Dt13([dst]));
#line 457 "src/jit/emit_x64.dasc"
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
        dasm_put(Dst, 507, Dt13([obj]), Dt13([val]), Dt4(->body), Dt5(->replaced), Dt5(->replaced));
#line 473 "src/jit/emit_x64.dasc"
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
            dasm_put(Dst, 535, Dt8(->flags), MVM_CF_SECOND_GEN, Dt8(->flags), MVM_CF_SECOND_GEN, Dt13([obj]), (unsigned int)((uintptr_t)&MVM_gc_write_barrier_hit), (unsigned int)(((uintptr_t)&MVM_gc_write_barrier_hit)>>32));
#line 483 "src/jit/emit_x64.dasc"
        }
        //| mov [TMP3+offset], TMP2; // store value into body
        dasm_put(Dst, 606, offset);
#line 485 "src/jit/emit_x64.dasc"
        break;
    }
    case MVM_OP_getwhere:
    case MVM_OP_set: {
         MVMint32 reg1 = ins->operands[0].reg.orig;
         MVMint32 reg2 = ins->operands[1].reg.orig;
         //| mov TMP1, WORK[reg2];
         //| mov WORK[reg1], TMP1;
         dasm_put(Dst, 611, Dt13([reg2]), Dt13([reg1]));
#line 493 "src/jit/emit_x64.dasc"
         break;
    }
    case MVM_OP_sp_getspeshslot: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint16 spesh_idx = ins->operands[1].lit_i16;
        //| get_spesh_slot TMP1, spesh_idx;
        //| mov WORK[dst], TMP1;
        dasm_put(Dst, 198, Dt14(->effective_spesh_slots), DtB([spesh_idx]), Dt13([dst]));
#line 500 "src/jit/emit_x64.dasc"
        break;
    }
    case MVM_OP_setdispatcher: {
        MVMint16 src = ins->operands[0].reg.orig;
        //| mov TMP1, aword WORK[src];
        //| mov aword TC->cur_dispatcher, TMP1;
        dasm_put(Dst, 620, Dt13([src]), Dt12(->cur_dispatcher));
#line 506 "src/jit/emit_x64.dasc"
        break;
    }
    case MVM_OP_takedispatcher: {
        MVMint16 dst = ins->operands[0].reg.orig;
        //| mov TMP1, aword TC->cur_dispatcher;
        //| mov aword WORK[dst], TMP1;
        //| mov aword TC->cur_dispatcher, NULL;
        dasm_put(Dst, 629, Dt12(->cur_dispatcher), Dt13([dst]), Dt12(->cur_dispatcher), NULL);
#line 513 "src/jit/emit_x64.dasc"
        break;
    }
    case MVM_OP_getcode: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMuint16 idx = ins->operands[1].coderef_idx;
        //| mov TMP1, aword CU->body.coderefs;
        //| mov TMP1, aword OBJECTPTR:TMP1[idx];
        //| mov aword WORK[dst], TMP1;
        dasm_put(Dst, 77, Dt15(->body.coderefs), DtB([idx]), Dt13([dst]));
#line 521 "src/jit/emit_x64.dasc"
        break;
    }
    case MVM_OP_hllboxtype_n:
    case MVM_OP_hllboxtype_s:
    case MVM_OP_hllboxtype_i: {
        MVMint16 dst = ins->operands[0].reg.orig;
        //| mov TMP1, CU->body.hll_config;
        dasm_put(Dst, 643, Dt15(->body.hll_config));
#line 528 "src/jit/emit_x64.dasc"
        if (op == MVM_OP_hllboxtype_n) {
            //| mov TMP1, aword HLLCONFIG:TMP1->num_box_type;
            dasm_put(Dst, 107, DtD(->num_box_type));
#line 530 "src/jit/emit_x64.dasc"
        } else if (op == MVM_OP_hllboxtype_s) {
            //| mov TMP1, aword HLLCONFIG:TMP1->str_box_type;
            dasm_put(Dst, 107, DtD(->str_box_type));
#line 532 "src/jit/emit_x64.dasc"
        } else {
            //| mov TMP1, aword HLLCONFIG:TMP1->int_box_type;
            dasm_put(Dst, 107, DtD(->int_box_type));
#line 534 "src/jit/emit_x64.dasc"
        }
        //| mov aword WORK[dst], TMP1;
        dasm_put(Dst, 66, Dt13([dst]));
#line 536 "src/jit/emit_x64.dasc"
        break;
    }
    case MVM_OP_null_s: {
        MVMint16 dst = ins->operands[0].reg.orig;
        //| mov qword WORK[dst], 0;
        dasm_put(Dst, 648, Dt13([dst]));
#line 541 "src/jit/emit_x64.dasc"
        break;
     }
    case MVM_OP_isnull_s: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint16 src = ins->operands[1].reg.orig;
        //| mov TMP2, 0;
        //| cmp qword WORK[src], 0;
        //| jne >1;
        //| mov TMP2, 1;
        //|1:
        //| mov qword WORK[dst], TMP2;
        dasm_put(Dst, 657, Dt13([src]), Dt13([dst]));
#line 552 "src/jit/emit_x64.dasc"
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
        dasm_put(Dst, 687, Dt13([reg_b]));
#line 564 "src/jit/emit_x64.dasc"
        switch(ins->info->opcode) {
        case MVM_OP_add_i:
            //| add rax, WORK[reg_c];
            dasm_put(Dst, 692, Dt13([reg_c]));
#line 567 "src/jit/emit_x64.dasc"
            break;
        case MVM_OP_sub_i:
            //| sub rax, WORK[reg_c];
            dasm_put(Dst, 697, Dt13([reg_c]));
#line 570 "src/jit/emit_x64.dasc"
            break;
        case MVM_OP_mul_i:
            //| imul rax, WORK[reg_c];
            dasm_put(Dst, 702, Dt13([reg_c]));
#line 573 "src/jit/emit_x64.dasc"
            break;
        case MVM_OP_div_i:
        case MVM_OP_mod_i:
            // Convert Quadword to Octoword, i.e. use rax:rdx as one
            // single 16 byte register
            //| cqo; 
            //| idiv qword WORK[reg_c];
            dasm_put(Dst, 708, Dt13([reg_c]));
#line 580 "src/jit/emit_x64.dasc"
            break;
        }
        if (ins->info->opcode == MVM_OP_mod_i) {
            // result of modula is stored in rdx
            //| mov WORK[reg_a], rdx;
            dasm_put(Dst, 682, Dt13([reg_a]));
#line 585 "src/jit/emit_x64.dasc"
        } else {
            // all others in rax
            //| mov WORK[reg_a], rax;
            dasm_put(Dst, 716, Dt13([reg_a]));
#line 588 "src/jit/emit_x64.dasc"
        }
        break;
    }
    case MVM_OP_inc_i: {
         MVMint32 reg = ins->operands[0].reg.orig;
         //| inc qword WORK[reg];
         dasm_put(Dst, 721, Dt13([reg]));
#line 594 "src/jit/emit_x64.dasc"
         break;
    }
    case MVM_OP_dec_i: {
        MVMint32 reg = ins->operands[0].reg.orig;
        //| dec qword WORK[reg];
        dasm_put(Dst, 727, Dt13([reg]));
#line 599 "src/jit/emit_x64.dasc"
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
        dasm_put(Dst, 733, Dt13([reg_b]));
#line 611 "src/jit/emit_x64.dasc"
        switch(ins->info->opcode) {
        case MVM_OP_add_n:
            //| addsd xmm0, qword WORK[reg_c];
            dasm_put(Dst, 740, Dt13([reg_c]));
#line 614 "src/jit/emit_x64.dasc"
            break;
        case MVM_OP_sub_n:
            //| subsd xmm0, qword WORK[reg_c];
            dasm_put(Dst, 747, Dt13([reg_c]));
#line 617 "src/jit/emit_x64.dasc"
            break;
        case MVM_OP_mul_n:
            //| mulsd xmm0, qword WORK[reg_c];
            dasm_put(Dst, 754, Dt13([reg_c]));
#line 620 "src/jit/emit_x64.dasc"
            break;
        case MVM_OP_div_n:
            //| divsd xmm0, qword WORK[reg_c];
            dasm_put(Dst, 761, Dt13([reg_c]));
#line 623 "src/jit/emit_x64.dasc"
            break;
        }
        //| movsd qword WORK[reg_a], xmm0;
        dasm_put(Dst, 768, Dt13([reg_a]));
#line 626 "src/jit/emit_x64.dasc"
        break;
    }
    case MVM_OP_coerce_in: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint16 src = ins->operands[1].reg.orig;
        /* convert simple integer to double precision */
        //| cvtsi2sd xmm0, qword WORK[src];
        //| movsd qword WORK[dst], xmm0;
        dasm_put(Dst, 775, Dt13([src]), Dt13([dst]));
#line 634 "src/jit/emit_x64.dasc"
        break;
    }
    case MVM_OP_coerce_ni: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint16 src = ins->operands[1].reg.orig;
        /* convert double precision to simple intege */
        //| cvttsd2si rax, qword WORK[src];
        //| mov WORK[dst], rax;
        dasm_put(Dst, 789, Dt13([src]), Dt13([dst]));
#line 642 "src/jit/emit_x64.dasc"
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
        dasm_put(Dst, 687, Dt13([reg_b]));
#line 655 "src/jit/emit_x64.dasc"
        /* comparison result in the setting bits in the rflags register */
        //| cmp rax, WORK[reg_c];
        dasm_put(Dst, 801, Dt13([reg_c]));
#line 657 "src/jit/emit_x64.dasc"
        /* copy the right comparison bit to the lower byte of the rax register */
        switch(ins->info->opcode) {
        case MVM_OP_eqaddr:
        case MVM_OP_eq_i:
            //| sete al;
            dasm_put(Dst, 806);
#line 662 "src/jit/emit_x64.dasc"
            break;
        case MVM_OP_ne_i:
            //| setne al;
            dasm_put(Dst, 810);
#line 665 "src/jit/emit_x64.dasc"
            break;
        case MVM_OP_lt_i:
            //| setl al;
            dasm_put(Dst, 814);
#line 668 "src/jit/emit_x64.dasc"
            break;
        case MVM_OP_le_i:
            //| setle al;
            dasm_put(Dst, 818);
#line 671 "src/jit/emit_x64.dasc"
            break;
        case MVM_OP_gt_i:
            //| setg al;
            dasm_put(Dst, 822);
#line 674 "src/jit/emit_x64.dasc"
            break;
        case MVM_OP_ge_i:
            //| setge al;
            dasm_put(Dst, 826);
#line 677 "src/jit/emit_x64.dasc"
            break;
        }
        /* zero extend al (lower byte) to rax (whole register) */
        //| movzx rax, al;
        //| mov WORK[reg_a], rax; 
        dasm_put(Dst, 830, Dt13([reg_a]));
#line 682 "src/jit/emit_x64.dasc"
        break;
    }
    case MVM_OP_sp_fastcreate: {
        MVMint16 dst       = ins->operands[0].reg.orig;
        MVMuint16 size     = ins->operands[1].lit_i16;
        MVMint16 spesh_idx = ins->operands[2].lit_i16;
        //| mov ARG1, TC;
        //| mov ARG2, size;
        //| callp &MVM_gc_allocate_zeroed;
        //| get_spesh_slot TMP1, spesh_idx;
        //| mov aword OBJECT:RV->st, TMP1;  // st is 64 bit (pointer)
        //| mov word OBJECT:RV->header.size, size; // object size is 16 bit
        //| mov TMP1d, dword TC->thread_id;  // thread id is 32 bit
        //| mov dword OBJECT:RV->header.owner, TMP1d; // does this even work?
        //| mov aword WORK[dst], RV; // store in local register
        dasm_put(Dst, 839, size, (unsigned int)((uintptr_t)&MVM_gc_allocate_zeroed), (unsigned int)(((uintptr_t)&MVM_gc_allocate_zeroed)>>32), Dt14(->effective_spesh_slots), DtB([spesh_idx]), Dt7(->st), Dt7(->header.size), size, Dt12(->thread_id), Dt7(->header.owner), Dt13([dst]));
#line 697 "src/jit/emit_x64.dasc"
        break;
    }
    case MVM_OP_decont: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint16 src = ins->operands[1].reg.orig;
        //| mov TMP5, WORK[src];
        //| test TMP5, TMP5;
        dasm_put(Dst, 895, Dt13([src]));
#line 704 "src/jit/emit_x64.dasc"
        // obj is null
        //| jz >1; 
        //| is_type_object TMP5;
        dasm_put(Dst, 903, Dt7(->header.flags), MVM_CF_TYPE_OBJECT);
#line 707 "src/jit/emit_x64.dasc"
        // object is type object (not concrete)
        //| jnz >1;
        //| mov TMP6, OBJECT:TMP5->st;
        //| mov TMP6, STABLE:TMP6->container_spec;
        //| test TMP6, TMP6;
        dasm_put(Dst, 915, Dt7(->st), Dt9(->container_spec));
#line 712 "src/jit/emit_x64.dasc"
        // container spec is zero
        //| jz >1;
        //| mov ARG1, TC;
        //| mov ARG2, TMP5;      // object
        //| lea ARG3, WORK[dst]; // destination register
        //| mov FUNCTION, CONTAINERSPEC:TMP6->fetch; // get function pointer
        //| callr FUNCTION;
        //| jmp >2;
        //|1:
        dasm_put(Dst, 931, Dt13([dst]), DtC(->fetch));
#line 721 "src/jit/emit_x64.dasc"
        // otherwise just move the object into the register
        //| mov WORK[dst], TMP5;
        //|2:
        dasm_put(Dst, 970, Dt13([dst]));
#line 724 "src/jit/emit_x64.dasc"
        break;
    }
    case MVM_OP_sp_namedarg_used: {
        MVMuint16 param = ins->operands[0].lit_i16;
        //| mov TMP1, FRAME->params.named_used;
        //| mov byte U8:TMP1[param], 1;
        dasm_put(Dst, 977, Dt14(->params.named_used), DtE([param]));
#line 730 "src/jit/emit_x64.dasc"
        break;
    }
    default:

        MVM_exception_throw_adhoc(tc, "Can't JIT opcode");
    }
}

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
//|         mov TMP6, val;
//|         push TMP6;
//||}
//|.endmacro

//|.macro addarg_f, i, val
//||switch(i) {
//||    case 0:
//|         movd ARG1F, val
//||        break;
//||    case 1:
//|         movd ARG2F, val
//||        break;
//||    case 2:
//|         movd ARG3F, val
//||        break;
//||    case 3:
//|         movd ARG4F, val
//||        break;
//|.if not WIN32
//||    case 4:
//|         movd ARG5F, val
//||        break;
//||    case 5:
//|         movd ARG6F, val
//||        break;
//||    case 6:
//|         movd ARG7F, val
//||        break;
//||    case 7:
//|         movd ARG8F, val
//||        break;
//|.endif
//||    default:
//|         mov TMP6, val;
//|         push TMP6;
//||        break;
//||}
//|.endmacro


/* Call argument decoder */
static void emit_call_arg(MVMThreadContext *tc, MVMJitCallArg arg,
                          MVMint32 i, dasm_State **Dst) {
    switch(arg.type) {
    case MVM_JIT_INTERP_VAR:
        MVM_jit_log(tc, "emit interp call arg %d %d \n", i, arg.v.ivar);
        switch (arg.v.ivar) {
        case MVM_JIT_INTERP_TC:
            //| addarg i, TC;
            switch(i) {
                case 0:
            dasm_put(Dst, 988);
                    break;
                case 1:
            dasm_put(Dst, 993);
                    break;
                case 2:
            dasm_put(Dst, 998);
                    break;
                case 3:
            dasm_put(Dst, 1003);
                    break;
                default:
            dasm_put(Dst, 1008);
            }
#line 811 "src/jit/emit_x64.dasc"
            break;
        case MVM_JIT_INTERP_FRAME:
            //| addarg i, FRAME;
            switch(i) {
                case 0:
            dasm_put(Dst, 181);
                    break;
                case 1:
            dasm_put(Dst, 1015);
                    break;
                case 2:
            dasm_put(Dst, 1019);
                    break;
                case 3:
            dasm_put(Dst, 1023);
                    break;
                default:
            dasm_put(Dst, 1027);
            }
#line 814 "src/jit/emit_x64.dasc"
            break;
        case MVM_JIT_INTERP_CU:
            //| addarg i, CU;
            switch(i) {
                case 0:
            dasm_put(Dst, 1033);
                    break;
                case 1:
            dasm_put(Dst, 1038);
                    break;
                case 2:
            dasm_put(Dst, 1043);
                    break;
                case 3:
            dasm_put(Dst, 1047);
                    break;
                default:
            dasm_put(Dst, 1052);
            }
#line 817 "src/jit/emit_x64.dasc"
            break;
        case MVM_JIT_INTERP_PARAMS:
            //| lea TMP6, FRAME->params;
            //| addarg i, TMP6;
            dasm_put(Dst, 1059, Dt14(->params));
            switch(i) {
                case 0:
            dasm_put(Dst, 1066);
                    break;
                case 1:
            dasm_put(Dst, 1070);
                    break;
                case 2:
            dasm_put(Dst, 1074);
                    break;
                case 3:
            dasm_put(Dst, 1078);
                    break;
                default:
            dasm_put(Dst, 1082);
            }
#line 821 "src/jit/emit_x64.dasc"
            break;
        }
        break;
    case MVM_JIT_REG_VAL:
        //| addarg i, WORK[arg.v.reg];
        switch(i) {
            case 0:
        dasm_put(Dst, 1088, Dt13([arg.v.reg]));
                break;
            case 1:
        dasm_put(Dst, 1093, Dt13([arg.v.reg]));
                break;
            case 2:
        dasm_put(Dst, 1098, Dt13([arg.v.reg]));
                break;
            case 3:
        dasm_put(Dst, 1103, Dt13([arg.v.reg]));
                break;
            default:
        dasm_put(Dst, 1108, Dt13([arg.v.reg]));
        }
#line 826 "src/jit/emit_x64.dasc"
        break;
    case MVM_JIT_REG_VAL_F:
        //| addarg_f i, qword WORK[arg.v.reg];
        switch(i) {
            case 0:
        dasm_put(Dst, 1115, Dt13([arg.v.reg]));
                break;
            case 1:
        dasm_put(Dst, 1122, Dt13([arg.v.reg]));
                break;
            case 2:
        dasm_put(Dst, 1129, Dt13([arg.v.reg]));
                break;
            case 3:
        dasm_put(Dst, 1136, Dt13([arg.v.reg]));
                break;
            default:
        dasm_put(Dst, 1108, Dt13([arg.v.reg]));
                break;
        }
#line 829 "src/jit/emit_x64.dasc"
        break;
    case MVM_JIT_REG_ADDR:
        //| lea TMP6, WORK[arg.v.reg];
        //| addarg i, TMP6;
        dasm_put(Dst, 1143, Dt13([arg.v.reg]));
        switch(i) {
            case 0:
        dasm_put(Dst, 1066);
                break;
            case 1:
        dasm_put(Dst, 1070);
                break;
            case 2:
        dasm_put(Dst, 1074);
                break;
            case 3:
        dasm_put(Dst, 1078);
                break;
            default:
        dasm_put(Dst, 1082);
        }
#line 833 "src/jit/emit_x64.dasc"
        break;
    case MVM_JIT_STR_IDX:
        //| get_string TMP6, arg.v.lit_i64;
        //| addarg i, TMP6;
        dasm_put(Dst, 1148, Dt15(->body.strings), DtA([arg.v.lit_i64]));
        switch(i) {
            case 0:
        dasm_put(Dst, 1066);
                break;
            case 1:
        dasm_put(Dst, 1070);
                break;
            case 2:
        dasm_put(Dst, 1074);
                break;
            case 3:
        dasm_put(Dst, 1078);
                break;
            default:
        dasm_put(Dst, 1082);
        }
#line 837 "src/jit/emit_x64.dasc"
        break;
    case MVM_JIT_LITERAL:
        //| addarg i, arg.v.lit_i64;
        switch(i) {
            case 0:
        dasm_put(Dst, 1157, arg.v.lit_i64);
                break;
            case 1:
        dasm_put(Dst, 1162, arg.v.lit_i64);
                break;
            case 2:
        dasm_put(Dst, 1167, arg.v.lit_i64);
                break;
            case 3:
        dasm_put(Dst, 1172, arg.v.lit_i64);
                break;
            default:
        dasm_put(Dst, 1177, arg.v.lit_i64);
        }
#line 840 "src/jit/emit_x64.dasc"
        break;
    case MVM_JIT_LITERAL_64:
    case MVM_JIT_LITERAL_PTR:
        //| mov64 TMP6, arg.v.lit_i64;
        //| addarg i, TMP6;
        dasm_put(Dst, 1184, (unsigned int)(arg.v.lit_i64), (unsigned int)((arg.v.lit_i64)>>32));
        switch(i) {
            case 0:
        dasm_put(Dst, 1066);
                break;
            case 1:
        dasm_put(Dst, 1070);
                break;
            case 2:
        dasm_put(Dst, 1074);
                break;
            case 3:
        dasm_put(Dst, 1078);
                break;
            default:
        dasm_put(Dst, 1082);
        }
#line 845 "src/jit/emit_x64.dasc"
        break;
    case MVM_JIT_LITERAL_F:
        //| mov64 TMP6, arg.v.lit_i64;
        //| addarg_f i, TMP6;
        dasm_put(Dst, 1184, (unsigned int)(arg.v.lit_i64), (unsigned int)((arg.v.lit_i64)>>32));
        switch(i) {
            case 0:
        dasm_put(Dst, 1189);
                break;
            case 1:
        dasm_put(Dst, 1195);
                break;
            case 2:
        dasm_put(Dst, 1201);
                break;
            case 3:
        dasm_put(Dst, 1207);
                break;
            default:
        dasm_put(Dst, 1082);
                break;
        }
#line 849 "src/jit/emit_x64.dasc"
        break;
    }
}


static void emit_posix_callargs(MVMThreadContext *tc, MVMJitGraph *jg,
                                MVMJitCallArg args[], MVMint32 num_args,
                                dasm_State **Dst) {
    MVMint32 num_gpr = 0, num_fpr = 0, num_stack = 0, i;
    MVMJitCallArg in_gpr[6], in_fpr[8], *on_stack;
    on_stack = malloc(sizeof(MVMJitCallArg) * (num_args - 6));
    /* divide in gpr, fpr, stack values */
    for (i = 0; i < num_args; i++) {
        switch (args[i].type) {
        case MVM_JIT_INTERP_VAR:
        case MVM_JIT_REG_VAL:
        case MVM_JIT_REG_ADDR:
        case MVM_JIT_STR_IDX:
        case MVM_JIT_LITERAL:
        case MVM_JIT_LITERAL_64:
        case MVM_JIT_LITERAL_PTR:
            if (num_gpr < 6) {
                in_gpr[num_gpr++] = args[i];
            } else {
                on_stack[num_stack++] = args[i];
            }
            break;
        case MVM_JIT_REG_VAL_F:
        case MVM_JIT_LITERAL_F:
            if (num_fpr < 8) {
                in_fpr[num_fpr++] = args[i];
            } else {
                on_stack[num_stack++] = args[i];
            }
            break;
        }
    }
    for (i = 0; i < num_gpr; i++) {
        emit_call_arg(tc, in_gpr[i], i, Dst);
    }
    for (i = 0; i < num_fpr; i++) {
        emit_call_arg(tc, in_fpr[i], i, Dst);
    }
    /* push right-to-left */
    for (i = num_stack - 1; i >= 0; i--) {
        emit_call_arg(tc, on_stack[i], i + 8, Dst); // always a stack arg
    }
    free(on_stack);
}

static void emit_win64_callargs(MVMThreadContext *tc, MVMJitGraph *jg,
                                MVMJitCallArg args[], MVMint32 num_args,
                                dasm_State **Dst) {
    MVMint32 i;
    /* The difficultiy in win64 call args is handled adequately
     * by the addarg macros. */
    for (i = 0; i < num_args; i++) {
        emit_call_arg(tc, args[i], i, Dst);
    }
}

void MVM_jit_emit_call_c(MVMThreadContext *tc, MVMJitGraph *jg,
                         MVMJitCallC * call_spec, dasm_State **Dst) {

    MVM_jit_log(tc, "emit c call <%d args>\n", call_spec->num_args);
    if (call_spec->has_vargs) {
        MVM_exception_throw_adhoc(tc, "JIT can't handle varargs yet");
    }
    //|.if WIN32;
     emit_win64_callargs(tc, jg, call_spec->args, call_spec->num_args, Dst);
    //|.else;
#line 921 "src/jit/emit_x64.dasc"
    //|.endif
    /* Emit the call. I think we should be able to do something smarter than
     * store the constant into the bytecode, like a data segment. But I'm
     * not sure. */
    //| callp call_spec->func_ptr;
    dasm_put(Dst, 152, (unsigned int)((uintptr_t)call_spec->func_ptr), (unsigned int)(((uintptr_t)call_spec->func_ptr)>>32));
#line 926 "src/jit/emit_x64.dasc"
    /* right, now determine what to do with the return value */
    switch(call_spec->rv_mode) {
    case MVM_JIT_RV_VOID:
        break;
    case MVM_JIT_RV_INT:
    case MVM_JIT_RV_PTR:
        //| mov WORK[call_spec->rv_idx], RV;
        dasm_put(Dst, 716, Dt13([call_spec->rv_idx]));
#line 933 "src/jit/emit_x64.dasc"
        break;
    case MVM_JIT_RV_NUM:
        //| movsd qword WORK[call_spec->rv_idx], RVF;
        dasm_put(Dst, 768, Dt13([call_spec->rv_idx]));
#line 936 "src/jit/emit_x64.dasc"
        break;
    case MVM_JIT_RV_DEREF:
        //| mov TMP1, [RV];
        //| mov WORK[call_spec->rv_idx], TMP1;
        dasm_put(Dst, 1213, Dt13([call_spec->rv_idx]));
#line 940 "src/jit/emit_x64.dasc"
        break;
    case MVM_JIT_RV_ADDR:
        /* store local at address */
        //| mov TMP1, WORK[call_spec->rv_idx];
        //| mov [RV], TMP1;
        dasm_put(Dst, 1221, Dt13([call_spec->rv_idx]));
#line 945 "src/jit/emit_x64.dasc"
        break;
    }
}

void MVM_jit_emit_branch(MVMThreadContext *tc, MVMJitGraph *jg,
                         MVMJitBranch * branch, dasm_State **Dst) {
    MVMSpeshIns *ins = branch->ins;
    MVMint32 name = branch->dest.name;
    /* move gc sync point to the front so as to not have
     * awkward dispatching issues */
    //| gc_sync_point;
    dasm_put(Dst, 1229, Dt12(->gc_status), (unsigned int)((uintptr_t)&MVM_gc_enter_from_interrupt), (unsigned int)(((uintptr_t)&MVM_gc_enter_from_interrupt)>>32));
#line 956 "src/jit/emit_x64.dasc"
    if (ins == NULL || ins->info->opcode == MVM_OP_goto) {
        MVM_jit_log(tc, "emit jump to label %d\n", name);
        if (name == MVM_JIT_BRANCH_EXIT) {
            //| jmp ->exit
            dasm_put(Dst, 1262);
#line 960 "src/jit/emit_x64.dasc"
        } else {
            //| jmp =>(name)
            dasm_put(Dst, 1267, (name));
#line 962 "src/jit/emit_x64.dasc"
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
            dasm_put(Dst, 1271, Dt13([reg]), (name));
#line 972 "src/jit/emit_x64.dasc"
            break;
        case MVM_OP_unless_i:
            //| mov rax, WORK[reg];
            //| test rax, rax;
            //| jz =>(name);
            dasm_put(Dst, 1282, Dt13([reg]), (name));
#line 977 "src/jit/emit_x64.dasc"
            break;
        case MVM_OP_ifnonnull:
            //| mov TMP1, WORK[reg];
            //| test TMP1, TMP1;
            //| jz >1;
            //| get_vmnull TMP2;
            //| cmp TMP1, TMP2;
            //| je >1;
            //| jmp =>(name);
            //|1:
            dasm_put(Dst, 1293, Dt13([reg]), Dt12(->instance), Dt6(->VMNull), (name));
#line 987 "src/jit/emit_x64.dasc"
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
    dasm_put(Dst, 259, (label->name));
#line 998 "src/jit/emit_x64.dasc"
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
    dasm_put(Dst, 1325, Dt13([obj]), Dt14(->effective_spesh_slots), DtB([spesh_idx]));
#line 1009 "src/jit/emit_x64.dasc"
    if (op == MVM_OP_sp_guardtype) {
        /* object in queston should be a type object, so it shouldn't
         * be zero, should not be concrete, and the STABLE should be
         * equal to the value in the spesh slot */
        /* check for null */
        //| cmp TMP1, 0;
        //| je >1;
        dasm_put(Dst, 1340);
#line 1016 "src/jit/emit_x64.dasc"
        /* check if type object (not concrete) */
        //| is_type_object TMP1;
        dasm_put(Dst, 1350, Dt7(->header.flags), MVM_CF_TYPE_OBJECT);
#line 1018 "src/jit/emit_x64.dasc"
        /* if zero, this is a concrete object, and we should deopt */
        //| jz >1;
        dasm_put(Dst, 1345);
#line 1020 "src/jit/emit_x64.dasc"
        /* get stable and compare */
        //| cmp TMP2, OBJECT:TMP1->st;
        //| jne >1;
        dasm_put(Dst, 1357, Dt7(->st));
#line 1023 "src/jit/emit_x64.dasc"
        /* we're good, no need to deopt */
    } else if (op == MVM_OP_sp_guardconc) {
        /* object should be a non-null concrete (non-type) object */
        //| cmp TMP1, 0;
        //| je >1;
        dasm_put(Dst, 1340);
#line 1028 "src/jit/emit_x64.dasc"
        /* shouldn't be type object */
        //| is_type_object TMP1;
        //| jnz >1;
        dasm_put(Dst, 1366, Dt7(->header.flags), MVM_CF_TYPE_OBJECT);
#line 1031 "src/jit/emit_x64.dasc"
        /* should have our stable */
        //| cmp TMP2, OBJECT:TMP1->st;
        //| jne >1;
        dasm_put(Dst, 1357, Dt7(->st));
#line 1034 "src/jit/emit_x64.dasc"
    }
    /* if we're here, we didn't jump to deopt, so skip it */
    //| jmp >2;
    //|1:
    dasm_put(Dst, 963);
#line 1038 "src/jit/emit_x64.dasc"
    /* emit deopt */
    //| mov ARG1, TC;
    //| mov ARG2, guard->deopt_offset;
    //| mov ARG3, guard->deopt_target;
    //| callp &MVM_spesh_deopt_one_direct;
    dasm_put(Dst, 1377, guard->deopt_offset, guard->deopt_target, (unsigned int)((uintptr_t)&MVM_spesh_deopt_one_direct), (unsigned int)(((uintptr_t)&MVM_spesh_deopt_one_direct)>>32));
#line 1043 "src/jit/emit_x64.dasc"
    /* tell jit driver we're deopting */
    //| mov RV, MVM_JIT_CTRL_DEOPT
    //| jmp ->out;
    //|2:
    dasm_put(Dst, 1407, MVM_JIT_CTRL_DEOPT);
#line 1047 "src/jit/emit_x64.dasc"
}

void MVM_jit_emit_invoke(MVMThreadContext *tc, MVMJitGraph *jg, MVMJitInvoke *invoke,
                         dasm_State **Dst) {
    MVMint16 i;
    /* setup the callsite */
    //| mov ARG1, TC;
    //| mov ARG2, CU;
    //| mov ARG3, invoke->callsite_idx;
    //| callp &MVM_args_prepare;
    //| mov TMP6, RV; // store callsite in tmp6, which we don't use until the end
    dasm_put(Dst, 1418, invoke->callsite_idx, (unsigned int)((uintptr_t)&MVM_args_prepare), (unsigned int)(((uintptr_t)&MVM_args_prepare)>>32));
#line 1058 "src/jit/emit_x64.dasc"
    /* Store arguments in the buffer. I use TMP5 as it never conflicts
     * with argument passing (like TMP6, but unlike other TMP regs) */
    //| mov TMP5, FRAME->args;
    dasm_put(Dst, 1451, Dt14(->args));
#line 1061 "src/jit/emit_x64.dasc"
    for (i = 0;  i < invoke->arg_count; i++) {
        MVMSpeshIns *ins = invoke->arg_ins[i];
        switch (ins->info->opcode) {
        case MVM_OP_arg_i:
        case MVM_OP_arg_s:
        case MVM_OP_arg_n:
        case MVM_OP_arg_o: {
            MVMint16 dst = ins->operands[0].lit_i16;
            MVMint16 src = ins->operands[1].reg.orig;
            //| mov TMP4, WORK[src];
            //| mov REGISTER:TMP5[dst], TMP4;
            dasm_put(Dst, 1458, Dt13([src]), Dt1([dst]));
#line 1072 "src/jit/emit_x64.dasc"
            break;
        }
        case MVM_OP_argconst_n:
        case MVM_OP_argconst_i: {
            MVMint16 dst = ins->operands[0].lit_i16;
            MVMint64 val = ins->operands[1].lit_i64;
            //| mov64 TMP4, val;
            //| mov REGISTER:TMP5[dst], TMP4;
            dasm_put(Dst, 1467, (unsigned int)(val), (unsigned int)((val)>>32), Dt1([dst]));
#line 1080 "src/jit/emit_x64.dasc"
            break;
        }
        case MVM_OP_argconst_s: {
            MVMint16 dst = ins->operands[0].lit_i16;
            MVMint32 idx = ins->operands[1].lit_str_idx;
            //| get_string TMP4, idx;
            //| mov REGISTER:TMP5[dst], TMP4;
            dasm_put(Dst, 1476, Dt15(->body.strings), DtA([idx]), Dt1([dst]));
#line 1087 "src/jit/emit_x64.dasc"
            break;
        }
        default:
            MVM_exception_throw_adhoc(tc, "JIT invoke: Can't add arg <%s>",
                                      ins->info->name);
        }
    }

    /* Setup the frame for returning to our current position */
    if (sizeof(MVMReturnType) == 4) {
        //| mov dword FRAME->return_type, invoke->return_type;
        dasm_put(Dst, 1489, Dt14(->return_type), invoke->return_type);
#line 1098 "src/jit/emit_x64.dasc"
    } else {
        MVM_exception_throw_adhoc(tc, "JIT: MVMReturnType has unexpected size");
    }
    /* The register for our return value */
    if (invoke->return_type == MVM_RETURN_VOID) {
        //| mov aword FRAME->return_value, NULL;
        dasm_put(Dst, 1497, Dt14(->return_value), NULL);
#line 1104 "src/jit/emit_x64.dasc"
    } else {
        //| lea TMP2, WORK[invoke->return_register];
        //| mov aword FRAME->return_value, TMP2;
        dasm_put(Dst, 1505, Dt13([invoke->return_register]), Dt14(->return_value));
#line 1107 "src/jit/emit_x64.dasc"
    }
    /* The return address for the interpreter */
    //| get_cur_op TMP2;
    //| mov aword FRAME->return_address, TMP2;
    dasm_put(Dst, 1516, Dt12(->interp_cur_op), Dt14(->return_address));
#line 1111 "src/jit/emit_x64.dasc"

    /* The re-entry label for the JIT, so that we continue in the next BB */
    //| lea TMP2, [=>(invoke->reentry_label)];
    //| mov aword FRAME->jit_entry_label, TMP2;
    dasm_put(Dst, 1530, (invoke->reentry_label), Dt14(->jit_entry_label));
#line 1115 "src/jit/emit_x64.dasc"

    /* if we're not fast, then we should get the code from multi resolution */
    if (!invoke->is_fast) {
        /* first, save callsite and args */
        //| push TMP5; // args
        //| push TMP6; // callsite
        dasm_put(Dst, 1541);
#line 1121 "src/jit/emit_x64.dasc"
        /* setup call MVM_frame_multi_ok(tc, code, &cur_callsite, args); */
        //| mov ARG1, TC;
        //| mov ARG2, WORK[invoke->code_register]; // code object
        //| mov ARG3, rsp; // basically, [rsp] == TMP6 == &cur_callsite
        //| mov ARG4, TMP5; // args
        //| callp &MVM_frame_find_invokee_multi_ok;
        dasm_put(Dst, 1546, Dt13([invoke->code_register]), (unsigned int)((uintptr_t)&MVM_frame_find_invokee_multi_ok), (unsigned int)(((uintptr_t)&MVM_frame_find_invokee_multi_ok)>>32));
#line 1127 "src/jit/emit_x64.dasc"
        /* restore callsite, args, RV now holds code object */
        //| pop TMP6; // callsite
        //| pop TMP5; // args
        dasm_put(Dst, 1578);
#line 1130 "src/jit/emit_x64.dasc"
        /* setup args for call to invoke(tc, code, cur_callsite, args) */
        //| mov ARG1, TC;
        //| mov ARG2, RV;   // code object
        //| mov ARG3, TMP6; // cur_callsite
        //| mov ARG4, TMP5; // nb - this could be pop ARG4, but that would be confusing
        dasm_put(Dst, 1583);
#line 1135 "src/jit/emit_x64.dasc"
        /* get the actual function */
        //| mov FUNCTION, OBJECT:RV->st;
        //| mov FUNCTION, STABLE:FUNCTION->invoke;
        //| callr FUNCTION;
        dasm_put(Dst, 1597, Dt7(->st), Dt9(->invoke));
#line 1139 "src/jit/emit_x64.dasc"
    } else {
        /* call MVM_frame_invoke_code */
        //| mov ARG1, TC;
        //| mov ARG2, WORK[invoke->code_register];
        //| mov ARG3, TMP6; // this is the callsite object
        //| mov ARG4, invoke->spesh_cand;
        //| callp &MVM_frame_invoke_code;
        dasm_put(Dst, 1619, Dt13([invoke->code_register]), invoke->spesh_cand, (unsigned int)((uintptr_t)&MVM_frame_invoke_code), (unsigned int)(((uintptr_t)&MVM_frame_invoke_code)>>32));
#line 1146 "src/jit/emit_x64.dasc"
    }
    /* Almost done. jump out into the interprete */
    //| mov RV, 1;
    //| jmp ->out;
    dasm_put(Dst, 1652);
#line 1150 "src/jit/emit_x64.dasc"
}


void MVM_jit_emit_control(MVMThreadContext *tc, MVMJitGraph *jg,
                          MVMJitControl *ctrl, dasm_State **Dst) {
    if (ctrl->type == MVM_JIT_CONTROL_INVOKISH) {
        //| cmp FRAME, TC->cur_frame;
        //| je >1;
        //| lea TMP1, [>1];
        //| mov aword FRAME->jit_entry_label, TMP1;
        //| mov RV, 1;
        //| jmp ->out;
        //|1:
        dasm_put(Dst, 1664, Dt12(->cur_frame), Dt14(->jit_entry_label));
#line 1163 "src/jit/emit_x64.dasc"
    } else if (ctrl->type == MVM_JIT_CONTROL_BREAKPOINT) {
        //| int 3;
        dasm_put(Dst, 1697);
#line 1165 "src/jit/emit_x64.dasc"
    } else {
        MVM_exception_throw_adhoc(tc, "Unknown contol code: <%s>",
                                  ctrl->ins->info->name);
    }
}
