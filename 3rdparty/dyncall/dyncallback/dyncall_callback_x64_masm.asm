;//////////////////////////////////////////////////////////////////////////////
;
; Copyright (c) 2007-2009 Daniel Adler <dadler@uni-goettingen.de>, 
;                         Tassilo Philipp <tphilipp@potion-studios.com>
;
; Permission to use, copy, modify, and distribute this software for any
; purpose with or without fee is hereby granted, provided that the above
; copyright notice and this permission notice appear in all copies.
;
; THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
; WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
; MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
; ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
; WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
; ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
; OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
;
;//////////////////////////////////////////////////////////////////////////////

;///////////////////////////////////////////////////////////////////////
;
; Package: dyncall
; Library: dyncallback
; File: dyncallback/dyncall_callback_x64_masm.asm
; Description: Callback Thunk - MASM implementation for x64
;
;///////////////////////////////////////////////////////////////////////

.CODE

; sizes
DCThunk_size    =  24
DCArgs_size     =  80
DCValue_size    =   8

; frame local variable offsets relative to rbp
FRAME_arg0      =  48
FRAME_DCArgs    = -80
FRAME_DCValue   = -80

; struct DCCallback
CTX_thunk       =    0
CTX_handler     =   24
CTX_userdata    =   32
DCCallback_size =   40


dcCallbackThunkEntry PROC

  OPTION PROLOGUE:NONE, EPILOGUE:NONE

  ; prolog
  push     rbp
  mov      rbp, rsp
  
  ; initialize DCArgs

  ; float parameters (4 registers spill to DCArgs)
  sub      rsp, 4*8
  movq     qword ptr[rsp+8*3], xmm3  ; struct offset 72: float parameter 3
  movq     qword ptr[rsp+8*2], xmm2  ; struct offset 64: float parameter 2
  movq     qword ptr[rsp+8*1], xmm1  ; struct offset 56: float parameter 1
  movq     qword ptr[rsp+8*0], xmm0  ; struct offset 48: float parameter 0

  ; integer parameters (4 registers spill to DCArgs)
  push     r9                        ; struct offset 40: int parameter 3
  push     r8                        ; struct offset 32: int parameter 2
  push     rdx                       ; struct offset 24: int parameter 1
  push     rcx                       ; struct offset 16: int parameter 0

  push     0                         ; struct offset  8: register count

  lea      rdx, [rbp+FRAME_arg0]     ; struct offset  0: stack pointer
  push     rdx
  
  mov      rdx, rsp                  ; parameter 1 (RDX) = DCArgs*

  ;push     0                         ; @@@ needed??? (don't think so - already aligned)... @@@ align to 16 bytes and provide long long for return value DCValue

  ; call handler( *ctx, *args, *value, *userdata)
  mov      rcx, rax                  ; parameter 0 (RCX) = DCCallback* (RAX)
  mov      r9,  [rax+CTX_userdata]   ; parameter 3 (R9) : void* userdata
  mov      r8,  rsp                  ; parameter 2 (R8) : DCValue* value

  ; make room for spill area and call
  sub      rsp, 4*8
  call     qword ptr[rax+CTX_handler]

  ; Always put return value in rax and xmm0 (so we get ints and floats covered)
  mov      rax, [rbp+FRAME_DCValue]
  movd     xmm0, rax

  ; epilog
  mov      rsp, rbp
  pop      rbp

  ret

dcCallbackThunkEntry ENDP

END
