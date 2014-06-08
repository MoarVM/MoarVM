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

#define OFFSET_ENV offsetof(MVMFrame, env)
#define OFFSET_ARGS offsetof(MVMFrame, args)
#define OFFSET_WORK offsetof(MVMFrame, work)

//|.arch x64
#if DASM_VERSION != 10300
#error "Version mismatch between DynASM and included encoding engine"
#endif
#line 10 "src/jit/x86_64.dasc"
//|.actionlist actions
static const unsigned char actions[24] = {
  85,72,137,229,72,137,124,36,252,248,72,137,116,36,252,240,72,139,158,233,
  255,93,195,255
};

#line 11 "src/jit/x86_64.dasc"

/* The 'work' registers that MVM supplies will be referenced a lot.
 * So the rbx register will be set up to hold the work space base. */
//|.define WORK, rbx

/* A function prologue is always the same in x86 / x64, becuase
 * we do not provide variable arguments, instead arguments are provided
 * via a frame. All JIT entry points receive prologue */
void MVM_jit_gen_prologue(dasm_State **Dst) {
    //| push rbp
    //| mov rbp, rsp
    //| mov [rsp-8], rdi // thread context
    //| mov [rsp-16], rsi // frame
    //| mov WORK, [rsi + OFFSET_WORK]
    dasm_put(Dst, 0, OFFSET_WORK);
#line 25 "src/jit/x86_64.dasc"
}

/* And a function epilogue is also always the same */
void MVM_jit_gen_epilogue(dasm_State **Dst) {
    //| pop rbp
    //| ret
    dasm_put(Dst, 21);
#line 31 "src/jit/x86_64.dasc"
}

