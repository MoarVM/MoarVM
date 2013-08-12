/*

 Package: dyncall
 Library: dyncall
 File: dyncall/dyncall_value.h
 Description: Value variant type
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


/*

  dyncall value variant

  a value variant union-type that carries all supported dyncall types.

  REVISION
  2007/12/11 initial

*/

#ifndef DYNCALL_VALUE_H
#define DYNCALL_VALUE_H

#include "dyncall_types.h"

#ifdef __cplusplus
extern "C" {
#endif 

typedef union DCValue_ DCValue;

union DCValue_
{
  DCbool        B;
  DCchar        c;
  DCuchar       C;
  DCshort       s;
  DCushort      S;
  DCint         i;
  DCuint        I;
  DClong        j;
  DCulong       J;
  DClonglong    l;
  DCulonglong   L;
  DCfloat       f;
  DCdouble      d;
  DCpointer     p;
  DCstring      Z;
};

#ifdef __cplusplus
}
#endif

#endif /* DYNCALL_VALUE_H */

