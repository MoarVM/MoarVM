#include "moarvm.h"

MVMint64 MVM_coerce_istrue_s(MVMThreadContext *tc, MVMString *str) {
    return str == NULL || !IS_CONCRETE(str) || NUM_GRAPHS(str) == 0 || (NUM_GRAPHS(str) == 1 && MVM_string_get_codepoint_at_nocheck(tc, str, 0) == 48) ? 0 : 1;
}

MVMint64 MVM_coerce_istrue(MVMThreadContext *tc, MVMObject *obj) {
    MVMBoolificationSpec *bs;
    MVMint64 result = 0;
    if (obj == NULL)
        return 0;
    bs = obj->st->boolification_spec;
    switch (bs == NULL ? MVM_BOOL_MODE_NOT_TYPE_OBJECT : bs->mode) {
        case MVM_BOOL_MODE_UNBOX_INT:
            result = !IS_CONCRETE(obj) || REPR(obj)->box_funcs->get_int(tc, STABLE(obj), obj, OBJECT_BODY(obj)) == 0 ? 0 : 1;
            break;
        case MVM_BOOL_MODE_UNBOX_NUM:
            result = !IS_CONCRETE(obj) || REPR(obj)->box_funcs->get_num(tc, STABLE(obj), obj, OBJECT_BODY(obj)) == 0.0 ? 0 : 1;
            break;
        case MVM_BOOL_MODE_UNBOX_STR_NOT_EMPTY:
            result = !IS_CONCRETE(obj) || NUM_GRAPHS(REPR(obj)->box_funcs->get_str(tc, STABLE(obj), obj, OBJECT_BODY(obj))) == 0 ? 0 : 1;
            break;
        case MVM_BOOL_MODE_UNBOX_STR_NOT_EMPTY_OR_ZERO: {
            MVMString *str;
            if (!IS_CONCRETE(obj)) {
                result = 0;
                break;
            }
            str = REPR(obj)->box_funcs->get_str(tc, STABLE(obj), obj, OBJECT_BODY(obj));
            result = MVM_coerce_istrue_s(tc, str);
            break;
        }
        case MVM_BOOL_MODE_NOT_TYPE_OBJECT:
            result = !IS_CONCRETE(obj) ? 0 : 1;
            break;
        case MVM_BOOL_MODE_ITER: {
            MVMIter *iter = (MVMIter *)obj;
            if (!IS_CONCRETE(obj)) {
                result = 0;
                break;
            }
            switch (iter->body.mode) {
                case MVM_ITER_MODE_ARRAY:
                    result = iter->body.array_state.index + 1 < iter->body.array_state.limit ? 1 : 0;
                    break;
                case MVM_ITER_MODE_HASH:
                    result = iter->body.hash_state.next != NULL ? 1 : 0;
                    break;
                default:
                    MVM_exception_throw_adhoc(tc, "Invalid iteration mode used");
            }
            break;
        }
        default:
            MVM_exception_throw_adhoc(tc, "Invalid boolification spec mode used");
    }
    return result;
}

MVMString * MVM_coerce_o_s(MVMThreadContext *tc, MVMObject *obj) {
    return MVM_coerce_smart_stringify(tc, obj);
}

MVMString * MVM_coerce_smart_stringify(MVMThreadContext *tc, MVMObject *obj) {
    if (!obj || !IS_CONCRETE(obj))
        return MVM_string_ascii_decode(tc, tc->instance->VMString, "", 0);
    else {
        MVMStorageSpec ss = REPR(obj)->get_storage_spec(tc, STABLE(obj));
        if (REPR(obj)->ID == MVM_REPR_ID_MVMString)
            return (MVMString *)obj;
        else if (ss.can_box & MVM_STORAGE_SPEC_CAN_BOX_STR)
            return REPR(obj)->box_funcs->get_str(tc, STABLE(obj), obj, OBJECT_BODY(obj));
        else if (ss.can_box & MVM_STORAGE_SPEC_CAN_BOX_INT) {
            char buffer[25];
            sprintf(buffer, "%lld", REPR(obj)->box_funcs->get_int(tc, STABLE(obj), obj, OBJECT_BODY(obj)));
            return MVM_string_ascii_decode(tc, tc->instance->VMString, buffer, strlen(buffer));
        }
        else if (ss.can_box & MVM_STORAGE_SPEC_CAN_BOX_NUM) {
            char buf[16];
            int i;
            sprintf(buf, "%-15f", REPR(obj)->box_funcs->get_num(tc, STABLE(obj), obj, OBJECT_BODY(obj)));
            i = strlen(buf);
            while (i > 1 && (buf[--i] == '0' || buf[i] == '.' || buf[i] == ' '))
                buf[i] = '\0';
            return MVM_string_ascii_decode(tc, tc->instance->VMString, buf, strlen(buf));
        }
        else
            MVM_exception_throw_adhoc(tc, "cannot stringify this");
    }
    return NULL;
}
