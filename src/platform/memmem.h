#if defined _WIN32
#include "../3rdparty/freebsd/memmem.c"
#else
/* On systems that use Glibc, you must defined _GNU_SOURCE before including string.h
 * to get access to memmem. On BSD and MacOS this is not needed, though if they
 * happen to be using glibc instead of libc, it shouldn't hurt have defined _GNU_SOURCE */
#define _GNU_SOURCE
#endif
#include <string.h>

void * MVM_memmem(const void *haystack, size_t haystacklen, const void *needle, size_t needlelen) {
    return memmem(haystack, haystacklen, needle, needlelen);
}
