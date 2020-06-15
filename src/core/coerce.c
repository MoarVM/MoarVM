#include "moar.h"
#include "math/grisu.h"

#if defined(_MSC_VER)
#define strtoll _strtoi64
#define snprintf _snprintf
#endif

/* Special return structure for boolification handling. */
typedef struct {
    MVMuint8    *true_addr;
    MVMuint8    *false_addr;
    MVMuint8     flip;
    MVMRegister  res_reg;
} BoolMethReturnData;

MVMint64 MVM_coerce_istrue_s(MVMThreadContext *tc, MVMString *str) {
    return str == NULL || !IS_CONCRETE(str) || MVM_string_graphs_nocheck(tc, str) == 0 ? 0 : 1;
}

/* Tries to do the boolification. It may be that a method call is needed. In
 * this case, a return hook is set up to handle doing the right thing. The
 * result register to put the result in should be indicated in res_reg, or
 * alternatively the true/false addresses to set the PC to should be set.
 * In the register case, expects that the current PC is already at the
 * next instruction before this is called. */
static void boolify_return(MVMThreadContext *tc, void *sr_data);
static void flip_return(MVMThreadContext *tc, void *sr_data);
static void free_boolify_return_data(MVMThreadContext *tc, void *sr_data) {
    MVM_free(sr_data);
}
void MVM_coerce_istrue(MVMThreadContext *tc, MVMObject *obj, MVMRegister *res_reg,
        MVMuint8 *true_addr, MVMuint8 *false_addr, MVMuint8 flip) {
    MVMint64 result = 0;
    if (!MVM_is_null(tc, obj)) {
        MVMBoolificationSpec *bs = obj->st->boolification_spec;
        switch (bs == NULL ? MVM_BOOL_MODE_NOT_TYPE_OBJECT : bs->mode) {
            case MVM_BOOL_MODE_CALL_METHOD: {
                MVMObject *code = MVM_frame_find_invokee(tc, bs->method, NULL);
                MVMCallsite *inv_arg_callsite = MVM_callsite_get_common(tc, MVM_CALLSITE_ID_OBJ);
                if (res_reg) {
                    /* We need to do the invocation, and set this register
                     * the result. Then we just do the call. For the flip
                     * case, just set up special return handler to flip
                     * the register. */
                    MVM_args_setup_thunk(tc, res_reg, MVM_RETURN_INT, inv_arg_callsite);
                    tc->cur_frame->args[0].o = obj;
                    if (flip)
                        MVM_frame_special_return(tc, tc->cur_frame, flip_return, NULL,
                            res_reg, NULL);
                    STABLE(code)->invoke(tc, code, inv_arg_callsite, tc->cur_frame->args);
                }
                else {
                    /* Need to set up special return hook. */
                    BoolMethReturnData *data = MVM_malloc(sizeof(BoolMethReturnData));
                    data->true_addr  = true_addr;
                    data->false_addr = false_addr;
                    data->flip       = flip;
                    MVM_frame_special_return(tc, tc->cur_frame, boolify_return, free_boolify_return_data, data, NULL);
                    MVM_args_setup_thunk(tc, &data->res_reg, MVM_RETURN_INT, inv_arg_callsite);
                    tc->cur_frame->args[0].o = obj;
                    STABLE(code)->invoke(tc, code, inv_arg_callsite, tc->cur_frame->args);
                }
                return;
            }
            case MVM_BOOL_MODE_UNBOX_INT:
                result = !IS_CONCRETE(obj) || REPR(obj)->box_funcs.get_int(tc, STABLE(obj), obj, OBJECT_BODY(obj)) == 0 ? 0 : 1;
                break;
            case MVM_BOOL_MODE_UNBOX_NUM:
                result = !IS_CONCRETE(obj) || REPR(obj)->box_funcs.get_num(tc, STABLE(obj), obj, OBJECT_BODY(obj)) == 0.0 ? 0 : 1;
                break;
            case MVM_BOOL_MODE_UNBOX_STR_NOT_EMPTY: {
                MVMString *str;
                if (!IS_CONCRETE(obj)) {
                    result = 0;
                    break;
                }
                str = REPR(obj)->box_funcs.get_str(tc, STABLE(obj), obj, OBJECT_BODY(obj));
                result = MVM_coerce_istrue_s(tc, str);
                break;
            }
            case MVM_BOOL_MODE_UNBOX_STR_NOT_EMPTY_OR_ZERO: {
                MVMString *str;
                MVMint64 chars;
                if (!IS_CONCRETE(obj)) {
                    result = 0;
                    break;
                }
                str = REPR(obj)->box_funcs.get_str(tc, STABLE(obj), obj, OBJECT_BODY(obj));

                if (str == NULL || !IS_CONCRETE(str)) {
                    result = 0;
                    break;
                }

                chars = MVM_string_graphs_nocheck(tc, str);

                result = chars == 0 ||
                        (chars == 1 && MVM_string_get_grapheme_at_nocheck(tc, str, 0) == 48)
                        ? 0 : 1;
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
static void boolify_return(MVMThreadContext *tc, void *sr_data) {
    BoolMethReturnData *data = (BoolMethReturnData *)sr_data;
    MVMint64 result = data->res_reg.i64;
    if (data->flip)
        result = result ? 0 : 1;
    if (result)
        *(tc->interp_cur_op) = data->true_addr;
    else
        *(tc->interp_cur_op) = data->false_addr;
    MVM_free(data);
}

/* Callback to flip result. */
static void flip_return(MVMThreadContext *tc, void *sr_data) {
    MVMRegister *r = (MVMRegister *)sr_data;
    r->i64 = r->i64 ? 0 : 1;
}
/* ui64toa and i64toa
 * Copyright(c) 2014-2016 Milo Yip (miloyip@gmail.com)
 * https://github.com/miloyip/itoa-benchmark
 * With minor modifications.

 Copyright (C) 2014 Milo Yip

MIT License:

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
    THE SOFTWARE. */
static char * u64toa_naive_worker(uint64_t value, char* buffer) {
    char temp[20];
    char *p = temp;
    do {
        *p++ = (char)(value % 10) + '0';
        value /= 10;
    } while (value > 0);

    do {
        *buffer++ = *--p;
    } while (p != temp);

    return buffer;
}
static size_t i64toa_naive(int64_t value, char* buffer) {
    uint64_t u = value;
    char *buf = buffer;
    if (value < 0) {
        *buf++ = '-';
        u = ~u + 1;
    }
    return u64toa_naive_worker(u, buf) - buffer;
}
/* End code */
static size_t u64toa_naive(uint64_t value, char* buffer) {
    return u64toa_naive_worker(value, buffer) - buffer;
}
MVMString * MVM_coerce_i_s(MVMThreadContext *tc, MVMint64 i) {
    char buffer[20];
    int len;
    /* See if we can hit the cache. */
    int cache = 0 <= i && i < MVM_INT_TO_STR_CACHE_SIZE;
    if (cache) {
        MVMString *cached = tc->instance->int_to_str_cache[i];
        if (cached)
            return cached;
    }
    /* Otherwise, need to do the work; cache it if in range. */
    len = i64toa_naive(i, buffer);
    if (0 <= len) {
        MVMString *result = NULL;
        MVMGrapheme8 *blob = MVM_malloc(len);
        memcpy(blob, buffer, len);
        result = MVM_string_ascii_from_buf_nocheck(tc, blob, len);
        if (cache)
            tc->instance->int_to_str_cache[i] = result;
        return result;
    }
    else {
        MVM_exception_throw_adhoc(tc, "Could not stringify integer (%"PRId64")", i);
    }
}

MVMString * MVM_coerce_u_s(MVMThreadContext *tc, MVMuint64 i) {
    char buffer[20];
    int len;
    /* See if we can hit the cache. */
    int cache = i < MVM_INT_TO_STR_CACHE_SIZE;
    if (cache) {
        MVMString *cached = tc->instance->int_to_str_cache[i];
        if (cached)
            return cached;
    }
    /* Otherwise, need to do the work; cache it if in range. */
    len = u64toa_naive(i, buffer);
    if (0 <= len) {
        MVMString *result = NULL;
        MVMGrapheme8 *blob = MVM_malloc(len);
        memcpy(blob, buffer, len);
        result = MVM_string_ascii_from_buf_nocheck(tc, blob, len);
        if (cache)
            tc->instance->int_to_str_cache[i] = result;
        return result;
    }
    else {
        MVM_exception_throw_adhoc(tc, "Could not stringify integer (%"PRIu64")", i);
    }
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
        if (dtoa_grisu3(n, buf, 64) < 0)
            MVM_exception_throw_adhoc(tc, "Could not stringify number (%f)", n);
        else {
            MVMStringIndex len = strlen(buf);
            MVMGrapheme8 *blob = MVM_malloc(len);
            memcpy(blob, buf, len);
            return MVM_string_ascii_from_buf_nocheck(tc, blob, len);
        }
    }
}

void MVM_coerce_smart_stringify(MVMThreadContext *tc, MVMObject *obj, MVMRegister *res_reg) {
    MVMObject *strmeth;
    const MVMStorageSpec *ss;

    /* Handle null case. */
    if (MVM_is_null(tc, obj)) {
        res_reg->s = tc->instance->str_consts.empty;
        return;
    }

    /* If it can unbox as a string, that wins right off. */
    ss = REPR(obj)->get_storage_spec(tc, STABLE(obj));
    if (ss->can_box & MVM_STORAGE_SPEC_CAN_BOX_STR && IS_CONCRETE(obj)) {
        res_reg->s = REPR(obj)->box_funcs.get_str(tc, STABLE(obj), obj, OBJECT_BODY(obj));
        return;
    }

    /* Check if there is a Str method. */
    MVMROOT(tc, obj, {
        strmeth = MVM_6model_find_method_cache_only(tc, obj,
            tc->instance->str_consts.Str);
    });

    if (!MVM_is_null(tc, strmeth)) {
        /* We need to do the invocation; just set it up with our result reg as
         * the one for the call. */
        MVMObject *code = MVM_frame_find_invokee(tc, strmeth, NULL);
        MVMCallsite *inv_arg_callsite = MVM_callsite_get_common(tc, MVM_CALLSITE_ID_OBJ);

        MVM_args_setup_thunk(tc, res_reg, MVM_RETURN_STR, inv_arg_callsite);
        tc->cur_frame->args[0].o = obj;
        STABLE(code)->invoke(tc, code, inv_arg_callsite, tc->cur_frame->args);
        return;
    }

    /* Otherwise, guess something appropriate. */
    if (!IS_CONCRETE(obj))
        res_reg->s = tc->instance->str_consts.empty;
    else {
        if (REPR(obj)->ID == MVM_REPR_ID_MVMException)
            res_reg->s = ((MVMException *)obj)->body.message;
        else if (ss->can_box & MVM_STORAGE_SPEC_CAN_BOX_INT)
            res_reg->s = MVM_coerce_i_s(tc, REPR(obj)->box_funcs.get_int(tc, STABLE(obj), obj, OBJECT_BODY(obj)));
        else if (ss->can_box & MVM_STORAGE_SPEC_CAN_BOX_NUM)
            res_reg->s = MVM_coerce_n_s(tc, REPR(obj)->box_funcs.get_num(tc, STABLE(obj), obj, OBJECT_BODY(obj)));
        else
            MVM_exception_throw_adhoc(tc, "Cannot stringify this object of type %s (%s)", REPR(obj)->name, MVM_6model_get_stable_debug_name(tc, STABLE(obj)));
    }
}

MVMint64 MVM_coerce_s_i(MVMThreadContext *tc, MVMString *s) {
    char     *enc = MVM_string_ascii_encode_any(tc, s);
    MVMint64  i   = strtoll(enc, NULL, 10);
    MVM_free(enc);
    return i;
}

void MVM_coerce_smart_numify(MVMThreadContext *tc, MVMObject *obj, MVMRegister *res_reg) {
    MVMObject *nummeth;

    /* Handle null case. */
    if (MVM_is_null(tc, obj)) {
        res_reg->n64 = 0.0;
        return;
    }

    /* Check if there is a Num method. */
    MVMROOT(tc, obj, {
        nummeth = MVM_6model_find_method_cache_only(tc, obj,
            tc->instance->str_consts.Num);
    });

    if (!MVM_is_null(tc, nummeth)) {
        /* We need to do the invocation; just set it up with our result reg as
         * the one for the call. */
        MVMObject *code = MVM_frame_find_invokee(tc, nummeth, NULL);
        MVMCallsite *inv_arg_callsite = MVM_callsite_get_common(tc, MVM_CALLSITE_ID_OBJ);

        MVM_args_setup_thunk(tc, res_reg, MVM_RETURN_NUM, inv_arg_callsite);
        tc->cur_frame->args[0].o = obj;
        STABLE(code)->invoke(tc, code, inv_arg_callsite, tc->cur_frame->args);
        return;
    }

    /* Otherwise, guess something appropriate. */
    if (!IS_CONCRETE(obj)) {
        res_reg->n64 = 0.0;
    }
    else {
        const MVMStorageSpec *ss = REPR(obj)->get_storage_spec(tc, STABLE(obj));
        if (ss->can_box & MVM_STORAGE_SPEC_CAN_BOX_INT)
            res_reg->n64 = (MVMnum64)REPR(obj)->box_funcs.get_int(tc, STABLE(obj), obj, OBJECT_BODY(obj));
        else if (ss->can_box & MVM_STORAGE_SPEC_CAN_BOX_NUM)
            res_reg->n64 = REPR(obj)->box_funcs.get_num(tc, STABLE(obj), obj, OBJECT_BODY(obj));
        else if (ss->can_box & MVM_STORAGE_SPEC_CAN_BOX_STR)
            res_reg->n64 = MVM_coerce_s_n(tc, REPR(obj)->box_funcs.get_str(tc, STABLE(obj), obj, OBJECT_BODY(obj)));
        else if (REPR(obj)->ID == MVM_REPR_ID_VMArray)
            res_reg->n64 = (MVMnum64)REPR(obj)->elems(tc, STABLE(obj), obj, OBJECT_BODY(obj));
        else if (REPR(obj)->ID == MVM_REPR_ID_MVMHash)
            res_reg->n64 = (MVMnum64)REPR(obj)->elems(tc, STABLE(obj), obj, OBJECT_BODY(obj));
        else
            MVM_exception_throw_adhoc(tc, "Cannot numify this object of type %s (%s)", REPR(obj)->name, MVM_6model_get_stable_debug_name(tc, STABLE(obj)));
    }
}

void MVM_coerce_smart_intify(MVMThreadContext *tc, MVMObject *obj, MVMRegister *res_reg) {
    MVMObject *intmeth;

    /* Handle null case. */
    if (MVM_is_null(tc, obj)) {
        res_reg->i64 = 0;
    }

    /* Check if there is an Int method. */
    MVMROOT(tc, obj, {
        intmeth = MVM_6model_find_method_cache_only(tc, obj,
            tc->instance->str_consts.Int);
    });

    if (!MVM_is_null(tc, intmeth)) {
        /* We need to do the invocation; just set it up with our result reg as
         * the one for the call. */
        MVMObject *code = MVM_frame_find_invokee(tc, intmeth, NULL);
        MVMCallsite *inv_arg_callsite = MVM_callsite_get_common(tc, MVM_CALLSITE_ID_OBJ);

        MVM_args_setup_thunk(tc, res_reg, MVM_RETURN_INT, inv_arg_callsite);
        tc->cur_frame->args[0].o = obj;
        STABLE(code)->invoke(tc, code, inv_arg_callsite, tc->cur_frame->args);
        return;
    }

    /* Otherwise, guess something appropriate. */
    if (!IS_CONCRETE(obj)) {
        res_reg->i64 = 0;
    }
    else {
        const MVMStorageSpec *ss = REPR(obj)->get_storage_spec(tc, STABLE(obj));
        if (ss->can_box & MVM_STORAGE_SPEC_CAN_BOX_INT)
            res_reg->i64 = REPR(obj)->box_funcs.get_int(tc, STABLE(obj), obj, OBJECT_BODY(obj));
        else if (ss->can_box & MVM_STORAGE_SPEC_CAN_BOX_NUM)
            res_reg->i64 = (MVMint64)REPR(obj)->box_funcs.get_num(tc, STABLE(obj), obj, OBJECT_BODY(obj));
        else if (ss->can_box & MVM_STORAGE_SPEC_CAN_BOX_STR)
            res_reg->i64 = MVM_coerce_s_i(tc, REPR(obj)->box_funcs.get_str(tc, STABLE(obj), obj, OBJECT_BODY(obj)));
        else if (REPR(obj)->ID == MVM_REPR_ID_VMArray)
            res_reg->i64 = REPR(obj)->elems(tc, STABLE(obj), obj, OBJECT_BODY(obj));
        else if (REPR(obj)->ID == MVM_REPR_ID_MVMHash)
            res_reg->i64 = REPR(obj)->elems(tc, STABLE(obj), obj, OBJECT_BODY(obj));
        else
            MVM_exception_throw_adhoc(tc, "Cannot intify this object of type %s (%s)", REPR(obj)->name, MVM_6model_get_stable_debug_name(tc, STABLE(obj)));
    }
}

MVMint64 MVM_coerce_simple_intify(MVMThreadContext *tc, MVMObject *obj) {
    /* Handle null and non-concrete case. */
    if (MVM_is_null(tc, obj) || !IS_CONCRETE(obj)) {
        return 0;
    }

    /* Otherwise, guess something appropriate. */
    else {
        const MVMStorageSpec *ss = REPR(obj)->get_storage_spec(tc, STABLE(obj));
        if (ss->can_box & MVM_STORAGE_SPEC_CAN_BOX_INT)
            return REPR(obj)->box_funcs.get_int(tc, STABLE(obj), obj, OBJECT_BODY(obj));
        else if (ss->can_box & MVM_STORAGE_SPEC_CAN_BOX_NUM)
            return (MVMint64)REPR(obj)->box_funcs.get_num(tc, STABLE(obj), obj, OBJECT_BODY(obj));
        else if (ss->can_box & MVM_STORAGE_SPEC_CAN_BOX_STR)
            return MVM_coerce_s_i(tc, REPR(obj)->box_funcs.get_str(tc, STABLE(obj), obj, OBJECT_BODY(obj)));
        else if (REPR(obj)->ID == MVM_REPR_ID_VMArray)
            return REPR(obj)->elems(tc, STABLE(obj), obj, OBJECT_BODY(obj));
        else if (REPR(obj)->ID == MVM_REPR_ID_MVMHash)
            return REPR(obj)->elems(tc, STABLE(obj), obj, OBJECT_BODY(obj));
        else
            MVM_exception_throw_adhoc(tc, "Cannot intify this object of type %s (%s)", REPR(obj)->name, MVM_6model_get_stable_debug_name(tc, STABLE(obj)));
    }
}

MVMObject * MVM_radix(MVMThreadContext *tc, MVMint64 radix, MVMString *str, MVMint64 offset, MVMint64 flag) {
    MVMObject *result;
    MVMint64 zvalue = 0;
    MVMint64 zbase  = 1;
    MVMint64 chars  = MVM_string_graphs(tc, str);
    MVMint64 value  = zvalue;
    MVMint64 base   = zbase;
    MVMint64   pos  = -1;
    MVMuint16  neg  = 0;
    MVMint64   ch;

    if (radix > 36) {
        MVM_exception_throw_adhoc(tc, "Cannot convert radix of %"PRId64" (max 36)", radix);
    }

    ch = (offset < chars) ? MVM_string_get_grapheme_at_nocheck(tc, str, offset) : 0;
    if ((flag & 0x02) && (ch == '+' || ch == '-' || ch == 0x2212)) {  /* MINUS SIGN */
        neg = (ch == '-' || ch == 0x2212);
        offset++;
        ch = (offset < chars) ? MVM_string_get_grapheme_at_nocheck(tc, str, offset) : 0;
    }

    while (offset < chars) {
        if (ch >= '0' && ch <= '9') ch = ch - '0'; /* fast-path for ASCII 0..9 */
        else if (ch >= 'a' && ch <= 'z') ch = ch - 'a' + 10;
        else if (ch >= 'A' && ch <= 'Z') ch = ch - 'A' + 10;
        else if (ch >= 0xFF21 && ch <= 0xFF3A) ch = ch - 0xFF21 + 10; /* uppercase fullwidth */
        else if (ch >= 0xFF41 && ch <= 0xFF5A) ch = ch - 0xFF41 + 10; /* lowercase fullwidth */
        else if (ch > 0 && MVM_unicode_codepoint_get_property_int(tc, ch, MVM_UNICODE_PROPERTY_NUMERIC_TYPE)
         == MVM_UNICODE_PVALUE_Numeric_Type_DECIMAL) {
            /* as of Unicode 9.0.0, characters with the 'de' Numeric Type (and are
             * thus also of General Category Nd, since 4.0.0) are contiguous
             * sequences of 10 chars whose Numeric Values ascend from 0 through 9.
             */

            /* the string returned for NUMERIC_VALUE_NUMERATOR contains an integer
             * value. We can use numerator because they all are from 0-9 and have
             * denominator of 1 */
            ch = fast_atoi(MVM_unicode_codepoint_get_property_cstr(tc, ch, MVM_UNICODE_PROPERTY_NUMERIC_VALUE_NUMERATOR));
        }
        else break;
        if (ch >= radix) break;
        zvalue = zvalue * radix + ch;
        zbase = zbase * radix;
        offset++; pos = offset;
        if (ch != 0 || !(flag & 0x04)) { value=zvalue; base=zbase; }
        if (offset >= chars) break;
        ch = MVM_string_get_grapheme_at_nocheck(tc, str, offset);
        if (ch != '_') continue;
        offset++;
        if (offset >= chars) break;
        ch = MVM_string_get_grapheme_at_nocheck(tc, str, offset);
    }

    if (neg || flag & 0x01) { value = -value; }

    /* initialize the object */
    result = MVM_repr_alloc_init(tc, MVM_hll_current(tc)->slurpy_array_type);
    MVMROOT(tc, result, {
        MVMObject *box_type = MVM_hll_current(tc)->int_box_type;
        MVMROOT(tc, box_type, {
            MVMObject *boxed = MVM_repr_box_int(tc, box_type, value);
            MVM_repr_push_o(tc, result, boxed);
            boxed = MVM_repr_box_int(tc, box_type, base);
            MVM_repr_push_o(tc, result, boxed);
            boxed = MVM_repr_box_int(tc, box_type, pos);
            MVM_repr_push_o(tc, result, boxed);
        });
    });

    return result;
}
