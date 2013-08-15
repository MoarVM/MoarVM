/*

 Package: dyncall
 Library: dyncall
 File: dyncall/dyncall_callvm_sparc64.h
 Description: Call VM for sparc64 processor architecture.
 License:

   Copyright (c) 2011 Daniel Adler <dadler@uni-goettingen.de>

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


#ifndef DYNCALL_CALLVM_SPARC_H
#define DYNCALL_CALLVM_SPARC_H

#include "dyncall_callvm.h"
#include "dyncall_vector.h"

typedef struct DCCallVM_sparc64_ DCCallVM_sparc64;
struct DCCallVM_sparc64_
{
  DCCallVM  mInterface;	/* 12:8 -> 16 */
  int       mIntRegs;	 /* 16 */
  int       mFloatRegs;  /* 20 */
  int       mSingleRegs; /* 24 */
  unsigned int mUseSingleFlags; /* 32 */
  DCVecHead mVecHead;   /* 36:16, 32 */
                        /* 40 */
};

DCCallVM* dcNewCallVM_sparc64(DCsize size);

#endif /* DYNCALL_CALLVM_SPARC64_H */



