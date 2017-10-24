#if defined(__APPLE__) || defined(__Darwin__)
#include <moar.h>
#include <platform/io.h>
#include <sys/types.h>
#include <unistd.h>

short MVM_platform_is_fd_seekable(int fd) {
    off_t can_seek = MVM_platform_lseek(fd, 0, SEEK_CUR);
    if (can_seek != -1) {
        /*
            On MacOS, lseek of TTYs still returns some seek position,
            which makes us think they're seekable handles and messes up
            our EOF detection. So if lseek tells us it's a seekable
            handle, we do an extra check for whether it's a TTY
            and claim non-seekability if it is one.
        */
        return isatty(fd) ? 0 : 1;
    }
    else
        return 0;
}
#endif
