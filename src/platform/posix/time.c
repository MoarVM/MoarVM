#include "moarvm.h"
#include "platform/time.h"

#include <time.h>
#include <sys/time.h>

MVMint64 MVM_platform_now(void)
{
#ifdef CLOCK_REALTIME
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0)
        return 0;

    return (MVMint64)ts.tv_sec * 1000000000 + ts.tv_nsec;
#else
    struct timeval tv;
    if (gettimeofday(&tv, NULL) != 0)
        return 0;

    return (MVMint64)tv.tv_sec * 1000000000 + tv.tv_usec * 1000;
#endif
}
