#include "moarvm.h"

static void verify_dirhandle_type(MVMThreadContext *tc, MVMObject *oshandle, MVMOSHandle **handle, const char *msg) {

    /* work on only MVMOSHandle of type MVM_OSHANDLE_DIR */
    if (REPR(oshandle)->ID != MVM_REPR_ID_MVMOSHandle) {
        MVM_exception_throw_adhoc(tc, "%s requires an object with REPR MVMOSHandle", msg);
    }
    *handle = (MVMOSHandle *)oshandle;
    if ((*handle)->body.type != MVM_OSHANDLE_DIR) {
        MVM_exception_throw_adhoc(tc, "%s requires an MVMOSHandle of type dir handle", msg);
    }
}

#ifdef _WIN32
#  define IS_SLASH(c)     ((c) == L'\\' || (c) == L'/')
#  define IS_NOT_SLASH(c) ((c) != L'\\' && (c) != L'/')
#else
#  define IS_SLASH(c)     ((c) == '/')
#  define IS_NOT_SLASH(c) ((c) != '/')
#endif

#ifdef _WIN32
static wchar_t * UTF8ToUnicode(char *str)
{
     const int         len = MultiByteToWideChar(CP_UTF8, 0, str, -1, NULL, 0);
     wchar_t * const result = (wchar_t *)malloc(len * sizeof(wchar_t));

     MultiByteToWideChar(CP_UTF8, 0, str, -1, result, len);

     return result;
}

static char * UnicodeToUTF8(const wchar_t *str)
{
     const int       len = WideCharToMultiByte(CP_UTF8, 0, str, -1, NULL, 0, NULL, NULL);
     char * const result = (char *)malloc(len * sizeof(char));

     WideCharToMultiByte(CP_UTF8, 0, str, -1, result, len, NULL, NULL);

     return result;
}

static int mkdir_p(wchar_t *pathname, MVMint64 mode) {
    size_t len = wcslen(pathname);
#else
static int mkdir_p(char *pathname, MVMint64 mode) {
    size_t len = strlen(pathname);
#endif
    ssize_t r;
    char tmp;

    while (len > 0 && IS_SLASH(pathname[len - 1]))
        len--;

    tmp = pathname[len];
    pathname[len] = '\0';
#ifdef _WIN32
    r = CreateDirectoryW(pathname, NULL);

    if (!r && GetLastError() == ERROR_PATH_NOT_FOUND)
#else
    r = mkdir(pathname, mode);

    if (r == -1 && errno == ENOENT)
#endif
    {
        ssize_t _len = len - 1;
        char _tmp;

        while (_len >= 0 && IS_NOT_SLASH(pathname[_len]))
            _len--;

        _tmp = pathname[_len];
        pathname[_len] = '\0';

        r = mkdir_p(pathname, mode);

        pathname[_len] = _tmp;

#ifdef _WIN32
        if(r) {
            r = CreateDirectoryW(pathname, NULL);
        }
#else
        if(r == 0) {
            r = mkdir(pathname, mode);
        }
#endif
    }

    pathname[len] = tmp;

    return r;
}

/* create a directory recursively */
void MVM_dir_mkdir(MVMThreadContext *tc, MVMString *path, MVMint64 mode) {
    char * const pathname = MVM_string_utf8_encode_C_string(tc, path);

#ifdef _WIN32
    /* Must using UTF8ToUnicode for supporting CJK Windows file name. */
    wchar_t * const wpathname = UTF8ToUnicode(pathname);
    if (!mkdir_p(wpathname, mode)) {
        DWORD error = GetLastError();
        if (error != ERROR_ALREADY_EXISTS) {
            free(pathname);
            free(wpathname);
            MVM_exception_throw_adhoc(tc, "Failed to mkdir: %d", GetLastError());
        }
    }
    free(wpathname);
#else

    if (mkdir_p(pathname, mode) == -1 && errno != EEXIST) {
        free(pathname);
        MVM_exception_throw_adhoc(tc, "Failed to mkdir: %s", errno);
    }

#endif
    free(pathname);
}

/* remove a directory recursively */
void MVM_dir_rmdir(MVMThreadContext *tc, MVMString *path) {
    char * const pathname = MVM_string_utf8_encode_C_string(tc, path);
    uv_fs_t req;

    if(uv_fs_rmdir(tc->loop, &req, pathname, NULL) < 0 ) {
        free(pathname);
        MVM_exception_throw_adhoc(tc, "Failed to rmdir: %s", uv_strerror(req.result));
    }

    free(pathname);
}

/* open a filehandle; takes a type object */
MVMObject * MVM_dir_open(MVMThreadContext *tc, MVMString *dirname) {
    MVMObject * const type_object = tc->instance->boot_types->BOOTIO;
    MVMOSHandle *result = (MVMOSHandle *)REPR(type_object)->allocate(tc, STABLE(type_object));
#ifdef _WIN32
    char *name;
    int str_len;
    wchar_t *wname;
    wchar_t *dir_name;
    wchar_t  abs_dirname[4096]; /* 4096 should be enough for absolute path */
    wchar_t *lpp_part;

    if (REPR(type_object)->ID != MVM_REPR_ID_MVMOSHandle || IS_CONCRETE(type_object)) {
        MVM_exception_throw_adhoc(tc, "Open dir needs a type object with MVMOSHandle REPR");
    }

    name  = MVM_string_utf8_encode_C_string(tc, dirname);
    wname = UTF8ToUnicode(name);
    free(name);

    /* You cannot use the "\\?\" prefix with a relative path,
     * relative paths are always limited to a total of MAX_PATH characters.
     * see http://msdn.microsoft.com/en-us/library/windows/desktop/aa365247%28v=vs.85%29.aspx */
    if (!GetFullPathNameW(wname, 4096, abs_dirname, &lpp_part)) {
        MVM_exception_throw_adhoc(tc, "Directory path is wrong: %d", GetLastError());
    }

    free(wname);

    str_len  = wcslen(abs_dirname) ;
    dir_name = (wchar_t *)malloc((str_len + 7) * sizeof(wchar_t));

    wcscpy(dir_name, L"\\\\?\\");
    wcscat(dir_name, abs_dirname);
    wcscat(dir_name, L"\\*");     /* Three characters are for the "\*" plus NULL appended.
                                   * see http://msdn.microsoft.com/en-us/library/windows/desktop/aa365200%28v=vs.85%29.aspx */

    result->body.type          = MVM_OSHANDLE_DIR;
    result->body.dir_name      = dir_name;
    result->body.dir_handle    = INVALID_HANDLE_VALUE;

#else
    char *  const dir_name = MVM_string_utf8_encode_C_string(tc, dirname);
    DIR * const dir_handle = opendir(dir_name);

    if (!dir_handle) {
        MVM_exception_throw_adhoc(tc, "Failed to open dir: %d", errno);
    }

    result->body.type          = MVM_OSHANDLE_DIR;
    result->body.dir_handle    = dir_handle;
    result->body.encoding_type = MVM_encoding_type_utf8;

#endif
    return (MVMObject *)result;
}

/* reads a directory entry from a directory.  Assumes utf8 for now */
MVMString * MVM_dir_read(MVMThreadContext *tc, MVMObject *oshandle) {
    MVMOSHandle *handle;
#ifdef _WIN32
    MVMString *result;
    TCHAR dir[MAX_PATH];
    WIN32_FIND_DATAW ffd;
    char *dir_str;

    verify_dirhandle_type(tc, oshandle, &handle, "read from dirhandle");

    if (handle->body.dir_handle == INVALID_HANDLE_VALUE) {
        HANDLE hFind = FindFirstFileW(handle->body.dir_name, &ffd);

        if (hFind == INVALID_HANDLE_VALUE) {
            MVM_exception_throw_adhoc(tc, "read from dirhandle failed: %d", GetLastError());
        }

        handle->body.dir_handle = hFind;
        dir_str = UnicodeToUTF8(ffd.cFileName);
        result = MVM_string_utf8_decode(tc, tc->instance->VMString, dir_str, strlen(dir_str));
        free(dir_str);
        return result;
    }
    else if (FindNextFileW(handle->body.dir_handle, &ffd) != 0)  {
        dir_str = UnicodeToUTF8(ffd.cFileName);
        result  = MVM_string_utf8_decode(tc, tc->instance->VMString, dir_str, strlen(dir_str));
        free(dir_str);
        return result;
    } else {
        return MVM_string_utf8_decode(tc, tc->instance->VMString, "", 0);
    }
#else
    struct dirent entry;
    struct dirent *result;
    int ret;

    verify_dirhandle_type(tc, oshandle, &handle, "read from dirhandle");

    ret = readdir_r(handle->body.dir_handle, &entry, &result);

    if (ret == 0) {
        if (result == NULL) {
            return MVM_decode_C_buffer_to_string(tc, tc->instance->VMString, "", 0, handle->body.encoding_type);
        }
        return MVM_decode_C_buffer_to_string(tc, tc->instance->VMString, entry.d_name, strlen(entry.d_name), handle->body.encoding_type);
    }

    MVM_exception_throw_adhoc(tc, "Failed to read dirhandle: %d", errno);
#endif
}

void MVM_dir_close(MVMThreadContext *tc, MVMObject *oshandle) {
    MVMOSHandle *handle;

    verify_dirhandle_type(tc, oshandle, &handle, "close dirhandle");
#ifdef _WIN32
    if(handle->body.dir_name) {
        free(handle->body.dir_name);
        handle->body.dir_name = NULL;
    }

    if (!FindClose(handle->body.dir_handle))
        MVM_exception_throw_adhoc(tc, "Failed to close dirhandle: %d", GetLastError());
#else

    if (closedir(handle->body.dir_handle) == -1)
        MVM_exception_throw_adhoc(tc, "Failed to close dirhandle: %d", errno);
#endif
}

void MVM_dir_chdir(MVMThreadContext *tc, MVMString *dir) {
    char * const dirstring = MVM_string_utf8_encode_C_string(tc, dir);

    if (uv_chdir((const char *)dirstring) != 0) {
        free(dirstring);
        MVM_exception_throw_adhoc(tc, "chdir failed: %s", uv_strerror(errno));
    }

    free(dirstring);
}
