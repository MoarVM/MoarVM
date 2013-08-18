#include "moarvm.h"

#ifdef _WIN32
#include <strsafe.h>
#endif

static void verify_dirhandle_type(MVMThreadContext *tc, MVMObject *oshandle, MVMOSHandle **handle, const char *msg) {

    /* work on only MVMOSHandle of type MVM_OSHANDLE_DIR */
    if (REPR(oshandle)->ID != MVM_REPR_ID_MVMOSHandle) {
        MVM_exception_throw_adhoc(tc, "%s requires an object with REPR MVMOSHandle", msg);
    }
    *handle = (MVMOSHandle *)oshandle;
    if ((*handle)->body.handle_type != MVM_OSHANDLE_DIR) {
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
     wchar_t * const result = (wchar_t *)calloc(len, sizeof(wchar_t));

     memset(result, 0, len * sizeof(wchar_t));

     MultiByteToWideChar(CP_UTF8, 0, str, -1, result, len);

     return result;
}

static char * UnicodeToUTF8(const wchar_t *str)
{
     const int       len = WideCharToMultiByte(CP_UTF8, 0, str, -1, NULL, 0, NULL, NULL);
     char * const result = (char *)calloc(len, sizeof(char));

     memset(result, 0, len * sizeof(char));

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
        size_t _len = len - 1;
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
            MVM_exception_throw_adhoc(tc, "Failed to mkdir: %s", GetLastError());
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
MVMObject * MVM_dir_open(MVMThreadContext *tc, MVMObject *type_object, MVMString *dirname, MVMString *encoding_name) {
#ifdef _WIN32
    MVMOSHandle *result = (MVMOSHandle *)REPR(type_object)->allocate(tc, STABLE(type_object));
    char *name;
    int str_len;
    wchar_t *wname;
    wchar_t *dir_name;

    if (REPR(type_object)->ID != MVM_REPR_ID_MVMOSHandle || IS_CONCRETE(type_object)) {
        MVM_exception_throw_adhoc(tc, "Open dir needs a type object with MVMOSHandle REPR");
    }

    name  = MVM_string_utf8_encode_C_string(tc, dirname);
    wname = UTF8ToUnicode(name);
    free(name);

    str_len = wcslen(wname) + 7;

    if (str_len > (MAX_PATH - 3)) {
        free(wname);
        MVM_exception_throw_adhoc(tc, "Directory path is too long.");
    }

    dir_name = (wchar_t *)calloc(str_len, sizeof(wchar_t));

    StringCbCopyW(dir_name, str_len, L"\\\\?\\");
    StringCchCatW(dir_name, str_len, wname);
    StringCchCatW(dir_name, str_len, L"\\*");

    free(wname);
    result->body.type          = MVM_OSHANDLE_DIR;
    result->body.dir_name      = dir_name;
    result->body.dir_handle    = INVALID_HANDLE_VALUE;
    result->body.encoding_type = MVM_find_encoding_by_name(tc, encoding_name);

    return (MVMObject *)result;
#endif
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

    if (handle->body.dir_handle = INVALID_HANDLE_VALUE) {
        HANDLE hFind = FindFirstFileW(handle->body.dir_name, &ffd);

        if (hFind == INVALID_HANDLE_VALUE) {
            MVM_exception_throw_adhoc(tc, "read from dirhandle failed: %s", GetLastError());
        }

        handle->body.dir_handle = hFind;
        dir_str = UnicodeToUTF8(ffd.cFileName);
        result = MVM_decode_C_buffer_to_string(tc, tc->instance->VMString, dir_str, strlen(dir_str), handle->body.encoding_type);
        free(dir_str);
        return result;
    }
    else if (FindNextFileW(handle->body.dir_handle, &ffd) != 0)  {
        dir_str = UnicodeToUTF8(ffd.cFileName);
        result  = MVM_decode_C_buffer_to_string(tc, tc->instance->VMString, dir_str, strlen(dir_str), handle->body.encoding_type);
        free(dir_str);
        return result;
    } else {
        return MVM_decode_C_buffer_to_string(tc, tc->instance->VMString, "", 0, handle->body.encoding_type);
    }
#endif
}

void MVM_dir_close(MVMThreadContext *tc, MVMObject *oshandle) {
    apr_status_t rv;
    MVMOSHandle *handle;

    verify_dirhandle_type(tc, oshandle, &handle, "close dirhandle");

    if (!FindClose(handle->body.dir_handle))
        MVM_exception_throw_adhoc(tc, "Failed to close dirhandle: %s", GetLastError());
}

void MVM_dir_chdir(MVMThreadContext *tc, MVMString *dir) {
    char * const dirstring = MVM_string_utf8_encode_C_string(tc, dir);

    if (uv_chdir((const char *)dirstring) != 0) {
        free(dirstring);
        MVM_exception_throw_adhoc(tc, "chdir failed: %s", uv_strerror(errno));
    }

    free(dirstring);
}
