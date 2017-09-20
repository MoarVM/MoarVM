#include "moar.h"
#include <math.h>

/* We put a ' ' into the current code point buffer when we reach the end of the string,
 *  as it's something that can be harmlessly added to the end of a number */

#define END_OF_NUM ' '
static int is_whitespace(MVMThreadContext *tc, MVMCodepoint cp) {
    if (cp <= '~') {
        if (cp == ' ' || (cp <= 13 && cp >= 9))
            return 1;
        else
            return 0;
     }
     return MVM_unicode_codepoint_has_property_value(tc, cp, MVM_UNICODE_PROPERTY_WHITE_SPACE, 1);
}

static int cp_value(MVMThreadContext *tc, MVMCodepoint cp) {
    if (cp >= '0' && cp <= '9') return cp - '0'; /* fast-path for ASCII 0..9 */
    else if (cp >= 'a' && cp <= 'z') return cp - 'a' + 10;
    else if (cp >= 'A' && cp <= 'Z') return cp - 'A' + 10;
    else if (cp >= 0xFF21 && cp <= 0xFF3A) return cp - 0xFF21 + 10; /* uppercase fullwidth */
    else if (cp >= 0xFF41 && cp <= 0xFF5A) return cp - 0xFF41 + 10; /* lowercase fullwidth */
    else if (cp > 0 && MVM_unicode_codepoint_get_property_int(tc, cp, MVM_UNICODE_PROPERTY_NUMERIC_TYPE)
     == MVM_UNICODE_PVALUE_Numeric_Type_DECIMAL) {
        /* as of Unicode 9.0.0, characters with the 'de' Numeric Type (and are
         * thus also of General Category Nd, since 4.0.0) are contiguous
         * sequences of 10 chars whose Numeric Values ascend from 0 through 9.
         */

        /* the string returned for NUMERIC_VALUE_NUMERATOR contains an integer
         * value. We can use numerator because they all are from 0-9 and have
         * denominator of 1 */
        return fast_atoi(MVM_unicode_codepoint_get_property_cstr(tc, cp, MVM_UNICODE_PROPERTY_NUMERIC_VALUE_NUMERATOR));
    }
    return -1;
}

int static get_cp(MVMThreadContext *tc, MVMCodepointIter *ci, MVMCodepoint *cp) {
    if (!MVM_string_ci_has_more(tc, ci)) {
        *cp = END_OF_NUM; // FIXME pick a safe value
        return 1;
    }
    else {
        *cp = MVM_string_ci_get_codepoint(tc, ci);
        return 0;
    }
}

static void parse_error(MVMThreadContext *tc, MVMString *s, const char* reason) {
    char* got = MVM_string_utf8_c8_encode_C_string(tc, s);
    char *waste[] = { got, NULL };
    MVM_exception_throw_adhoc_free(tc, waste, "Can't convert '%s' to num: %s", got, reason);
}

static void skip_whitespace(MVMThreadContext *tc, MVMCodepointIter *ci, MVMCodepoint *cp) {
    while (is_whitespace(tc, *cp)) {
        if (get_cp(tc, ci, cp)) return; 
    }
}

static int parse_sign(MVMThreadContext *tc, MVMCodepointIter *ci, MVMCodepoint *cp) {
    // Handle any leading +/-/− sign
    int has_minus = (*cp == '-' || *cp == 8722); // '-', '−'

    if (has_minus || *cp == '+') {  // '-', '−', '+'
        get_cp(tc, ci, cp);
    }

    return (has_minus ? -1 : 1);
}

static double parse_decimal_integer(MVMThreadContext *tc, MVMCodepointIter *ci, MVMCodepoint *cp, MVMString* s) {
    int ends_with_underscore = 0;
    double value = 0;
    int digit;
    if (*cp == '_') parse_error(tc, s, "number can't be start with _");
    while (*cp == '_' || (digit = cp_value(tc, *cp)) != -1) {
        ends_with_underscore = *cp == '_';
        if (*cp != '_') {
            if (digit >= 10) parse_error(tc, s, "expecting comma seprated decimal numbers after :$radix[]");
            value = value * 10 + digit;
        }
        get_cp(tc, ci, cp);
    }
    if (ends_with_underscore) parse_error(tc, s, "a number can't end in underscore");
    return value;
}

static double parse_int_frac_exp(MVMThreadContext *tc, MVMCodepointIter *ci, MVMCodepoint *cp, MVMString* s, double radix, int leading_zero) {
    double integer = 0;
    double frac = 0;
    double base = 1;
    int digits = 0;
    int frac_digits = 0;
    int digit;
    int ends_with_underscore = 0;

    if (*cp == '_') parse_error(tc, s, "number can't start with _");

    if (*cp != '.') {
        while (*cp == '_' || (digit = cp_value(tc, *cp)) != -1) {
            ends_with_underscore = *cp == '_';
            if (*cp != '_') {
                if (digit >= radix) break;
                integer = integer * radix + digit;
                digits++;
            }
            get_cp(tc, ci, cp);
        }
        if (ends_with_underscore) parse_error(tc, s, "a number can't end in underscore");
    }


    if (*cp == '.') {
        get_cp(tc, ci, cp);
        if (*cp == '_') parse_error(tc, s, "radix point can't be followed by _");
        while (*cp == '_' || (digit = cp_value(tc, *cp)) != -1) {
            ends_with_underscore = *cp == '_';
            if (*cp != '_') {
                if (digit >= radix) break;
                frac = frac * radix + digit;
                base = base * radix;
                frac_digits++;
            }
            get_cp(tc, ci, cp);
        }
        if (frac_digits == 0) {
            parse_error(tc, s, "radix point must be followed by one or more valid digits");
        }
        if (ends_with_underscore) parse_error(tc, s, "a number can't end in underscore");
    }

    if (digits == 0 && frac_digits == 0 && !leading_zero) parse_error(tc, s, "expecting a number");

    if (*cp == 'E' || *cp == 'e') {
        int e_digits = 0;
        double exponent = 0;
        double sign;

        get_cp(tc, ci, cp);

        sign = parse_sign(tc, ci, cp);

        if (*cp == '_') parse_error(tc, s, "'e' or 'E' can't be followed by _");
        while (*cp == '_' || (digit = cp_value(tc, *cp)) != -1) {
            if (*cp != '_') {
                if (digit >= radix) break;
                exponent = exponent * radix + digit;
                e_digits++;
            }
            get_cp(tc, ci, cp);
        }
        if (e_digits == 0) {
            parse_error(tc, s, "'e' or 'E' must be followed by one or more valid digits");
        }

        return (integer + frac/base) * pow(10, sign * exponent);
    }
    else {
        return integer + frac/base;
    }
}

static int match_word(MVMThreadContext *tc,  MVMCodepointIter *ci, MVMCodepoint *cp, char word[3], MVMString *s) {
    if (*cp == word[0]) {
        get_cp(tc, ci, cp);
        if (*cp == word[1]) {
            get_cp(tc, ci, cp);
            if (*cp == word[2]) {
                get_cp(tc, ci, cp);
                return 1;
            }
            else {
                parse_error(tc, s, "that's not a number");
            }
        }
        else {
            parse_error(tc, s, "that's not a number");
        }
    }
    return 0;
}


static double parse_simple_number(MVMThreadContext *tc, MVMCodepointIter *ci, MVMCodepoint *cp, MVMString *s) {
    double sign;
    // Handle NaN here, to make later parsing simpler

    if (match_word(tc, ci, cp, "NaN", s)) {
        return MVM_num_nan(tc);
    }

    sign = parse_sign(tc, ci, cp);

    if (match_word(tc, ci, cp, "Inf", s)) {
        return sign * MVM_num_posinf(tc);
    }
    else if (*cp == ':') {
        int radix;
        double body;
        get_cp(tc, ci, cp);
        radix = (int) parse_int_frac_exp(tc, ci, cp, s, 10, 0);
        if (*cp == '<') {
            get_cp(tc, ci, cp);
            body = parse_int_frac_exp(tc, ci, cp, s, radix, 0);
            if (*cp == '>') {
                get_cp(tc, ci, cp);
                return sign * body;
            }
            else {
                parse_error(tc, s, "malformed ':radix<>' style radix number, expecting '>' after the body");
            }
        }
        else if (*cp == 171) { // «
            get_cp(tc, ci, cp);
            body = parse_int_frac_exp(tc, ci, cp, s, radix, 0);
            if (*cp == 187) { // »
                get_cp(tc, ci, cp);
                return sign * body;
            }
            else {
                parse_error(tc, s, "malformed ':radix«»' style radix number, expecting '>' after the body");
            }
        }
        else if (*cp == '[') { // «
            double result = 0;
            get_cp(tc, ci, cp);
            while (*cp != ']' && MVM_string_ci_has_more(tc, ci)) {
                double digit = parse_decimal_integer(tc, ci, cp, s);
                result = result * radix + digit;
                if (*cp == ',') {
                    get_cp(tc, ci, cp);
                }
            }
            if (*cp == ']') { // »
                get_cp(tc, ci, cp);
                return sign * result;
            }
            else {
                parse_error(tc, s, "malformed ':radix[]' style radix number, expecting ']' after the body");
            }
        }
    }
    else if (*cp == '0') {
        int radix = 0;

        get_cp(tc, ci, cp);
        if (*cp == 'b') radix = 2;
        else if (*cp == 'o') radix = 8;
        else if (*cp == 'd') radix = 10;
        else if (*cp == 'x') radix = 16;

        if (radix) {
            get_cp(tc, ci, cp);
            if (*cp == '_') get_cp(tc, ci, cp);
            return sign * parse_int_frac_exp(tc, ci, cp, s, radix, 1);
        } else {
            return sign * parse_int_frac_exp(tc, ci, cp, s, 10, 1);
        }
    }
    else {
        return sign * parse_int_frac_exp(tc, ci, cp, s, 10, 0);
    }
}

static double parse_real(MVMThreadContext *tc, MVMCodepointIter *ci, MVMCodepoint *cp, MVMString *s) {
    double result = parse_simple_number(tc, ci, cp, s);
    double denom;

    // Check for '/' indicating Rat denominator
    if (*cp == '/') {
        get_cp(tc, ci, cp);
        denom = parse_simple_number(tc, ci, cp, s);
        result = result / denom;
    }
    return result;
}

MVMnum64 MVM_coerce_s_n(MVMThreadContext *tc, MVMString *s) {
    MVMCodepointIter ci;
    MVMCodepoint cp;
    MVMnum64  n = 123;
    MVM_string_ci_init(tc, &ci, s, 0, 0);

    if (get_cp(tc, &ci, &cp)) return 0; 

    skip_whitespace(tc, &ci, &cp);

    // Do we have only whitespace
    if (!MVM_string_ci_has_more(tc, &ci) && cp == END_OF_NUM) {
        return 0;
    }

    n = parse_real(tc, &ci, &cp, s);

    skip_whitespace(tc, &ci, &cp);

    if (MVM_string_ci_has_more(tc, &ci) || cp != END_OF_NUM) {
        parse_error(tc, s, "trailing characters");
    }

    return n;
}
