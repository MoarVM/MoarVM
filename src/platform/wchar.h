#include <wchar.h>

#if defined(__WCHAR_TYPE__)
#  define MVMwchar __WCHAR_TYPE__
#elif defined(WCHAR_TYPE)
#  define MVMwchar WCHAR_TYPE
#else
#  define MVMwchar wchar_t
#endif

#if defined(__WINT_TYPE__)
#  define MVMwint __WINT_TYPE__
#elif defined(WINT_TYPE)
#  define MVMwint WINT_TYPE
#else
#  define MVMwint wint_t
#endif

#if defined(WCHAR_SIZE)
#  define MVM_WCHAR_SIZE WCHAR_SIZE
#elif defined(__WCHAR_SIZE__)
#  define MVM_WCHAR_SIZE __WCHAR_SIZE__
#elif defined(__SIZEOF_WCHAR_T__)
#  define MVM_WCHAR_SIZE __SIZEOF_WCHAR_T__
#elif defined(__WCHAR_WIDTH__)
#  define MVM_WCHAR_SIZE (__WCHAR_WIDTH__ / 8)
#elif defined(_WIN32)
#  ifdef _MSC_VER
#    define MVM_WCHAR_SIZE 2
#  else
#    define MVM_WCHAR_SIZE 4
#  endif
#elif defined(__APPLE__) && defined(__ICC)
#  if MVM_PTR_SIZE == 4
#    define MVM_WCHAR_SIZE 4
#  else
#    define MVM_WCHAR_SIZE 8
#  endif
#elif defined(__sun)
#  if MVM_PTR_SIZE == 4
#    define MVM_WCHAR_SIZE 4
#  else
#    define MVM_WCHAR_SIZE 8
#  endif
#elif defined(_AIX) && !defined(__GNUC__)
#  if MVM_PTR_SIZE == 4
#    define MVM_WCHAR_SIZE 2
#  else
#    define MVM_WCHAR_SIZE 4
#  endif
#else
#  define MVM_WCHAR_SIZE 4
#endif

#if defined(__SIZEOF_WINT_T__)
#  define MVM_WINT_SIZE __SIZEOF_WINT_T__
#elif defined(__WINT_WIDTH__)
#  define MVM_WINT_SIZE (__WINT_WIDTH__ / 8)
#elif defined(_WIN32) && defined(_MSC_VER)
#  define MVM_WINT_SIZE 2
#elif defined(__sun)
#  if MVM_PTR_SIZE == 4
#    define MVM_WINT_SIZE 4
#  else
#    define MVM_WINT_SIZE 8
#  endif
#else
#  define MVM_WINT_SIZE 4
#endif

#if defined(_WIN32) && defined(_MSC_VER)
#  define MVM_WCHAR_UNSIGNED
#elif defined(__ANDROID__)
#  define MVM_WCHAR_UNSIGNED
#  define MVM_WINT_UNSIGNED
#elif defined(__FreeBSD__) && (defined(__arm__) || defined(__thumb__) || defined(__aarch64__))
#  define MVM_WCHAR_UNSIGNED
#  define MVM_WINT_UNSIGNED
#elif defined(__ICC)
#  define MVM_WINT_UNSIGNED
#elif defined(_AIX) && !defined(__GNUC__)
#  define MVM_WCHAR_UNSIGNED
#else
/**
 * We have to assume GCC also set the type of wint_t if wchar_t's type was set,
 * since there's no macro for it. Thanks a lot, GNU! 🙄
 */
#  if defined(_GCC_WCHAR_T)
#    define MVM_WINT_UNSIGNED
#  elif defined(__WCHAR_UNSIGNED__)
#    define MVM_WCHAR_UNSIGNED
#  endif
#endif

/**
 *  Explicitly limit wchar_t and wint_t sizes to a set of sizes known to exist
 *  so wide string support doesn't end up breaking silently on more obscure
 *  platforms.
 */

#if MVM_WCHAR_SIZE != 1 && MVM_WCHAR_SIZE != 2 && MVM_WCHAR_SIZE != 4 && MVM_WCHAR_SIZE != 8
#error Unsupported wchar_t size.
#endif

#if MVM_WINT_SIZE != 2 && MVM_WINT_SIZE != 4 && MVM_WINT_SIZE != 8
#error Unsupported wint_t size.
#endif
