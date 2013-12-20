#include "moar.h"

#if defined(_MSC_VER)
#define strtoll _strtoi64
#define snprintf _snprintf
#endif

/* Dummy, invocant-arg callsite. */
static MVMCallsiteEntry obj_arg_flags[] = { MVM_CALLSITE_ARG_OBJ };
static MVMCallsite     inv_arg_callsite = { obj_arg_flags, 1, 1, 0 };

/* Special return structure for boolification handling. */
typedef struct {
    MVMuint8    *true_addr;
    MVMuint8    *false_addr;
    MVMuint8     flip;
    MVMRegister  res_reg;
} BoolMethReturnData;

MVMint64 MVM_coerce_istrue_s(MVMThreadContext *tc, MVMString *str) {
    return str == NULL || !IS_CONCRETE(str) || NUM_GRAPHS(str) == 0 || (NUM_GRAPHS(str) == 1 && MVM_string_get_codepoint_at_nocheck(tc, str, 0) == 48) ? 0 : 1;
}

/* Tries to do the boolification. It may be that a method call is needed. In
 * this case, a return hook is set up to handle doing the right thing. The
 * result register to put the result in should be indicated in res_reg, or
 * alternatively the true/false addresses to set the PC to should be set.
 * In the register case, expects that the current PC is already at the
 * next instruction before this is called. */
void boolify_return(MVMThreadContext *tc, void *sr_data);
void flip_return(MVMThreadContext *tc, void *sr_data);
void MVM_coerce_istrue(MVMThreadContext *tc, MVMObject *obj, MVMRegister *res_reg,
        MVMuint8 *true_addr, MVMuint8 *false_addr, MVMuint8 flip) {
    MVMint64 result;
    if (obj == NULL) {
        result = 0;
    }
    else {
        MVMBoolificationSpec *bs = obj->st->boolification_spec;
        switch (bs == NULL ? MVM_BOOL_MODE_NOT_TYPE_OBJECT : bs->mode) {
            case MVM_BOOL_MODE_CALL_METHOD:
                if (res_reg) {
                    /* We need to do the invocation, and set this register
                     * the result. Then we just do the call. For the flip
                     * case, just set up special return handler to flip
                     * the register. */
                    MVMObject *code = MVM_frame_find_invokee(tc, bs->method, NULL);
                    tc->cur_frame->return_value   = res_reg;
                    tc->cur_frame->return_type    = MVM_RETURN_INT;
                    tc->cur_frame->return_address = *(tc->interp_cur_op);
                    tc->cur_frame->args[0].o = obj;
                    if (flip) {
                        tc->cur_frame->special_return      = flip_return;
                        tc->cur_frame->special_return_data = res_reg;
                    }
                    STABLE(code)->invoke(tc, code, &inv_arg_callsite, tc->cur_frame->args);
                }
                else {
                    /* Need to set up special return hook. */
                    MVMObject *code = MVM_frame_find_invokee(tc, bs->method, NULL);
                    BoolMethReturnData *data = malloc(sizeof(BoolMethReturnData));
                    data->true_addr  = true_addr;
                    data->false_addr = false_addr;
                    data->flip       = flip;
                    tc->cur_frame->special_return      = boolify_return;
                    tc->cur_frame->special_return_data = data;
                    tc->cur_frame->return_value        = &data->res_reg;
                    tc->cur_frame->return_type         = MVM_RETURN_INT;
                    tc->cur_frame->return_address      = *(tc->interp_cur_op);
                    tc->cur_frame->args[0].o = obj;
                    STABLE(code)->invoke(tc, code, &inv_arg_callsite, tc->cur_frame->args);
                    return;
                }
                break;
            case MVM_BOOL_MODE_UNBOX_INT:
                result = !IS_CONCRETE(obj) || REPR(obj)->box_funcs.get_int(tc, STABLE(obj), obj, OBJECT_BODY(obj)) == 0 ? 0 : 1;
                break;
            case MVM_BOOL_MODE_UNBOX_NUM:
                result = !IS_CONCRETE(obj) || REPR(obj)->box_funcs.get_num(tc, STABLE(obj), obj, OBJECT_BODY(obj)) == 0.0 ? 0 : 1;
                break;
            case MVM_BOOL_MODE_UNBOX_STR_NOT_EMPTY:
                result = !IS_CONCRETE(obj) || NUM_GRAPHS(REPR(obj)->box_funcs.get_str(tc, STABLE(obj), obj, OBJECT_BODY(obj))) == 0 ? 0 : 1;
                break;
            case MVM_BOOL_MODE_UNBOX_STR_NOT_EMPTY_OR_ZERO: {
                MVMString *str;
                if (!IS_CONCRETE(obj)) {
                    result = 0;
                    break;
                }
                str = REPR(obj)->box_funcs.get_str(tc, STABLE(obj), obj, OBJECT_BODY(obj));
                result = MVM_coerce_istrue_s(tc, str);
                break;
            }
            case MVM_BOOL_MODE_NOT_TYPE_OBJECT:
                result = !IS_CONCRETE(obj) ? 0 : 1;
                break;
            case MVM_BOOL_MODE_BIGINT:
                result = IS_CONCRETE(obj) ? MVM_bigint_bool(tc, obj) : 0;
                break;
            case MVM_BOOL_MODE_ITER:
                result = IS_CONCRETE(obj) ? MVM_iter_istrue(tc, (MVMIter *)obj) : 0;
                break;
            case MVM_BOOL_MODE_HAS_ELEMS:
                result = IS_CONCRETE(obj) ? MVM_repr_elems(tc, obj) != 0 : 0;
                break;
            default:
                MVM_exception_throw_adhoc(tc, "Invalid boolification spec mode used");
        }
    }

    if (flip)
        result = result ? 0 : 1;

    if (res_reg) {
        res_reg->i64 = result;
    }
    else {
        if (result)
            *(tc->interp_cur_op) = true_addr;
        else
            *(tc->interp_cur_op) = false_addr;
    }
}

/* Callback after running boolification method. */
void boolify_return(MVMThreadContext *tc, void *sr_data) {
    BoolMethReturnData *data = (BoolMethReturnData *)sr_data;
    MVMint64 result = data->res_reg.i64;
    if (data->flip)
        result = result ? 0 : 1;
    if (result)
        *(tc->interp_cur_op) = data->true_addr;
    else
        *(tc->interp_cur_op) = data->false_addr;
    free(data);
}

/* Callback to flip result. */
void flip_return(MVMThreadContext *tc, void *sr_data) {
    MVMRegister *r = (MVMRegister *)sr_data;
    r->i64 = r->i64 ? 0 : 1;
}

MVMString * MVM_coerce_i_s(MVMThreadContext *tc, MVMint64 i) {
    char buffer[64];
    int len = snprintf(buffer, 64, "%lld", i);
    if (len >= 0)
        return MVM_string_ascii_decode(tc, tc->instance->VMString, buffer, len);
    else
        MVM_exception_throw_adhoc(tc, "Could not stringify integer");
}

MVMString * MVM_coerce_n_s(MVMThreadContext *tc, MVMnum64 n) {
    if (n == MVM_num_posinf(tc)) {
        return MVM_string_ascii_decode_nt(tc, tc->instance->VMString, "Inf");
    }
    else if (n == MVM_num_neginf(tc)) {
        return MVM_string_ascii_decode_nt(tc, tc->instance->VMString, "-Inf");
    }
    else if (n != n) {
        return MVM_string_ascii_decode_nt(tc, tc->instance->VMString, "NaN");
    }
    else {
        char buf[64];
        int i;
        if (snprintf(buf, 64, "%-15f", n) < 0)
            MVM_exception_throw_adhoc(tc, "Could not stringify number");
        if (strstr(buf, ".")) {
            i = strlen(buf);
            while (i > 1 && (buf[--i] == '0' || buf[i] == ' '))
                buf[i] = '\0';
            if (buf[i] == '.')
                buf[i] = '\0';
        }
        return MVM_string_ascii_decode(tc, tc->instance->VMString, buf, strlen(buf));
    }
}

void MVM_coerce_smart_stringify(MVMThreadContext *tc, MVMObject *obj, MVMRegister *res_reg) {
    MVMObject *strmeth;
    MVMStorageSpec ss;

    /* Handle null case. */
    if (!obj) {
        res_reg->s = tc->instance->str_consts.empty;
        return;
    }

    /* If it can unbox as a string, that wins right off. */
    ss = REPR(obj)->get_storage_spec(tc, STABLE(obj));
    if (ss.can_box & MVM_STORAGE_SPEC_CAN_BOX_STR && IS_CONCRETE(obj)) {
        res_reg->s = REPR(obj)->box_funcs.get_str(tc, STABLE(obj), obj, OBJECT_BODY(obj));
        return;
    }

    /* Check if there is a Str method. */
    strmeth = MVM_6model_find_method_cache_only(tc, obj,
        tc->instance->str_consts.Str);
    if (strmeth) {
        /* We need to do the invocation; just set it up with our result reg as
         * the one for the call. */
        MVMObject *code = MVM_frame_find_invokee(tc, strmeth, NULL);
        tc->cur_frame->return_value   = res_reg;
        tc->cur_frame->return_type    = MVM_RETURN_STR;
        tc->cur_frame->return_address = *(tc->interp_cur_op);
        tc->cur_frame->args[0].o = obj;
        STABLE(code)->invoke(tc, code, &inv_arg_callsite, tc->cur_frame->args);
        return;
    }

    /* Otherwise, guess something appropriate. */
    if (!IS_CONCRETE(obj))
        res_reg->s = tc->instance->str_consts.empty;
    else {
        if (REPR(obj)->ID == MVM_REPR_ID_MVMString)
            res_reg->s = (MVMString *)obj;
        else if (REPR(obj)->ID == MVM_REPR_ID_MVMException)
            res_reg->s = ((MVMException *)obj)->body.message;
        else if (ss.can_box & MVM_STORAGE_SPEC_CAN_BOX_INT)
            res_reg->s = MVM_coerce_i_s(tc, REPR(obj)->box_funcs.get_int(tc, STABLE(obj), obj, OBJECT_BODY(obj)));
        else if (ss.can_box & MVM_STORAGE_SPEC_CAN_BOX_NUM)
            res_reg->s = MVM_coerce_n_s(tc, REPR(obj)->box_funcs.get_num(tc, STABLE(obj), obj, OBJECT_BODY(obj)));
        else
            MVM_exception_throw_adhoc(tc, "cannot stringify this");
    }
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

void MVM_coerce_smart_numify(MVMThreadContext *tc, MVMObject *obj, MVMRegister *res_reg) {
    MVMObject *nummeth;

    /* Handle null case. */
    if (!obj) {
        res_reg->n64 = 0.0;
        return;
    }

    /* Check if there is a Num method. */
    nummeth = MVM_6model_find_method_cache_only(tc, obj,
        tc->instance->str_consts.Num);
    if (nummeth) {
        /* We need to do the invocation; just set it up with our result reg as
         * the one for the call. */
        MVMObject *code = MVM_frame_find_invokee(tc, nummeth, NULL);
        tc->cur_frame->return_value   = res_reg;
        tc->cur_frame->return_type    = MVM_RETURN_NUM;
        tc->cur_frame->return_address = *(tc->interp_cur_op);
        tc->cur_frame->args[0].o = obj;
        STABLE(code)->invoke(tc, code, &inv_arg_callsite, tc->cur_frame->args);
        return;
    }

    /* Otherwise, guess something appropriate. */
    if (!IS_CONCRETE(obj)) {
        res_reg->n64 = 0.0;
    }
    else {
        MVMStorageSpec ss = REPR(obj)->get_storage_spec(tc, STABLE(obj));
        if (ss.can_box & MVM_STORAGE_SPEC_CAN_BOX_INT)
            res_reg->n64 = (MVMnum64)REPR(obj)->box_funcs.get_int(tc, STABLE(obj), obj, OBJECT_BODY(obj));
        else if (ss.can_box & MVM_STORAGE_SPEC_CAN_BOX_NUM)
            res_reg->n64 = REPR(obj)->box_funcs.get_num(tc, STABLE(obj), obj, OBJECT_BODY(obj));
        else if (ss.can_box & MVM_STORAGE_SPEC_CAN_BOX_STR)
            res_reg->n64 = MVM_coerce_s_n(tc, REPR(obj)->box_funcs.get_str(tc, STABLE(obj), obj, OBJECT_BODY(obj)));
        else if (REPR(obj)->ID == MVM_REPR_ID_MVMArray)
            res_reg->n64 = (MVMnum64)REPR(obj)->elems(tc, STABLE(obj), obj, OBJECT_BODY(obj));
        else if (REPR(obj)->ID == MVM_REPR_ID_MVMHash)
            res_reg->n64 = (MVMnum64)REPR(obj)->elems(tc, STABLE(obj), obj, OBJECT_BODY(obj));
        else
            MVM_exception_throw_adhoc(tc, "cannot numify this");
    }
}

MVMint64 MVM_coerce_simple_intify(MVMThreadContext *tc, MVMObject *obj) {
    /* Handle null and non-concrete case. */
    if (!obj || !IS_CONCRETE(obj)) {
        return 0;
    }

    /* Otherwise, guess something appropriate. */
    else {
        MVMStorageSpec ss = REPR(obj)->get_storage_spec(tc, STABLE(obj));
        if (ss.can_box & MVM_STORAGE_SPEC_CAN_BOX_INT)
            return REPR(obj)->box_funcs.get_int(tc, STABLE(obj), obj, OBJECT_BODY(obj));
        else if (ss.can_box & MVM_STORAGE_SPEC_CAN_BOX_NUM)
            return (MVMint64)REPR(obj)->box_funcs.get_num(tc, STABLE(obj), obj, OBJECT_BODY(obj));
        else if (ss.can_box & MVM_STORAGE_SPEC_CAN_BOX_STR)
            return MVM_coerce_s_i(tc, REPR(obj)->box_funcs.get_str(tc, STABLE(obj), obj, OBJECT_BODY(obj)));
        else if (REPR(obj)->ID == MVM_REPR_ID_MVMArray)
            return REPR(obj)->elems(tc, STABLE(obj), obj, OBJECT_BODY(obj));
        else if (REPR(obj)->ID == MVM_REPR_ID_MVMHash)
            return REPR(obj)->elems(tc, STABLE(obj), obj, OBJECT_BODY(obj));
        else
            MVM_exception_throw_adhoc(tc, "cannot intify this");
    }
}

MVMObject * MVM_radix(MVMThreadContext *tc, MVMint64 radix, MVMString *str, MVMint64 offset, MVMint64 flag) {
    MVMObject *result;
    MVMnum64 zvalue = 0.0;
    MVMnum64 zbase  = 1.0;
    MVMint64 chars  = NUM_GRAPHS(str);
    MVMnum64 value  = zvalue;
    MVMnum64 base   = zbase;
    MVMint64   pos  = -1;
    MVMuint16  neg  = 0;
    MVMint64   ch;

    if (radix > 36) {
        MVM_exception_throw_adhoc(tc, "Cannot convert radix of %d (max 36)", radix);
    }

    ch = (offset < chars) ? MVM_string_get_codepoint_at_nocheck(tc, str, offset) : 0;
    if ((flag & 0x02) && (ch == '+' || ch == '-')) {
        neg = (ch == '-');
        offset++;
        ch = (offset < chars) ? MVM_string_get_codepoint_at_nocheck(tc, str, offset) : 0;
    }

    while (offset < chars) {
        if (ch >= '0' && ch <= '9') ch = ch - '0';
        else if (ch >= 'a' && ch <= 'z') ch = ch - 'a' + 10;
        else if (ch >= 'A' && ch <= 'Z') ch = ch - 'A' + 10;
        else break;
        if (ch >= radix) break;
        zvalue = zvalue * radix + ch;
        zbase = zbase * radix;
        offset++; pos = offset;
        if (ch != 0 || !(flag & 0x04)) { value=zvalue; base=zbase; }
        if (offset >= chars) break;
        ch = MVM_string_get_codepoint_at_nocheck(tc, str, offset);
        if (ch != '_') continue;
        offset++;
        if (offset >= chars) break;
        ch = MVM_string_get_codepoint_at_nocheck(tc, str, offset);
    }

    if (neg || flag & 0x01) { value = -value; }

    /* initialize the object */
    result = MVM_repr_alloc_init(tc, MVM_hll_current(tc)->slurpy_array_type);
    MVMROOT(tc, result, {
        MVMObject *box_type = MVM_hll_current(tc)->num_box_type;
        MVMROOT(tc, box_type, {
            MVMObject *boxed = MVM_repr_box_num(tc, box_type, value);
            MVM_repr_push_o(tc, result, boxed);
            boxed = MVM_repr_box_num(tc, box_type, base);
            MVM_repr_push_o(tc, result, boxed);
            boxed = MVM_repr_box_num(tc, box_type, pos);
            MVM_repr_push_o(tc, result, boxed);
        });
    });

    return result;
}