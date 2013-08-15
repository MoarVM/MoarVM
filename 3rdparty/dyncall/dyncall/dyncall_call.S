/*

 Package: dyncall
 Library: dyncall
 File: dyncall/dyncall_call.S
 Description: assembly call kernel, auto-selected by gcc / Plan9 build system
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
/* Plan9 */
#if defined(DC__OS_Plan9)
#  if defined(DC__Arch_Intel_x86)
#    include "dyncall_call_x86_8a.s"
#  else
#    error Unsupported Architecture on Plan9.
#  endif
#elif defined(DC__C_MSVC)
#  if defined(DC__OS_Win32)
#    include "dyncall_call_x86_generic_masm.asm"
#  elif defined(DC__OS_Win64)
#    include "dyncall_call_x64_generic_masm.asm"
#  endif
#else
#  if defined(DC__Arch_Intel_x86)
#    include "dyncall_call_x86.S"
#  elif defined(DC__Arch_AMD64)
#    include "dyncall_call_x64-att.S"
#  elif defined(DC__Arch_ARM_ARM)
#    include "dyncall_call_arm32_arm.S"
#  elif defined(DC__Arch_ARM_THUMB)
#    if defined(DC__OS_Darwin)
#      include "dyncall_call_arm32_thumb_apple.s"
#    else
#      include "dyncall_call_arm32_thumb_gas.s"
#    endif
#  elif defined(DC__Arch_PowerPC)
#    include "dyncall_call_ppc32.S"
#  elif defined(DC__Arch_MIPS) || defined(DC__Arch_MIPS64)
#    include "dyncall_call_mips_gas.S"
#  elif defined(DC__Arch_Sparcv9)
#    include "dyncall_call_sparc64.S"
#  elif defined(DC__Arch_Sparc)
#    include "dyncall_call_sparc.S"
#  else
#    error Unsupported Architecture.
#  endif
#endif
