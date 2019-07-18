#include <wchar.h>

#if defined(__WCHAR_TYPE__)
typedef __WCHAR_TYPE__ MVMwchar;
#elif defined(WCHAR_TYPE)
typedef WCHAR_TYPE     MVMwchar;
#else
typedef wchar_t        MVMwchar;
#endif

#if defined(__WINT_TYPE__)
typedef __WINT_TYPE__ MVMwint;
#elif defined(WINT_TYPE)
typedef WINT_TYPE     MVMwint;
#else
typedef wint_t        MVMwint;
#endif

MVMint64 MVM_platform_is_wchar_unsigned(void);
MVMint64 MVM_platform_is_wint_unsigned(void);
