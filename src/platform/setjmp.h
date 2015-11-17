/* MinGW x64 BUGFIX: Pass a NULL argument instead of the frame pointer to the
 * setjmp implementation to make the JIT not choke on exceptions on MinGW. 
 * cygx++ for fix */
#ifdef __MINGW32__
#  ifndef USE_NO_MINGW_SETJMP_TWO_ARGS
#    undef setjmp
#    ifndef _INC_SETJMPEX
#      ifdef _WIN64
#        define setjmp(BUF) _setjmp((BUF), NULL)
#      else
#        define setjmp(BUF) _setjmp3((BUF), NULL)
#      endif
#    else
#      define setjmp(BUF) _setjmpex((BUF), NULL)
#      define setjmpex(BUF) _setjmpex((BUF), NULL)
#    endif
#  endif
#endif
