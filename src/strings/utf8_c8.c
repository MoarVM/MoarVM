#include "moar.h"

/* UTF-8 Clean-8 is an encoder/decoder that primarily works as the UTF-8 one.
 * However, upon encountering a byte sequence that will either not decode as
 * valid UTF-8, or that would not round-trip due to normalization, it will use
 * NFG synthetics to keep track of the original bytes involved. This means that
 * encoding back to UTF-8 Clean-8 will be able to recreate the bytes as they
 * originally existed. The synthetics contain 4 codepoints:
 *
 *   * The codepoint 0x10FFFD (which is a private use codepoint)
 *   * The codepoint 'x'
 *   * The upper 4 bits of the non-decodable byte as a hex char (0..9A..F)
 *   * The lower 4 bits as the non-decodable byte as a hex char (0..9A..F)
 *
 * Under normal UTF-8 encoding, this means the unrepresentable characters will
 * come out as something like `?xFF`.
 *
 * UTF-8 Clean-8 is used in places where MoarVM receives strings from the
 * environment, command line arguments, and file system queries.
 */

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

    if (!(cc & (CP_CHAR | CP_NONCHAR)))
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

static void ensure_buffer(MVMGrapheme32 **buffer, MVMint32 *bufsize, MVMint32 needed) {
    while (needed >= *bufsize)
        *buffer = MVM_realloc(*buffer, sizeof(MVMGrapheme32) * (
            *bufsize >= UTF8_MAXINC ? (*bufsize += UTF8_MAXINC) : (*bufsize *= 2)
        ));
}

static const MVMuint8 hex_chars[] = { '0', '1', '2', '3', '4', '5', '6', '7',
                                      '8', '9', 'A', 'B', 'C', 'D', 'E', 'F' };
static MVMGrapheme32 synthetic_for(MVMThreadContext *tc, MVMuint8 invalid) {
    if (invalid > 0x7F) {
        /* A real invalid. */
        MVMuint8 high = invalid >> 4;
        MVMuint8 low = invalid & 0x0F;
        MVMCodepoint cps[] = { 0x10FFFD, 'x', hex_chars[high], hex_chars[low] };
        return MVM_nfg_codes_to_grapheme_utf8_c8(tc, cps, 4);
    }
    else {
        /* Was in things thrown out as invalid by the decoder, but has an
         * ASCII interpretation, so hand it back as is. */
        return invalid;
    }
}

/* What the UTF-C8 decode process is expecting. */
typedef enum {
    EXPECT_START = 0,
    EXPECT_CONTINUATION = 1
} Expecting;

/* Decode state for the UTF8-C8 decoder. */
typedef struct {
    /* The UTF-8 we're decoding. */
    const MVMuint8 *utf8;

    /* The index of the current byte we're decoding. */
    size_t cur_byte;

    /* The index of the first unaccepted byte. */
    size_t unaccepted_start;

    /* What kind of byte we're expecting next. */
    Expecting expecting;

    /* The current codepoint we're decoding. */
    MVMCodepoint cur_codepoint;

    /* The result buffer we're decoding into. */
    MVMGrapheme32 *result;

    /* The current position in the result buffer. */
    size_t result_pos;

    /* Buffer of original codepoints, to ensure we will not spit out any
     * synthetics into the result that will re-order on round-trip. */
    MVMCodepoint *orig_codes;

    /* Position we're at in inserting into orig_codes. */
    size_t orig_codes_pos;

    /* First orig_codes index that did not yet go through the normalizer. */
    size_t orig_codes_unnormalized;

    /* The normalizer we're using to make synthetics that will not cause an
     * order change on output. */
    MVMNormalizer norm;
} DecodeState;

/* Appends a single grapheme to the buffer if it will not cause a mismatch
 * with the original codepoints upon encoding back to UTF-8. Returns non-zero
 * in this case. Otherwise, appends synthetics for the bytes the original code
 * points were encoded as. Since we can end up with index mis-matches, we just
 * spit out codepoints to catch the normalizer up to everything in the orig
 * codes buffer. */
static int append_grapheme(MVMThreadContext *tc, DecodeState *state, MVMGrapheme32 g) {
    if (g == state->orig_codes[state->orig_codes_unnormalized]) {
        /* Easy case: exact match. */
        state->result[state->result_pos++] = g;
        state->orig_codes_unnormalized++;
        return 1;
    }
    else if (g < 0) {
        MVMNFGSynthetic *synth = MVM_nfg_get_synthetic_info(tc, g);
        int mismatch = 0;
        if (synth->base == state->orig_codes[state->orig_codes_unnormalized]) {
            MVMint32 i;
            for (i = 0; i < synth->num_combs; i++) {
                size_t orig_idx = state->orig_codes_unnormalized + i + 1;
                if (orig_idx >= state->orig_codes_pos) {
                    mismatch = 1;
                    break;
                }
                if (state->orig_codes[orig_idx] != synth->combs[i])
                    mismatch = 1;
            }
        }
        else {
            mismatch = 1;
        }
        if (!mismatch) {
            state->result[state->result_pos++] = g;
            state->orig_codes_unnormalized += 1 + synth->num_combs;
            return 1;
        }
    }

    /* If we get here, then normalization would trash the original bytes. */
    {
        /* Spit out synthetics to keep the bytes as is. */
        size_t i, j;
        for (i = state->orig_codes_unnormalized; i < state->orig_codes_pos; i++) {
            MVMCodepoint to_encode = state->orig_codes[i];
            MVMuint8 encoded[4];
            MVMint32 bytes = utf8_encode(encoded, to_encode);
            for (j = 0; j < bytes; j++)
                state->result[state->result_pos++] = synthetic_for(tc, encoded[j]);
        }

        /* Consider all codes pushed now normalized. */
        state->orig_codes_unnormalized = state->orig_codes_pos;

        /* Put a clean normalizer in place. */
        MVM_unicode_normalizer_cleanup(tc, &(state->norm));
        MVM_unicode_normalizer_init(tc, &(state->norm), MVM_NORMALIZE_NFG);
        return 0;
    }
}

/* Called when decoding has reached an acceptable codepoint. */
static void process_ok_codepoint(MVMThreadContext *tc, DecodeState *state) {
    MVMint32 ready;
    MVMGrapheme32 g;

    /* Consider the byte range accepted. */
    state->unaccepted_start = state->cur_byte + 1;

    /* Insert into original codepoints list and hand it to the normalizer. */
    state->orig_codes[state->orig_codes_pos++] = state->cur_codepoint;
    ready = MVM_unicode_normalizer_process_codepoint_to_grapheme(tc,
            &(state->norm), state->cur_codepoint, &g);

    /* If the normalizer produced some output... */
    if (ready) {
        if (append_grapheme(tc, state, g)) {
            while (--ready > 0) {
                g = MVM_unicode_normalizer_get_grapheme(tc, &(state->norm));
                if (!append_grapheme(tc, state, g))
                    break;
            }
        }
    }
}

/* Called when a bad byte has been encountered, or at the end of output. */
static void process_bad_bytes(MVMThreadContext *tc, DecodeState *state) {
    size_t i;
    MVMint32 ready;

    /* Flush normalization buffer and take from that. */
    MVM_unicode_normalizer_eof(tc, &(state->norm));
    ready = MVM_unicode_normalizer_available(tc, &(state->norm));
    while (ready-- > 0) {
        MVMGrapheme32 g = MVM_unicode_normalizer_get_grapheme(tc, &(state->norm));
        if (!append_grapheme(tc, state, g))
            break;
    }

    /* Now add in synthetics for bad bytes. */
    for (i = state->unaccepted_start; i <= state->cur_byte; i++)
        state->result[state->result_pos++] = synthetic_for(tc, state->utf8[i]);
    state->unaccepted_start = state->cur_byte + 1;
}

/* Decodes the specified number of bytes of utf8 into an NFG string, creating
 * a result of the specified type. The type must have the MVMString REPR. */
MVMString * MVM_string_utf8_c8_decode(MVMThreadContext *tc, const MVMObject *result_type,
                                      const char *utf8, size_t bytes) {
    DecodeState state;

    /* Local state for decode loop. */
    int expected_continuations = 0;

    /* Don't do anything if empty. */
    if (bytes == 0)
        return tc->instance->str_consts.empty;

    /* Decoding state, in a struct to easily pass to utility routines.
     * Result buffer is a maximum estimate to avoid realloc; we can shrink
     * it at the end. */
    state.utf8 = (MVMuint8 *)utf8;
    state.cur_byte = 0;
    state.unaccepted_start = 0;
    state.expecting = EXPECT_START;
    state.cur_codepoint = 0;
    state.result = MVM_malloc(sizeof(MVMGrapheme32) * bytes);
    state.result_pos = 0;
    state.orig_codes = MVM_malloc(sizeof(MVMCodepoint) * bytes);
    state.orig_codes_pos = 0;
    state.orig_codes_unnormalized = 0;
    MVM_unicode_normalizer_init(tc, &(state.norm), MVM_NORMALIZE_NFG);

    while (state.cur_byte < bytes) {
        MVMuint8 decode_byte = utf8[state.cur_byte];
        switch (state.expecting) {
            case EXPECT_START:
                if ((decode_byte & 0b10000000) == 0) {
                    /* Single byte sequence. */
                    state.cur_codepoint = decode_byte;
                    process_ok_codepoint(tc, &state);
                }
                else if ((decode_byte & 0b11100000) == 0b11000000) {
                    state.cur_codepoint = decode_byte & 0b00011111;
                    state.expecting = EXPECT_CONTINUATION;
                    expected_continuations = 1;
                }
                else if ((decode_byte & 0b11110000) == 0b11100000) {
                    state.cur_codepoint = decode_byte & 0b00001111;
                    state.expecting = EXPECT_CONTINUATION;
                    expected_continuations = 2;
                }
                else if ((decode_byte & 0b11111000) == 0b11110000) {
                    state.cur_codepoint = decode_byte & 0b00000111;
                    state.expecting = EXPECT_CONTINUATION;
                    expected_continuations = 3;
                }
                else {
                    /* Invalid byte sequence. */
                    process_bad_bytes(tc, &state);
                }
                break;
            case EXPECT_CONTINUATION:
                if ((decode_byte & 0b11000000) == 0b10000000) {
                    state.cur_codepoint = (state.cur_codepoint << 6)
                                          | (decode_byte & 0b00111111);
                    expected_continuations--;
                    if (expected_continuations == 0) {
                        process_ok_codepoint(tc, &state);
                        state.expecting = EXPECT_START;
                    }
                }
                else {
                    /* Invalid byte sequence. */
                    process_bad_bytes(tc, &state);
                    state.expecting = EXPECT_START;
                }
                break;
        }
        state.cur_byte++;
    }

    /* Handle anything dangling off the end. */
    state.cur_byte--; /* So we don't read 1 past the end. */
    process_bad_bytes(tc, &state);

    MVM_free(state.orig_codes);
    MVM_unicode_normalizer_cleanup(tc, &(state.norm));

    {
        MVMString *result = (MVMString *)REPR(result_type)->allocate(tc, STABLE(result_type));
        result->body.storage.blob_32 = state.result;
        result->body.storage_type    = MVM_STRING_GRAPHEME_32;
        result->body.num_graphs      = state.result_pos;
        return result;
    }
}

/* Decodes using a decodestream. Decodes as far as it can with the input
 * buffers, or until a stopper is reached. */
MVMuint32 MVM_string_utf8_c8_decodestream(MVMThreadContext *tc, MVMDecodeStream *ds,
                                     const MVMint32 *stopper_chars,
                                     MVMDecodeStreamSeparators *seps) {
    MVMint32 count = 0, total = 0;
    MVMint32 state = 0;
    MVMCodepoint codepoint = 0;
    MVMint32 bufsize;
    MVMGrapheme32 *buffer;
    MVMDecodeStreamBytes *cur_bytes;
    MVMDecodeStreamBytes *last_accept_bytes = ds->bytes_head;
    MVMint32 last_accept_pos, ready;
    MVMuint32 reached_stopper;

    /* If there's no buffers, we're done. */
    if (!ds->bytes_head)
        return 0;
    last_accept_pos = ds->bytes_head_pos;

    /* If we're asked for zero chars, also done. */
    if (stopper_chars && *stopper_chars == 0)
        return 1;

    /* Rough starting-size estimate is number of bytes in the head buffer. */
    bufsize = ds->bytes_head->length;
    buffer = MVM_malloc(bufsize * sizeof(MVMGrapheme32));

    /* Decode each of the buffers. */
    cur_bytes = ds->bytes_head;
    reached_stopper = 0;
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
                    if (stopper_chars && *stopper_chars == total) {
                        reached_stopper = 1;
                        goto done;
                    }
                    if (MVM_string_decode_stream_maybe_sep(tc, seps, g)) {
                        reached_stopper = 1;
                        goto done;
                    }
                }
                break;
            }
            case UTF8_REJECT: {
                /* First, flush anything in the normalizer. */
                MVMint32 ready;
                MVM_unicode_normalizer_eof(tc, &(ds->norm));
                ready = MVM_unicode_normalizer_available(tc, &(ds->norm));
                /* Get a new result buffer, if we'd overflow existing. We
                 * should never be able to get an invalid sequence longer
                 * than 4 bytes. */
                if (count + ready + 4 >= bufsize) {
                    MVM_string_decodestream_add_chars(tc, ds, buffer, count);
                    if (ready + 4 > bufsize)
                        bufsize = ready + 4;
                    buffer = MVM_malloc(bufsize * sizeof(MVMGrapheme32));
                    count = 0;
                }

                /* Add chars from the normalizer; look out for any of them
                 * being the separator. */
                while (ready--) {
                    MVMGrapheme32 g = MVM_unicode_normalizer_get_grapheme(tc, &(ds->norm));
                    buffer[count++] = g;
                    total++;
                    if (stopper_chars && *stopper_chars == total) {
                        reached_stopper = 1;
                        goto done;
                    }
                    if (MVM_string_decode_stream_maybe_sep(tc, seps, g)) {
                        reached_stopper = 1;
                        goto done;
                    }
                }

                /* Go through invalid bytes, making synthetics. */
                do {
                    if (last_accept_pos < last_accept_bytes->length) {
                        /* Still some in the last accepted byte buffer. */
                        buffer[count++] = synthetic_for(tc,
                            last_accept_bytes->bytes[last_accept_pos]);
                        total++;
                        last_accept_pos++;
                        if (stopper_chars && *stopper_chars == total) {
                            reached_stopper = 1;
                            goto done;
                        }
                    }
                    else if (last_accept_bytes->next) {
                        /* Progress to next buffer. */
                        last_accept_bytes = last_accept_bytes->next;
                        last_accept_pos = -1;
                    }
                }
                while (last_accept_bytes != cur_bytes && last_accept_pos != pos - 1);

                /* Accept the invalid bytes. */
                state = UTF8_ACCEPT;

                break;
            }
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

/* Encodes the specified string to UTF-8. */
static void emit_cp(MVMThreadContext *tc, MVMCodepoint cp, MVMuint8 **result,
                    size_t *result_pos, size_t *result_limit,
                    MVMuint8 *repl_bytes, MVMuint64 repl_length) {
    MVMint32 bytes;
    if (*result_pos >= *result_limit) {
        *result_limit *= 2;
        *result = MVM_realloc(*result, *result_limit + 4);
    }
    bytes = utf8_encode(*result + *result_pos, cp);
    if (bytes)
        *result_pos += bytes;
    else if (repl_bytes) {
        if (repl_length >= *result_limit || *result_pos >= *result_limit - repl_length) {
            *result_limit += repl_length;
            *result = MVM_realloc(*result, *result_limit + 4);
        }
        memcpy(*result + *result_pos, repl_bytes, repl_length);
        *result_pos += repl_length;
    }
    else {
        MVM_free(*result);
        MVM_free(repl_bytes);
        MVM_exception_throw_adhoc(tc,
            "Error encoding UTF-8 string: could not encode codepoint %d",
            cp);
    }
}
static int hex2int(MVMThreadContext *tc, MVMCodepoint cp) {
    if (cp >= '0' && cp <= '9')
        return cp - '0';
    else if (cp >= 'A' && cp <= 'F')
        return 10 + (cp - 'A');
    else
        MVM_exception_throw_adhoc(tc, "UTF-8 C-8 encoding encountered corrupt synthetic");
}
char * MVM_string_utf8_c8_encode_substr(MVMThreadContext *tc,
        MVMString *str, MVMuint64 *output_size, MVMint64 start, MVMint64 length, MVMString *replacement) {
    MVMuint8        *result;
    size_t           result_pos, result_limit;
    MVMGraphemeIter  gi;
    MVMStringIndex   strgraphs = MVM_string_graphs(tc, str);
    MVMuint8        *repl_bytes = NULL;
    MVMuint64        repl_length;

    if (start < 0 || start > strgraphs)
        MVM_exception_throw_adhoc(tc, "start out of range");
    if (length == -1)
        length = strgraphs;
    if (length < 0 || start + length > strgraphs)
        MVM_exception_throw_adhoc(tc, "length out of range");

    if (replacement)
        repl_bytes = (MVMuint8 *) MVM_string_utf8_c8_encode_substr(tc, replacement, &repl_length, 0, -1, NULL);

    /* Guesstimate that we'll be within 2 bytes for most chars most of the
     * time, and give ourselves 4 bytes breathing space. */
    result_limit = 2 * length;
    result       = MVM_malloc(result_limit + 4);
    result_pos   = 0;

    /* We iterate graphemes, looking out for any synthetics. If we find a
     * UTF-8 C-8 synthetic, then we spit out the raw byte. If we find any
     * other synthetic, we iterate its codepoints. */
    MVM_string_gi_init(tc, &gi, str);
    while (MVM_string_gi_has_more(tc, &gi)) {
        MVMGrapheme32 g = MVM_string_gi_get_grapheme(tc, &gi);
        if (g >= 0) {
            emit_cp(tc, g, &result, &result_pos, &result_limit, repl_bytes, repl_length);
        }
        else {
            MVMNFGSynthetic *synth = MVM_nfg_get_synthetic_info(tc, g);
            if (synth->is_utf8_c8) {
                /* UTF-8 C-8 synthetic; emit the byte. */
                if (result_pos >= result_limit) {
                    result_limit *= 2;
                    result = MVM_realloc(result, result_limit + 1);
                }
                result[result_pos++] = (hex2int(tc, synth->combs[1]) << 4) +
                    hex2int(tc, synth->combs[2]);
            }
            else {
                MVMint32 i;
                emit_cp(tc, synth->base, &result, &result_pos, &result_limit, repl_bytes, repl_length);
                for (i = 0; i < synth->num_combs; i++)
                    emit_cp(tc, synth->combs[i], &result, &result_pos, &result_limit, repl_bytes, repl_length);
            }
        }
    }

    if (output_size)
        *output_size = (MVMuint64)result_pos;
    MVM_free(repl_bytes);
    return (char *)result;
}

/* Encodes the specified string to UTF-8 C-8. */
char * MVM_string_utf8_c8_encode(MVMThreadContext *tc, MVMString *str, MVMuint64 *output_size) {
    return MVM_string_utf8_c8_encode_substr(tc, str, output_size, 0,
        MVM_string_graphs(tc, str), NULL);
}

/* Encodes the specified string to a UTF-8 C-8 C string. */
char * MVM_string_utf8_c8_encode_C_string(MVMThreadContext *tc, MVMString *str) {
    MVMuint64 output_size;
    char *result;
    char *utf8_string = MVM_string_utf8_c8_encode(tc, str, &output_size);
    result = MVM_malloc(output_size + 1);
    memcpy(result, utf8_string, output_size);
    MVM_free(utf8_string);
    result[output_size] = (char)0;
    return result;
}
