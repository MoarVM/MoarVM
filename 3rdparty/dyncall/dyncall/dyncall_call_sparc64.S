/*

 Package: dyncall
 Library: dyncall
 File: dyncall/dyncall_call_sparc64.S
 Description: Call kernel for sparc64 v9 ABI.
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



/* --------------------------------------------------------------------------- 

call kernel for sparc64 v9 abi
tested on sparc64/linux/debian [gcc54.fsffrance.org - thanx to the farm!] 

new C Interface:
  void dcCall_sparc (DCCallVM* callvm, DCpointer target);
                     %i0               %1 

see dyncall_call_sparc.S for details.

old C Interface:
  void dcCall_sparc (DCpointer target, DCsize size, DCpointer data);
		     %i0             , %i1        , %i2
Input:
  i0   target
  i1   size
  i2   data

*/

#define REGSIZE 8

#define BIAS 2047

#define ALIGN 16 

#define IREGS 6
#define FREGS 16
#define SREGS 16
#define IBASE  0
#define FBASE  (IREGS*8)

#define SHEAD ((16+6)*8)
#define DHEAD  ((IREGS+FREGS)*8)+SREGS*4

CALLVM_singleUseFlags = 28
CALLVM_size    = 40
CALLVM_dataoff = 48


.global dcCall_sparc64
dcCall_sparc64:

/* Basic Prolog: supports up to 6 arguments. */
	
	/* new C interface */
	/* o0-1: callvm,target */
	
	or   %o0, %g0, %o3	       /* %o3: callvm */
	or   %o1, %g0, %o0	       /* %o0: target */
	ldx [%o3+CALLVM_size], %o1     /* %o1: size */
	add %o3, CALLVM_dataoff, %o2  /* %o2: data */
	ld  [%o3+CALLVM_singleUseFlags], %o4 /* %o4: flags */
	/*leaf functions: may use the first six output registers.*/
	/*o0-2:target,size,data*/
	/*o3-5:free to use */
		
		/* Arguments: */
		/* %o0 = ptr to target. */
		/* %o1 = size of data. */
		/* %o2 = data pointer. */
		/* %o4 = use flags. */

	/* Compute a matching stack size (approximate): o3 = align(o1+136,16) */

	add     %o1, SHEAD+ALIGN-1, %o3
	and     %o3,   -ALIGN, %o3
	neg     %o3
	
	/* Prolog. */
	save	%sp, %o3, %sp	

		/* Arguments: */
		/* %i0 = ptr to target. */
		/* %i1 = size of data. */
		/* %i2 = data pointer. */
		/* %i3 = stack size. */
	
	/* Load output registers. */

	ldx	[%i2+IBASE+REGSIZE*0 ],%o0
	ldx	[%i2+IBASE+REGSIZE*1 ],%o1
	ldx	[%i2+IBASE+REGSIZE*2 ],%o2
	ldx	[%i2+IBASE+REGSIZE*3 ],%o3
	ldx	[%i2+IBASE+REGSIZE*4 ],%o4
	ldx	[%i2+IBASE+REGSIZE*5 ],%o5

	/* Load double-precision float registers. */

	ldd     [%i2+FBASE+REGSIZE*0 ],%f0
	ldd     [%i2+FBASE+REGSIZE*1 ],%f2
	ldd     [%i2+FBASE+REGSIZE*2 ],%f4
	ldd     [%i2+FBASE+REGSIZE*3 ],%f6
	ldd     [%i2+FBASE+REGSIZE*4 ],%f8
	ldd     [%i2+FBASE+REGSIZE*5 ],%f10
	ldd     [%i2+FBASE+REGSIZE*6 ],%f12
	ldd     [%i2+FBASE+REGSIZE*7 ],%f14
	ldd     [%i2+FBASE+REGSIZE*8 ],%f16
        ldd     [%i2+FBASE+REGSIZE*9 ],%f18
	ldd     [%i2+FBASE+REGSIZE*10],%f20
	ldd     [%i2+FBASE+REGSIZE*11],%f22
	ldd     [%i2+FBASE+REGSIZE*12],%f24
	ldd     [%i2+FBASE+REGSIZE*13],%f26
	ldd     [%i2+FBASE+REGSIZE*14],%f28
	ldd     [%i2+FBASE+REGSIZE*15],%f30

	/* load single-precision float registers */

	or	%g0, 1, %l0
.f0:
	andcc   %i4, %l0, %g0
	beq	.f1	
	nop
	ld	[%i2+FBASE+REGSIZE*16+4*0 ], %f1
.f1:
	sll     %l0, 1, %l0
	andcc   %i4, %l0, %g0
	beq	.f2	
	nop
	ld	[%i2+FBASE+REGSIZE*16+4*1 ], %f3
.f2:
	sll     %l0, 1, %l0
	andcc   %i4, %l0, %g0
	beq	.f3	
	nop
	ld	[%i2+FBASE+REGSIZE*16+4*2 ], %f5
.f3:
	sll     %l0, 1, %l0
	andcc   %i4, %l0, %g0
	beq	.f4	
	nop
	ld	[%i2+FBASE+REGSIZE*16+4*3 ], %f7
.f4:
	sll     %l0, 1, %l0
	andcc   %i4, %l0, %g0
	beq	.f5	
	nop
	ld	[%i2+FBASE+REGSIZE*16+4*4 ], %f9
.f5:
	sll     %l0, 1, %l0
	andcc   %i4, %l0, %g0
	beq	.f6	
	nop
	ld	[%i2+FBASE+REGSIZE*16+4*5 ], %f11
.f6:
	sll     %l0, 1, %l0
	andcc   %i4, %l0, %g0
	beq	.f7
	nop	
	ld	[%i2+FBASE+REGSIZE*16+4*6 ], %f13
.f7:
	sll     %l0, 1, %l0
	andcc   %i4, %l0, %g0
	beq	.f8
	nop	
	ld	[%i2+FBASE+REGSIZE*16+4*7 ], %f15
.f8:
	sll     %l0, 1, %l0
	andcc   %i4, %l0, %g0
	beq	.f9
	nop	
	ld	[%i2+FBASE+REGSIZE*16+4*8 ], %f17
.f9:
	sll     %l0, 1, %l0
	andcc   %i4, %l0, %g0
	beq	.f10
	nop	
	ld	[%i2+FBASE+REGSIZE*16+4*9 ], %f19
.f10:
	sll     %l0, 1, %l0
	andcc   %i4, %l0, %g0
	beq	.f11
	nop	
	ld	[%i2+FBASE+REGSIZE*16+4*10], %f21
.f11:
	sll     %l0, 1, %l0
	andcc   %i4, %l0, %g0
	beq	.f12
	nop	
	ld	[%i2+FBASE+REGSIZE*16+4*11], %f23
.f12:
	sll     %l0, 1, %l0
	andcc   %i4, %l0, %g0
	beq	.f13
	nop	
	ld	[%i2+FBASE+REGSIZE*16+4*12], %f25
.f13:
	sll     %l0, 1, %l0
	andcc   %i4, %l0, %g0
	beq	.f14
	nop	
	ld	[%i2+FBASE+REGSIZE*16+4*13], %f27
.f14:
	sll     %l0, 1, %l0
	andcc   %i4, %l0, %g0
	beq	.f15
	nop	
	ld	[%i2+FBASE+REGSIZE*16+4*14], %f29
.f15:
	sll     %l0, 1, %l0
	andcc   %i4, %l0, %g0
	beq	.f_end
	nop	
	ld	[%i2+FBASE+REGSIZE*16+4*15], %f31
.f_end:
        /* Skip Register Data, do we nee to copy on stack at all? */
	sub	%i1,  DHEAD, %i1   		/* skip data header. */
	cmp     %i1, 0
        ble	.do_call
	 nop

	/* Copy loop: */
	add     %i2,  DHEAD, %i2		/* i2 = skip data header. */
	or	%g0, %g0, %l0			/* l0 = offset initialized to 0. */
	add     %sp,  BIAS+SHEAD, %l2		/* l2 = argument area on stack space (7th word). (64+4+6*4 = byte offset 92). */

.next:
	ldx     [%i2+%l0],%l1			/* Read from arg buffer(%i2) to %l1. */
	stx     %l1, [%l2+%l0]			/* Write %l1 to stack space(%l2). */
	add     %l0, REGSIZE, %l0		/* Increment offset. */
	sub     %i1, REGSIZE, %i1		/* Decrement copy size. */
	cmp     %i1, 0
        bgt     .next
	 nop
.do_call:
	call	%i0				/* Call target. */
	 nop
        or     %o0, %g0, %i0
        or     %o1, %g0, %i1
        or     %o2, %g0, %i2
        or     %o3, %g0, %i3
	return  %i7 + 8
	 nop
/*
        or      %o0, %g0, %i0
	jmpl %i7 + 8, %g0
	 nop
        restore
	ret
*/

/* 

Changes from v8:
- fundamental data types
	- (un)signed int: 8,16,32,64
	- float: 32,64,128
- float: IEEE 754 compilant
	32 32-bit  float registers f0,f1,..,f31
	32 64-bit  float registers f0,f2,..,f62
	16 128-bit float registers f0,f4,..,f60

Description:
We need to raise up a dynamic stack frame.
Therefore we need to compute the stack size. We do this first, 
in the context of the caller as a leaf function (using o3 as scratch for addition).
Then we raise the frame, ending up in o0-o3 is then i0-i3.


Stack Layout:
     BIAS = 2047

   BIAS+XX: should be 16 byte aligned.
                 ...
       136: argument overflow area
       128:  1 extended word  for struct/union poiner return value
   BIAS+ 0: 16 extended words for registers (in/local) save area [register window]


Function Argument Passing:
- integer %o0..%o5 (caller view).
- floating-point %f0 .. %f15
- continuous memory starting at %sp+BIAS+136 (caller view).

Register Usage:
%fp0..%fp31  : floating-point arguments.
%sp  or  %o6 : stack pointer, always 8 (or 16?)-byte aligned.
%fp  or  %i6 : frame pointer.
%i0  and %o0 : integer and pointer return values.
%i7  and %o7 : return address. (caller puts return address to %o7, callee uses %i7)
%fp0 and %fp1: return value (float).
%i0..%i5     : input argument registers 
%o0..%o5     : output argument registers
%g0          : always zero, writes to it have no effect.

Register Mappings:
r0-7    -> globals
r8-15   -> outs
r16-r23 -> locals
r24-r31 -> ins

Integer Register Overview Table:
ID      Class   Name    Description
------------------------------------------------------------------------------
0	globals	g0	always zero, writes to it have no effect
1		g1
2		g2
3		g3
4		g4
5		g5
6		g6
7		g7
8	out	o0	[int/ptr] arg 0 and return
9		o1	          arg 1
10		o2	          arg 2
11		o3	          arg 3
12              o4                arg 4
13              o5                arg 5
14              o6	stack pointer
15		o7
16	local	l0	scratch
17		l1
18		l2
19		l3
20		l4
21		l5
22		l6
23		l7
24	in	i0	[int/pt] arg 0 and return
25		i1
26		i2
27		i3
28		i4
29		i5
30		i6	frame pointer
31		i7
*/

