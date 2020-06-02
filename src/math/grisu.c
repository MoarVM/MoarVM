/*
 *  Code in this file is taken from MathGeoLib by JUKKA JYLANKI,
 *  licensed under Apache 2.0 license.
 *  http://clb.demon.fi/MathGeoLib/nightly/sourcecode.html#license
 *  http://www.apache.org/licenses/LICENSE-2.0.html
 *  http://clb.demon.fi/MathGeoLib/nightly/docs/grisu3.c_code.html
 *
 *  It contains minor modifications, to match output expected by Raku along
 *  with minor edits to arguments and formatting.
 *
 *  This file is part of an implementation of the "grisu3" double to string
 *  conversion algorithm described in the research paper
 *  "Printing Floating-Point Numbers Quickly And Accurately with Integers"
 *  by Florian Loitsch, available at
 *  http://www.cs.tufts.edu/~nr/cs257/archive/florian-loitsch/printf.pdf
 *
 *  Grisu3 is able to handle about 99.5% of numbers, for the remaining 0.5%
 *  the code below fallsback to snprintf(). It's possible that using a
 *  different fallback (such as Dragon4 algorithm) can improve performance.
*/

#include "grisu.h"
#include "platform/stdint.h"  // uint64_t etc.
#include <math.h>   // ceil
#include <stdio.h>  // snprintf
#define ULL UINT64_C
#define D64_SIGN         ULL(0x8000000000000000)
#define D64_EXP_MASK     ULL(0x7FF0000000000000)
#define D64_FRACT_MASK   ULL(0x000FFFFFFFFFFFFF)
#define D64_IMPLICIT_ONE ULL(0x0010000000000000)
#define D64_EXP_POS      52
#define D64_EXP_BIAS     1075
#define DIYFP_FRACT_SIZE 64
#define D_1_LOG2_10      0.30102999566398114 // 1 / lg(10)
#define MIN_TARGET_EXP   -60
#define MASK32           ULL(0xFFFFFFFF)

#define MIN(x,y) ((x) <= (y) ? (x) : (y))
#define MAX(x,y) ((x) >= (y) ? (x) : (y))

#define MIN_CACHED_EXP -348
#define CACHED_EXP_STEP 8

typedef struct diy_fp
{
        uint64_t f;
        int e;
} diy_fp;

typedef struct power
{
        uint64_t fract;
        int16_t b_exp, d_exp;
} power;

typedef union double_memory
{
        uint64_t u;
        double d;
} double_memory;

static const power pow_cache[] =
{
        { ULL(0xfa8fd5a0081c0288), -1220, -348 },
        { ULL(0xbaaee17fa23ebf76), -1193, -340 },
        { ULL(0x8b16fb203055ac76), -1166, -332 },
        { ULL(0xcf42894a5dce35ea), -1140, -324 },
        { ULL(0x9a6bb0aa55653b2d), -1113, -316 },
        { ULL(0xe61acf033d1a45df), -1087, -308 },
        { ULL(0xab70fe17c79ac6ca), -1060, -300 },
        { ULL(0xff77b1fcbebcdc4f), -1034, -292 },
        { ULL(0xbe5691ef416bd60c), -1007, -284 },
        { ULL(0x8dd01fad907ffc3c),  -980, -276 },
        { ULL(0xd3515c2831559a83),  -954, -268 },
        { ULL(0x9d71ac8fada6c9b5),  -927, -260 },
        { ULL(0xea9c227723ee8bcb),  -901, -252 },
        { ULL(0xaecc49914078536d),  -874, -244 },
        { ULL(0x823c12795db6ce57),  -847, -236 },
        { ULL(0xc21094364dfb5637),  -821, -228 },
        { ULL(0x9096ea6f3848984f),  -794, -220 },
        { ULL(0xd77485cb25823ac7),  -768, -212 },
        { ULL(0xa086cfcd97bf97f4),  -741, -204 },
        { ULL(0xef340a98172aace5),  -715, -196 },
        { ULL(0xb23867fb2a35b28e),  -688, -188 },
        { ULL(0x84c8d4dfd2c63f3b),  -661, -180 },
        { ULL(0xc5dd44271ad3cdba),  -635, -172 },
        { ULL(0x936b9fcebb25c996),  -608, -164 },
        { ULL(0xdbac6c247d62a584),  -582, -156 },
        { ULL(0xa3ab66580d5fdaf6),  -555, -148 },
        { ULL(0xf3e2f893dec3f126),  -529, -140 },
        { ULL(0xb5b5ada8aaff80b8),  -502, -132 },
        { ULL(0x87625f056c7c4a8b),  -475, -124 },
        { ULL(0xc9bcff6034c13053),  -449, -116 },
        { ULL(0x964e858c91ba2655),  -422, -108 },
        { ULL(0xdff9772470297ebd),  -396, -100 },
        { ULL(0xa6dfbd9fb8e5b88f),  -369,  -92 },
        { ULL(0xf8a95fcf88747d94),  -343,  -84 },
        { ULL(0xb94470938fa89bcf),  -316,  -76 },
        { ULL(0x8a08f0f8bf0f156b),  -289,  -68 },
        { ULL(0xcdb02555653131b6),  -263,  -60 },
        { ULL(0x993fe2c6d07b7fac),  -236,  -52 },
        { ULL(0xe45c10c42a2b3b06),  -210,  -44 },
        { ULL(0xaa242499697392d3),  -183,  -36 },
        { ULL(0xfd87b5f28300ca0e),  -157,  -28 },
        { ULL(0xbce5086492111aeb),  -130,  -20 },
        { ULL(0x8cbccc096f5088cc),  -103,  -12 },
        { ULL(0xd1b71758e219652c),   -77,   -4 },
        { ULL(0x9c40000000000000),   -50,    4 },
        { ULL(0xe8d4a51000000000),   -24,   12 },
        { ULL(0xad78ebc5ac620000),     3,   20 },
        { ULL(0x813f3978f8940984),    30,   28 },
        { ULL(0xc097ce7bc90715b3),    56,   36 },
        { ULL(0x8f7e32ce7bea5c70),    83,   44 },
        { ULL(0xd5d238a4abe98068),   109,   52 },
        { ULL(0x9f4f2726179a2245),   136,   60 },
        { ULL(0xed63a231d4c4fb27),   162,   68 },
        { ULL(0xb0de65388cc8ada8),   189,   76 },
        { ULL(0x83c7088e1aab65db),   216,   84 },
        { ULL(0xc45d1df942711d9a),   242,   92 },
        { ULL(0x924d692ca61be758),   269,  100 },
        { ULL(0xda01ee641a708dea),   295,  108 },
        { ULL(0xa26da3999aef774a),   322,  116 },
        { ULL(0xf209787bb47d6b85),   348,  124 },
        { ULL(0xb454e4a179dd1877),   375,  132 },
        { ULL(0x865b86925b9bc5c2),   402,  140 },
        { ULL(0xc83553c5c8965d3d),   428,  148 },
        { ULL(0x952ab45cfa97a0b3),   455,  156 },
        { ULL(0xde469fbd99a05fe3),   481,  164 },
        { ULL(0xa59bc234db398c25),   508,  172 },
        { ULL(0xf6c69a72a3989f5c),   534,  180 },
        { ULL(0xb7dcbf5354e9bece),   561,  188 },
        { ULL(0x88fcf317f22241e2),   588,  196 },
        { ULL(0xcc20ce9bd35c78a5),   614,  204 },
        { ULL(0x98165af37b2153df),   641,  212 },
        { ULL(0xe2a0b5dc971f303a),   667,  220 },
        { ULL(0xa8d9d1535ce3b396),   694,  228 },
        { ULL(0xfb9b7cd9a4a7443c),   720,  236 },
        { ULL(0xbb764c4ca7a44410),   747,  244 },
        { ULL(0x8bab8eefb6409c1a),   774,  252 },
        { ULL(0xd01fef10a657842c),   800,  260 },
        { ULL(0x9b10a4e5e9913129),   827,  268 },
        { ULL(0xe7109bfba19c0c9d),   853,  276 },
        { ULL(0xac2820d9623bf429),   880,  284 },
        { ULL(0x80444b5e7aa7cf85),   907,  292 },
        { ULL(0xbf21e44003acdd2d),   933,  300 },
        { ULL(0x8e679c2f5e44ff8f),   960,  308 },
        { ULL(0xd433179d9c8cb841),   986,  316 },
        { ULL(0x9e19db92b4e31ba9),  1013,  324 },
        { ULL(0xeb96bf6ebadf77d9),  1039,  332 },
        { ULL(0xaf87023b9bf0ee6b),  1066,  340 }
};

static int cached_pow(int exp, diy_fp *p)
{
        int k = (int)ceil((exp+DIYFP_FRACT_SIZE-1) * D_1_LOG2_10);
        int i = (k-MIN_CACHED_EXP-1) / CACHED_EXP_STEP + 1;
        p->f = pow_cache[i].fract;
        p->e = pow_cache[i].b_exp;
        return pow_cache[i].d_exp;
}

static diy_fp minus(diy_fp x, diy_fp y)
{
        diy_fp d; d.f = x.f - y.f; d.e = x.e;
        return d;
}

static diy_fp multiply(diy_fp x, diy_fp y)
{
        uint64_t a, b, c, d, ac, bc, ad, bd, tmp;
        diy_fp r;
        a = x.f >> 32; b = x.f & MASK32;
        c = y.f >> 32; d = y.f & MASK32;
        ac = a*c; bc = b*c;
        ad = a*d; bd = b*d;
        tmp = (bd >> 32) + (ad & MASK32) + (bc & MASK32);
        tmp += 1U << 31; // round
        r.f = ac + (ad >> 32) + (bc >> 32) + (tmp >> 32);
        r.e = x.e + y.e + 64;
        return r;
}

static diy_fp normalize_diy_fp(diy_fp n)
{
        while(!(n.f & ULL(0xFFC0000000000000))) { n.f <<= 10; n.e -= 10; }
        while(!(n.f & D64_SIGN)) { n.f <<= 1; --n.e; }
        return n;
}

static diy_fp double2diy_fp(double d)
{
        diy_fp fp;
        double_memory u64;
        u64.d = d;
        if (!(u64.u & D64_EXP_MASK)) { fp.f = u64.u & D64_FRACT_MASK; fp.e = 1 - D64_EXP_BIAS; }
        else { fp.f = (u64.u & D64_FRACT_MASK) + D64_IMPLICIT_ONE; fp.e = (int)((u64.u & D64_EXP_MASK) >> D64_EXP_POS) - D64_EXP_BIAS; }
        return fp;
}

// pow10_cache[i] = 10^(i-1)
static const unsigned int pow10_cache[] = { 0, 1, 10, 100, 1000, 10000, 100000, 1000000, 10000000, 100000000, 1000000000 };

static int largest_pow10(uint32_t n, int n_bits, uint32_t *power)
{
        int guess = ((n_bits + 1) * 1233 >> 12) + 1/*skip first entry*/;
        if (n < pow10_cache[guess]) --guess; // We don't have any guarantees that 2^n_bits <= n.
        *power = pow10_cache[guess];
        return guess;
}

static int round_weed(char *buffer, int len, uint64_t wp_W, uint64_t delta, uint64_t rest, uint64_t ten_kappa, uint64_t ulp)
{
        uint64_t wp_Wup = wp_W - ulp;
        uint64_t wp_Wdown = wp_W + ulp;
        while(rest < wp_Wup && delta - rest >= ten_kappa
                && (rest + ten_kappa < wp_Wup || wp_Wup - rest >= rest + ten_kappa - wp_Wup))
        {
                --buffer[len-1];
                rest += ten_kappa;
        }
        if (rest < wp_Wdown && delta - rest >= ten_kappa
                && (rest + ten_kappa < wp_Wdown || wp_Wdown - rest > rest + ten_kappa - wp_Wdown))
                return 0;

        return 2*ulp <= rest && rest <= delta - 4*ulp;
}

static int digit_gen(diy_fp low, diy_fp w, diy_fp high, char *buffer, int *length, int *kappa)
{
        uint64_t unit = 1;
        diy_fp too_low = { low.f - unit, low.e };
        diy_fp too_high = { high.f + unit, high.e };
        diy_fp unsafe_interval = minus(too_high, too_low);
        diy_fp one = { ULL(1) << -w.e, w.e };
        uint32_t p1 = (uint32_t)(too_high.f >> -one.e);
        uint64_t p2 = too_high.f & (one.f - 1);
        uint32_t div;
        *kappa = largest_pow10(p1, DIYFP_FRACT_SIZE + one.e, &div);
        *length = 0;

        while(*kappa > 0)
        {
                uint64_t rest;
                int digit = p1 / div;
                buffer[*length] = (char)('0' + digit);
                ++*length;
                p1 %= div;
                --*kappa;
                rest = ((uint64_t)p1 << -one.e) + p2;
                if (rest < unsafe_interval.f) return round_weed(buffer, *length, minus(too_high, w).f, unsafe_interval.f, rest, (uint64_t)div << -one.e, unit);
                div /= 10;
        }

        for(;;)
        {
                int digit;
                p2 *= 10;
                unit *= 10;
                unsafe_interval.f *= 10;
                // Integer division by one.
                digit = (int)(p2 >> -one.e);
                buffer[*length] = (char)('0' + digit);
                ++*length;
                p2 &= one.f - 1;  // Modulo by one.
                --*kappa;
                if (p2 < unsafe_interval.f) return round_weed(buffer, *length, minus(too_high, w).f * unit, unsafe_interval.f, p2, one.f, unit);
        }
}

static int grisu3(double v, char *buffer, int *length, int *d_exp)
{
        int mk, kappa, success;
        diy_fp dfp = double2diy_fp(v);
        diy_fp w = normalize_diy_fp(dfp);

        // normalize boundaries
        diy_fp t = { (dfp.f << 1) + 1, dfp.e - 1 };
        diy_fp b_plus = normalize_diy_fp(t);
        diy_fp b_minus;
        diy_fp c_mk; // Cached power of ten: 10^-k
        double_memory u64;
        u64.d = v;
        if (!(u64.u & D64_FRACT_MASK) && (u64.u & D64_EXP_MASK) != 0) { b_minus.f = (dfp.f << 2) - 1; b_minus.e =  dfp.e - 2;} // lower boundary is closer?
        else { b_minus.f = (dfp.f << 1) - 1; b_minus.e = dfp.e - 1; }
        b_minus.f = b_minus.f << (b_minus.e - b_plus.e);
        b_minus.e = b_plus.e;

        mk = cached_pow(MIN_TARGET_EXP - DIYFP_FRACT_SIZE - w.e, &c_mk);

        w = multiply(w, c_mk);
        b_minus = multiply(b_minus, c_mk);
        b_plus  = multiply(b_plus,  c_mk);

        success = digit_gen(b_minus, w, b_plus, buffer, length, &kappa);
        *d_exp = kappa - mk;
        return success;
}

static int i_to_str(int val, char *str)
{
        int len, i;
        char *s;
        char *begin = str;
        if (val < 0) {
            *str++ = '-';
            if (val > -10)
                *str++ = '0';
            val = -val;
        }
        else
            *str++ = '+';
        s = str;

        for(;;)
        {
                int ni = val / 10;
                int digit = val - ni*10;
                *s++ = (char)('0' + digit);
                if (ni == 0)
                        break;
                val = ni;
        }
        *s = '\0';
        len = (int)(s - str);
        for(i = 0; i < len/2; ++i)
        {
                char ch = str[i];
                str[i] = str[len-1-i];
                str[len-1-i] = ch;
        }

        return (int)(s - begin);
}

int dtoa_grisu3(double v, char *dst, int size) {
        int d_exp, len, success, i, decimal_pos;
        double_memory u64;
        char *s2 = dst;
        u64.d = v;

        // Prehandle NaNs
        if ((u64.u << 1) > ULL(0xFFE0000000000000)) {
            *s2++ = 'N'; *s2++ = 'a'; *s2++ = 'N'; *s2 = '\0';
            return (int)(s2 - dst);
        }
        // Prehandle negative values.
        if ((u64.u & D64_SIGN) != 0) {
            *s2++ = '-'; v = -v; u64.u ^= D64_SIGN;
        }
        // Prehandle zero.
        if (!u64.u) {
            *s2++ = '0'; *s2 = '\0';
            return (int)(s2 - dst);
        }
        // Prehandle infinity.
        if (u64.u == D64_EXP_MASK) {
            *s2++ = 'I'; *s2++ = 'n'; *s2++ = 'f'; *s2 = '\0';
            return (int)(s2 - dst);
        }

        success = grisu3(v, s2, &len, &d_exp);
        // If grisu3 was not able to convert the number to a string, then use old snprintf (suboptimal).
        if (!success) return snprintf(s2, size, "%.17g", v) + (int)(s2 - dst);

        decimal_pos = len + d_exp;
        if (decimal_pos > 0) {
            decimal_pos -= len;
            if (decimal_pos > 0) {
                if (len + d_exp <= 15) {
                    while (decimal_pos--) s2[len++] = '0';
                }
                else {
                    if (len > 1) {
                        for (i = 0; i < len-1; i++)
                            s2[len-i] = s2[len-i-1];
                        d_exp += i;
                        s2[1] = '.';
                        len++;
                    }
                    s2[len++] = 'e';
                    len += i_to_str(d_exp, s2+len);
                }
            }
            else if (decimal_pos < 0) {
                for (i = 0; i < -decimal_pos; i++)
                    s2[len-i] = s2[len-i-1];
                s2[len + decimal_pos] = '.';
                len++;
            }
        }
        else if (decimal_pos > -4) {
            for (i = 0; i < len; i++)
                s2[len-decimal_pos-i+1] = s2[len-i-1];
            s2[0] = '0';
            s2[1] = '.';
            for (i = 0; i < -decimal_pos; i++)
                s2[2+i] = '0';
            len -= decimal_pos-2;
        }
        else {
            if (len > 1) {
                for (i = 0; i < len-1; i++)
                    s2[len-i] = s2[len-i-1];
                d_exp += i;
                s2[1] = '.';
                len++;
            }
            s2[len++] = 'e';
            len += i_to_str(d_exp, s2+len);
        }

        s2[len] = '\0'; // grisu3 doesn't null terminate, so ensure termination.
        return (int)(s2+len-dst);
}
