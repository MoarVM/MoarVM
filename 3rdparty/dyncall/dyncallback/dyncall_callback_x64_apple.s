/*

 Package: dyncall
 Library: dyncallback
 File: dyncallback/dyncall_callback_x64_apple.s
 Description: Callback Thunk - Implementation for x64 (Apple as assembly)
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


.intel_syntax
.text

/* sizes */

.set DCThunk_size	,  24
.set DCArgs_size	, 128
.set DCValue_size	,   8

/* frame local variable offsets relative to %rbp*/

.set FRAME_arg0		,  16
.set FRAME_return	,   8
.set FRAME_parent	,   0
.set FRAME_DCArgs	,-128
.set FRAME_DCValue	,-136

/* struct DCCallback */

.set CTX_thunk		,   0
.set CTX_handler	,  24
.set CTX_userdata	,  32
.set DCCallback_size	,  40

.globl _dcCallbackThunkEntry
_dcCallbackThunkEntry:

	pushq %rbp
	movq  %rbp, %rsp

	// initialize DCArgs

	// float parameters (8 registers spill to DCArgs)
	sub %rsp, 8*8
	movq [%rsp+8*7], %xmm7  # struct offset 120: float parameter 7
	movq [%rsp+8*6], %xmm6  # struct offset 112: float parameter 6
	movq [%rsp+8*5], %xmm5  # struct offset 104: float parameter 5
	movq [%rsp+8*4], %xmm4  # struct offset  96: float parameter 4
	movq [%rsp+8*3], %xmm3  # struct offset  88: float parameter 3
	movq [%rsp+8*2], %xmm2  # struct offset  80: float parameter 2
	movq [%rsp+8*1], %xmm1  # struct offset  72: float parameter 1
	movq [%rsp+8*0], %xmm0	# struct offset  64: float parameter 0

	// integer parameters (6 registers spill to DCArgs)
	pushq %r9	# struct offset 56: parameter 5
	pushq %r8	# struct offset 48: parameter 4
	pushq %rcx	# struct offset 40: parameter 3
	pushq %rdx	# struct offset 32: parameter 2
	pushq %rsi	# struct offset 24: parameter 1
	pushq %rdi	# struct offset 16: parameter 0

	// register counts for integer/pointer and float regs
			# struct offset 12: fcount
	pushq 0		# struct offset 8:  icount

	lea  %rdx, [%rbp+FRAME_arg0]	# struct offset 0: stack pointer
	pushq %rdx

	mov  %rsi, %rsp			# parameter 1 (RSI) = DCArgs*

	// initialize DCValue

	pushq 0				# structo offset 0: return value (max long long)

	// call handler( *ctx, *args, *value, *userdata)

	mov  %rdi, %rax			# parameter 0 (RDI) = DCCallback* (RAX)
	mov  %rcx, [%rdi+CTX_userdata]	# arg3 = userdata*
	mov  %rdx, %rsp 		# arg2 (RDX) = DCValue*

	pushq 0				# align to 16 bytes

	call [%rax+CTX_handler]

	// pass return type via registers
	// distinguish two basic classes 'integer' and 'float'
	
	mov  %dl, %al
	movq %rax, [%rbp+FRAME_DCValue]

ASCII_f	= 102
ASCII_d = 100

	cmpb	%dl, ASCII_f
	je	.float
	cmpb	%dl, ASCII_d
	jne	.return
.float:
	movd %xmm0, %rax

.return:
	mov  %rsp, %rbp
	pop  %rbp
	ret


