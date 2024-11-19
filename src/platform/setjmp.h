/* In later Windows versions the longjmp implementation tries to perform stack
 * unwinding (for the purposes of cleanup). This is problematic for MoarVM
 * because our JIT produces frames that this stack unwinder doesn't understand.
 * Luckily the longjmp function can be instructed to not do the stack unwinding
 * by setting a respective field in the jmpbuf structure. The setjmp function
 * does so when it's *second* argument (the frame pointer) is NULL. Microsoft
 * however only provides a function signature with a single argument.
 * MinGW didn't care and added a signature with two args in their headers. This
 * is what we use on MinGW. (cygx++ came up with that fix)
 * On MSVC it's not possible to provide a two arg signature ourselves, because
 * the function is marked as purely intrinsic and the compiler won't let you
 * take the address of such a function. So we instead modify the resulting
 * jmpbuf and set that flag ourself. Microsofts `setjmp.h` header file has
 * proven helpful in disecting the otherwise opaque jmp_buf struct.
 * All of the above (both, the two arg setjmp we use on MinGW and modifying
 * the jmpbuf we do on MSVC) is not part of any public Windows API and could in
 * principle break any time. Given that
 * - Microsoft has a long history of being very careful to keep back-compat and
 * - these hacks are in use by MinGW and by extension some high profile
 *   projects (e.g. QEMU) and
 * - MoarVM explodes pretty violently when this code breaks
 * I believe it's fine to just use these hacks and enjoy our lives.
 * See https://blog.lazym.io/2020/09/21/Unicorn-Devblog-setjmp-longjmp-on-Windows/
 * for some deeper exploration of this issue.
 */
#ifdef __MINGW32__
#  ifndef USE_NO_MINGW_SETJMP_TWO_ARGS
#    ifndef _INC_SETJMPEX
#      ifdef _WIN64
#        define MVM_setjmp(BUF) _setjmp((BUF), NULL)
#      else
#        define MVM_setjmp(BUF) _setjmp3((BUF), NULL)
#      endif
#    else
#      define MVM_setjmp(BUF) _setjmpex((BUF), NULL)
#      define MVM_setjmpex(BUF) _setjmpex((BUF), NULL)
#    endif
#  endif
#else
#  ifdef _MSC_VER
#    if defined _M_X64
#      define MVM_setjmp(BUF) do { setjmp((BUF)); tc->interp_jump[0].Part[0] = 0; } while (0)
#    elif defined _M_ARM
#      define MVM_setjmp(BUF) do { setjmp((BUF)); tc->interp_jump[0] = 0; } while (0)
#    elif defined _M_ARM64
#      define MVM_setjmp(BUF) do { setjmp((BUF)); tc->interp_jump[0] = 0; } while (0)
#    endif
#  else
#    define MVM_setjmp setjmp
#  endif
#endif
