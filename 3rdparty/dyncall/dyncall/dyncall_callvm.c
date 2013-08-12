/*

 Package: dyncall
 Library: dyncall
 File: dyncall/dyncall_callvm.c
 Description: auto-select default callvm (includes other C sources).
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


#include "dyncall_macros.h"

#if defined(DC__Arch_Intel_x86)
#  include "dyncall_callvm_x86.c"
#elif defined(DC__Arch_AMD64)
#  include "dyncall_callvm_x64.c"
#elif defined(DC__Arch_PowerPC)
#  include "dyncall_callvm_ppc32.c"
#elif defined(DC__Arch_PPC64)
#  include "dyncall_callvm_ppc64.c"
#elif defined(DC__Arch_MIPS) || defined(DC__Arch_MIPS64)
#  if defined(DC__ABI_MIPS_EABI)
#    include "dyncall_callvm_mips_eabi.c"
#  elif defined(DC__ABI_MIPS_O32)
#    include "dyncall_callvm_mips_o32.c"
#  elif defined(DC__ABI_MIPS_N64)
#    include "dyncall_callvm_mips_n64.c"
#  elif defined(DC__ABI_MIPS_N32)
#    include "dyncall_callvm_mips_n32.c"
#  else
#    error Unknown MIPS ABI.
#  endif /* DC__Arch_MIPS || DC__Arch_MIPS64 */
#elif defined(DC__Arch_ARM_ARM)
#  include "dyncall_callvm_arm32_arm.c"
#elif defined(DC__Arch_ARM_THUMB)
#  include "dyncall_callvm_arm32_thumb.c"
#elif defined(DC__Arch_Sparc)
#  include "dyncall_callvm_sparc.c"
#elif defined(DC__Arch_Sparcv9)
#  include "dyncall_callvm_sparc64.c"
#else
#  error unsupported platform
#endif

