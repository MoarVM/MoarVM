gcc -E -P -DGEN_MASM dyncall_call_x86.S | unix2dos >dyncall_call_x86_generic_masm.asm
gcc -E -P -DGEN_MASM dyncall_call_x64-att.S | unix2dos >dyncall_call_x64_generic_masm.asm

