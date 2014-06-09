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
#define JIT_FRAME_SIZE 16

//|.arch x64
#if DASM_VERSION != 10300
#error "Version mismatch between DynASM and included encoding engine"
#endif
#line 12 "src/jit/x86_64.dasc"
//|.actionlist actions
static const unsigned char actions[50] = {
  85,72,137,229,72,129,252,236,239,255,72,137,125,252,248,72,137,117,252,240,
  72,139,158,233,255,72,129,196,239,93,195,255,72,187,237,237,252,255,211,255,
  72,139,93,252,240,72,139,155,233,255
};

#line 13 "src/jit/x86_64.dasc"

/* The 'work' registers that MVM supplies will be referenced a lot.
 * So the rbx register will be set up to hold the work space base. */
//|.define WORK, rbx

const unsigned char * MVM_jit_actions(void) {
    return actions;
}

/* A function prologue is always the same in x86 / x64, becuase
 * we do not provide variable arguments, instead arguments are provided
 * via a frame. All JIT entry points receive prologue */
void MVM_jit_emit_prologue(MVMThreadContext *tc, dasm_State **Dst) {
    /* set up our C call frame, i.e. allocate stack space*/
    //| push rbp
    //| mov rbp, rsp
    //| sub rsp, JIT_FRAME_SIZE
    dasm_put(Dst, 0, JIT_FRAME_SIZE);
#line 30 "src/jit/x86_64.dasc"

    //| mov [rbp-8], rdi              // thread context
    //| mov [rbp-16], rsi             // mvm frame
    //| mov WORK, [rsi + OFFSET_WORK] // work register base
    dasm_put(Dst, 10, OFFSET_WORK);
#line 34 "src/jit/x86_64.dasc"
}

/* And a function epilogue is also always the same */
void MVM_jit_emit_epilogue(MVMThreadContext *tc, dasm_State **Dst) {
    //| add rsp, JIT_FRAME_SIZE
    //| pop rbp
    //| ret
    dasm_put(Dst, 25, JIT_FRAME_SIZE);
#line 41 "src/jit/x86_64.dasc"
}

void MVM_jit_emit_c_call(MVMThreadContext *tc, MVMJitCallC * call_spec, dasm_State **Dst) {
    int i;
    if (call_spec->has_vargs) {
        MVM_exception_throw_adhoc(tc, "JIT can't handle varargs yet");
    }
    /* first, add arguments */
    for (i = 0; i < call_spec->num_args; i++) {
        /* just kidding */
        MVM_exception_throw_adhoc(tc, "JIT can't handle arguments yet");
    }
    /* Set up and emit the call. I re-use the work pointer register
     * because it has to be restored anyway and does not normally
     * participate in argument passing, so it is 'free'. Also, rax
     * is unavailable as it has to hold the number of arguments on the
     * stack. I've moved this below the argument setup as the work
     * register pointer might be needed there. */
    //| mov64 WORK, (uintptr_t)call_spec->func_ptr
    //| call WORK
    dasm_put(Dst, 32, (unsigned int)((uintptr_t)call_spec->func_ptr), (unsigned int)(((uintptr_t)call_spec->func_ptr)>>32));
#line 61 "src/jit/x86_64.dasc"
    /* Restore the work register pointer */
    //| mov WORK, [rbp-16]             // load the mvm frame
    //| mov WORK, [WORK + OFFSET_WORK] // load the work register pointer
    dasm_put(Dst, 40, OFFSET_WORK);
#line 64 "src/jit/x86_64.dasc"
}
