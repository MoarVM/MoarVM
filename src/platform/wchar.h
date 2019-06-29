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
