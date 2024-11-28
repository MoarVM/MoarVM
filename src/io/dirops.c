#include "moar.h"

// Windows does not define the S_ISREG and S_ISDIR macros in stat.h, so we do.
// We have to define _CRT_INTERNAL_NONSTDC_NAMES 1 before #including sys/stat.h
// in order for Microsoft's stat.h to define names like S_IFMT, S_IFREG, and S_IFDIR,
// rather than just defining _S_IFMT, _S_IFREG, and _S_IFDIR as it normally does.
// See: https://stackoverflow.com/a/62371749/1772220
#ifdef _WIN32
#  define _CRT_INTERNAL_NONSTDC_NAMES 1
#include <sys/stat.h>
#  if !defined(S_ISDIR) && defined(S_IFMT) && defined(S_IFDIR)
#    define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#  endif
#  define IS_SLASH(c)     ((c) == L'\\' || (c) == L'/')
#else
#  define IS_SLASH(c)     ((c) == '/')
#endif

#ifndef PATH_MAX
#  define PATH_MAX 2048
#endif

#define ERR_STR_MAX 1024

static int mkdir_p(MVMThreadContext *tc, char *pathname, MVMint64 mode) {
    char *p = pathname, ch;
    uv_fs_t req;
    int mkdir_error = 0;

    for (;; ++p)
        if (!*p || IS_SLASH(*p)) {
            ch = *p;
            *p  = '\0';
            mkdir_error = uv_fs_mkdir(NULL, &req, pathname, mode, NULL);
            uv_fs_req_cleanup(&req);
            if (!(*p = ch)) break;
        }


    if (mkdir_error == 0 || (mkdir_error == UV_EEXIST
                             && uv_fs_stat(NULL, &req, pathname, NULL) == 0
                             && S_ISDIR(req.statbuf.st_mode))) {
        uv_fs_req_cleanup(&req);
        return 0;
    }
    else
        return mkdir_error;
}

/* Create a directory recursively. */
void MVM_dir_mkdir(MVMThreadContext *tc, MVMString *path, MVMint64 mode) {
    char * const pathname = MVM_platform_path(tc, path);
    int mkdir_error = mkdir_p(tc, pathname, mode);
    MVM_free(pathname);

    if (mkdir_error != 0) {
        char *err = MVM_malloc(ERR_STR_MAX);
        uv_strerror_r(mkdir_error, err, ERR_STR_MAX);
        char *waste[] = { err, NULL };
        MVM_exception_throw_adhoc_free(tc, waste, "Failed to mkdir: %s", err);
    }
}

/* Remove a directory recursively. */
void MVM_dir_rmdir(MVMThreadContext *tc, MVMString *path) {
    char * const pathname = MVM_platform_path(tc, path);
    uv_fs_t req;
    int rmdir_error = uv_fs_rmdir(NULL, &req, pathname, NULL);
    MVM_free(pathname);
    uv_fs_req_cleanup(&req);

    if (rmdir_error != 0) {
        char *err = MVM_malloc(ERR_STR_MAX);
        uv_strerror_r(rmdir_error, err, ERR_STR_MAX);
        char *waste[] = { err, NULL };
        MVM_exception_throw_adhoc_free(tc, waste, "Failed to rmdir: %s", err);
    }
}

/* Get the current working directory. */
MVMString * MVM_dir_cwd(MVMThreadContext *tc) {
    char path[PATH_MAX];
    size_t max_path = PATH_MAX;
    int cwd_error = uv_cwd(path, (size_t *)&max_path);

    if (cwd_error < 0) {
        char *err = MVM_malloc(ERR_STR_MAX);
        uv_strerror_r(cwd_error, err, ERR_STR_MAX);
        char *waste[] = { err, NULL };
        MVM_exception_throw_adhoc_free(tc, waste, "Failed to determine cwd: %s", err);
    }

    return MVM_string_utf8_c8_decode(tc, tc->instance->VMString, path, strlen(path));
}

/* Change directory. */
void MVM_dir_chdir(MVMThreadContext *tc, MVMString *dir) {
    const char *dirstring = MVM_platform_path(tc, dir);
    int chdir_error = uv_chdir(dirstring);
    MVM_free((void*)dirstring);

    if (chdir_error != 0) {
        char *err = MVM_malloc(ERR_STR_MAX);
        uv_strerror_r(chdir_error, err, ERR_STR_MAX);
        char *waste[] = { err, NULL };
        MVM_exception_throw_adhoc_free(tc, waste, "chdir failed: %s", err);
    }
}

/* Frees data associated with the directory handle. */
static void gc_free(MVMThreadContext *tc, MVMObject *h, void *d) {
    uv_dir_t *dir = (uv_dir_t *)d;
    if (dir) {
        uv_fs_t req;
        uv_fs_closedir(NULL, &req, dir, NULL);
        uv_fs_req_cleanup(&req);
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

/* Casts to a handle, checking it's a directory handle along the way. */
static MVMOSHandle * get_dirhandle(MVMThreadContext *tc, MVMObject *oshandle, const char *msg) {
    MVMOSHandle *handle = (MVMOSHandle *)oshandle;
    if (REPR(oshandle)->ID != MVM_REPR_ID_MVMOSHandle)
        MVM_exception_throw_adhoc(tc, "%s requires an object with REPR MVMOSHandle (got %s with REPR %s)", msg, MVM_6model_get_debug_name(tc, (MVMObject *)handle), REPR(handle)->name);
    if (handle->body.ops != &op_table)
        MVM_exception_throw_adhoc(tc, "%s got incorrect kind of handle", msg);
    return handle;
}

/* Open a filehandle, returning a handle. */
MVMObject * MVM_dir_open(MVMThreadContext *tc, MVMString *dirname) {
    char * const dir_name = MVM_platform_path(tc, dirname);
    uv_fs_t req;

    int opendir_error = uv_fs_opendir(NULL, &req, dir_name, NULL);
    MVM_free(dir_name);

    if (opendir_error != 0) {
        uv_fs_req_cleanup(&req);
        char *err = MVM_malloc(ERR_STR_MAX);
        uv_strerror_r(opendir_error, err, ERR_STR_MAX);
        char *waste[] = { err, NULL };
        MVM_exception_throw_adhoc_free(tc, waste, "Failed to open dir: %s", err);
    }

    MVMOSHandle  * result;
    MVMROOT(tc, dirname) {
        result = (MVMOSHandle *)MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTIO);
    }
    result->body.ops  = &op_table;
    result->body.data = req.ptr;

    uv_fs_req_cleanup(&req);

    return (MVMObject *)result;
}

/* Reads a directory entry from a directory. */
MVMString * MVM_dir_read(MVMThreadContext *tc, MVMObject *oshandle) {
    MVMOSHandle *handle = get_dirhandle(tc, oshandle, "readdir");
    uv_dir_t    *dir    = (uv_dir_t *)handle->body.data;
    if (dir == NULL)
        return tc->instance->str_consts.empty;

    uv_dirent_t dirent[1];
    dir->dirents = dirent;
    dir->nentries = 1;

    uv_fs_t req;
    int readdir_error = uv_fs_readdir(NULL, &req, dir, NULL);

    if (readdir_error == 0 || readdir_error == 1) {
        MVMString *ret = (readdir_error == 0)
                       ? tc->instance->str_consts.empty
                       : MVM_string_decode(tc, tc->instance->VMString, (char *)dirent[0].name,
                               strlen(dirent[0].name), MVM_encoding_type_utf8_c8);
        uv_fs_req_cleanup(&req);
        return ret;
    }
    uv_fs_req_cleanup(&req);

    char *err = MVM_malloc(ERR_STR_MAX);
    uv_strerror_r(readdir_error, err, ERR_STR_MAX);
    char *waste[] = { err, NULL };
    MVM_exception_throw_adhoc_free(tc, waste, "Failed to read dirhandle: %s", err);
}

void MVM_dir_close(MVMThreadContext *tc, MVMObject *oshandle) {
    MVMOSHandle *handle = get_dirhandle(tc, oshandle, "closedir");
    uv_dir_t    *dir    = (uv_dir_t *)handle->body.data;
    uv_fs_t      req;
    int closedir_error  = uv_fs_closedir(NULL, &req, dir, NULL);
    uv_fs_req_cleanup(&req);

    handle->body.data = NULL;

    if (closedir_error != 0) {
        char *err = MVM_malloc(ERR_STR_MAX);
        uv_strerror_r(closedir_error, err, ERR_STR_MAX);
        char *waste[] = { err, NULL };
        MVM_exception_throw_adhoc_free(tc, waste, "Failed to close dirhandle: %s", err);
    }
}
