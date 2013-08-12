/*

 Package: dyncall
 Library: dyncallback
 File: dyncallback/dyncall_thunk_arm32_arm.c
 Description: Thunk - Implementation for ARM32 (ARM mode)
 License:

   Copyright (c) 2007-2011 Daniel Adler <dadler@uni-goettingen.de>,
                           Tassilo Philipp <tphilipp@potion-studios.com>

   Permission to use, copy, modify, and distribute this software for any
   purpose with or without fee is hereby granted, provided that the above
   copyright notice and this permission notice appear in all copies.

   THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
   WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
   MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
   ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
   WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
   ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
   OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

*/

#include "dyncall_thunk.h"

void dcbInitThunk(DCThunk* p, void (*entry)())
{
  /*
    # ARM32 (ARM mode) thunk code:
    .code 32
      sub %r12, %r15, #8
      ldr %r15, [%r15, #-4]
  */

  /* This code loads 'entry+8' into r15. The -4 is needed, because r15 as  */
  /* program counter points to the current instruction+8, but the pointer  */
  /* to the code to execute follows the ldr instruction directly. Add 8 to */
  /* entry for similar reasons. NOTE: Latter seems to be implicit with     */ 
  /* latest update of arm-eabi tools.                                      */
  p->code[0]  = 0xe24fc008UL;  /* sub %r12, %r15, #8 */
  p->code[1]  = 0xe51ff004UL;  /* ldr %r15, [%r15, #-4] */
  p->entry = entry/*+8*/;
}
