#include "moar.h"

/* The below section has an MIT-style license, included here.

// Copyright (c) 2008-2010 Bjoern Hoehrmann <bjoern@hoehrmann.de>
// See http://bjoern.hoehrmann.de/utf-8/decoder/dfa/ for details.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject
 * to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#define UTF8_ACCEPT 0
#define UTF8_REJECT 12

static const MVMuint8 utf8d[] = {
  // The first part of the table maps bytes to character classes that
  // to reduce the size of the transition table and create bitmasks.
   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,  9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,
   7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,  7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
   8,8,2,2,2,2,2,2,2,2,2,2,2,2,2,2,  2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
  10,3,3,3,3,3,3,3,3,3,3,3,3,4,3,3, 11,6,6,6,5,8,8,8,8,8,8,8,8,8,8,8,

  // The second part is a transition table that maps a combination
  // of a state of the automaton and a character class to a state.
   0,12,24,36,60,96,84,12,12,12,48,72, 12,12,12,12,12,12,12,12,12,12,12,12,
  12, 0,12,12,12,12,12, 0,12, 0,12,12, 12,24,12,12,12,12,12,24,12,24,12,12,
  12,12,12,12,12,12,12,24,12,12,12,12, 12,24,12,12,12,12,12,12,12,24,12,12,
  12,12,12,12,12,12,12,36,12,36,12,12, 12,36,12,12,12,12,12,36,12,36,12,12,
  12,36,12,12,12,12,12,12,12,12,12,12,
};

static MVMint32
decode_utf8_byte(MVMint32 *state, MVMGrapheme32 *codep, MVMuint8 byte) {
  MVMint32 type = utf8d[byte];

  *codep = (*state != UTF8_ACCEPT) ?
    (byte & 0x3fu) | (*codep << 6) :
    (0xff >> type) & (byte);

  *state = utf8d[256 + *state + type];
  return *state;
}
/* end Bjoern Hoehrmann section (some things were changed from the original) */

/* begin not_gerd section (modified from original)
// Copyright 2012 not_gerd
// see http://irclog.perlgeek.de/perl6/2012-06-04#i_5681122

Permission is granted to use, modify, and / or redistribute at will.

This includes removing authorship notices, re-use of code parts in
other software (with or without giving credit), and / or creating a
commercial product based on it.

This permission is not revocable by the author.

This software is provided as-is. Use it at your own risk. There is
no warranty whatsoever, neither expressed nor implied, and by using
this software you accept that the author(s) shall not be held liable
for any loss of data, loss of service, or other damages, be they
incidental or consequential. Your only option other than accepting
this is not to use the software at all.
*/

enum {
    CP_CHAR            = 1 << 0,
    CP_LOW_SURROGATE   = 1 << 1,
    CP_HIGH_SURROGATE  = 1 << 2,
    CP_NONCHAR         = 1 << 3,
    CP_OVERFLOW        = 1 << 4,

    U8_SINGLE          = 1 << 5,
    U8_DOUBLE          = 1 << 6,
    U8_TRIPLE          = 1 << 7,
    U8_QUAD            = 1 << 8
};

static unsigned classify(MVMCodepoint cp) {
    if(cp <= 0x7F)
        return CP_CHAR | U8_SINGLE;

    if(cp <= 0x07FF)
        return CP_CHAR | U8_DOUBLE;

    if(0xD800 <= cp && cp <= 0xDBFF)
        return CP_HIGH_SURROGATE | U8_TRIPLE;

    if(0xDC00 <= cp && cp <= 0xDFFF)
        return CP_LOW_SURROGATE | U8_TRIPLE;

    if(0xFDD0 <= cp && cp <= 0xFDEF)
        return CP_NONCHAR | U8_TRIPLE;

    if(cp <= 0xFFFD)
        return CP_CHAR | U8_TRIPLE;

    if(cp == 0xFFFE || cp == 0xFFFF)
        return CP_NONCHAR | U8_TRIPLE;

    if(cp <= 0x10FFFF && ((cp & 0xFFFF) == 0xFFFE || (cp & 0xFFFF) == 0xFFFF))
        return CP_NONCHAR | U8_QUAD;

    if(cp <= 0x10FFFF)
        return CP_CHAR | U8_QUAD;

    if(cp <= 0x1FFFFF)
        return CP_OVERFLOW | U8_QUAD;

    return 0;
}

static MVMint32 utf8_encode(MVMuint8 *bp, MVMCodepoint cp) {
    unsigned cc = classify(cp);

    if (!(cc & CP_CHAR))
        return 0;

    if (cc & U8_SINGLE) {
        bp[0] = (MVMuint8)cp;
        return 1;
    }

    if (cc & U8_DOUBLE) {
        bp[0] = (MVMuint8)(( 6 << 5) |  (cp >> 6));
        bp[1] = (MVMuint8)(( 2 << 6) |  (cp &  0x3F));
        return 2;
    }

    if (cc & U8_TRIPLE) {
        bp[0] = (MVMuint8)((14 << 4) |  (cp >> 12));
        bp[1] = (MVMuint8)(( 2 << 6) | ((cp >> 6) & 0x3F));
        bp[2] = (MVMuint8)(( 2 << 6) | ( cp       & 0x3F));
        return 3;
    }

    if (cc & U8_QUAD) {
        bp[0] = (MVMuint8)((30 << 3) |  (cp >> 18));
        bp[1] = (MVMuint8)(( 2 << 6) | ((cp >> 12) & 0x3F));
        bp[2] = (MVMuint8)(( 2 << 6) | ((cp >>  6) & 0x3F));
        bp[3] = (MVMuint8)(( 2 << 6) | ( cp        & 0x3F));
        return 4;
    }

    return 0;
}

 /* end not_gerd section */

#define UTF8_MAXINC (32 * 1024 * 1024)

/* Decodes the specified number of bytes of utf8 into an NFG string, creating
 * a result of the specified type. The type must have the MVMString REPR.
 * Only bring in the raw codepoints for now. */
MVMString * MVM_string_utf8_decode(MVMThreadContext *tc, MVMObject *result_type, const char *utf8, size_t bytes) {
    MVMString *result = (MVMString *)REPR(result_type)->allocate(tc, STABLE(result_type));
    MVMint32 count = 0;
    MVMCodepoint codepoint;
    MVMint32 line_ending = 0;
    MVMint32 state = 0;
    MVMint32 bufsize = bytes;
    MVMGrapheme32 *buffer = MVM_malloc(sizeof(MVMGrapheme32) * bufsize);
    size_t orig_bytes;
    const char *orig_utf8;
    MVMint32 line;
    MVMint32 col;
    MVMint32 ready;

    /* Need to normalize to NFG as we decode. */
    MVMNormalizer norm;
    MVM_unicode_normalizer_init(tc, &norm, MVM_NORMALIZE_NFG);

    orig_bytes = bytes;
    orig_utf8 = utf8;

    for (; bytes; ++utf8, --bytes) {
        switch(decode_utf8_byte(&state, &codepoint, (MVMuint8)*utf8)) {
        case UTF8_ACCEPT: { /* got a codepoint */
            MVMGrapheme32 g;
            ready = MVM_unicode_normalizer_process_codepoint_to_grapheme(tc, &norm, codepoint, &g);
            if (ready) {
                while (count + ready >= bufsize) { /* if the buffer's full make a bigger one */
                    buffer = MVM_realloc(buffer, sizeof(MVMGrapheme32) * (
                        bufsize >= UTF8_MAXINC ? (bufsize += UTF8_MAXINC) : (bufsize *= 2)
                    ));
                }
                buffer[count++] = g;
                while (--ready > 0)
                    buffer[count++] = MVM_unicode_normalizer_get_grapheme(tc, &norm);
            }
            break;
        }
        case UTF8_REJECT:
            /* found a malformed sequence; parse it again this time tracking
             * line and col numbers. */
            MVM_unicode_normalizer_cleanup(tc, &norm); /* Since we'll throw. */
            bytes = orig_bytes; utf8 = orig_utf8; state = 0; line = 1; col = 1;
            for (; bytes; ++utf8, --bytes) {
                switch(decode_utf8_byte(&state, &codepoint, (MVMuint8)*utf8)) {
                case UTF8_ACCEPT:
                    /* this could be reorganized into several nested ugly if/else :/ */
                    if (!line_ending && (codepoint == 10 || codepoint == 13)) {
                        /* Detect the style of line endings.
                         * Select whichever comes first.
                         * First or only part of first line ending. */
                        line_ending = codepoint;
                        col = 1; line++;
                    }
                    else if (line_ending && codepoint == line_ending) {
                        /* first or only part of next line ending */
                        col = 1; line++;
                    }
                    else if (codepoint == 10 || codepoint == 13) {
                        /* second part of line ending; ignore */
                    }
                    else /* non-line ending codepoint */
                        col++;
                    break;
                case UTF8_REJECT:
                    MVM_free(buffer);
                    MVM_exception_throw_adhoc(tc, "Malformed UTF-8 at line %u col %u", line, col);
                }
            }
            MVM_free(buffer);
            MVM_exception_throw_adhoc(tc, "Concurrent modification of UTF-8 input buffer!");
            break;
        }
    }
    if (state != UTF8_ACCEPT) {
        MVM_unicode_normalizer_cleanup(tc, &norm);
        MVM_free(buffer);
        MVM_exception_throw_adhoc(tc, "Malformed termination of UTF-8 string");
    }

    /* Get any final graphemes from the normalizer, and clean it up. */
    MVM_unicode_normalizer_eof(tc, &norm);
    ready = MVM_unicode_normalizer_available(tc, &norm);
    if (ready) {
        if (count + ready >= bufsize) {
            buffer = MVM_realloc(buffer, sizeof(MVMGrapheme32) * (count + ready));
        }
        while (ready--)
            buffer[count++] = MVM_unicode_normalizer_get_grapheme(tc, &norm);
    }
    MVM_unicode_normalizer_cleanup(tc, &norm);

    /* just keep the same buffer as the MVMString's buffer.  Later
     * we can add heuristics to resize it if we have enough free
     * memory */
    if (bufsize - count > 4) {
        buffer = MVM_realloc(buffer, count * sizeof(MVMGrapheme32));
        bufsize = count;
    }
    result->body.storage.blob_32 = buffer;
    result->body.storage_type    = MVM_STRING_GRAPHEME_32;
    result->body.num_graphs      = count;

    return result;
}

/* Decodes using a decodestream. Decodes as far as it can with the input
 * buffers, or until a stopper is reached. */
void MVM_string_utf8_decodestream(MVMThreadContext *tc, MVMDecodeStream *ds,
                                  MVMint32 *stopper_chars, MVMint32 *stopper_sep) {
    MVMint32 count = 0, total = 0;
    MVMint32 state = 0;
    MVMCodepoint codepoint = 0;
    MVMint32 bufsize;
    MVMGrapheme32 *buffer;
    MVMDecodeStreamBytes *cur_bytes;
    MVMDecodeStreamBytes *last_accept_bytes = ds->bytes_head;
    MVMint32 last_accept_pos, ready;

    /* If there's no buffers, we're done. */
    if (!ds->bytes_head)
        return;
    last_accept_pos = ds->bytes_head_pos;

    /* If we're asked for zero chars, also done. */
    if (stopper_chars && *stopper_chars == 0)
        return;

    /* Rough starting-size estimate is number of bytes in the head buffer. */
    bufsize = ds->bytes_head->length;
    buffer = MVM_malloc(bufsize * sizeof(MVMGrapheme32));

    /* Decode each of the buffers. */
    cur_bytes = ds->bytes_head;
    while (cur_bytes) {
        /* Process this buffer. */
        MVMint32  pos   = cur_bytes == ds->bytes_head ? ds->bytes_head_pos : 0;
        char     *bytes = cur_bytes->bytes;
        while (pos < cur_bytes->length) {
            switch(decode_utf8_byte(&state, &codepoint, bytes[pos++])) {
            case UTF8_ACCEPT: {
                MVMint32 first = 1;
                MVMGrapheme32 g;
                last_accept_bytes = cur_bytes;
                last_accept_pos = pos;
                ready = MVM_unicode_normalizer_process_codepoint_to_grapheme(tc, &(ds->norm), codepoint, &g);
                while (ready--) {
                    if (first)
                        first = 0;
                    else
                        g = MVM_unicode_normalizer_get_grapheme(tc, &(ds->norm));
                    if (count == bufsize) {
                        /* Valid character, but we filled the buffer. Attach this
                        * one to the buffers linked list, and continue with a new
                        * one. */
                        MVM_string_decodestream_add_chars(tc, ds, buffer, bufsize);
                        buffer = MVM_malloc(bufsize * sizeof(MVMGrapheme32));
                        count = 0;
                    }
                    buffer[count++] = g;
                    total++;
                    if (stopper_chars && *stopper_chars == total)
                        goto done;
                    if (stopper_sep && *stopper_sep == g)
                        goto done;
                }
                break;
            }
            case UTF8_REJECT:
                MVM_exception_throw_adhoc(tc, "Malformed UTF-8");
                break;
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
}

/* Encodes the specified string to UTF-8. */
char * MVM_string_utf8_encode_substr(MVMThreadContext *tc,
        MVMString *str, MVMuint64 *output_size, MVMint64 start, MVMint64 length) {
    MVMuint8        *result;
    size_t           result_pos, result_limit;
    MVMCodepointIter ci;
    MVMStringIndex   strgraphs = MVM_string_graphs(tc, str);

    if (start < 0 || start > strgraphs)
        MVM_exception_throw_adhoc(tc, "start out of range");
    if (length == -1)
        length = strgraphs;
    if (length < 0 || start + length > strgraphs)
        MVM_exception_throw_adhoc(tc, "length out of range");

    /* Guesstimate that we'll be within 2 bytes for most chars most of the
     * time, and give ourselves 4 bytes breathing space. */
    result_limit = 2 * length;
    result       = MVM_malloc(result_limit + 4);
    result_pos   = 0;

    /* Iterate the codepoints and encode them. */
    MVM_string_ci_init(tc, &ci, str);
    while (MVM_string_ci_has_more(tc, &ci)) {
        MVMint32 bytes;
        MVMCodepoint cp = MVM_string_ci_get_codepoint(tc, &ci);
        if (result_pos >= result_limit) {
            result_limit *= 2;
            result = MVM_realloc(result, result_limit + 4);
        }
        bytes = utf8_encode(result + result_pos, cp);
        if (!bytes) {
            MVM_free(result);
            MVM_exception_throw_adhoc(tc,
                "Error encoding UTF-8 string: could not encode codepoint %d",
                cp);
        }
        result_pos += bytes;
    }

    if (output_size)
        *output_size = (MVMuint64)result_pos;
    return (char *)result;
}

/* Encodes the specified string to UTF-8. */
char * MVM_string_utf8_encode(MVMThreadContext *tc, MVMString *str, MVMuint64 *output_size) {
    return MVM_string_utf8_encode_substr(tc, str, output_size, 0,
        MVM_string_graphs(tc, str));
}

/* Encodes the specified string to a UTF-8 C string. */
char * MVM_string_utf8_encode_C_string(MVMThreadContext *tc, MVMString *str) {
    MVMuint64 output_size;
    char * result;
    char * utf8_string = MVM_string_utf8_encode(tc, str, &output_size);
    /* this is almost always called from error-handling code. Don't care if it
     * contains embedded NULs. XXX TODO: Make sure all uses of this free what it returns */
    result = MVM_malloc(output_size + 1);
    memcpy(result, utf8_string, output_size);
    MVM_free(utf8_string);
    result[output_size] = (char)0;
    return result;
}
