.code
	;; rcx = arg(0) = code-region-start
	;; rdx = arg(1) = size-of-region
	;; r8 = arg(2) = top-of-stack
stack_find_return_address_in_frame proc
    push rbp			; save frame pointer
    mov r9, rsp			; stack pointer
    add rdx, rcx		; compute end-of-region
_loop label near
    cmp r8, r9			; have we passed the top-of-stack?
    jle done
    mov rax, [r9+8h]		; load return pointer
    mov r9, [r9]		; load frame pointer
    cmp rax, rcx		; greater than start?
    jl  _loop
    cmp rax, rdx		; smaller than end?
    jg  _loop
done label near			; we're done
    pop rbp
    ret
stack_find_return_address_in_frame endp
end	
