#include "moar.h"

static MVMCallsite no_arg_callsite = { NULL, 0, 0, 0 };

void MVM_continuation_reset(MVMThreadContext *tc, MVMObject *tag, MVMObject *code, MVMRegister *res_reg) {
    /* XXX TODO: Save tag. */

    /* Run the passed code. */
    code = MVM_frame_find_invokee(tc, code, NULL);
    MVM_args_setup_thunk(tc, res_reg, MVM_RETURN_OBJ, &no_arg_callsite);
    /* XXX Add special_return to clear the tag. */
    STABLE(code)->invoke(tc, code, &no_arg_callsite, tc->cur_frame->args);
}
