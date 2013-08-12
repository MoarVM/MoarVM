.CODE
DCThunk_size = 24
DCArgs_size_win64 = 80
DCArgs_size_sysv = 128
DCValue_size = 8
FRAME_arg0_win64 = 48
FRAME_arg0_sysv = 16
FRAME_return = 8
FRAME_parent = 0
FRAME_DCArgs_sysv = -128
FRAME_DCValue_sysv = -136
FRAME_DCArgs_win64 = -80
FRAME_DCValue_win64 = -80
CTX_thunk = 0
CTX_handler = 24
CTX_userdata = 32
DCCallback_size = 40
dcCallback_x64_sysv PROC
OPTION PROLOGUE:NONE, EPILOGUE:NONE
 push RBP
 mov RBP,RSP
 sub RSP,8*8
 movsd qword ptr [RSP+8*7],XMM7
 movsd qword ptr [RSP+8*6],XMM6
 movsd qword ptr [RSP+8*5],XMM5
 movsd qword ptr [RSP+8*4],XMM4
 movsd qword ptr [RSP+8*3],XMM3
 movsd qword ptr [RSP+8*2],XMM2
 movsd qword ptr [RSP+8*1],XMM1
 movsd qword ptr [RSP+8*0],XMM0
 push R9
 push R8
 push RCX
 push RDX
 push RSI
 push RDI
 push 0
 lea RDX,qword ptr [RBP+FRAME_arg0_sysv]
 push RDX
 mov RSI,RSP
 push 0
 mov RDI,RAX
 mov RCX,qword ptr [RDI+CTX_userdata]
 mov RDX,RSP
 push 0
 call qword ptr [RAX+CTX_handler]
 mov DL,AL
 mov RAX,qword ptr [RBP+FRAME_DCValue_sysv]
 cmp DL,102
 je return_f64
 cmp DL,100
 jne return_i64
return_f64:
 movd XMM0,RAX
return_i64:
 mov RSP,RBP
 pop RBP
 ret
dcCallback_x64_sysv ENDP
dcCallback_x64_win64 PROC
OPTION PROLOGUE:NONE, EPILOGUE:NONE
 push RBP
 mov RBP,RSP
 sub RSP,4*8
 movsd qword ptr [RSP+8*3],XMM3
 movsd qword ptr [RSP+8*2],XMM2
 movsd qword ptr [RSP+8*1],XMM1
 movsd qword ptr [RSP+8*0],XMM0
 push R9
 push R8
 push RDX
 push RCX
 push 0
 lea RDX,qword ptr [RBP+FRAME_arg0_win64]
 push RDX
 mov RDX,RSP
 mov RCX,RAX
 mov R9,qword ptr [RAX+CTX_userdata]
 mov R8,RSP
 sub RSP,4*8
 call qword ptr [RAX+CTX_handler]
 mov RAX,qword ptr [RBP+FRAME_DCValue_win64]
 movd XMM0,RAX
 mov RSP,RBP
 pop RBP
 ret
dcCallback_x64_win64 ENDP
END
