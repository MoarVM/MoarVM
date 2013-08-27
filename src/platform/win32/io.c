#include <moarvm.h>
#include <platform/io.h>

/* undocumented, so check if these really hold */
#if SEEK_SET != FILE_BEGIN   || \
    SEEK_CUR != FILE_CURRENT || \
    SEEK_END != FILE_END
#error "Standard and WinAPI seek modes not compatible"
#endif

MVMint64 MVM_platform_lseek(int fd, MVMint64 offset, int origin)
{
    HANDLE hf;
    LARGE_INTEGER li;

    hf = (HANDLE)_get_osfhandle(fd);
    if (hf == INVALID_HANDLE_VALUE) {
        errno = EBADF;
        return -1;
    }

    li.QuadPart = 0;
    li.LowPart = SetFilePointer(hf, li.LowPart, &li.HighPart, origin);

    if (li.LowPart == INVALID_SET_FILE_POINTER) {
        errno = ESPIPE;
        return -1;
    }

    return li.QuadPart;
}
