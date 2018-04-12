/* Get random numbers from OS. Returns 1 if it succeeded and otherwise 0
 * Does not block. Designed for getting small amounts of random data at a time */
#include <stddef.h>
/* Solaris has both getrandom and getentropy. We use getrandom since getentropy
 * can block. Solaris has had getrandom() and getentropy() since 11.3 */
#if defined(__sun)
    #include <sys/random.h>
    /* On solaris, _GRND_ENTROPY is defined if getentropy/getrandom are available */
    #if defined(_GRND_ENTROPY)
        #define MVM_random_use_getrandom 1
    #endif
#endif
/* Linux added getrandom to kernel in 3.17 */
#if defined(__linux__)
    #include <sys/syscall.h>
    #if defined(SYS_getrandom)
    /* With glibc you are supposed to declare _GNU_SOURCE to use the
     * syscall function */
        #define _GNU_SOURCE
        #define GRND_NONBLOCK 0x01
        #include <unistd.h>
        #define MVM_random_use_getrandom_syscall 1
    #else
        #define MVM_random_use_urandom 1
    #endif
#endif
/* FreeBSD added it with SVN revision 331279 Wed Mar 21, 2018
 * This coorasponds to __FreeBSD_version version identifier: 1200061.
 * https://svnweb.freebsd.org/base?view=revision&revision=r331279 */
#if defined(__FreeBSD__)
    #include <osreldate.h>
    #if __FreeBSD_version >= 1200061
        #include <sys/random.h>
        #define MVM_random_use_getrandom
    #endif
#endif
/* OpenBSD's getentropy never blocks and always succeeds. OpenBSD has had
 * getentropy() since 5.6 */
#if defined(__OpenBSD__)
    #include <sys/param.h>
    #if OpenBSD >= 201301
        #define MVM_random_use_getentropy
    #endif
#endif
/* MacOS has had getentropy() since 10.12 */
#if defined(__APPLE__)
    #include <AvailabilityMacros.h>
    #include <Availability.h>
    #if !defined(MAC_OS_X_VERSION_10_12)
        #define MAC_OS_X_VERSION_10_12 101200
    #endif
    //#include <AvailabilityMacros.h>
    #if __MAC_OS_X_VERSION_MAX_ALLOWED >= MAC_OS_X_VERSION_10_12
        #include <sys/random.h>
        #define MVM_random_use_getentropy 1
    #endif
#endif
/* Other info:
 * NetBSD: I have not found evidence it has getentropy() or getrandom()
 *   Note: Uses __NetBSD_Version__ included from file <sys/param.h>.
 * All BSD's should support arc4random
 * AIX is a unix but has no arc4random, does have /dev/urandom */
#include "moar.h"

#if defined(MVM_random_use_getrandom_syscall)
/* getrandom() was added to glibc much later than it was added to the kernel. Since
 * we detect the presence of the system call to decide whether to use this,
 * just use the syscall instead since the wrapper is not guaranteed to exist.*/
    MVMint32 MVM_getrandom (MVMThreadContext *tc, void *out, size_t size) {
        return syscall(SYS_getrandom, out, size, GRND_NONBLOCK) <= 0 ? 0 : 1;
    }
#elif defined(MVM_random_use_getrandom)
    /* Call the getrandom() wrapper in Solaris and FreeBSD since they were
     * added at the same time as getentropy() and this allows us to avoid blocking. */
    MVMint32 MVM_getrandom (MVMThreadContext *tc, void *out, size_t size) {
        return getrandom(out, size, GRND_NONBLOCK) <= 0 ? 0 : 1;
    }

#elif defined(MVM_random_use_getentropy)
    MVMint32 MVM_getrandom (MVMThreadContext *tc, void *out, size_t size) {
        return getentropy(out, size) < 0 ? 0 : 1;
    }

#elif defined(_WIN32)
    #include <windows.h>
    #include <wincrypt.h>
    typedef BOOL (WINAPI *CRYPTACQUIRECONTEXTA)(HCRYPTPROV *phProv,\
                  LPCSTR pszContainer, LPCSTR pszProvider, DWORD dwProvType,\
                  DWORD dwFlags );
    typedef BOOL (WINAPI *CRYPTGENRANDOM)(HCRYPTPROV hProv, DWORD dwLen,\
                  BYTE *pbBuffer );
    /* This is needed to so pCryptGenRandom() can be called. */
    static CRYPTGENRANDOM pCryptGenRandom = NULL;
    static HCRYPTPROV       hCryptContext = 0;
    static int win32_urandom_init(void) {
        HINSTANCE hAdvAPI32 = NULL;
        /* This is needed to so pCryptAcquireContext() can be called. */
        CRYPTACQUIRECONTEXTA pCryptAcquireContext = NULL;
        /* Get Module Handle to CryptoAPI */
        hAdvAPI32 = GetModuleHandle("advapi32.dll");
        if (hAdvAPI32 == NULL) return 0;
        /* Check the pointers to the CryptoAPI functions. These shouldn't fail
         * but makes sure we won't have problems getting the context or getting
         * random. */
        if (!GetProcAddress(hAdvAPI32, "CryptAcquireContextA")
        ||  !GetProcAddress(hAdvAPI32, "CryptGenRandom")) {
            return 0;
        }
        /* Get the pCrypt Context */
        if (!pCryptAcquireContext(&hCryptContext, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT))
            return 0;

        return 1;
    }
    MVMint32 MVM_getrandom (MVMThreadContext *tc, void *out, size_t size) {
        if (!hCryptContext) {
            int rtrn = win32_urandom_init();
            if (!rtrn) return 0;
        }
        if (!pCryptGenRandom(hCryptContext, (DWORD)size, (BYTE*)out)) {
            return 0;
        }
        return 1;
    }
#else
    #include <unistd.h>
    MVMint32 MVM_getrandom (MVMThreadContext *tc, void *out, size_t size) {
        int fd = open("/dev/urandom", O_RDONLY);
        ssize_t num_read = 0;
        if (fd < 0 || (num_read = read(fd, out, size) <= 0)) {
            if (fd) close(fd);
            #if defined(BSD)
                #include <stdlib.h>
                arc4random_buf(out, size);
                return 1;
            #else
                return 0;
            #endif
        }
        return 1;
    }
#endif
