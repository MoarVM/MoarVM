/* MinGW x64 BUGFIX: Pass a NULL argument instead of the frame pointer to the
 * setjmp implementation to make the JIT not choke on exceptions on MinGW.
 * cygx++ for fix */
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
