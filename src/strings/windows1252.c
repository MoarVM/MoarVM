#include "moar.h"
#define UNMAPPED 0xFFFF

/* Windows-1252 Latin */
static const MVMuint16 windows1252_codepoints[] = {
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
    0x20AC,0xFFFF,0x201A,0x0192,0x201E,0x2026,0x2020,0x2021,
    0x02C6,0x2030,0x0160,0x2039,0x0152,0xFFFF,0x017D,0xFFFF,
    0xFFFF,0x2018,0x2019,0x201C,0x201D,0x2022,0x2013,0x2014,
    0x02DC,0x2122,0x0161,0x203A,0x0153,0xFFFF,0x017E,0x0178,
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
/* Windows-1251 Cyrillic */
static const MVMuint16 windows1251_codepoints[] = {
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
    0x0402,0x0403,0x201A,0x0453,0x201E,0x2026,0x2020,0x2021,
    0x20AC,0x2030,0x0409,0x2039,0x040A,0x040C,0x040B,0x040F,
    0x0452,0x2018,0x2019,0x201C,0x201D,0x2022,0x2013,0x2014,
    0xFFFF,0x2122,0x0459,0x203A,0x045A,0x045C,0x045B,0x045F,
    0x00A0,0x040E,0x045E,0x0408,0x00A4,0x0490,0x00A6,0x00A7,
    0x0401,0x00A9,0x0404,0x00AB,0x00AC,0x00AD,0x00AE,0x0407,
    0x00B0,0x00B1,0x0406,0x0456,0x0491,0x00B5,0x00B6,0x00B7,
    0x0451,0x2116,0x0454,0x00BB,0x0458,0x0405,0x0455,0x0457,
    0x0410,0x0411,0x0412,0x0413,0x0414,0x0415,0x0416,0x0417,
    0x0418,0x0419,0x041A,0x041B,0x041C,0x041D,0x041E,0x041F,
    0x0420,0x0421,0x0422,0x0423,0x0424,0x0425,0x0426,0x0427,
    0x0428,0x0429,0x042A,0x042B,0x042C,0x042D,0x042E,0x042F,
    0x0430,0x0431,0x0432,0x0433,0x0434,0x0435,0x0436,0x0437,
    0x0438,0x0439,0x043A,0x043B,0x043C,0x043D,0x043E,0x043F,
    0x0440,0x0441,0x0442,0x0443,0x0444,0x0445,0x0446,0x0447,
    0x0448,0x0449,0x044A,0x044B,0x044C,0x044D,0x044E,0x044F
};
static MVMuint8 windows1252_cp_to_char(MVMint32 codepoint) {
    if (8482 < codepoint || codepoint < 0)
        return '\0';
    switch (codepoint) {
        case 160: return 160;
        case 161: return 161;
        case 162: return 162;
        case 163: return 163;
        case 164: return 164;
        case 165: return 165;
        case 166: return 166;
        case 167: return 167;
        case 168: return 168;
        case 169: return 169;
        case 170: return 170;
        case 171: return 171;
        case 172: return 172;
        case 173: return 173;
        case 174: return 174;
        case 175: return 175;
        case 176: return 176;
        case 177: return 177;
        case 178: return 178;
        case 179: return 179;
        case 180: return 180;
        case 181: return 181;
        case 182: return 182;
        case 183: return 183;
        case 184: return 184;
        case 185: return 185;
        case 186: return 186;
        case 187: return 187;
        case 188: return 188;
        case 189: return 189;
        case 190: return 190;
        case 191: return 191;
        case 192: return 192;
        case 193: return 193;
        case 194: return 194;
        case 195: return 195;
        case 196: return 196;
        case 197: return 197;
        case 198: return 198;
        case 199: return 199;
        case 200: return 200;
        case 201: return 201;
        case 202: return 202;
        case 203: return 203;
        case 204: return 204;
        case 205: return 205;
        case 206: return 206;
        case 207: return 207;
        case 208: return 208;
        case 209: return 209;
        case 210: return 210;
        case 211: return 211;
        case 212: return 212;
        case 213: return 213;
        case 214: return 214;
        case 215: return 215;
        case 216: return 216;
        case 217: return 217;
        case 218: return 218;
        case 219: return 219;
        case 220: return 220;
        case 221: return 221;
        case 222: return 222;
        case 223: return 223;
        case 224: return 224;
        case 225: return 225;
        case 226: return 226;
        case 227: return 227;
        case 228: return 228;
        case 229: return 229;
        case 230: return 230;
        case 231: return 231;
        case 232: return 232;
        case 233: return 233;
        case 234: return 234;
        case 235: return 235;
        case 236: return 236;
        case 237: return 237;
        case 238: return 238;
        case 239: return 239;
        case 240: return 240;
        case 241: return 241;
        case 242: return 242;
        case 243: return 243;
        case 244: return 244;
        case 245: return 245;
        case 246: return 246;
        case 247: return 247;
        case 248: return 248;
        case 249: return 249;
        case 250: return 250;
        case 251: return 251;
        case 252: return 252;
        case 253: return 253;
        case 254: return 254;
        case 255: return 255;
        case 338: return 140;
        case 339: return 156;
        case 352: return 138;
        case 353: return 154;
        case 376: return 159;
        case 381: return 142;
        case 382: return 158;
        case 402: return 131;
        case 710: return 136;
        case 732: return 152;
        case 8211: return 150;
        case 8212: return 151;
        case 8216: return 145;
        case 8217: return 146;
        case 8218: return 130;
        case 8220: return 147;
        case 8221: return 148;
        case 8222: return 132;
        case 8224: return 134;
        case 8225: return 135;
        case 8226: return 149;
        case 8230: return 133;
        case 8240: return 137;
        case 8249: return 139;
        case 8250: return 155;
        case 8364: return 128;
        case 8482: return 153;
        default: return '\0';
    };
}
static MVMuint8 windows1251_cp_to_char(MVMint32 codepoint) {
    if (8482 < codepoint || codepoint < 0)
        return '\0';
    switch (codepoint) {
        case 160: return 160;
        case 164: return 164;
        case 166: return 166;
        case 167: return 167;
        case 169: return 169;
        case 171: return 171;
        case 172: return 172;
        case 173: return 173;
        case 174: return 174;
        case 176: return 176;
        case 177: return 177;
        case 181: return 181;
        case 182: return 182;
        case 183: return 183;
        case 187: return 187;
        case 1025: return 168;
        case 1026: return 128;
        case 1027: return 129;
        case 1028: return 170;
        case 1029: return 189;
        case 1030: return 178;
        case 1031: return 175;
        case 1032: return 163;
        case 1033: return 138;
        case 1034: return 140;
        case 1035: return 142;
        case 1036: return 141;
        case 1038: return 161;
        case 1039: return 143;
        case 1040: return 192;
        case 1041: return 193;
        case 1042: return 194;
        case 1043: return 195;
        case 1044: return 196;
        case 1045: return 197;
        case 1046: return 198;
        case 1047: return 199;
        case 1048: return 200;
        case 1049: return 201;
        case 1050: return 202;
        case 1051: return 203;
        case 1052: return 204;
        case 1053: return 205;
        case 1054: return 206;
        case 1055: return 207;
        case 1056: return 208;
        case 1057: return 209;
        case 1058: return 210;
        case 1059: return 211;
        case 1060: return 212;
        case 1061: return 213;
        case 1062: return 214;
        case 1063: return 215;
        case 1064: return 216;
        case 1065: return 217;
        case 1066: return 218;
        case 1067: return 219;
        case 1068: return 220;
        case 1069: return 221;
        case 1070: return 222;
        case 1071: return 223;
        case 1072: return 224;
        case 1073: return 225;
        case 1074: return 226;
        case 1075: return 227;
        case 1076: return 228;
        case 1077: return 229;
        case 1078: return 230;
        case 1079: return 231;
        case 1080: return 232;
        case 1081: return 233;
        case 1082: return 234;
        case 1083: return 235;
        case 1084: return 236;
        case 1085: return 237;
        case 1086: return 238;
        case 1087: return 239;
        case 1088: return 240;
        case 1089: return 241;
        case 1090: return 242;
        case 1091: return 243;
        case 1092: return 244;
        case 1093: return 245;
        case 1094: return 246;
        case 1095: return 247;
        case 1096: return 248;
        case 1097: return 249;
        case 1098: return 250;
        case 1099: return 251;
        case 1100: return 252;
        case 1101: return 253;
        case 1102: return 254;
        case 1103: return 255;
        case 1105: return 184;
        case 1106: return 144;
        case 1107: return 131;
        case 1108: return 186;
        case 1109: return 190;
        case 1110: return 179;
        case 1111: return 191;
        case 1112: return 188;
        case 1113: return 154;
        case 1114: return 156;
        case 1115: return 158;
        case 1116: return 157;
        case 1118: return 162;
        case 1119: return 159;
        case 1168: return 165;
        case 1169: return 180;
        case 8211: return 150;
        case 8212: return 151;
        case 8216: return 145;
        case 8217: return 146;
        case 8218: return 130;
        case 8220: return 147;
        case 8221: return 148;
        case 8222: return 132;
        case 8224: return 134;
        case 8225: return 135;
        case 8226: return 149;
        case 8230: return 133;
        case 8240: return 137;
        case 8249: return 139;
        case 8250: return 155;
        case 8364: return 136;
        case 8470: return 185;
        case 8482: return 153;
        default: return '\0';
    };
}

/* Decodes using a decodestream. Decodes as far as it can with the input
 * buffers, or until a stopper is reached. */
MVMuint32 MVM_string_windows125X_decodestream(MVMThreadContext *tc, MVMDecodeStream *ds,
                                         const MVMuint32 *stopper_chars,
                                         MVMDecodeStreamSeparators *seps,
                                         const MVMuint16 *codetable) {
    MVMuint32 count = 0, total = 0;
    MVMuint32 bufsize;
    MVMGrapheme32 *buffer = NULL;
    MVMDecodeStreamBytes *cur_bytes = NULL;
    MVMDecodeStreamBytes *last_accept_bytes = ds->bytes_head;
    MVMint32 last_accept_pos, last_was_cr;
    MVMuint32 reached_stopper;
    MVMStringIndex repl_length = ds->replacement ? MVM_string_graphs(tc, ds->replacement) : 0;
    MVMStringIndex repl_pos = 0;

    /* If there's no buffers, we're done. */
    if (!ds->bytes_head)
        return 0;
    last_accept_pos = ds->bytes_head_pos;

    /* If we're asked for zero chars, also done. */
    if (stopper_chars && *stopper_chars == 0)
        return 1;

    bufsize = ds->result_size_guess;
    buffer = MVM_malloc(bufsize * sizeof(MVMGrapheme32));

    /* Decode each of the buffers. */
    cur_bytes = ds->bytes_head;
    last_was_cr = 0;
    reached_stopper = 0;
    while (cur_bytes) {
        /* Process this buffer. */
        MVMint32  pos = cur_bytes == ds->bytes_head ? ds->bytes_head_pos : 0;
        MVMuint8 *bytes = cur_bytes->bytes;
        while (pos < cur_bytes->length || repl_pos) {
            MVMGrapheme32 graph;
            MVMCodepoint codepoint = codetable[bytes[pos++]];
            if (repl_pos) {
                graph = MVM_string_get_grapheme_at_nocheck(tc, ds->replacement, repl_pos++);
                if (repl_length <= repl_pos) repl_pos = 0;
                pos--;
            }
            else if (codepoint == UNMAPPED) {
                if (MVM_ENCODING_CONFIG_STRICT(ds->config)) {
                    if (ds->replacement) {
                        graph = MVM_string_get_grapheme_at_nocheck(tc, ds->replacement, repl_pos);
                        /* If the replacement is more than one grapheme we need
                         * to set repl_pos++ so we will grab the next grapheme on
                         * the next loop */
                        if (1 < repl_length) repl_pos++;
                    }
                    else {
                        /* Throw if it's unmapped */
                        char *enc_name = codetable == windows1252_codepoints
                            ? "Windows-1252" : "Windows-1251";
                        MVM_free(buffer);
                        MVM_exception_throw_adhoc(tc,
                            "Error decoding %s string: could not decode codepoint %d",
                             enc_name,
                             bytes[pos - 1]);
                    }
                }
                else {
                    /* Set it without translating, even though it creates
                     * standards uncompliant results */
                    graph = bytes[pos-1];
                }
            }
            else if (last_was_cr) {
                if (codepoint == '\n') {
                    graph = MVM_unicode_normalizer_translated_crlf(tc, &(ds->norm));
                }
                else {
                    graph = '\r';
                    pos--;
                }
                last_was_cr = 0;
            }
            else if (codepoint == '\r') {
                last_was_cr = 1;
                continue;
            }
            else {
                graph = codepoint;
            }
            if (count == bufsize) {
                /* We filled the buffer. Attach this one to the buffers
                 * linked list, and continue with a new one. */
                MVM_string_decodestream_add_chars(tc, ds, buffer, bufsize);
                buffer = MVM_malloc(bufsize * sizeof(MVMGrapheme32));
                count = 0;
            }
            buffer[count++] = graph;
            last_accept_bytes = cur_bytes;
            last_accept_pos = pos;
            total++;
            if (MVM_string_decode_stream_maybe_sep(tc, seps, codepoint)) {
                reached_stopper = 1;
                goto done;
            }
            else if (stopper_chars && *stopper_chars == total) {
                reached_stopper = 1;
                goto done;
            }
        }
        cur_bytes = cur_bytes->next;
    }
  done:

    /* Attach what we successfully parsed as a result buffer, and trim away
     * what we chewed through. */
    if (count) {
        MVM_string_decodestream_add_chars(tc, ds, buffer, count);
    }
    else {
        MVM_free(buffer);
    }
    MVM_string_decodestream_discard_to(tc, ds, last_accept_bytes, last_accept_pos);

    return reached_stopper;
}
/* Decodes using a decodestream. Decodes as far as it can with the input
 * buffers, or until a stopper is reached. */
MVMuint32 MVM_string_windows1252_decodestream(MVMThreadContext *tc, MVMDecodeStream *ds,
                                         const MVMuint32 *stopper_chars,
                                         MVMDecodeStreamSeparators *seps) {
    return MVM_string_windows125X_decodestream(tc, ds, stopper_chars, seps, windows1252_codepoints);
}
/* Decodes using a decodestream. Decodes as far as it can with the input
 * buffers, or until a stopper is reached. */
MVMuint32 MVM_string_windows1251_decodestream(MVMThreadContext *tc, MVMDecodeStream *ds,
                                         const MVMuint32 *stopper_chars,
                                         MVMDecodeStreamSeparators *seps) {
    return MVM_string_windows125X_decodestream(tc, ds, stopper_chars, seps, windows1251_codepoints);
}

/* Decodes the specified number of bytes of windows1252 into an NFG string,
 * creating a result of the specified type. The type must have the MVMString
 * REPR. */
MVMString * MVM_string_windows125X_decode(MVMThreadContext *tc,
        const MVMObject *result_type, char *windows125X_c, size_t bytes,
        MVMString *replacement, const MVMuint16 *codetable, MVMint64 config) {
    MVMuint8 *windows125X = (MVMuint8 *)windows125X_c;
    MVMString *result;
    size_t pos, result_graphs, additional_bytes = 0;
    MVMStringIndex repl_length = replacement ? MVM_string_graphs(tc, replacement) : 0;

    MVMGrapheme32 *buffer = MVM_malloc(sizeof(MVMGrapheme32) * bytes);

    result_graphs = 0;
    for (pos = 0; pos < bytes; pos++) {
        MVMGrapheme32 codepoint;
        if (windows125X[pos] == '\r' && pos + 1 < bytes && windows125X[pos + 1] == '\n') {
            codepoint = MVM_nfg_crlf_grapheme(tc);
            pos++;
        }
        else {
            codepoint = codetable[windows125X[pos]];
            if (codepoint == UNMAPPED) {
                /* Since things we are decoding always fit into Unicode, if we are
                 * using a replacement, it won't get used unless we use strict */
                if (replacement && MVM_ENCODING_CONFIG_STRICT(config)) {
                    MVMStringIndex i = 0;
                    /* Only triggered if repl_length > 1. Copies all but the last
                     * grapheme in the replacement string */
                    if (1 < repl_length) {
                        additional_bytes += repl_length - 1;
                        buffer = MVM_realloc(buffer, sizeof(MVMGrapheme32) * (additional_bytes + bytes));
                        for (; i < repl_length - 1; i++) {
                            MVMGrapheme32 graph = MVM_string_get_grapheme_at(tc, replacement, i);
                            buffer[result_graphs++] = graph;
                        }
                    }
                    /* Now we set `codepoint` to the last grapheme in the replacement
                     * and proceed normally from here. */
                    codepoint = MVM_string_get_grapheme_at(tc, replacement, i);
                }
                else if (MVM_ENCODING_CONFIG_STRICT(config)) {
                    MVM_free(buffer);
                    /* Throw an exception if that codepoint has no mapping */
                    char *enc_name = codetable == windows1252_codepoints
                        ? "Windows-1252" : "Windows-1251";
                    MVM_exception_throw_adhoc(tc,
                        "Error decoding %s string: could not decode codepoint %d",
                         enc_name,
                         windows125X[pos]);
                }
                else {
                    /* Don't convert and just map to identical. This creates
                     * standards uncompliant results, but will decode buggy
                     * input */
                    codepoint = windows125X[pos];
                }
            }
        }
        buffer[result_graphs++] = codepoint;
    }
    result = (MVMString *)REPR(result_type)->allocate(tc, STABLE(result_type));
    result->body.storage.blob_32 = buffer;
    result->body.storage_type = MVM_STRING_GRAPHEME_32;
    result->body.num_graphs = result_graphs;

    return result;
}
MVMString * MVM_string_windows1252_decode(MVMThreadContext *tc,
        const MVMObject *result_type, char *windows125X_c, size_t bytes) {
    return MVM_string_windows125X_decode(tc, result_type, windows125X_c, bytes, NULL, windows1252_codepoints, MVM_ENCODING_PERMISSIVE);
}
MVMString * MVM_string_windows1251_decode(MVMThreadContext *tc,
        const MVMObject *result_type, char *windows125X_c, size_t bytes) {
    return MVM_string_windows125X_decode(tc, result_type, windows125X_c, bytes, NULL, windows1251_codepoints, MVM_ENCODING_PERMISSIVE);
}
MVMString * MVM_string_windows1252_decode_config(MVMThreadContext *tc,
        const MVMObject *result_type, char *windows125X_c, size_t bytes, MVMString *replacement, MVMint64 config) {
    return MVM_string_windows125X_decode(tc, result_type, windows125X_c, bytes, replacement, windows1252_codepoints, config);
}
MVMString * MVM_string_windows1251_decode_config(MVMThreadContext *tc,
        const MVMObject *result_type, char *windows125X_c, size_t bytes, MVMString *replacement, MVMint64 config) {
    return MVM_string_windows125X_decode(tc, result_type, windows125X_c, bytes, replacement, windows1251_codepoints, config);
}
/* Encodes the specified substring to Windows-1252 or Windows-1251. It is passed
 * in the encoding, as well as the function that resolves Unicode to the result
 * encoding. Anything not in range will cause an exception unless a replacement
 * string is supplied. The result string is NULL terminated, but the specified
 * size is the non-null part. */
char * MVM_string_windows125X_encode_substr(MVMThreadContext *tc, MVMString *str,
        MVMuint64 *output_size, MVMint64 start, MVMint64 length, MVMString *replacement,
        MVMint32 translate_newlines, MVMuint8(*cp_to_char)(MVMint32), MVMint64 config) {
    /* Windows-1252 and Windows-1251 are single byte encodings, so each grapheme
     * will just become a single byte. */
    MVMuint32 startu = (MVMuint32)start;
    MVMStringIndex strgraphs = MVM_string_graphs(tc, str);
    MVMuint32 lengthu = (MVMuint32)(length == -1 ? strgraphs - startu : length);
    MVMuint8 *result  = NULL;
    size_t result_alloc;
    MVMuint8 *repl_bytes = NULL;
    MVMuint64 repl_length;

    /* must check start first since it's used in the length check */
    if (start < 0 || strgraphs < start)
        MVM_exception_throw_adhoc(tc, "start (%"PRId64") out of range (0..%"PRIu32")", start, strgraphs);
    if (length < -1 || strgraphs < start + lengthu)
        MVM_exception_throw_adhoc(tc, "length (%"PRId64") out of range (-1..%"PRIu32")", length, strgraphs);

    if (replacement)
        repl_bytes = (MVMuint8 *) MVM_string_windows125X_encode_substr(tc,
            replacement, &repl_length, 0, -1, NULL, translate_newlines, cp_to_char, config);

    result_alloc = lengthu;
    result = MVM_malloc(result_alloc + 1);
    if (str->body.storage_type == MVM_STRING_GRAPHEME_ASCII) {
        /* No encoding needed; directly copy. */
        memcpy(result, str->body.storage.blob_ascii, lengthu);
        result[lengthu] = 0;
        if (output_size)
            *output_size = lengthu;
    }
    else {
        MVMuint32 pos = 0;
        MVMCodepointIter ci;
        MVM_string_ci_init(tc, &ci, str, translate_newlines, 0);
        while (MVM_string_ci_has_more(tc, &ci)) {
            MVMCodepoint codepoint = MVM_string_ci_get_codepoint(tc, &ci);
            if (result_alloc <= pos) {
                result_alloc += 8;
                result = MVM_realloc(result, result_alloc + 1);
            }
            /* If it's within ASCII just pass it through */
            if (0 <= codepoint && codepoint <= 127) {
                result[pos] = (MVMuint8)codepoint;
                pos++;
            }
            else if ((result[pos] = cp_to_char(codepoint)) != '\0') {
                pos++;
            }
            /* If we have a replacement and are we either have it set to strict,
             * or the codepoint can't fit within one byte, insert a replacement */
            else if (replacement && (MVM_ENCODING_CONFIG_STRICT(config) || codepoint < 0 || 255 < codepoint)) {
                if (result_alloc <= pos + repl_length) {
                    result_alloc += repl_length;
                    result = MVM_realloc(result, result_alloc + 1);
                }
                memcpy(result + pos, repl_bytes, repl_length);
                pos += repl_length;
            }
            else {
                /* If we're decoding strictly or the codepoint cannot fit in
                 * one byte, throw an exception */
                if (MVM_ENCODING_CONFIG_STRICT(config) || codepoint < 0 || 255 < codepoint) {
                    char *enc_name = cp_to_char == windows1252_cp_to_char
                        ? "Windows-1252" : "Windows-1251";
                    MVM_free(result);
                    MVM_free(repl_bytes);
                    MVM_exception_throw_adhoc(tc,
                        "Error encoding %s string: could not encode codepoint %d",
                         enc_name,
                         codepoint);
                }
                /* It fits in one byte and we're not decoding strictly, so pass
                 * it through unchanged */
                else {
                    result[pos++] = codepoint;
                }
            }
        }
        result[pos] = 0;
        if (output_size)
            *output_size = pos;
    }

    MVM_free(repl_bytes);
    return (char *)result;
}
char * MVM_string_windows1252_encode_substr(MVMThreadContext *tc, MVMString *str,
        MVMuint64 *output_size, MVMint64 start, MVMint64 length, MVMString *replacement,
        MVMint32 translate_newlines) {
    return MVM_string_windows125X_encode_substr(tc, str, output_size, start, length, replacement, translate_newlines, windows1252_cp_to_char, MVM_ENCODING_PERMISSIVE);
}
char * MVM_string_windows1251_encode_substr(MVMThreadContext *tc, MVMString *str,
        MVMuint64 *output_size, MVMint64 start, MVMint64 length, MVMString *replacement,
        MVMint32 translate_newlines) {
    return MVM_string_windows125X_encode_substr(tc, str, output_size, start, length, replacement, translate_newlines, windows1251_cp_to_char, MVM_ENCODING_PERMISSIVE);
}
char * MVM_string_windows1252_encode_substr_config(MVMThreadContext *tc, MVMString *str,
        MVMuint64 *output_size, MVMint64 start, MVMint64 length, MVMString *replacement,
        MVMint32 translate_newlines, MVMint64 config) {
    return MVM_string_windows125X_encode_substr(tc, str, output_size, start, length, replacement, translate_newlines, windows1252_cp_to_char, config);
}
char * MVM_string_windows1251_encode_substr_config(MVMThreadContext *tc, MVMString *str,
        MVMuint64 *output_size, MVMint64 start, MVMint64 length, MVMString *replacement,
        MVMint32 translate_newlines, MVMint64 config) {
    return MVM_string_windows125X_encode_substr(tc, str, output_size, start, length, replacement, translate_newlines, windows1251_cp_to_char, config);
}
