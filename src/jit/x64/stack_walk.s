	.intel_syntax noprefix
	.text
        .globl stack_find_return_address_in_frame_posix
	.globl _stack_find_return_address_in_frame_posix
stack_find_return_address_in_frame_posix:
_stack_find_return_address_in_frame_posix:
	mov rcx, rdi /* base pointer */
	mov r8,  rdx /* maximum number of steps */
	mov rdx, rsi /* end pointer */
_stack_find_return_address_in_frame_win64:
	/* rdi = base pointer, rsi = end pointer */
	push rbp
	mov r9, rsp
loop:
	dec r8 /* counter */
	jz done
	mov rax, qword ptr [r9+0x8]
	mov r9, qword ptr [r9]
	cmp rax, rcx
	jl  loop
	cmp rax, rdx
	jg  loop
done:
	/* rax is now within range by definition, or, we're to deep */
	pop rbp
	ret
