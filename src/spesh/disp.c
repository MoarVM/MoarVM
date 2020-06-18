#include "moar.h"

/* Create op info for a dispatch instruction, so that during specialization we
 * can pretend it's not varargs. */
MVMOpInfo * MVM_spesh_disp_create_dispatch_op_info(MVMThreadContext *tc, MVMSpeshGraph *g,
        MVMOpInfo *base_info, MVMCallsite *callsite) {
    /* In general, ops support up to an operand limit; in the case that there are more,
     * we'd overrun the buffer. We thus allocate more. */
    MVMuint32 total_ops = base_info->num_operands + callsite->flag_count;
    size_t total_size = sizeof(MVMOpInfo) + (total_ops > MVM_MAX_OPERANDS
            ? total_ops - MVM_MAX_OPERANDS
            : 0);
    MVMOpInfo *dispatch_info = MVM_spesh_alloc(tc, g, total_size);

    /* Populate based on the original operation. */
    memcpy(dispatch_info, base_info, sizeof(MVMOpInfo));

    /* Tweak the operand count and set up new operand info based on the callsite. */
    dispatch_info->num_operands += callsite->flag_count;
    MVMuint16 operand_index = base_info->opcode == MVM_OP_dispatch_v ? 2 : 3;
    MVMuint16 flag_index;
    for (flag_index = 0; flag_index < callsite->flag_count; operand_index++, flag_index++) {
        MVMCallsiteFlags flag = callsite->arg_flags[flag_index];
        if (flag & MVM_CALLSITE_ARG_OBJ) {
            dispatch_info->operands[operand_index] = MVM_operand_obj;
        }
        else if (flag & MVM_CALLSITE_ARG_INT) {
            dispatch_info->operands[operand_index] = MVM_operand_int64;
        }
        else if (flag & MVM_CALLSITE_ARG_NUM) {
            dispatch_info->operands[operand_index] = MVM_operand_num64;
        }
        else if (flag & MVM_CALLSITE_ARG_STR) {
            dispatch_info->operands[operand_index] = MVM_operand_str;
        }
        dispatch_info->operands[operand_index] |= MVM_operand_read_reg;
    }

    return dispatch_info;
}
