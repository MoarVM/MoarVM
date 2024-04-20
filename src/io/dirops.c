#include "moar.h"

#ifdef _WIN32
#include <sys/stat.h>
#  define IS_SLASH(c)     ((c) == L'\\' || (c) == L'/')
#  define _S_ISTYPE(mode, mask)  (((mode) & _S_IFMT) == (mask))
#  define S_ISDIR(mode) _S_ISTYPE((mode), _S_IFDIR)
#else
#  define IS_SLASH(c)     ((c) == '/')
#endif

#ifndef PATH_MAX
#  define PATH_MAX 2048
#endif

static int mkdir_p(MVMThreadContext *tc, char *pathname, MVMint64 mode) {
    char *p = pathname, ch;
    uv_fs_t req;
    int mkdir_error = 0;
    int created = 0;

    for (;; ++p)
        if (!*p || IS_SLASH(*p)) {
            ch = *p;
            *p  = '\0';
            created = ((mkdir_error = uv_fs_mkdir(NULL, &req, pathname, mode, NULL)) == 0
                       || (mkdir_error == UV_EEXIST
                           && uv_fs_stat(NULL, &req, pathname, NULL) == 0
                           && S_ISDIR(req.statbuf.st_mode)));
            if (!(*p = ch)) break;
        }

    uv_fs_req_cleanup(&req);

    if (!created) return -1;

    return 0;
}

/* Create a directory recursively. */
void MVM_dir_mkdir(MVMThreadContext *tc, MVMString *path, MVMint64 mode) {
    char * const pathname = MVM_string_utf8_c8_encode_C_string(tc, path);
    int mkdir_error = 0;

    if ((mkdir_error = mkdir_p(tc, pathname, mode)) != 0) {
        MVM_free(pathname);
        MVM_exception_throw_adhoc(tc, "Failed to mkdir: %s", strerror(mkdir_error));
    }

    MVM_free(pathname);
}

/* Remove a directory recursively. */
void MVM_dir_rmdir(MVMThreadContext *tc, MVMString *path) {
    char * const pathname = MVM_string_utf8_c8_encode_C_string(tc, path);
    uv_fs_t req;
    int rmdir_error = 0;

    if ((rmdir_error = uv_fs_rmdir(NULL, &req, pathname, NULL)) < 0 ) {
        MVM_free(pathname);
        uv_fs_req_cleanup(&req);
        MVM_exception_throw_adhoc(tc, "Failed to rmdir: %s", uv_strerror(rmdir_error));
    }

    MVM_free(pathname);
    uv_fs_req_cleanup(&req);
}

/* Get the current working directory. */
MVMString * MVM_dir_cwd(MVMThreadContext *tc) {
    char path[PATH_MAX];
    size_t max_path = PATH_MAX;
    int cwd_error = 0;

    if ((cwd_error = uv_cwd(path, (size_t *)&max_path)) < 0) {
        MVM_exception_throw_adhoc(tc, "Failed to determine cwd: %s", uv_strerror(cwd_error));
    }

    return MVM_string_utf8_c8_decode(tc, tc->instance->VMString, path, strlen(path));
}

/* Change directory. */
void MVM_dir_chdir(MVMThreadContext *tc, MVMString *dir) {
    char * const dirstring = MVM_string_utf8_c8_encode_C_string(tc, dir);
    int chdir_error = uv_chdir(dirstring);
    MVM_free(dirstring);
    if (chdir_error) {
        MVM_exception_throw_adhoc(tc, "chdir failed: %s", uv_strerror(chdir_error));
    }
}

/* Frees data associated with the directory handle. */
static void gc_free(MVMThreadContext *tc, MVMObject *h, void *d) {
    uv_fs_t req;
    uv_dir_t *dir = (uv_dir_t *)d;
    if (dir) {
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
    MVMOSHandle  * result;
    int opendir_error = 0;
    uv_fs_t req;
    MVMROOT(tc, dirname, {
        result = (MVMOSHandle *)MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTIO);
    });

    char * const dir_name = MVM_string_utf8_c8_encode_C_string(tc, dirname);
    opendir_error = uv_fs_opendir(NULL, &req, dir_name, NULL);
    MVM_free(dir_name);

    if (opendir_error) {
        uv_fs_req_cleanup(&req);
        MVM_exception_throw_adhoc(tc, "Failed to open dir: %s", strerror(opendir_error));
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
    int readdir_error   = 0;
    uv_fs_t     req;

    uv_dirent_t dirent[1];
    dir->dirents = dirent;
    dir->nentries = 1;

    readdir_error = uv_fs_readdir(NULL, &req, dir, NULL);

    if (readdir_error == 0 || readdir_error == 1) {
        MVMString *ret = (readdir_error == 0)
                       ? tc->instance->str_consts.empty
                       : MVM_string_decode(tc, tc->instance->VMString, (char *)dirent[0].name,
                               strlen(dirent[0].name), MVM_encoding_type_utf8_c8);
        uv_fs_req_cleanup(&req);
        return ret;
    }

    uv_fs_req_cleanup(&req);

    MVM_exception_throw_adhoc(tc, "Failed to read dirhandle: %s", strerror(readdir_error));
}

void MVM_dir_close(MVMThreadContext *tc, MVMObject *oshandle) {
    MVMOSHandle *handle = get_dirhandle(tc, oshandle, "closedir");
    uv_dir_t    *dir    = (uv_dir_t *)handle->body.data;
    int closedir_error  = 0;
    uv_fs_t     req;

    closedir_error = uv_fs_closedir(NULL, &req, dir, NULL);

    uv_fs_req_cleanup(&req);
    handle->body.data = NULL;

    if (closedir_error != 0)
        MVM_exception_throw_adhoc(tc, "Failed to close dirhandle: %s", strerror(closedir_error));
}
