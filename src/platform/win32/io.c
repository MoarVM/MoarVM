#include <moarvm.h>
#include <platform/io.h>

/* undocumented, so check if these really hold */
#if SEEK_SET != FILE_BEGIN   || \
    SEEK_CUR != FILE_CURRENT || \
    SEEK_END != FILE_END
#error "Standard and WinAPI seek modes not compatible"
#endif

static wchar_t * UTF8ToUnicode(const char *str)
{
     const int         len = MultiByteToWideChar(CP_UTF8, 0, str, -1, NULL, 0);
     wchar_t * const result = (wchar_t *)malloc(len * sizeof(wchar_t));

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

    li.QuadPart = offset;
    li.LowPart = SetFilePointer(hf, li.LowPart, &li.HighPart, origin);

    if (li.LowPart == INVALID_SET_FILE_POINTER) {
        errno = ESPIPE;
        return -1;
    }

    return li.QuadPart;
}

/* XXX: this is not used in MVM_file_delete unless uv_fs_unlink doesn't work well enough.
 * i.e: uv_fs_unlink can't delete Read Only files. */
MVMint64 MVM_platform_unlink(const char *pathname) {
    /* Must using UTF8ToUnicode for supporting CJK Windows file name. */
    wchar_t *wpathname = UTF8ToUnicode(pathname);
    int str_len = wcslen(wpathname);
    DWORD attrs;

    if (str_len > MAX_PATH) {
        wchar_t  abs_wpathname[4096]; /* 4096 should be enough for absolute path */
        wchar_t *lpp_part;

        /* You cannot use the "\\?\" prefix with a relative path,
         * relative paths are always limited to a total of MAX_PATH characters.
         * see http://msdn.microsoft.com/en-us/library/windows/desktop/aa365247%28v=vs.85%29.aspx */
        if (!GetFullPathNameW(wpathname, 4096, abs_wpathname, &lpp_part)) {
            errno = ENOENT;
            return -1;
        }

        free(wpathname);

        str_len  = wcslen(abs_wpathname);
        wpathname = (wchar_t *)malloc((str_len + 4) * sizeof(wchar_t));
        wcscpy(wpathname, L"\\\\?\\");
        wcscat(wpathname, abs_wpathname);
    }

    attrs = GetFileAttributesW(wpathname);

    if (attrs == INVALID_FILE_ATTRIBUTES) {
        errno = ENOENT;
        return -1;
    }
    else if (attrs & FILE_ATTRIBUTE_READONLY) {
        (void)SetFileAttributesW(wpathname, attrs & ~FILE_ATTRIBUTE_READONLY);
    }

    if (DeleteFileW(wpathname) == 0) {
        DWORD LastError = GetLastError();

        if (LastError == ERROR_FILE_NOT_FOUND) {
            errno = ENOENT;
        }

        else if (LastError == ERROR_ACCESS_DENIED) {
            errno = EACCES;
        }

        return -1;
    }

    return 0;
}
