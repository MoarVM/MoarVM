/* On Linux we use glibc's memmem which uses the Knuth-Morris-Pratt algorithm.
 * We use FreeBSD's libc memmem on Windows and MacOS, which uses
 * Crochemore-Perrin two-way string matching.
 * Reasoning:
 * Windows, does not include any native memmem
 * MacOS has a memmem but is slower and originates from FreeBSD dated to 2005
 * Solaris doesn't seem to have memmem                                        */

#if defined(_WIN32) || defined(__APPLE__) || defined(__Darwin__) || defined(__sun)
#include "../3rdparty/freebsd/memmem.c"
#else
/* On systems that use glibc, you must define _GNU_SOURCE before including string.h
 * to get access to memmem. */
#define _GNU_SOURCE
#include <string.h>
#endif

void * MVM_memmem(const void *haystack, size_t haystacklen, const void *needle, size_t needlelen) {
    return memmem(haystack, haystacklen, needle, needlelen);
}

/* Extended info:
 * In glibc, the Knuth-Morris-Pratt algorithm was added as of git tag glibc-2.8-44-g0caca71ac9 */
