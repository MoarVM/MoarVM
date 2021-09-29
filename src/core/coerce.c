#include "moar.h"
#include "ryu/ryu.h"

#if defined(_MSC_VER)
#define strtoll _strtoi64
#define snprintf _snprintf
#endif

MVMint64 MVM_coerce_istrue_s(MVMThreadContext *tc, MVMString *str) {
    return str == NULL || !IS_CONCRETE(str) || MVM_string_graphs_nocheck(tc, str) == 0 ? 0 : 1;
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
    if (MVM_UNLIKELY(MVM_num_isnanorinf(tc, n))) {
        if (n == MVM_num_posinf(tc)) {
            return MVM_string_ascii_decode_nt(tc, tc->instance->VMString, "Inf");
        }
        else if (n == MVM_num_neginf(tc)) {
            return MVM_string_ascii_decode_nt(tc, tc->instance->VMString, "-Inf");
        }
        else {
            return MVM_string_ascii_decode_nt(tc, tc->instance->VMString, "NaN");
        }
    }

    char buf[64];
    /* What we get back is 0E0, 1E0, 3.14E0, 1E2, ... Infinity.
     * What we'd like is the classic "fixed decimal" representation for
     * small values, and the exponent as a lower case 'e'. So we do some
     * massaging, and handle infinity above. We could leave NaN to fall
     * through here, but if so it would hit our "something went wrong" code,
     * which somewhat downplays the absolute "this path means a bug". So I think
     * that it's still clearer handling it above. */
    const int orig_len = d2s_buffered_n(n, buf);
    const char *first = buf;

    /* Take any leading minus away. We put it back at the end. */
    int len = orig_len;
    if (*first == '-') {
        ++first;
        --len;
    }

    if (len < 3 || !(first[1] == '.' || first[1] == 'E')) {
        /* Well, this shouldn't be possible. */
    }
    else {
        const char *end = first + len;
        const char *E = NULL;
        if (end[-2] == 'E') {
            E = end - 2;
        }
        else if (end[-3] == 'E') {
            E = end - 3;
        }
        else if (len >= 4 && end[-4] == 'E') {
            E = end - 4;
        }
        else if (len > 4 && end[-5] == 'E') {
            E = end - 5;
        }

        /* This is written out verbosely - arguably there is some DRY-violation.
         * It seems clearer to write it all out, than attempt to fold the common
         * parts together and make it confusing and potentially buggy.
         * I hope that the C compiler optimiser can spot the common code paths.
         */

        if (E) {
            MVMGrapheme8 *blob;
            size_t e_len = end - (E + 1);
            if (e_len == 2 && E[1] == '-') {
                /* 1E-1 etc to 1E-9 etc */
                if (E[2] > '4') {
                    /* 1E-5 etc to 1E-9 etc. Need to add a zero. */
                    len = orig_len + 1;
                    blob = MVM_malloc(len);
                    /* Using buf here, not first, means that we copy any '-'
                     * too. */
                    size_t new_e = E - buf;
                    memcpy(blob, buf, new_e);
                    blob[new_e] = 'e';
                    blob[new_e + 1] = '-';
                    blob[new_e + 2] = '0';
                    blob[new_e + 3] = E[2];
                }
                else {
                    /* Convert to fixed format, value < 1 */
                    unsigned int zeros = E[2] - '0' - 1;
                    size_t dec_len;
                    if (E == first + 1) {
                        /* No trailing decimals */
                        dec_len = 0;
                    }
                    else {
                        dec_len = E - (first + 2);
                    }
                    len = 2         /* "0." */
                        + zeros     /* "", "0", "00" or "000" */
                        + 1         /* first digit */
                        + dec_len;  /* rest */

                    MVMGrapheme8 *pos;
                    if (first == buf) {
                        blob = MVM_malloc(len);
                        pos = blob;
                    } else {
                        ++len;
                        blob = MVM_malloc(len);
                        pos = blob;
                        *pos++ = '-';
                    }

                    *pos++ = '0';
                    *pos++ = '.';

                    while (zeros) {
                        *pos++ = '0';
                        --zeros;
                    }

                    *pos++ = *first;

                    if (dec_len) {
                        memcpy(pos, first + 2, dec_len);
                    }
                }
            }
            else if (e_len == 1 || (e_len == 2 && E[1] == '1' && E[2] < '5')) {
                /* 1E0 etc to 1E14 etc.
                 * Convert to fixed format, possibly needing padding,
                 * possibly with trailing decimals, possibly neither. */
                unsigned int exp = e_len == 1 ? E[1] - '0' : 10 + E[2] - '0';
                size_t dec_len;
                if (E == first + 1) {
                    /* No trailing decimals */
                    dec_len = 0;
                }
                else {
                    dec_len = E - (first + 2);
                }
                size_t padding = exp > dec_len ? exp - dec_len : 0;
                size_t before_dp = exp > dec_len ? dec_len : exp;
                int has_dp = dec_len > exp;

                len = 1 + padding + dec_len + has_dp;

                MVMGrapheme8 *pos;
                if (first == buf) {
                    blob = MVM_malloc(len);
                    pos = blob;
                } else {
                    ++len;
                    blob = MVM_malloc(len);
                    pos = blob;
                    *pos++ = '-';
                }

                *pos++ = *first;

                if (before_dp) {
                    memcpy(pos, first + 2, before_dp);
                    pos += before_dp;
                }

                if (has_dp) {
                    /* In this case, we never need to pad with zeros. */
                    *pos++ = '.';
                    memcpy(pos, first + 2 + before_dp, dec_len - exp);
                }
                else {
                    /* In this case, we might need to pad with zeros. */
                    while (padding) {
                        *pos++ = '0';
                        --padding;
                    }
                }
            }
            else if (E[1] == '-') {
                /* Stays in scientific notation, but need to change to 'e'. */
                len = orig_len;
                blob = MVM_malloc(len);
                size_t new_e = E - buf;
                memcpy(blob, buf, new_e);
                blob[new_e] = 'e';
                memcpy(blob + new_e + 1, E + 1, e_len);
            } else {
                /* Stays in scientific notation, but need to change to 'e'
                 * and add a + */
                len = orig_len + 1;
                blob = MVM_malloc(len);
                size_t new_e = E - buf;
                memcpy(blob, buf, new_e);
                blob[new_e] = 'e';
                blob[new_e + 1] = '+';
                memcpy(blob + new_e + 2, E + 1, e_len);
            }
            return MVM_string_ascii_from_buf_nocheck(tc, blob, len);
        }
    }

    /* Something went wrong. Should we oops? */
    MVMGrapheme8 *blob = MVM_malloc(orig_len);
    memcpy(blob, buf, orig_len);
    return MVM_string_ascii_from_buf_nocheck(tc, blob, orig_len);
}

MVMint64 MVM_coerce_s_i(MVMThreadContext *tc, MVMString *s) {
    char     *enc = MVM_string_ascii_encode_any(tc, s);
    MVMint64  i   = strtoll(enc, NULL, 10);
    MVM_free(enc);
    return i;
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
