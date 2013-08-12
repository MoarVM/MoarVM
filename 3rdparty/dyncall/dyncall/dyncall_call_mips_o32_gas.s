/*

 Package: dyncall
 Library: dyncall
 File: dyncall/dyncall_call_mips_o32_gas.s
 Description: mips "o32" abi call kernel implementation in GNU Assembler
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


	.section .mdebug.abi32
	.previous
	.abicalls
	.text
	.align	2
	.globl	dcCall_mips_o32
	.ent	dcCall_mips_o32
	.type	dcCall_mips_o32, @function
dcCall_mips_o32:
	.frame	$fp,40,$31		/* vars=8, regs=2/0, args=16, gp=8 */
	.mask	0xc0000000,-4
	.fmask	0x00000000,0
	.set	noreorder
	.set	nomacro
	
	addiu	$sp,$sp,-40
	sw	$31,36($sp)
	sw	$fp,32($sp)
	move	$fp,$sp
	.cprestore	16
	sw	$4,40($fp)
	sw	$5,44($fp)
	sw	$6,48($fp)
	sw	$7,52($fp)
	
	/* $4   target function */
 	/* $5   register data */
	/* $6   stack size (min 16-byte aligned to 8-bytes already) */
	/* $7   stack data */

	/* increment stack */
	
	sub	$sp, $sp, $6

	/* copy stack data */

	/* $12  source pointer (parameter stack data) */
	/* $14  destination (stack pointer) */
	/* $6   byte count */

	move	$12, $7
	move	$14, $sp

.next:
	beq	$6, $0, .skip
	nop
	addiu	$6, $6, -4
	lw	$2, 0($12)
	nop
	sw	$2, 0($14)
	nop
	addiu	$12,$12, 4
	addiu	$14,$14, 4
	j	.next
	nop
.skip:

	/* load two double-precision floating-point argument registers ($f12, $f14) */

#if defined(__MIPSEL__)
	lwc1	$f12, 0($5)		/* float arg 0 */
	nop
	lwc1	$f13, 4($5)
	nop
	lwc1	$f14, 8($5)		/* float arg 1 */
	nop
	lwc1	$f15,12($5)
	nop
#else
	lwc1	$f12,  4($5)		/* float arg 0 */
	nop
	lwc1	$f13,  0($5)
	nop
	lwc1	$f14, 12($5)		/* float arg 1 */
	nop
	lwc1	$f15,  8($5)
	nop
#endif

	/* prepare call */

	/* $12  stack data */
	/* $25  target function */

	move	$12, $7
	move	$25, $4

	/* load first four integer arguments ($4-$7) */

	lw	$4, 0($12)
	nop
	lw	$5, 4($12)
	nop
	lw	$6, 8($12)
	nop
	lw	$7,12($12)
	nop

	/* call target function */

	jalr	$25
	nop

	lw	$28,16($fp)	/* restore global pointer */
	move	$sp,$fp 	/* restore stack pointer */
	lw	$31,36($sp)	/* restore return address */
	lw	$fp,32($sp)	/* restore frame pointer */
	addiu	$sp,$sp,40	/* end stack frame */
	j	$31		/* return */
	nop

	.set	macro
	.set	reorder
	.end	dcCall_mips_o32 
	.ident 	"handwritten"

