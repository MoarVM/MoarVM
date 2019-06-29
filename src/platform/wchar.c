#include "moar.h"

MVMint64 MVM_platform_is_wchar_unsigned(void) {
#ifdef MVM_WCHAR_UNSIGNED
    return 1;
#else
    return 0;
#endif
}

MVMint64 MVM_platform_is_wint_unsigned(void) {
#ifdef MVM_WINT_UNSIGNED
    return 1;
#else
    return 0;
#endif
}
