#include "moarvm.h"

#if defined(_MSC_VER)
#define strtoll _strtoi64
#endif

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

MVMString * MVM_coerce_i_s(MVMThreadContext *tc, MVMint64 i) {
    char buffer[25];
    sprintf(buffer, "%lld", i);
    return MVM_string_ascii_decode(tc, tc->instance->VMString, buffer, strlen(buffer));
}

MVMString * MVM_coerce_n_s(MVMThreadContext *tc, MVMnum64 n) {
    char buf[20];
    int i;
    sprintf(buf, "%-15f", n);
    if (strstr(buf, ".")) {
        i = strlen(buf);
        while (i > 1 && (buf[--i] == '0' || buf[i] == ' '))
            buf[i] = '\0';
        if (buf[i] == '.')
            buf[i] = '\0';
    }
    return MVM_string_ascii_decode(tc, tc->instance->VMString, buf, strlen(buf));
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
        else if (ss.can_box & MVM_STORAGE_SPEC_CAN_BOX_INT)
            return MVM_coerce_i_s(tc, REPR(obj)->box_funcs->get_int(tc, STABLE(obj), obj, OBJECT_BODY(obj)));
        else if (ss.can_box & MVM_STORAGE_SPEC_CAN_BOX_NUM)
            return MVM_coerce_n_s(tc, REPR(obj)->box_funcs->get_num(tc, STABLE(obj), obj, OBJECT_BODY(obj)));
        else
            MVM_exception_throw_adhoc(tc, "cannot stringify this");
    }
    return NULL;
}

MVMint64 MVM_coerce_s_i(MVMThreadContext *tc, MVMString *s) {
    char     *enc = MVM_string_ascii_encode(tc, s, NULL);
    MVMint64  i   = strtoll(enc, NULL, 10);
    free(enc);
    return i;
}

MVMnum64 MVM_coerce_s_n(MVMThreadContext *tc, MVMString *s) {
    char     *enc = MVM_string_ascii_encode(tc, s, NULL);
    MVMnum64  n   = atof(enc);
    free(enc);
    return n;
}
