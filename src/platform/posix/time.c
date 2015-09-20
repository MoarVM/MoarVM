#include "moar.h"
#include "platform/time.h"

#include <errno.h>
#include <sys/time.h>

#define E9 1000000000
#define E9F 1000000000.0f

MVMuint64 MVM_platform_now(void)
{
#ifdef CLOCK_REALTIME
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0)
        return 0;

    return (MVMuint64)ts.tv_sec * E9 + ts.tv_nsec;
#else
    struct timeval tv;
    if (gettimeofday(&tv, NULL) != 0)
        return 0;

    return (MVMuint64)tv.tv_sec * E9 + tv.tv_usec * 1000;
#endif
}

void MVM_platform_sleep(MVMnum64 second)
{
    struct timespec timeout;
    timeout.tv_sec = (time_t)second;
    timeout.tv_nsec = (long)((second - timeout.tv_sec) * E9F);
    while (nanosleep(&timeout, &timeout) && errno == EINTR);
}

void MVM_platform_nanosleep(MVMuint64 nanos)
{
    struct timespec timeout;
    timeout.tv_sec = nanos / E9;
    timeout.tv_nsec = nanos % E9;
    while (nanosleep(&timeout, &timeout) && errno == EINTR);
}
