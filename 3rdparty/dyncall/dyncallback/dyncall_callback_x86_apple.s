/*

 Package: dyncall
 Library: dyncallback
 File: dyncallback/dyncall_callback_x86_apple.s
 Description: Callback Thunk - Implementation for x86 on Darwin/Apple's as
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

.text
.file "dyncall_callback_x86_apple.s"
.intel_syntax

.globl _dcCallbackThunkEntry

DCThunk_size		= 16
DCArgs_size			= 20
DCValue_size		=  8

CTX_thunk			=  0
CTX_phandler		= 16
CTX_pargsvt			= 20
CTX_stack_cleanup	= 24
CTX_userdata		= 28

frame_arg0         =  8
frame_ret          =  4
frame_parent       =  0
frame_CTX          = -4
frame_DCArgs       = -24
frame_DCValue      = -32

ASCII_L = 76
ASCII_l = 108
ASCII_d	= 100
ASCII_f = 102
ASCII_i = 105
ASCII_v = 118

_dcCallbackThunkEntry:
	push %ebp
	mov  %ebp, %esp

	// local variable frame_CTX
	push %eax			# EAX = CTX*

	// initialize DCArgs
	push 0				# fast_count
	push %edx			# fast_data[1]
	push %ecx			# fast_data[0]
	lea  %ecx, [%ebp+frame_arg0]	#   compute arg stack address
	push %ecx			# stack_ptr
	push [%eax+CTX_pargsvt]		# virtual table*

	mov  %ecx, %esp			# ECX = DCArgs*

	// initialze DCValue
	push 0
	push 0

	mov  %edx, %esp			# EDX = DCValue*

	// align 16 bytes
	add  esp, 15		/* align size to 16 byte */
	and  esp, -16
	
	// call handler 
	push [%eax+CTX_userdata]	# userdata
	push %edx			# DCValue*
	push %ecx			# DCArgs*
	push %eax			# DCCallback*
	call [%eax+CTX_phandler]

	// cleanup stack

	mov  %esp, %ebp		# reset esp to frame
	pop  %ecx		# skip parent frame
	pop  %ecx		# pop return-address
	mov  %edx, [%ebp+frame_CTX]
	add  %esp, [%edx+CTX_stack_cleanup] # cleanup stack
	push %ecx		# push back return address
	lea  %edx, [%ebp+frame_DCValue]
	mov  %ebp, [%ebp]	# EBP = parent frame

	// handle return value

	cmp %al, ASCII_v
	je .return_void
	cmp %al, ASCII_d
	je .return_f64
	cmp %al, ASCII_f
	je .return_f32
	cmp %al, ASCII_l
	je .return_i64
	cmp %al, ASCII_L
	je .return_i64_
	
	// All int cases <= 32 bits (+ pointer & string cases) fall in the 32 bits int case	

.return_i32:
	mov  %eax, [%edx]
	ret

.return_i64:
.return_i64_:
	mov  %eax, [%edx]
	mov  %edx, [%edx+4]
	ret

.return_f32:
	fld dword ptr [%edx]
	ret

.return_f64:
	fld qword ptr [%edx]
	ret

.return_void:
	ret
