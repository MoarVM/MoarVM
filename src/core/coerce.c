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
MIT License

Copyright (c) 2017 James Edward Anhalt III - https://github.com/jeaiii/itoa
with minor modifications for formatting and so it will compile as C code

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE. */

typedef struct pair { char t, o; } pair;
#define P(T) T, '0',  T, '1', T, '2', T, '3', T, '4', T, '5', T, '6', T, '7', T, '8', T, '9'
static const pair s_pairs[] = { P('0'), P('1'), P('2'), P('3'), P('4'), P('5'), P('6'), P('7'), P('8'), P('9') };

#define W(N, I) *(pair*)&b[N] = s_pairs[I]
#define A(N) t = ((uint64_t)(1) << (32 + N / 5 * N * 53 / 16)) / (uint32_t)(1e##N) + 1 + N/6 - N/8, t *= u, t >>= N / 5 * N * 53 / 16, t += N / 6 * 4, W(0, t >> 32)
#define S(N) b[N] = (char)((uint64_t)(10) * (uint32_t)(t) >> 32) + '0'
#define D(N) t = (uint64_t)(100) * (uint32_t)(t), W(N, t >> 32)

#define L0 b[0] = (char)(u) + '0'
#define L1 W(0, u)
#define L2 A(1), S(2)
#define L3 A(2), D(2)
#define L4 A(3), D(2), S(4)
#define L5 A(4), D(2), D(4)
#define L6 A(5), D(2), D(4), S(6)
#define L7 A(6), D(2), D(4), D(6)
#define L8 A(7), D(2), D(4), D(6), S(8)
#define L9 A(8), D(2), D(4), D(6), D(8)

#define LN(N) (L##N, b += N + 1)
#define LZ LN

#define LG(F) (u<100 ? u<10 ? F(0) : F(1) : u<1000000 ? u<10000 ? u<1000 ? F(2) : F(3) : u<100000 ? F(4) : F(5) : u<100000000 ? u<10000000 ? F(6) : F(7) : u<1000000000 ? F(8) : F(9))

static char * u64toa_jeaiii(uint64_t n, char* b) {
    uint32_t u;
    uint64_t t;

    if ((uint32_t)(n >> 32) == 0)
        return u = (uint32_t)(n), LG(LZ);

    uint64_t a = n / 100000000;

    if ((uint32_t)(a >> 32) == 0) {
        u = (uint32_t)(a);
        LG(LN);
    }
    else {
        u = (uint32_t)(a / 100000000);
        LG(LN);
        u = a % 100000000;
        LN(7);
    }

    u = n % 100000000;
    return LZ(7);
}

static char * i64toa_jeaiii(int64_t i, char* b) {
    uint64_t n = i < 0 ? *b++ = '-', 0 - (uint64_t)(i) : (uint64_t)i;
    return u64toa_jeaiii(n, b);
}

/* End code */

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
    len = i64toa_jeaiii(i, buffer) - buffer;
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
    len = u64toa_jeaiii(i, buffer) - buffer;
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

MVMint64 MVM_coerce_s_i(MVMThreadContext *tc, MVMString *str) {
    MVMStringIndex strgraphs = MVM_string_graphs(tc, str);
    MVMint64       result = 0;
    MVMint32       any = 0, negative = 0;

    MVMint64 cutoff;
    MVMint32 cutlim;

    if (!strgraphs)
        return result;

/*-
 * Copyright (c) 1990 The Regents of the University of California.
 * All rights reserved.
 *
 * copied from https://github.com/gcc-mirror/gcc/blob/0c0f453c4af4880c522c8472c33eef42bee9eda1/libiberty/strtoll.c
 * with minor modifications to simplify and work in MoarVM
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. [rescinded 22 July 1999]
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
    if (str->body.storage_type == MVM_STRING_GRAPHEME_ASCII) {
        const MVMGraphemeASCII *s = str->body.storage.blob_ascii;
        MVMStringIndex i = 0;
        MVMGraphemeASCII c;
        do {
            c = *s++;
            ++i;
            /* The original C code was processing a NUL terminated string, hence
             * it could always be sure that it could read a value into c for
             * which isspace(c) was false, and so for a string entirely
             * whitespace this loop would terminate with c == 0 (without reading
             * beyond the end) and the code below would behave correctly too.
             *
             * Instead we can use the length test to terminate the loop
             * unconditionally just after we read the last character. This would
             * be for strings which are entirely whitespace - eg " ", "  " etc.
             * They all return 0, and we don't need to optimise their handling,
             * so we simply drop through into the rest of the code with c == ' '
             * for them. */
            /* isspace(...) is any of "\t\n\v\f\r ", ie [9 .. 13, 32] */
        } while (i != strgraphs && (c == ' ' || (c >= '\t' && c <= '\r')));

        /* `i` counts how many octets we have read. Hence `i == strgraphs` at
         * the point where `c` holds the final ASCII character of the string,
         * and there is no more to read. */
        if (c == '-') {
            negative = 1;
            if (i++ == strgraphs)
                return 0;
            c = *s++;
        } else if (c == '+') {
            if (i++ == strgraphs)
                return 0;
            c = *s++;
        }

        cutoff = negative ? -(unsigned long long)LLONG_MIN : LLONG_MAX;
        cutlim = cutoff % (unsigned long long)10;
        cutoff /= (unsigned long long)10;

        while (1) {
            if (c >= '0' && c <= '9')
                c -= '0';
            else
                break;

            if (any < 0 || result > cutoff || (result == cutoff && c > cutlim))
                any = -1;
            else {
                any = 1;
                result *= 10;
                result += c;
            }
            if (i++ == strgraphs)
                break;
            c = *s++;
        }

        if (any < 0)
            result = negative ? LLONG_MIN : LLONG_MAX;
        else if (negative)
            result = -result;
    }
    else {
        MVMCodepointIter ci;
        MVM_string_ci_init(tc, &ci, str, 0, 0);
        MVMCodepoint ord;

        do {
            ord = MVM_string_ci_get_codepoint(tc, &ci);
        } while ((ord == ' ' || (ord >= '\t' && ord <= '\r')) && MVM_string_ci_has_more(tc, &ci));

        if (ord == '-') {
            negative = 1;
            if (!MVM_string_ci_has_more(tc, &ci))
                return 0;
            ord = MVM_string_ci_get_codepoint(tc, &ci);
        }
        else if (ord == '+') {
            if (!MVM_string_ci_has_more(tc, &ci))
                return 0;
            ord = MVM_string_ci_get_codepoint(tc, &ci);
        }

        cutoff = negative ? -(unsigned long long)LLONG_MIN : LLONG_MAX;
        cutlim = cutoff % (unsigned long long)10;
        cutoff /= (unsigned long long)10;

        while (1) {
            if (ord >= '0' && ord <= '9')
                ord -= '0';
            else
                break;

            if (any < 0 || result > cutoff || (result == cutoff && ord > cutlim))
                any = -1;
            else {
                any = 1;
                result *= 10;
                result += ord;
            }
            if (!MVM_string_ci_has_more(tc, &ci))
                break;
            ord = MVM_string_ci_get_codepoint(tc, &ci);
        };

        if (any < 0)
            result = negative ? LLONG_MIN : LLONG_MAX;
        else if (negative)
            result = -result;
    }

    return result;
}

MVMuint64 MVM_coerce_s_u(MVMThreadContext *tc, MVMString *str) {
    MVMStringIndex strgraphs = MVM_string_graphs(tc, str);
    MVMuint64      result = 0;
    MVMint32       any = 0, negative = 0;

    MVMuint64 cutoff;
    MVMint32 cutlim;

    if (!strgraphs)
        return result;

/*-
 * Copyright (c) 1990 The Regents of the University of California.
 * All rights reserved.
 *
 * copied from https://github.com/gcc-mirror/gcc/blob/0c0f453c4af4880c522c8472c33eef42bee9eda1/libiberty/strtoull.c
 * with minor modifications to simplify and work in MoarVM
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. [rescinded 22 July 1999]
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
    if (str->body.storage_type == MVM_STRING_GRAPHEME_ASCII) {
        const MVMGraphemeASCII *s = str->body.storage.blob_ascii;
        MVMStringIndex i = 0;
        MVMGraphemeASCII c;
        do {
            c = *s++;
            ++i;
            /* The original C code was processing a NUL terminated string, hence
             * it could always be sure that it could read a value into c for
             * which isspace(c) was false, and so for a string entirely
             * whitespace this loop would terminate with c == 0 (without reading
             * beyond the end) and the code below would behave correctly too.
             *
             * Instead we can use the length test to terminate the loop
             * unconditionally just after we read the last character. This would
             * be for strings which are entirely whitespace - eg " ", "  " etc.
             * They all return 0, and we don't need to optimise their handling,
             * so we simply drop through into the rest of the code with c == ' '
             * for them. */
            /* isspace(...) is any of "\t\n\v\f\r ", ie [9 .. 13, 32] */
        } while (i != strgraphs && (c == ' ' || (c >= '\t' && c <= '\r')));

        /* `i` counts how many octets we have read. Hence `i == strgraphs` at
         * the point where `c` holds the final ASCII character of the string,
         * and there is no more to read. */
        if (c == '-') {
            negative = 1;
            if (i++ == strgraphs)
                return 0;
            c = *s++;
        } else if (c == '+') {
            if (i++ == strgraphs)
                return 0;
            c = *s++;
        }

        cutoff = (unsigned long long)LLONG_MAX;
        cutlim = cutoff % (unsigned long long)10;
        cutoff /= (unsigned long long)10;

        while (1) {
            if (c >= '0' && c <= '9')
                c -= '0';
            else
                break;

            if (any < 0 || result > cutoff || (result == cutoff && c > cutlim))
                any = -1;
            else {
                any = 1;
                result *= 10;
                result += c;
            }
            if (i++ == strgraphs)
                break;
            c = *s++;
        }

        if (any < 0)
            result = negative ? LLONG_MIN : LLONG_MAX;
        else if (negative)
            result = -result;
    }
    else {
        MVMCodepointIter ci;
        MVM_string_ci_init(tc, &ci, str, 0, 0);
        MVMCodepoint ord;

        do {
            ord = MVM_string_ci_get_codepoint(tc, &ci);
        } while ((ord == ' ' || (ord >= '\t' && ord <= '\r')) && MVM_string_ci_has_more(tc, &ci));

        if (ord == '-') {
            negative = 1;
            if (!MVM_string_ci_has_more(tc, &ci))
                return 0;
            ord = MVM_string_ci_get_codepoint(tc, &ci);
        }
        else if (ord == '+') {
            if (!MVM_string_ci_has_more(tc, &ci))
                return 0;
            ord = MVM_string_ci_get_codepoint(tc, &ci);
        }

        cutoff = (unsigned long long)LLONG_MAX;
        cutlim = cutoff % (unsigned long long)10;
        cutoff /= (unsigned long long)10;

        while (1) {
            if (ord >= '0' && ord <= '9')
                ord -= '0';
            else
                break;

            if (any < 0 || result > cutoff || (result == cutoff && ord > cutlim))
                any = -1;
            else {
                any = 1;
                result *= 10;
                result += ord;
            }
            if (!MVM_string_ci_has_more(tc, &ci))
                break;
            ord = MVM_string_ci_get_codepoint(tc, &ci);
        };

        if (any < 0)
            result = negative ? LLONG_MIN : LLONG_MAX;
        else if (negative)
            result = -result;
    }

    if (negative) {
        MVM_exception_throw_adhoc(tc, "Cannot coerce negative number from string to native unsigned integer");
    }

    return result;
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
    MVMint64 chars  = MVM_string_graphs(tc, str);
    MVMint64 value  = zvalue;
    MVMuint32 chars_converted = 0;
    MVMuint32 chars_really_converted = chars_converted;
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
        offset++; pos = offset;
        chars_converted++;
        if (ch != 0 || !(flag & 0x04)) { value=zvalue; chars_really_converted = chars_converted; }
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
            boxed = MVM_repr_box_int(tc, box_type, chars_really_converted);
            MVM_repr_push_o(tc, result, boxed);
            boxed = MVM_repr_box_int(tc, box_type, pos);
            MVM_repr_push_o(tc, result, boxed);
        });
    });

    return result;
}
