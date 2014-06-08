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

#define OFFSET_ENV offsetof(MVMFrame, env)
#define OFFSET_ARGS offsetof(MVMFrame, args)
#define OFFSET_WORK offsetof(MVMFrame, work)


//|.arch x64
#if DASM_VERSION != 10300
#error "Version mismatch between DynASM and included encoding engine"
#endif
#line 12 "src/jit/x86_64.dasc"
//|.actionlist actions
static const unsigned char actions[54] = {
  85,72,137,229,72,137,124,36,252,248,72,137,116,36,252,240,72,139,158,233,
  255,93,195,255,72,131,252,236,16,72,187,237,237,255,252,255,211,255,72,131,
  196,16,255,72,139,92,36,252,240,72,139,155,233,255
};

#line 13 "src/jit/x86_64.dasc"

/* The 'work' registers that MVM supplies will be referenced a lot.
 * So the rbx register will be set up to hold the work space base. */
//|.define WORK, rbx


/* A function prologue is always the same in x86 / x64, becuase
 * we do not provide variable arguments, instead arguments are provided
 * via a frame. All JIT entry points receive prologue */
void MVM_jit_emit_prologue(MVMThreadContext *tc, dasm_State **Dst) {
    //| push rbp
    //| mov rbp, rsp
    //| mov [rsp-8], rdi              // thread context
    //| mov [rsp-16], rsi             // frame
    //| mov WORK, [rsi + OFFSET_WORK] // work register base
    dasm_put(Dst, 0, OFFSET_WORK);
#line 28 "src/jit/x86_64.dasc"
}

/* And a function epilogue is also always the same */
void MVM_jit_emit_epilogue(MVMThreadContext *tc, dasm_State **Dst) {
    //| pop rbp
    //| ret
    dasm_put(Dst, 21);
#line 34 "src/jit/x86_64.dasc"
}

void MVM_jit_emit_c_call(MVMThreadContext *tc, MVMJitCCall * call_spec, dasm_State **Dst) {
    int i;
    if (call_spec->has_vargs) {
        MVM_exception_throw_adhoc(tc, "JIT can't handle varargs yet");
    }   
    /* Set up for the call */
    //| sub rsp, 16 
    //| mov64 rbx, (uintptr_t)call_spec->func_ptr 
    dasm_put(Dst, 24, (unsigned int)((uintptr_t)call_spec->func_ptr), (unsigned int)(((uintptr_t)call_spec->func_ptr)>>32));
#line 44 "src/jit/x86_64.dasc"
    /* now add arguments */
    for (i = 0; i < call_spec->num_args; i++) {
        /* just kidding */
        MVM_exception_throw_adhoc(tc, "JIT can't handle arguments yet");
    }
    /* emit a call */
    //| call rbx
    dasm_put(Dst, 34);
#line 51 "src/jit/x86_64.dasc"
    /* restore our frame */
    //| add rsp, 16
    dasm_put(Dst, 38);
#line 53 "src/jit/x86_64.dasc"
    /* and our work register pointer */
    //| mov WORK, [rsp-16]
    //| mov WORK, [WORK + OFFSET_WORK]
    dasm_put(Dst, 43, OFFSET_WORK);
#line 56 "src/jit/x86_64.dasc"
}
