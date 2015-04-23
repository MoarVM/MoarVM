#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "moar.h"
#include "platform/sys.h"

#if __GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 6)

#include <sched.h>
#include <pthread.h>

MVMuint32 MVM_platform_cpu_count(void) {
    cpu_set_t set;

    if (pthread_getaffinity_np(pthread_self(), sizeof set, &set) != 0)
        return 0;

    return CPU_COUNT(&set);
}

#else

#include <unistd.h>

#if defined _SC_NPROCESSORS_ONLN
#  define SYSCONF_ARG _SC_NPROCESSORS_ONLN
#elif defined _SC_NPROC_ONLN
#  define SYSCONF_ARG _SC_NPROC_ONLN
#else
#  include <sys/sysctl.h>
#  if defined HW_AVAILCPU
#    define SYSCTL_ARG HW_AVAILCPU
#  elif defined HW_NCPU
#    define SYSCTL_ARG HW_NCPU
#  endif
#endif

#if defined SYSCONF_ARG

MVMuint32 MVM_platform_cpu_count(void) {
    long count = sysconf(SYSCONF_ARG);
    if (count < 0)
        return 0;

    return (MVMuint32)count;
}

#elif defined SYSCTL_ARG

MVMuint32 MVM_platform_cpu_count(void) {
    int mib[2] = { CTL_HW, SYSCTL_ARG };
    int count;
    int size = sizeof count;

    if (sysctl(mib, 2, &count, &size, NULL, 0) != 0)
        return 0;

    return (MVMuint32)count;
}

#else

#error "Unsupported platform"

#endif

#endif
