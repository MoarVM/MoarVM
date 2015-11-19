#include "moar.h"
#ifndef _WIN32
#include <dirent.h>
#endif

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
     wchar_t * const result = (wchar_t *)MVM_malloc(len * sizeof(wchar_t));

     MultiByteToWideChar(CP_UTF8, 0, str, -1, result, len);

     return result;
}

static char * UnicodeToUTF8(const wchar_t *str)
{
     const int       len = WideCharToMultiByte(CP_UTF8, 0, str, -1, NULL, 0, NULL, NULL);
     char * const result = (char *)MVM_malloc(len * sizeof(char));

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

/* Create a directory recursively. */
void MVM_dir_mkdir(MVMThreadContext *tc, MVMString *path, MVMint64 mode) {
    char * const pathname = MVM_string_utf8_c8_encode_C_string(tc, path);

#ifdef _WIN32
    /* Must using UTF8ToUnicode for supporting CJK Windows file name. */
    wchar_t *wpathname = UTF8ToUnicode(pathname);
    int str_len = wcslen(wpathname);

    if (str_len > MAX_PATH) {
        wchar_t  abs_dirname[4096]; /* 4096 should be enough for absolute path */
        wchar_t *lpp_part;

        /* You cannot use the "\\?\" prefix with a relative path,
         * relative paths are always limited to a total of MAX_PATH characters.
         * see http://msdn.microsoft.com/en-us/library/windows/desktop/aa365247%28v=vs.85%29.aspx */
        if (!GetFullPathNameW(wpathname, 4096, abs_dirname, &lpp_part)) {
            MVM_exception_throw_adhoc(tc, "Directory path is wrong: %d", GetLastError());
        }

        MVM_free(wpathname);

        str_len  = wcslen(abs_dirname);
        wpathname = (wchar_t *)MVM_malloc((str_len + 4) * sizeof(wchar_t));
        wcscpy(wpathname, L"\\\\?\\");
        wcscat(wpathname, abs_dirname);
    }

    if (!mkdir_p(wpathname, mode)) {
        DWORD error = GetLastError();
        if (error != ERROR_ALREADY_EXISTS) {
            MVM_free(pathname);
            MVM_free(wpathname);
            MVM_exception_throw_adhoc(tc, "Failed to mkdir: %d", error);
        }
    }
    MVM_free(wpathname);
#else

    if (mkdir_p(pathname, mode) == -1 && errno != EEXIST) {
        MVM_free(pathname);
        MVM_exception_throw_adhoc(tc, "Failed to mkdir: %d", errno);
    }

#endif
    MVM_free(pathname);
}

/* Remove a directory recursively. */
void MVM_dir_rmdir(MVMThreadContext *tc, MVMString *path) {
    char * const pathname = MVM_string_utf8_c8_encode_C_string(tc, path);
    uv_fs_t req;

    if(uv_fs_rmdir(tc->loop, &req, pathname, NULL) < 0 ) {
        MVM_free(pathname);
        MVM_exception_throw_adhoc(tc, "Failed to rmdir: %s", uv_strerror(req.result));
    }

    MVM_free(pathname);
}

/* Get the current working directory. */
MVMString * MVM_dir_cwd(MVMThreadContext *tc) {
#ifdef _WIN32
    char path[MAX_PATH];
    size_t max_path = MAX_PATH;
    int r;
#else
    char path[PATH_MAX];
    size_t max_path = PATH_MAX;
    int r;
#endif

    if ((r = uv_cwd(path, (size_t *)&max_path)) < 0) {
        MVM_exception_throw_adhoc(tc, "chdir failed: %s", uv_strerror(r));
    }

    return MVM_string_utf8_c8_decode(tc, tc->instance->VMString, path, strlen(path));
}

/* Change directory. */
void MVM_dir_chdir(MVMThreadContext *tc, MVMString *dir) {
    char * const dirstring = MVM_string_utf8_c8_encode_C_string(tc, dir);

    if (uv_chdir((const char *)dirstring) != 0) {
        MVM_free(dirstring);
        MVM_exception_throw_adhoc(tc, "chdir failed: %s", uv_strerror(errno));
    }

    MVM_free(dirstring);
}

/* Structure to keep track of directory iteration state. */
typedef struct {
#ifdef _WIN32
    wchar_t *dir_name;
    HANDLE   dir_handle;
#else
    DIR     *dir_handle;
#endif
    MVMuint8 encoding;
} MVMIODirIter;

/* Sets the encoding used for reading the directory listing. */
static void set_encoding(MVMThreadContext *tc, MVMOSHandle *h, MVMint64 encoding) {
    MVMIODirIter *data = (MVMIODirIter *)h->body.data;
    data->encoding = encoding;
}

/* Frees data associated with the directory handle. */
static void gc_free(MVMThreadContext *tc, MVMObject *h, void *d) {
    MVMIODirIter *data = (MVMIODirIter *)d;
    if (data) {
#ifdef _WIN32
        if (data->dir_name)
            MVM_free(data->dir_name);
        if (data->dir_handle)
            FindClose(data->dir_handle);
#else
        if (data->dir_handle)
            closedir(data->dir_handle);
#endif
        MVM_free(data);
    }
}

/* Ops table for directory iterator; it all works off special ops, so almost
 * no entries. */
static const MVMIOEncodable encodable = { set_encoding };
static const MVMIOOps op_table = {
    NULL,
    &encodable,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    gc_free
};

/* Open a filehandle, returning a handle. */
MVMObject * MVM_dir_open(MVMThreadContext *tc, MVMString *dirname) {
    MVMOSHandle  * const result = (MVMOSHandle *)MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTIO);
    MVMIODirIter * const data   = MVM_calloc(1, sizeof(MVMIODirIter));
#ifdef _WIN32
    char *name;
    int str_len;
    wchar_t *wname;
    wchar_t *dir_name;

    name  = MVM_string_utf8_c8_encode_C_string(tc, dirname);
    wname = UTF8ToUnicode(name);
    MVM_free(name);

    str_len = wcslen(wname);

    if (str_len > MAX_PATH - 2) { // the length of later appended '\*' is 2
        wchar_t  abs_dirname[4096]; /* 4096 should be enough for absolute path */
        wchar_t *lpp_part;

        /* You cannot use the "\\?\" prefix with a relative path,
         * relative paths are always limited to a total of MAX_PATH characters.
         * see http://msdn.microsoft.com/en-us/library/windows/desktop/aa365247%28v=vs.85%29.aspx */
        if (!GetFullPathNameW(wname, 4096, abs_dirname, &lpp_part)) {
            MVM_free(wname);
            MVM_exception_throw_adhoc(tc, "Directory path is wrong: %d", GetLastError());
        }
        MVM_free(wname);

        str_len  = wcslen(abs_dirname);
        dir_name = (wchar_t *)MVM_malloc((str_len + 7) * sizeof(wchar_t));
        wcscpy(dir_name, L"\\\\?\\");
        wcscat(dir_name, abs_dirname);
    } else {
        dir_name = (wchar_t *)MVM_malloc((str_len + 3) * sizeof(wchar_t));
        wcscpy(dir_name, wname);
        MVM_free(wname);
    }

    wcscat(dir_name, L"\\*");     /* Three characters are for the "\*" plus NULL appended.
                                   * see http://msdn.microsoft.com/en-us/library/windows/desktop/aa365200%28v=vs.85%29.aspx */

    data->dir_name   = dir_name;
    data->dir_handle = INVALID_HANDLE_VALUE;

#else
    char * const dir_name = MVM_string_utf8_c8_encode_C_string(tc, dirname);
    DIR * const dir_handle = opendir(dir_name);
    MVM_free(dir_name);

    if (!dir_handle)
        MVM_exception_throw_adhoc(tc, "Failed to open dir: %d", errno);

    data->dir_handle = dir_handle;
#endif

    data->encoding = MVM_encoding_type_utf8;
    result->body.ops  = &op_table;
    result->body.data = data;

    return (MVMObject *)result;
}

/* Casts to a handle, checking it's a directory handle along the way. */
static MVMOSHandle * get_dirhandle(MVMThreadContext *tc, MVMObject *oshandle, const char *msg) {
    MVMOSHandle *handle = (MVMOSHandle *)oshandle;
    if (REPR(oshandle)->ID != MVM_REPR_ID_MVMOSHandle)
        MVM_exception_throw_adhoc(tc, "%s requires an object with REPR MVMOSHandle", msg);
    if (handle->body.ops != &op_table)
        MVM_exception_throw_adhoc(tc, "%s got incorrect kind of handle", msg);
    return handle;
}

/* Reads a directory entry from a directory. */
MVMString * MVM_dir_read(MVMThreadContext *tc, MVMObject *oshandle) {
    MVMOSHandle  *handle = get_dirhandle(tc, oshandle, "readdir");
    MVMIODirIter *data   = (MVMIODirIter *)handle->body.data;
#ifdef _WIN32
    MVMString *result;
    TCHAR dir[MAX_PATH];
    WIN32_FIND_DATAW ffd;
    char *dir_str;

    if (data->dir_handle == INVALID_HANDLE_VALUE) {
        HANDLE hFind = FindFirstFileW(data->dir_name, &ffd);

        if (hFind == INVALID_HANDLE_VALUE) {
            MVM_exception_throw_adhoc(tc, "read from dirhandle failed: %d", GetLastError());
        }

        data->dir_handle = hFind;
        dir_str = UnicodeToUTF8(ffd.cFileName);
        result = MVM_string_utf8_c8_decode(tc, tc->instance->VMString, dir_str, strlen(dir_str));
        MVM_free(dir_str);
        return result;
    }
    else if (FindNextFileW(data->dir_handle, &ffd) != 0)  {
        dir_str = UnicodeToUTF8(ffd.cFileName);
        result  = MVM_string_decode(tc, tc->instance->VMString, dir_str, strlen(dir_str),
                                    data->encoding);
        MVM_free(dir_str);
        return result;
    } else {
        return tc->instance->str_consts.empty;
    }
#else
    struct dirent entry;
    struct dirent *result;
    int ret;

    ret = readdir_r(data->dir_handle, &entry, &result);

    if (ret == 0) {
        if (result == NULL)
            return tc->instance->str_consts.empty;
        return MVM_string_decode(tc, tc->instance->VMString, entry.d_name, strlen(entry.d_name), data->encoding);
    }

    MVM_exception_throw_adhoc(tc, "Failed to read dirhandle: %d", errno);
#endif
}

void MVM_dir_close(MVMThreadContext *tc, MVMObject *oshandle) {
    MVMOSHandle  *handle = get_dirhandle(tc, oshandle, "readdir");
    MVMIODirIter *data   = (MVMIODirIter *)handle->body.data;

#ifdef _WIN32
    if (data->dir_name) {
        MVM_free(data->dir_name);
        data->dir_name = NULL;
    }

    if (!FindClose(data->dir_handle))
        MVM_exception_throw_adhoc(tc, "Failed to close dirhandle: %d", GetLastError());
    data->dir_handle = NULL;
#else
    if (closedir(data->dir_handle) == -1)
        MVM_exception_throw_adhoc(tc, "Failed to close dirhandle: %d", errno);
    data->dir_handle = NULL;
#endif
}
