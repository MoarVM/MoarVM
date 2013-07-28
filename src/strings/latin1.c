#include "moarvm.h"

#define LATIN1_CHAR_TO_CP(character) codepoints[character];

static const MVMuint16 codepoints[] = {
    0x0000,0x0001,0x0002,0x0003,0x0004,0x0005,0x0006,0x0007,
    0x0008,0x0009,0x000A,0x000B,0x000C,0x000D,0x000E,0x000F,
    0x0010,0x0011,0x0012,0x0013,0x0014,0x0015,0x0016,0x0017,
    0x0018,0x0019,0x001A,0x001B,0x001C,0x001D,0x001E,0x001F,
    0x0020,0x0021,0x0022,0x0023,0x0024,0x0025,0x0026,0x0027,
    0x0028,0x0029,0x002A,0x002B,0x002C,0x002D,0x002E,0x002F,
    0x0030,0x0031,0x0032,0x0033,0x0034,0x0035,0x0036,0x0037,
    0x0038,0x0039,0x003A,0x003B,0x003C,0x003D,0x003E,0x003F,
    0x0040,0x0041,0x0042,0x0043,0x0044,0x0045,0x0046,0x0047,
    0x0048,0x0049,0x004A,0x004B,0x004C,0x004D,0x004E,0x004F,
    0x0050,0x0051,0x0052,0x0053,0x0054,0x0055,0x0056,0x0057,
    0x0058,0x0059,0x005A,0x005B,0x005C,0x005D,0x005E,0x005F,
    0x0060,0x0061,0x0062,0x0063,0x0064,0x0065,0x0066,0x0067,
    0x0068,0x0069,0x006A,0x006B,0x006C,0x006D,0x006E,0x006F,
    0x0070,0x0071,0x0072,0x0073,0x0074,0x0075,0x0076,0x0077,
    0x0078,0x0079,0x007A,0x007B,0x007C,0x007D,0x007E,0x007F,
    0x20AC,0x0081,0x201A,0x0192,0x201E,0x2026,0x2020,0x2021,
    0x02C6,0x2030,0x0160,0x2039,0x0152,0x008D,0x017D,0x008F,
    0x0090,0x2018,0x2019,0x201C,0x201D,0x2022,0x2013,0x2014,
    0x02DC,0x2122,0x0161,0x203A,0x0153,0x009D,0x017E,0x0178,
    0x00A0,0x00A1,0x00A2,0x00A3,0x00A4,0x00A5,0x00A6,0x00A7,
    0x00A8,0x00A9,0x00AA,0x00AB,0x00AC,0x00AD,0x00AE,0x00AF,
    0x00B0,0x00B1,0x00B2,0x00B3,0x00B4,0x00B5,0x00B6,0x00B7,
    0x00B8,0x00B9,0x00BA,0x00BB,0x00BC,0x00BD,0x00BE,0x00BF,
    0x00C0,0x00C1,0x00C2,0x00C3,0x00C4,0x00C5,0x00C6,0x00C7,
    0x00C8,0x00C9,0x00CA,0x00CB,0x00CC,0x00CD,0x00CE,0x00CF,
    0x00D0,0x00D1,0x00D2,0x00D3,0x00D4,0x00D5,0x00D6,0x00D7,
    0x00D8,0x00D9,0x00DA,0x00DB,0x00DC,0x00DD,0x00DE,0x00DF,
    0x00E0,0x00E1,0x00E2,0x00E3,0x00E4,0x00E5,0x00E6,0x00E7,
    0x00E8,0x00E9,0x00EA,0x00EB,0x00EC,0x00ED,0x00EE,0x00EF,
    0x00F0,0x00F1,0x00F2,0x00F3,0x00F4,0x00F5,0x00F6,0x00F7,
    0x00F8,0x00F9,0x00FA,0x00FB,0x00FC,0x00FD,0x00FE,0x00FF
};

static MVMuint8 latin1_cp_to_char(MVMint32 codepoint) {
    if (codepoint <= 8216) {
        if (codepoint <= 352) {
            if (codepoint <= 143) {
                if (codepoint <= 141) {
                    if (codepoint == 129) { return 129; }
                    if (codepoint == 141) { return 141; }
                }
                else {
                    if (codepoint == 143) { return 143; }
                }
            }
            else {
                if (codepoint <= 338) {
                    if (codepoint == 144) { return 144; }
                    if (codepoint == 338) { return 140; }
                }
                else {
                    if (codepoint == 352) { return 138; }
                }
            }
        }
        else {
            if (codepoint <= 710) {
                if (codepoint <= 402) {
                    if (codepoint == 381) { return 142; }
                    if (codepoint == 402) { return 131; }
                }
                else {
                    if (codepoint == 710) { return 136; }
                }
            }
            else {
                if (codepoint <= 8212) {
                    if (codepoint == 8211) { return 150; }
                    if (codepoint == 8212) { return 151; }
                }
                else {
                    if (codepoint == 8216) { return 145; }
                }
            }
        }
    }
    else {
        if (codepoint <= 8224) {
            if (codepoint <= 8220) {
                if (codepoint <= 8218) {
                    if (codepoint == 8217) { return 146; }
                    if (codepoint == 8218) { return 130; }
                }
                else {
                    if (codepoint == 8220) { return 147; }
                }
            }
            else {
                if (codepoint <= 8222) {
                    if (codepoint == 8221) { return 148; }
                    if (codepoint == 8222) { return 132; }
                }
                else {
                    if (codepoint == 8224) { return 134; }
                }
            }
        }
        else {
            if (codepoint <= 8230) {
                if (codepoint <= 8226) {
                    if (codepoint == 8225) { return 135; }
                    if (codepoint == 8226) { return 149; }
                }
                else {
                    if (codepoint == 8230) { return 133; }
                }
            }
            else {
                if (codepoint <= 8249) {
                    if (codepoint == 8240) { return 137; }
                    if (codepoint == 8249) { return 139; }
                }
                else {
                    if (codepoint == 8364) { return 128; }
                }
            }
        }
    }

    return '?';

}

/* Decodes the specified number of bytes of latin1 (well, really Windows 1252)
 into an NFG string, creating
 * a result of the specified type. The type must have the MVMString REPR. */
MVMString * MVM_string_latin1_decode(MVMThreadContext *tc,
        MVMObject *result_type, MVMuint8 *latin1, size_t bytes) {
    MVMString *result = (MVMString *)REPR(result_type)->allocate(tc, STABLE(result_type));
    size_t i;

    result->body.codes  = bytes;
    result->body.graphs = bytes;

    result->body.int32s = malloc(sizeof(MVMint32) * bytes);
    for (i = 0; i < bytes; i++)
        /* actually decode like Windows-1252, since that is mostly a superset,
           and is recommended by the HTML5 standard when latin1 is claimed */
        result->body.int32s[i] = LATIN1_CHAR_TO_CP(latin1[i]);
    result->body.flags = MVM_STRING_TYPE_INT32;
    return result;
}

/* Encodes the specified substring to latin-1. Anything outside of latin-1 range
 * will become a ?. The result string is NULL terminated, but the specified
 * size is the non-null part. */
MVMuint8 * MVM_string_latin1_encode_substr(MVMThreadContext *tc, MVMString *str, MVMuint64 *output_size, MVMint64 start, MVMint64 length) {
    /* latin-1 is a single byte encoding, so each grapheme will just become
     * a single byte. */
    MVMuint32 startu = (MVMuint32)start;
    MVMStringIndex strgraphs = NUM_GRAPHS(str);
    MVMuint32 lengthu = (MVMuint32)(length == -1 ? strgraphs - startu : length);
    MVMuint8 *result;
    size_t i;

    /* must check start first since it's used in the length check */
    if (start < 0 || start > strgraphs)
        MVM_exception_throw_adhoc(tc, "start out of range");
    if (length < 0 || start + length > strgraphs)
        MVM_exception_throw_adhoc(tc, "length out of range");

    result = malloc(length + 1);
    for (i = 0; i < length; i++) {
        MVMint32 codepoint = MVM_string_get_codepoint_at_nocheck(tc, str, start + i);
        if ((codepoint >= 0 && codepoint < 128) || (codepoint >= 152 && codepoint < 256)) {
            result[i] = (MVMuint8)codepoint;
        }
        else if (codepoint > 8364 || codepoint < 0) {
            result[i] = '?';
        }
        else {
            result[i] = latin1_cp_to_char(codepoint);
        }
    }
    result[i] = 0;
    if (output_size)
        *output_size = length;
    return result;
}


