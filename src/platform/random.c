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
/* Linux added getrandom to the kernel in 3.17 */
#if defined(__linux__) || defined(__GNU__)
    #include <sys/syscall.h>
    #if defined(SYS_getrandom)
    /* With glibc you are supposed to declare _GNU_SOURCE to use the
     * syscall function */
        #ifndef _GNU_SOURCE
            #define _GNU_SOURCE
        #endif
        #define GRND_NONBLOCK 0x01
        #include <unistd.h>
        #define MVM_random_use_getrandom_syscall 1
    #else
        #define MVM_random_use_urandom 1
    #endif
#endif
/* FreeBSD added it with SVN revision 331279 Wed Mar 21, 2018
 * This corresponds to __FreeBSD_version version identifier: 1200061.
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
    #if __MAC_OS_X_VERSION_MAX_ALLOWED >= MAC_OS_X_VERSION_10_12
        #include <sys/random.h>
        #define MVM_random_use_getentropy 1
    #endif
#endif
/* Other info:
 * - All BSD's should support arc4random
 * - AIX is a Unix but has no arc4random, does have /dev/urandom.
 * - NetBSD: I have not found evidence it has getentropy() or getrandom()
 *     Note: Uses __NetBSD_Version__ included from file <sys/param.h>. */
#include "moar.h"
#include "platform/io.h"
/* On Unix like platforms that don't support getrandom() or getentropy()
 * we defualt to /dev/urandom. On platforms that do support these calls, we
 * only use /dev/urandom if those calls fail. This is also important on Linux,
 * since if MoarVM was compiled on a kernel >= 3.17 it will be set to use the
 * syscall. If the syscall doesn't exist, the syscall wrapper will gracefully
 * return a false return value and we will fallback to /dev/urandom */
#if !defined(_WIN32)
    #include <unistd.h>
    MVMint32 MVM_getrandom_urandom (MVMThreadContext *tc, void *out, size_t size) {
        int fd = MVM_platform_open("/dev/urandom", O_RDONLY);
        if (fd < 0 || read(fd, out, size) <= 0) {
            if (fd) close(fd);
            /* If using /dev/urandom fails (maybe we're in a chroot), on BSD's
             * use arc4random, which is likely seeded from the system's random
             * number generator */
            #if defined(BSD) && !defined(__GNU__)
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

#if defined(MVM_random_use_getrandom_syscall)
/* getrandom() was added to glibc much later than it was added to the kernel. Since
 * we detect the presence of the system call to decide whether to use this,
 * just use the syscall instead since the wrapper is not guaranteed to exist.*/
    MVMint32 MVM_getrandom (MVMThreadContext *tc, void *out, size_t size) {
        long rtrn = syscall(SYS_getrandom, out, size, GRND_NONBLOCK);
        return rtrn <= 0 ? MVM_getrandom_urandom(tc, out, size) : 1;
    }
#elif defined(MVM_random_use_getrandom)
    /* Call the getrandom() wrapper in Solaris and FreeBSD since they were
     * added at the same time as getentropy() and this allows us to avoid blocking. */
    MVMint32 MVM_getrandom (MVMThreadContext *tc, void *out, size_t size) {
        ssize_t rtrn = getrandom(out, size, GRND_NONBLOCK);
        return rtrn <= 0 ? MVM_getrandom_urandom(tc, out, size) : 1;
    }

#elif defined(MVM_random_use_getentropy)
    MVMint32 MVM_getrandom (MVMThreadContext *tc, void *out, size_t size) {
        int rtrn = getentropy(out, size);
        return rtrn <= 0 ? MVM_getrandom_urandom(tc, out, size) : 1;
    }

#elif defined(_WIN32)
    #include <windows.h>
    #include <wincrypt.h>
    /* Signatures for pCryptAcquireContext() and pCryptGenRandom() */
    typedef BOOL (WINAPI *CRYPTACQUIRECONTEXTA)(HCRYPTPROV *phProv,\
                  LPCSTR pszContainer, LPCSTR pszProvider, DWORD dwProvType,\
                  DWORD dwFlags );
    typedef BOOL (WINAPI *CRYPTGENRANDOM)(HCRYPTPROV hProv, DWORD dwLen,\
                  BYTE *pbBuffer );
    /* The functions themselves */
    static CRYPTGENRANDOM pCryptGenRandom = NULL;
    static HCRYPTPROV       hCryptContext = 0;
    static int win32_urandom_init(void) {
        /* Get Module Handle to CryptoAPI */
        HINSTANCE hAdvAPI32 = GetModuleHandle("advapi32.dll");
        if (hAdvAPI32) {
            CRYPTACQUIRECONTEXTA pCryptAcquireContext =
                              GetProcAddress(hAdvAPI32, "CryptAcquireContextA");
            pCryptGenRandom = GetProcAddress(hAdvAPI32, "CryptGenRandom");
            /* Check the pointers to the CryptoAPI functions. These shouldn't fail
             * but makes sure we won't have problems getting the context or getting
             * random. If those aren't NULL then get the pCrypt context */
            return pCryptAcquireContext && pCryptGenRandom &&
                pCryptAcquireContext(&hCryptContext, NULL, NULL, PROV_RSA_FULL,
                CRYPT_VERIFYCONTEXT) ? 1 : 0;
        }
        return 0;
    }
    MVMint32 MVM_getrandom (MVMThreadContext *tc, void *out, size_t size) {
        /* Return 0 if the context doesn't exist and we are unable to create it */
        if (!hCryptContext && !win32_urandom_init())
            return 0;
        return pCryptGenRandom(hCryptContext, (DWORD)size, (BYTE*)out) ? 1 : 0;
    }
#else
    MVMint32 MVM_getrandom (MVMThreadContext *tc, void *out, size_t size) {
        return MVM_getrandom_urandom(tc, out, size);
    }
#endif
