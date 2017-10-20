#include "moar.h"
#ifndef _WIN32
#include <dirent.h>
#endif

#ifdef _WIN32
#  define IS_SLASH(c)     ((c) == L'\\' || (c) == L'/')
#else
#  define IS_SLASH(c)     ((c) == '/')
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

static int mkdir_p(MVMThreadContext *tc, wchar_t *pathname, MVMint64 mode) {
    wchar_t *p = pathname, ch;
#else
static int mkdir_p(MVMThreadContext *tc, char *pathname, MVMint64 mode) {
    char *p = pathname, ch;
    uv_fs_t req;
#endif
    int created = 0;

    for (;; ++p)
        if (!*p || IS_SLASH(*p)) {
            ch = *p;
            *p  = '\0';
#ifdef _WIN32
            if (CreateDirectoryW(pathname, NULL)) {
                created = 1;
            }
#else
            if (uv_fs_stat(tc->loop, &req, pathname, NULL) <= 0) {
                if (mkdir(pathname, mode) != -1) {
                    created = 1;
                }
            }
#endif
            if (!(*p = ch)) break;
        }

    if (!created) return -1;

    return 0;
}

/* Create a directory recursively. */
void MVM_dir_mkdir(MVMThreadContext *tc, MVMString *path, MVMint64 mode) {
    char * const pathname = MVM_string_utf8_c8_encode_C_string(tc, path);

#ifdef _WIN32
    /* Must using UTF8ToUnicode for supporting CJK Windows file name. */
    wchar_t *wpathname = UTF8ToUnicode(pathname);
    int str_len = wcslen(wpathname);
    MVM_free(pathname);

    if (str_len > MAX_PATH) {
        wchar_t  abs_dirname[4096]; /* 4096 should be enough for absolute path */
        wchar_t *lpp_part;

        /* You cannot use the "\\?\" prefix with a relative path,
         * relative paths are always limited to a total of MAX_PATH characters.
         * see http://msdn.microsoft.com/en-us/library/windows/desktop/aa365247%28v=vs.85%29.aspx */
        if (!GetFullPathNameW(wpathname, 4096, abs_dirname, &lpp_part)) {
            MVM_free(wpathname);
            MVM_exception_throw_adhoc(tc, "Directory path is wrong: %d", GetLastError());
        }

        MVM_free(wpathname);

        str_len  = wcslen(abs_dirname);
        wpathname = (wchar_t *)MVM_malloc((str_len + 4) * sizeof(wchar_t));
        wcscpy(wpathname, L"\\\\?\\");
        wcscat(wpathname, abs_dirname);
    }

    if (mkdir_p(tc, wpathname, mode) == -1) {
        DWORD error = GetLastError();
        if (error != ERROR_ALREADY_EXISTS) {
            MVM_free(wpathname);
            MVM_exception_throw_adhoc(tc, "Failed to mkdir: %d", error);
        }
    }
    MVM_free(wpathname);
#else

    if (mkdir_p(tc, pathname, mode) == -1 && errno != EEXIST) {
        int mkdir_error = errno;
        MVM_free(pathname);
        MVM_exception_throw_adhoc(tc, "Failed to mkdir: %d", mkdir_error);
    }

    MVM_free(pathname);
#endif
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
        MVM_exception_throw_adhoc(tc, "Failed to determine cwd: %s", uv_strerror(r));
    }

    return MVM_string_utf8_c8_decode(tc, tc->instance->VMString, path, strlen(path));
}
int MVM_dir_chdir_C_string(MVMThreadContext *tc, const char *dirstring) {
    return uv_chdir(dirstring);
}
/* Change directory. */
void MVM_dir_chdir(MVMThreadContext *tc, MVMString *dir) {
    const char *dirstring = MVM_string_utf8_c8_encode_C_string(tc, dir);
    int chdir_error = MVM_dir_chdir_C_string(tc, dirstring);
    MVM_free((void*)dirstring);
    if (chdir_error) {
        MVM_exception_throw_adhoc(tc, "chdir failed: %s", uv_strerror(chdir_error));
    }
}

/* Structure to keep track of directory iteration state. */
typedef struct {
#ifdef _WIN32
    wchar_t *dir_name;
    HANDLE   dir_handle;
#else
    DIR     *dir_handle;
#endif
} MVMIODirIter;

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

/* Ops table for directory iterator; it all works off special ops, so no entries. */
static const MVMIOOps op_table = {
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
    int opendir_error = errno;
    MVM_free(dir_name);

    if (!dir_handle)
        MVM_exception_throw_adhoc(tc, "Failed to open dir: %d", opendir_error);

    data->dir_handle = dir_handle;
#endif

    result->body.ops  = &op_table;
    result->body.data = data;

    return (MVMObject *)result;
}

/* Casts to a handle, checking it's a directory handle along the way. */
static MVMOSHandle * get_dirhandle(MVMThreadContext *tc, MVMObject *oshandle, const char *msg) {
    MVMOSHandle *handle = (MVMOSHandle *)oshandle;
    if (REPR(oshandle)->ID != MVM_REPR_ID_MVMOSHandle)
        MVM_exception_throw_adhoc(tc, "%s requires an object with REPR MVMOSHandle (got %s with REPR %s)", msg, MVM_6model_get_debug_name(tc, handle), REPR(handle)->name);
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
                                    MVM_encoding_type_utf8_c8);
        MVM_free(dir_str);
        return result;
    } else {
        return tc->instance->str_consts.empty;
    }
#else

    struct dirent *entry;
    errno = 0; /* must reset errno so we won't check old errno */

    if (!data->dir_handle) {
        MVM_exception_throw_adhoc(tc, "Cannot read a closed dir handle.");
    }

    entry = readdir(data->dir_handle);

    if (errno == 0) {
        MVMString *ret = (entry == NULL)
                       ? tc->instance->str_consts.empty
                       : MVM_string_decode(tc, tc->instance->VMString, entry->d_name,
                               strlen(entry->d_name), MVM_encoding_type_utf8_c8);
        return ret;
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
