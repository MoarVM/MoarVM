/*

 Package: dyncall
 Library: dyncall
 File: dyncall/dyncall_call_x64.S
 Description: All x64 abi call kernel implementation
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


#include "../portasm/portasm-x64.S"

BEGIN_ASM

/*---------------------------------------------------------------------------
  
  Call Kernel for x64 System V
  
  Input:
    RDI : size of arguments to be passed via stack 
    RSI : pointer to arguments to be passed via the stack 
    RDX : pointer to arguments of integral/pointer type to be passed via registers 
    RCX : pointer to arguments of floating point type to be passed via registers 
    R8  : target function pointer 

*/

GLOBAL(dcCall_x64_sysv)
BEGIN_PROC(dcCall_x64_sysv)

	push	RBP			/* Pseudo-prolog - preserve RBP. */
	push	RBX			/* Preserve RBX and store pointer to function in it. */

	mov	RBP, RSP		/* Store stack pointer in RBP.   */

	mov	RBX, R8

	movsd	XMM0, qword ptr[RCX   ]	/* Copy first 8 floats to XMM0-XMM7 (this makes RCX free to use). */
	movsd	XMM1, qword ptr[RCX+ 8]
	movsd	XMM2, qword ptr[RCX+16]
	movsd	XMM3, qword ptr[RCX+24]
	movsd	XMM4, qword ptr[RCX+32]
	movsd	XMM5, qword ptr[RCX+40]
	movsd	XMM6, qword ptr[RCX+48]
	movsd	XMM7, qword ptr[RCX+56]

	sub	RSP, RDI		/* Setup stack frame by subtracting the size of the arguments. */

	mov	RAX, RDI		/* Align stack. */
	add	RAX, 8
	and	RAX, 15
	sub	RSP, RAX

	mov	RCX, RDI		/* Store number of bytes to copy to stack in RCX (for rep movsb). */
	mov	RDI, RSP		/* Store pointer to beginning of stack arguments in RDI (for rep movsb). */

	rep movsb			/* @@@ should be optimized (e.g. movq) */

	mov	RDI, qword ptr[RDX   ]	/* Copy first six int/pointer arguments to RDI, RSI, RDX, RCX, R8, R9. */
	mov	RSI, qword ptr[RDX+ 8]
	mov	RCX, qword ptr[RDX+24]
	mov	R8,  qword ptr[RDX+32]
	mov	R9,  qword ptr[RDX+40]
	mov	RDX, qword ptr[RDX+16]	/* Set RDX last to not overwrite it to soon. */

	mov	AL, 8						/* Put upper bound of number of used xmm registers in AL. */
	call	RBX						/* Invoke function. */

	mov	RSP, RBP					/* Restore stack pointer (such that we can pop the preserved vALues). */

	pop	RBX						/* Restore RBX. */
	pop	RBP						/* Pseudo-epilog. */

	ret
END_PROC(dcCALl_x64_sysv)

/*---------------------------------------------------------------------------

  Call Kernel for x64 Win64
	
  Input:
    RCX : size of arguments to be passed via stack 
    RDX : pointer to arguments to be passed via the stack 
    R8  : pointer to arguments of integral/pointer type to be passed via registers 
    R9  : target function pointer

*/

GLOBAL(dcCall_x64_win64)
BEGIN_PROC(dcCall_x64_win64)

	push	RBP			/* Pseudo-prolog - preserve RBP. */
	push	RSI			/* Preserve RSI and RDI. */
	push	RDI
	
	/* and  RSP, -16		/* Align frame to 16 bytes.    */
	mov	RBP, RSP		/* Store stack pointer in RBP. */

	add	RCX, 15			/* Align stack size to 16 bytes. */
	and	RCX, -16
	sub	RSP, RCX		/* Setup stack frame by subtracting the size of the arguments. */

	mov	RSI, RDX		/* Let RSI point to the arguments. */
	mov	RDI, RSP		/* Store pointer to beginning of stack arguments in RDI (for rep movsb). */
	mov	RAX, R9			/* Put function address in RAX. */

	rep movsb			/* @@@ should be optimized (e.g. movq) */

	mov	RCX,  qword ptr[R8   ]	/* Copy first four arguments to RCX, RDX, R8, R9 and XMM0-XMM3. */
	mov	RDX,  qword ptr[R8+ 8]
	mov	R9,   qword ptr[R8+24]	/* Set R9 first to not overwrite R8 too soon. */
	mov	R8,   qword ptr[R8+16]
	movd	XMM0, RCX
	movd	XMM1, RDX
	movd	XMM2, R8
	movd	XMM3, R9

	push	R9			/* Push first four arguments onto the stack preserve area. */
	push	R8
	push	RDX
	push	RCX

	call	RAX			/* Invoke function. */

	mov	RSP, RBP		/* Restore stack pointer (such that we can pop the preserved vALues). */

	pop	RDI			/* Restore RSI and RDI. */
	pop	RSI
	pop	RBP			/* Pseudo-epilog. */

	ret

END_PROC(dcCall_x64_win64)

END_ASM

