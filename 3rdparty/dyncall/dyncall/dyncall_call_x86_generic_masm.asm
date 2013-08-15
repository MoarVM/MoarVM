.386
.MODEL FLAT
.CODE

_dcCall_x86_cdecl PROC
OPTION PROLOGUE:NONE, EPILOGUE:NONE
 push EBP
 mov EBP,ESP
 push ESI
 push EDI
 mov ESI,dword ptr [EBP+12]
 mov ECX,dword ptr [EBP+16]
 add ECX,15
 and ECX,-16
 mov dword ptr [EBP+16],ECX
 sub ESP,ECX
 mov EDI,ESP
 rep movsb
 call dword ptr [EBP+8]
 add ESP,dword ptr [EBP+16]
 pop EDI
 pop ESI
 mov ESP,EBP
 pop EBP
 ret
_dcCall_x86_cdecl ENDP
_dcCall_x86_win32_msthis PROC
OPTION PROLOGUE:NONE, EPILOGUE:NONE
 push EBP
 mov EBP,ESP
 push ESI
 push EDI
 mov ESI,dword ptr [EBP+12]
 mov ECX,dword ptr [EBP+16]
 mov EAX,dword ptr [ESI+0]
 add ESI,4
 sub ECX,4
 sub ESP,ECX
 mov EDI,ESP
 rep movsb
 mov ECX,EAX
 call dword ptr [EBP+8]
 pop EDI
 pop ESI
 mov ESP,EBP
 pop EBP
 ret
_dcCall_x86_win32_msthis ENDP
_dcCall_x86_win32_std PROC
OPTION PROLOGUE:NONE, EPILOGUE:NONE
 push EBP
 mov EBP,ESP
 push ESI
 push EDI
 mov ESI,dword ptr [EBP+12]
 mov ECX,dword ptr [EBP+16]
 sub ESP,ECX
 mov EDI,ESP
 rep movsb
 call dword ptr [EBP+8]
 pop EDI
 pop ESI
 mov ESP,EBP
 pop EBP
 ret
_dcCall_x86_win32_std ENDP
_dcCall_x86_win32_fast PROC
OPTION PROLOGUE:NONE, EPILOGUE:NONE
 push EBP
 mov EBP,ESP
 push ESI
 push EDI
 mov ESI,dword ptr [EBP+12]
 mov ECX,dword ptr [EBP+16]
 mov EAX,dword ptr [ESI+0]
 mov EDX,dword ptr [ESI+4]
 add ESI,8
 sub ECX,8
 mov dword ptr [EBP+16],ECX
 sub ESP,ECX
 mov EDI,ESP
 rep movsb
 mov ECX,EAX
 call dword ptr [EBP+8]
 pop EDI
 pop ESI
 mov ESP,EBP
 pop EBP
 ret
_dcCall_x86_win32_fast ENDP
_dcCall_x86_sys_int80h_linux PROC
OPTION PROLOGUE:NONE, EPILOGUE:NONE
 push EBP
  mov EBP,ESP
 push EBX
 push ESI
 push EDI
 mov EAX,dword ptr [EBP+12]
 mov EBX,dword ptr [EAX+0]
 mov ECX,dword ptr [EAX+4]
 mov EDX,dword ptr [EAX+8]
 mov ESI,dword ptr [EAX+12]
 mov EDI,dword ptr [EAX+16]
 mov EAX,dword ptr [EBP+8]
 int 80h
 pop EDI
 pop ESI
 pop EBX
 mov ESP,EBP
 pop EBP
 ret
_dcCall_x86_sys_int80h_linux ENDP
_dcCall_x86_sys_int80h_bsd PROC
OPTION PROLOGUE:NONE, EPILOGUE:NONE
 push EBP
  mov EBP,ESP
 push ESI
 push EDI
 mov ESI,dword ptr [EBP+12]
 mov ECX,dword ptr [EBP+16]
 sub ESP,ECX
 mov EDI,ESP
 rep movsb
 mov EAX,dword ptr [EBP+8]
 call _do_int
 pop EDI
 pop ESI
 mov ESP,EBP
 pop EBP
 ret
_do_int:
 int 80h
 ret
_dcCall_x86_sys_int80h_bsd ENDP
END
