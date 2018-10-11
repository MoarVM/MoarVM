#include "moar.h"

void MVM_vectorized_apply_op(MVMThreadContext *tc, MVMObject *a, MVMObject *b, MVMObject *target, MVMuint16 opcode, MVMuint8 is_cross, MVMuint8 bitsize) {
    MVMArrayBody *left, *right, *output;
    MVMuint64 elems = 0;

    if (REPR(a)->ID != MVM_REPR_ID_VMArray || REPR(b)->ID != MVM_REPR_ID_VMArray || REPR(target)->ID != MVM_REPR_ID_VMArray) {
        MVM_exception_throw_adhoc(tc, "vectorized apply requires vmarray input data");
    }
    if (!IS_CONCRETE(a) || !IS_CONCRETE(b) || !IS_CONCRETE(target)) {
        MVM_exception_throw_adhoc(tc, "vectorized apply requires all three arrays to be concrete instances");
    }
    if (bitsize != 64) {
        MVM_exception_throw_adhoc(tc, "vectorized apply on bitsizes != 64 NYI (got: %d)", bitsize);
    }

    left   = &((MVMArray*)a)->body;
    right  = &((MVMArray*)b)->body;
    output = &((MVMArray*)target)->body;

    /* For simplicity, put the bigger one on the left */
    if (right->elems > left->elems) {
        MVMArrayBody *tmp = right;
        right = left;
        left = tmp;
    }
    elems = left->elems;

    if (is_cross && right->elems != 1) {
        MVM_exception_throw_adhoc(tc, "Vector-apply for cross operator with the smaller array having more than one elmeent NYI (got %d)", right->elems);
    }

    MVM_repr_pos_set_elems(tc, target, elems);

    switch (opcode) {
        /* Simple floating point arithmetic */
        case MVM_OP_add_n: {
            MVMuint64 index;
            MVMnum64 *a_slots = left->slots.n64 + left->start;
            MVMnum64 *b_slots = right->slots.n64 + right->start;
            MVMnum64 *c_slots = output->slots.n64 + output->start;
            if (is_cross) {
                for (index = 0; index < elems; index++) {
                    c_slots[index] = a_slots[index] + b_slots[0];
                }
            }
            else {
                for (index = 0; index < elems; index++) {
                    c_slots[index] = a_slots[index] + b_slots[index];
                }
            }
            break;
        }
        case MVM_OP_mul_n: {
            MVMuint64 index;
            MVMnum64 *a_slots = left->slots.n64 + left->start;
            MVMnum64 *b_slots = right->slots.n64 + right->start;
            MVMnum64 *c_slots = output->slots.n64 + output->start;
            if (is_cross) {
                for (index = 0; index < elems; index++) {
                    c_slots[index] = a_slots[index] * b_slots[0];
                }
            }
            else {
                for (index = 0; index < elems; index++) {
                    c_slots[index] = a_slots[index] * b_slots[index];
                }
            }
            break;
        }
        case MVM_OP_sub_n: {
            MVMuint64 index;
            MVMnum64 *a_slots = left->slots.n64 + left->start;
            MVMnum64 *b_slots = right->slots.n64 + right->start;
            MVMnum64 *c_slots = output->slots.n64 + output->start;
            if (is_cross) {
                for (index = 0; index < elems; index++) {
                    c_slots[index] = a_slots[index] - b_slots[0];
                }
            }
            else {
                for (index = 0; index < elems; index++) {
                    c_slots[index] = a_slots[index] - b_slots[index];
                }
            }
            break;
        }

        /* Simple integer arithmetic */
        case MVM_OP_add_i: {
            MVMuint64 index;
            MVMint64 *a_slots = left->slots.i64 + left->start;
            MVMint64 *b_slots = right->slots.i64 + right->start;
            MVMint64 *c_slots = output->slots.i64 + output->start;
            if (is_cross) {
                for (index = 0; index < elems; index++) {
                    c_slots[index] = a_slots[index] + b_slots[0];
                }
            }
            else {
                for (index = 0; index < elems; index++) {
                    c_slots[index] = a_slots[index] + b_slots[index];
                }
            }
            break;
        }
        case MVM_OP_mul_i: {
            MVMuint64 index;
            MVMint64 *a_slots = left->slots.i64 + left->start;
            MVMint64 *b_slots = right->slots.i64 + right->start;
            MVMint64 *c_slots = output->slots.i64 + output->start;
            if (is_cross) {
                for (index = 0; index < elems; index++) {
                    c_slots[index] = a_slots[index] * b_slots[0];
                }
            }
            else {
                for (index = 0; index < elems; index++) {
                    c_slots[index] = a_slots[index] * b_slots[index];
                }
            }
            break;
        }
        case MVM_OP_sub_i: {
            MVMuint64 index;
            MVMint64 *a_slots = left->slots.i64 + left->start;
            MVMint64 *b_slots = right->slots.i64 + right->start;
            MVMint64 *c_slots = output->slots.i64 + output->start;
            if (is_cross) {
                for (index = 0; index < elems; index++) {
                    c_slots[index] = a_slots[index] - b_slots[0];
                }
            }
            else {
                for (index = 0; index < elems; index++) {
                    c_slots[index] = a_slots[index] - b_slots[index];
                }
            }
            break;
        }
        default:
            MVM_exception_throw_adhoc(tc, "unsupported opcode in vectorized_apply: %s", MVM_op_get_op(opcode)->name);
    }
}
