#include "moarvm.h"

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

static MVMuint32
decode_utf8_byte(MVMuint32* state, MVMuint32* codep, MVMuint32 byte) {
  MVMuint32 type = utf8d[byte];

  *codep = (*state != UTF8_ACCEPT) ?
    (byte & 0x3fu) | (*codep << 6) :
    (0xff >> type) & (byte);

  *state = utf8d[256 + *state + type];
  return *state;
}
/* end Bjoern Hoehrmann section */

#define UTF8_MAXINC 128 * 1024 * 1024
/* Decodes the specified number of bytes of utf8 into an NFG string, creating
 * a result of the specified type. The type must have the MVMString REPR. 
 * Only bring in the raw codepoints for now. */
MVMString * MVM_string_utf8_decode(MVMThreadContext *tc, MVMObject *result_type, char *utf8, size_t bytes) {
    MVMString *result = (MVMString *)REPR(result_type)->allocate(tc, STABLE(result_type));
    MVMuint32 count = 0;
    MVMuint32 codepoint;
    MVMuint32 state = 0;
    MVMuint32 bufsize = 256;
    MVMuint32 *buffer = malloc(sizeof(MVMuint32) * bufsize);
    MVMuint32 *newbuffer;
    
    /* there's probably some (far better) buffer object in APR we can use instead. */
    for (; bytes; ++utf8, --bytes) {
        /* send the next byte to the decoder */
        if (!decode_utf8_byte(&state, &codepoint, *utf8)) {
            /* got a codepoint */
            if (count == bufsize) { /* if the buffer's full */
                newbuffer = malloc(sizeof(MVMuint32) * ( /* make a new one */
                    bufsize >= UTF8_MAXINC ? /* if we've reached the increment limit */
                    (bufsize += UTF8_MAXINC) : /* increment by that amount */
                    (bufsize *= 2) /* otherwise double it */
                ));
                /* copy the old buffer to the new buffer */
                memcpy(newbuffer, buffer, sizeof(MVMint32) * count);
                free(buffer); /* free the old buffer's memory */
                buffer = newbuffer; /* refer to the new buffer now */
            }
            buffer[count++] = codepoint; /* add the codepoint to the buffer */
            /* printf("U+%04X\n", codepoint); */
        }
    }
    if (state != UTF8_ACCEPT)
        MVM_exception_throw_adhoc(tc, "Malformed UTF-8 string");
    
    /* just keep the same buffer as the MVMString's buffer.  Later
     * we can add heuristics to resize it if we have enough free
     * memory */
    result->body.data = buffer;
    
    result->body.codes  = count;
    result->body.graphs = count; /* Ignore combining chars for now. */
    
    return result;
}

/* Encodes the specified string to UTF-8. */
MVMuint8 * MVM_string_utf8_encode(MVMThreadContext *tc, MVMString *str, MVMuint64 *output_size) {
    
    MVMuint8 *result = malloc(str->body.graphs + 1);
    /* XXX TODO */
    return result;
}
