#include "moar.h"
#include "platform/time.h"

#include <errno.h>
#include <sys/time.h>

#ifdef __MACH__
#include <mach/clock.h>
#include <mach/mach.h>
#endif

#define E9 1000000000
#define E9F 1000000000.0f

MVMuint64 MVM_platform_now(void)
{
#ifdef __MACH__ // OS X does not have clock_gettime, use clock_get_time
    clock_serv_t    cclock;
    mach_timespec_t ts;
    host_get_clock_service(mach_host_self(), CALENDAR_CLOCK, &cclock);
    clock_get_time(cclock, &ts);
    mach_port_deallocate(mach_task_self(), cclock);
    return (MVMuint64)ts.tv_sec * E9 + ts.tv_nsec;
#else
#  ifdef CLOCK_REALTIME
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0)
        return 0;

    return (MVMuint64)ts.tv_sec * E9 + ts.tv_nsec;
#  else
    struct timeval tv;
    if (gettimeofday(&tv, NULL) != 0)
        return 0;

    return (MVMuint64)tv.tv_sec * E9 + tv.tv_usec * 1000;
#  endif
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

void MVM_platform_decodelocaltime(MVMint64 time, MVMint64 decoded[]) {
    const time_t t = (time_t)time;
    struct tm tm;
    localtime_r(&t, &tm);

    decoded[0] = tm.tm_sec;
    decoded[1] = tm.tm_min;
    decoded[2] = tm.tm_hour;
    decoded[3] = tm.tm_mday;
    decoded[4] = tm.tm_mon + 1;
    decoded[5] = tm.tm_year + 1900;
    decoded[6] = tm.tm_wday;
    decoded[7] = tm.tm_yday;
    decoded[8] = tm.tm_isdst;
}
