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

//|.arch x64
#if DASM_VERSION != 10300
#error "Version mismatch between DynASM and included encoding engine"
#endif
#line 6 "src/jit/x86_64.dasc"
//|.actionlist actions
static const unsigned char actions[9] = {
  72,199,192,3,0,0,0,195,255
};

#line 7 "src/jit/x86_64.dasc"

MVMuint32 MVM_can_jit(MVMSpeshGraph *graph) {
    // can't jit anything right now
    if (graph == NULL)
	return 1;
    return 0;
}

void MVM_jit_generate(dasm_State **Dst, MVMSpeshGraph *graph) {
    //| mov rax, 3
    //| ret
    dasm_put(Dst, 0);
#line 18 "src/jit/x86_64.dasc"
}
