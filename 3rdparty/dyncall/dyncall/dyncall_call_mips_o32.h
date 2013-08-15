/*

 Package: dyncall
 Library: dyncall
 File: dyncall/dyncall_call_mips_o32.h
 Description: mips "o32" abi call kernel C interface.
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


#ifndef DYNCALL_CALL_MIPS_O32_H
#define DYNCALL_CALL_MIPS_O32_H

#include "dyncall_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Call-kernel register data:

  Details:
  The structure holds the argument data for transfering float/double arguments
  via registers as well.
  The call-kernel implements loads two doubles, which involves four
  32-bit floating pointer registers.
  Float arguments map as following:
  
    float argument 0 is at floats[1] and
    float argument 1 is at floats[3] of DCRegData_mips_o32 union.

*/

typedef union DCRegData_mips_o32_
{
  double doubles[2];
  float  floats [4];	 /* float 0 is at index 1 and float 1 is at index 3 */
} DCRegData_mips_o32;


/* Call kernel. */

void dcCall_mips_o32(DCpointer target, DCRegData_mips_o32* regdata, DCsize stksize, DCpointer stkdata);


#ifdef __cplusplus
}
#endif


#endif /* DYNCALL_CALL_MIPS_O32_H */

