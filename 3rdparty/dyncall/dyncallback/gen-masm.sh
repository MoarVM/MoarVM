gcc -E -P -DGEN_MASM dyncall_callback_arch_x86.S | unix2dos >dyncall_callback_arch_x86_masm.asm
gcc -E -P -DGEN_MASM dyncall_callback_arch_x64.S | unix2dos >dyncall_callback_arch_x64_masm.asm

