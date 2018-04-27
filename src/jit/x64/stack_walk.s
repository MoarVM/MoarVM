    .intel_syntax noprefix
    .text
    .globl stack_find_return_address_in_frame_posix
    .globl _stack_find_return_address_in_frame_posix
stack_find_return_address_in_frame_posix:
_stack_find_return_address_in_frame_posix:
    mov rcx, rdi /* arg0 = start-of-code region */
    mov r8,  rdx /* arg2 = stack base-pointer */
    mov rdx, rsi /* arg1 = size-of-code */
_stack_find_return_address_in_frame_win64:
    push rbp
    mov r9, rsp
    add rdx, rcx /* compute end-of-code region */
    /* rcx = start, rdx = end, r8 = stack base,
     * r9 = next stack frame, ax = return address */
loop:
    cmp r8, r9 /* have we gone past our start-of-stack pointer? */
    jle done /* stack grows downwards */
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
