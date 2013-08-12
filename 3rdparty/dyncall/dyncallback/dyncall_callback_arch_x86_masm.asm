.386
.MODEL FLAT
.CODE

DCThunk_size = 16
DCArgs_size = 20
DCValue_size = 8
CTX_thunk = 0
CTX_phandler = 16
CTX_pargsvt = 20
CTX_stack_cleanup = 24
CTX_userdata = 28
frame_arg0 = 8
frame_ret = 4
frame_parent = 0
frame_CTX = -4
frame_DCArgs = -24
frame_DCValue = -32
_dcCallbackThunkEntry PROC
OPTION PROLOGUE:NONE, EPILOGUE:NONE
 push EBP
 mov EBP,ESP
 push EAX
 push 0
 push EDX
 push ECX
 lea ECX,dword ptr [EBP+frame_arg0]
 push ECX
 push dword ptr [EAX+CTX_pargsvt]
 mov ECX,ESP
 push 0
 push 0
 mov EDX,ESP
 and ESP,-16
 push dword ptr [EAX+CTX_userdata]
 push EDX
 push ECX
 push EAX
 call dword ptr [EAX+CTX_phandler]
 mov ESP,EBP
 pop ECX
 pop ECX
 mov EDX,dword ptr [EBP+frame_CTX]
 add ESP,dword ptr [EDX+CTX_stack_cleanup]
 push ECX
 lea EDX,dword ptr [EBP+frame_DCValue]
 mov EBP,dword ptr [EBP+0]
 cmp AL,118
 je return_void
 cmp AL,100
 je return_f64
 cmp AL,102
 je return_f32
 cmp AL,108
 je return_i64
 cmp AL,76
 je return_i64
return_i32:
 mov EAX,dword ptr [EDX+0]
 ret
return_i64:
 mov EAX,dword ptr [EDX+0]
 mov EDX,dword ptr [EDX+4]
 ret
return_f32:
 fld dword ptr [EDX+0]
 ret
return_f64:
 fld qword ptr [EDX+0]
return_void:
 ret
_dcCallbackThunkEntry ENDP
END
