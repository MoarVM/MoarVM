/*

 Package: dyncall
 Library: dyncallback
 File: dyncallback/dyncall_callback_x64_gas_w64.s
 Description: Callback Thunk - Implementation for x64 (GNU as assembler)
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
.globl dcCallbackThunkEntry

/* sizes */

.set DCThunk_size	,  24
.set DCArgs_size	,  80 /* 128 */
.set DCValue_size	,   8

/* frame local variable offsets relative to %rbp*/

.set FRAME_arg0		,  48 /* 16 */
.set FRAME_return	,   8
.set FRAME_parent	,   0
.set FRAME_DCArgs	, -80  /* -128 */
.set FRAME_DCValue	, -80  /* -136 */

/* struct DCCallback */

.set CTX_thunk		,   0
.set CTX_handler	,  24
.set CTX_userdata	,  32
.set DCCallback_size	,  40

dcCallbackThunkEntry:

	pushq %rbp
	movq  %rbp, %rsp

	// initialize DCArgs

	// float parameters (4 registers spill to DCArgs)
	sub %rsp, 4*8
	movq [%rsp+8*3], %xmm3  # struct offset  72: float parameter 3
	movq [%rsp+8*2], %xmm2  # struct offset  64: float parameter 2
	movq [%rsp+8*1], %xmm1  # struct offset  56: float parameter 1
	movq [%rsp+8*0], %xmm0	# struct offset  48: float parameter 0

	// fill integer parameters (4 registers spill to DCArgs)
	pushq 	%r9			# struct offset 40: int parameter 3
	pushq 	%r8			# struct offset 32: int parameter 2
	pushq 	%rdx			# struct offset 24: int parameter 1
	pushq 	%rcx			# struct offset 16: int parameter 0

	// fill register counts for integer/pointer and float regs
	pushq	0			# struct offset 8:  register count

	// fill argument stack pointer
	lea  	%rdx, [%rbp+FRAME_arg0]
	pushq	%rdx			# struct offset 0: stack pointer

	mov 	%rdx, %rsp		# arg1 (RDX) = DCArgs*

	// initialize DCValue:
	// pushq 	0			# structo offset 0: return value (max long long)

	// call handler( *ctx, *args, *value, *userdata)

	mov  %rcx, %rax			# arg0 (RCX) = DCCallback* (RAX)
	mov  %r9,  [%rax+CTX_userdata]	# arg3 (R9)  = userdata*
	mov  %r8, %rsp 			# arg2 (RDX) = DCValue*

	sub  %rsp, 4*8			# make foom for spill area and call

	call [%rax+CTX_handler]

	// pass return type via registers
	// distinguish two basic classes 'integer' and 'float'

	movq %rax, [%rbp+FRAME_DCValue]
	movd %xmm0, %rax

	mov  %rsp, %rbp
	pop  %rbp
	ret

