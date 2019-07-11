MVMint16 MVM_nativecall_get_calling_convention(MVMThreadContext *tc, MVMString *name);
#define MVM_nativecall_load_lib(path)       dlLoadLibrary(path)
#define MVM_nativecall_free_lib(lib)        dlFreeLibrary(lib)
#define MVM_nativecall_find_sym(lib, name)  dlFindSymbol(lib, name)

#ifdef MVM_WCHAR_UNSIGNED
#  if MVM_WCHAR_SIZE == 1
#    define MVM_WCHAR_DC_SIG_CHAR              'C'
#    define MVM_WCHAR_DC_TYPE                  DCuchar
#    define MVM_WCHAR_DC_ARG                   dcArgChar
#    define MVM_WCHAR_DC_CALL(vm, entry_point) (MVMwchar)dcCallChar((vm), (entry_point))
#    define MVM_WCHAR_DCB_ARG(cb_args)         dcbArgUChar((cb_args))
#  elif MVM_WCHAR_SIZE == 2
#    define MVM_WCHAR_DC_SIG_CHAR              'S'
#    define MVM_WCHAR_DC_TYPE                  DCushort
#    define MVM_WCHAR_DC_ARG                   dcArgShort
#    define MVM_WCHAR_DC_CALL(vm, entry_point) (MVMwchar)dcCallShort((vm), (entry_point))
#    define MVM_WCHAR_DCB_ARG(cb_args)         dcbArgUShort((cb_args))
#  elif MVM_WCHAR_SIZE == 4
#    define MVM_WCHAR_DC_SIG_CHAR              'I'
#    define MVM_WCHAR_DC_TYPE                  DCuint
#    define MVM_WCHAR_DC_ARG                   dcArgInt
#    define MVM_WCHAR_DC_CALL(vm, entry_point) (MVMwchar)dcCallInt((vm), (entry_point))
#    define MVM_WCHAR_DCB_ARG(cb_args)         dcbArgUInt((cb_args))
#  elif MVM_WCHAR_SIZE == 8
#    define MVM_WCHAR_DC_SIG_CHAR              'L'
#    define MVM_WCHAR_DC_TYPE                  DCulonglong
#    define MVM_WCHAR_DC_ARG                   dcArgLongLong
#    define MVM_WCHAR_DC_CALL(vm, entry_point) (MVMwchar)dcCallLongLong((vm), (entry_point))
#    define MVM_WCHAR_DCB_ARG(cb_args)         dcbArgULongLong((cb_args))
#  else
#    error "Unsupported wchar_t size"
#  endif
#else
#  if MVM_WCHAR_SIZE == 1
#    define MVM_WCHAR_DC_SIG_CHAR              'c'
#    define MVM_WCHAR_DC_TYPE                  DCchar
#    define MVM_WCHAR_DC_ARG                   dcArgChar
#    define MVM_WCHAR_DC_CALL(vm, entry_point) (MVMwchar)dcCallChar((vm), (entry_point))
#    define MVM_WCHAR_DCB_ARG(cb_args)         dcbArgChar((cb_args))
#  elif MVM_WCHAR_SIZE == 2
#    define MVM_WCHAR_DC_SIG_CHAR              's'
#    define MVM_WCHAR_DC_TYPE                  DCshort
#    define MVM_WCHAR_DC_ARG                   dcArgShort
#    define MVM_WCHAR_DC_CALL(vm, entry_point) (MVMwchar)dcCallShort((vm), (entry_point))
#    define MVM_WCHAR_DCB_ARG(cb_args)         dcbArgShort((cb_args))
#  elif MVM_WCHAR_SIZE == 4
#    define MVM_WCHAR_DC_SIG_CHAR              'i'
#    define MVM_WCHAR_DC_TYPE                  DCint
#    define MVM_WCHAR_DC_ARG                   dcArgInt
#    define MVM_WCHAR_DC_CALL(vm, entry_point) (MVMwchar)dcCallInt((vm), (entry_point))
#    define MVM_WCHAR_DCB_ARG(cb_args)         dcbArgInt((cb_args))
#  elif MVM_WCHAR_SIZE == 8
#    define MVM_WCHAR_DC_SIG_CHAR              'l'
#    define MVM_WCHAR_DC_TYPE                  DClonglong
#    define MVM_WCHAR_DC_ARG                   dcArgLongLong
#    define MVM_WCHAR_DC_CALL(vm, entry_point) (MVMwchar)dcCallLongLong((vm), (entry_point))
#    define MVM_WCHAR_DCB_ARG(cb_args)         dcbArgLongLong((cb_args))
#  else
#    error "Unsupported wchar_t size"
#  endif
#endif

#ifdef MVM_WINT_UNSIGNED
#  if MVM_WINT_SIZE == 2
#    define MVM_WINT_DC_SIG_CHAR              'S'
#    define MVM_WINT_DC_TYPE                  DCushort
#    define MVM_WINT_DC_ARG                   dcArgShort
#    define MVM_WINT_DC_CALL(vm, entry_point) (MVMwint)dcCallShort((vm), (entry_point))
#    define MVM_WINT_DCB_ARG(cb_args)         dcbArgUShort((cb_args))
#  elif MVM_WINT_SIZE == 4
#    define MVM_WINT_DC_SIG_CHAR              'I'
#    define MVM_WINT_DC_TYPE                  DCuint
#    define MVM_WINT_DC_ARG                   dcArgInt
#    define MVM_WINT_DC_CALL(vm, entry_point) (MVMwint)dcCallInt((vm), (entry_point))
#    define MVM_WINT_DCB_ARG(cb_args)         dcbArgUInt((cb_args))
#  elif MVM_WINT_SIZE == 8
#    define MVM_WINT_DC_SIG_CHAR              'L'
#    define MVM_WINT_DC_TYPE                  DCulonglong
#    define MVM_WINT_DC_ARG                   dcArgLongLong
#    define MVM_WINT_DC_CALL(vm, entry_point) (MVMwint)dcCallLongLong((vm), (entry_point))
#    define MVM_WINT_DCB_ARG(cb_args)         dcbArgULongLong((cb_args))
#  else
#    error "Unsupported wint_t size"
#  endif
#else
#  if MVM_WINT_SIZE == 2
#    define MVM_WINT_DC_SIG_CHAR              's'
#    define MVM_WINT_DC_TYPE                  DCshort
#    define MVM_WINT_DC_ARG                   dcArgShort
#    define MVM_WINT_DC_CALL(vm, entry_point) (MVMwint)dcCallShort((vm), (entry_point))
#    define MVM_WINT_DCB_ARG(cb_args)         dcbArgShort((cb_args))
#  elif MVM_WINT_SIZE == 4
#    define MVM_WINT_DC_SIG_CHAR              'i'
#    define MVM_WINT_DC_TYPE                  DCint
#    define MVM_WINT_DC_ARG                   dcArgInt
#    define MVM_WINT_DC_CALL(vm, entry_point) (MVMwint)dcCallInt((vm), (entry_point))
#    define MVM_WINT_DCB_ARG(cb_args)         dcbArgInt((cb_args))
#  elif MVM_WINT_SIZE == 8
#    define MVM_WINT_DC_SIG_CHAR              'l'
#    define MVM_WINT_DC_TYPE                  DClonglong
#    define MVM_WINT_DC_ARG                   dcArgLongLong
#    define MVM_WINT_DC_CALL(vm, entry_point) (MVMwint)dcCallLongLong((vm), (entry_point))
#    define MVM_WINT_DCB_ARG(cb_args)         dcbArgLongLong((cb_args))
#  else
#    error "Unsupported wint_t size"
#  endif
#endif
