#include <dlfcn.h>

typedef void DLLib;

ffi_type * MVM_nativecall_get_ffi_type(MVMThreadContext *tc, MVMuint64 type_id);
ffi_abi MVM_nativecall_get_calling_convention(MVMThreadContext *tc, MVMString *name);
#define MVM_nativecall_load_lib(path)       dlopen(path, RTLD_NOW|RTLD_GLOBAL)
#define MVM_nativecall_free_lib(lib)        do { if(lib) dlclose(lib); } while (0)
#define MVM_nativecall_find_sym(lib, name)  dlsym(lib, name)

#ifdef MVM_WCHAR_UNSIGNED
#  define MVM_WCHAR_FFI_ARG ffi_arg
#  if MVM_WCHAR_SIZE == 1
#    define MVM_WCHAR_FFI_TYPE ffi_type_uchar
#  elif MVM_WCHAR_SIZE == 2
#    define MVM_WCHAR_FFI_TYPE ffi_type_ushort;
#  elif MVM_WCHAR_SIZE == 4
#    define MVM_WCHAR_FFI_TyPE ffi_type_uint
#  elif MVM_WCHAR_SIZE == 8
#    define MVM_WCHAR_FFI_TYPE ffi_type_uint64
#  endif
#else
#  define MVM_WCHAR_FFI_ARG ffi_sarg
#  if MVM_WCHAR_SIZE == 1
#    define MVM_WCHAR_FFI_TYPE ffi_type_schar
#  elif MVM_WCHAR_SIZE == 2
#    define MVM_WCHAR_FFI_TYPE ffi_type_sshort;
#  elif MVM_WCHAR_SIZE == 4
#    define MVM_WCHAR_FFI_TyPE ffi_type_sint
#  elif MVM_WCHAR_SIZE == 8
#    define MVM_WCHAR_FFI_TYPE ffi_type_sint64
#  endif
#endif

#ifdef MVM_WINT_UNSIGNED
#  define MVM_WINT_FFI_ARG ffi_arg
#  if MVM_WINT_SIZE == 2
#     define MVM_WINT_FFI_TYPE ffi_type_ushort
#  elif MVM_WINT_SIZE == 4
#     define MVM_WINT_FFI_TYPE ffi_type_uint
#  elif MVM_WINT_SIZE == 8
#     define MVM_WINT_FFI_TYPE ffi_type_uint64
#  endif
#else
#  define MVM_WINT_FFI_ARG ffi_sarg
#  if MVM_WINT_SIZE == 2
#     define MVM_WINT_FFI_TYPE ffi_type_sshort
#  elif MVM_WINT_SIZE == 4
#     define MVM_WINT_FFI_TYPE ffi_type_sint
#  elif MVM_WINT_SIZE == 8
#     define MVM_WINT_FFI_TYPE ffi_type_sint64
#  endif
#endif
