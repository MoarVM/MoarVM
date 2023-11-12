#include <moar.h>
#include <platform/io.h>
#include <stdarg.h>

/* undocumented, so check if these really hold */
#if SEEK_SET != FILE_BEGIN   || \
    SEEK_CUR != FILE_CURRENT || \
    SEEK_END != FILE_END
#error "Standard and WinAPI seek modes not compatible"
#endif

static wchar_t * UTF8ToUnicode(const char *str)
{
     const int         len = MultiByteToWideChar(CP_UTF8, 0, str, -1, NULL, 0);
     wchar_t * const result = (wchar_t *)MVM_malloc(len * sizeof(wchar_t));

     MultiByteToWideChar(CP_UTF8, 0, str, -1, result, len);

     return result;
}

MVMint64 MVM_platform_lseek(int fd, MVMint64 offset, int origin)
{
    HANDLE hf;
    LARGE_INTEGER li;

    hf = (HANDLE)_get_osfhandle(fd);
    if (hf == INVALID_HANDLE_VALUE) {
        errno = EBADF;
        return -1;
    }
    if (GetFileType(hf) != 1) {
        errno = ESPIPE;
        return -1;
    }

    li.QuadPart = offset;
    li.LowPart = SetFilePointer(hf, li.LowPart, &li.HighPart, origin);

    if (li.LowPart == INVALID_SET_FILE_POINTER) {
        errno = ESPIPE;
        return -1;
    }

    return li.QuadPart;
}

int MVM_platform_fsync(int fd) {
    if (FlushFileBuffers((HANDLE)_get_osfhandle(fd)))
        return 0;
    errno = GetLastError();
    if (errno == ENXIO)
        return 0; /* Not something we can flush. */
    return -1;
}

int MVM_platform_open(const char *pathname, int flags, ...) {
    va_list args;
    wchar_t *wpathname = UTF8ToUnicode(pathname);
    int res;
    if (flags & _O_CREAT) {
        va_start(args, flags);
        res = _wopen(wpathname, flags, va_arg(args, int));
        va_end(args);
    }
    else {
        res = _wopen(wpathname, flags);
    }
    MVM_free(wpathname);
    return res;
}

FILE *MVM_platform_fopen(const char *pathname, const char *mode) {
    wchar_t *wpathname = UTF8ToUnicode(pathname);
    wchar_t *wmode     = UTF8ToUnicode(mode);
    FILE    *res       = _wfopen(wpathname, wmode);
    MVM_free(wpathname);
    MVM_free(wmode);
    return res;
}
