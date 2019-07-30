#include "moar.h"

#ifndef _WIN32
#include <sys/types.h>
#include <unistd.h>
#define DEFAULT_MODE 0x01B6
#else
#include <fcntl.h>
#define O_CREAT  _O_CREAT
#define O_RDONLY _O_RDONLY
#define O_WRONLY _O_WRONLY
#define O_TRUNC  _O_TRUNC
#define DEFAULT_MODE _S_IWRITE /* work around sucky libuv defaults */
#endif

static uv_stat_t file_info(MVMThreadContext *tc, MVMString *filename, MVMint32 use_lstat) {
    char * const a = MVM_string_utf8_c8_encode_C_string(tc, filename);
    uv_fs_t req;

    if ((use_lstat
      ? uv_fs_lstat(NULL, &req, a, NULL)
      :  uv_fs_stat(NULL, &req, a, NULL)
    ) < 0) {
        MVM_free(a);
        MVM_exception_throw_adhoc(tc, "Failed to stat file: %s", uv_strerror(req.result));
    }

    MVM_free(a);
    return req.statbuf;
}

static uv_stat_t file_info_fd(MVMThreadContext *tc, int fd) {
    uv_fs_t req;
    if (uv_fs_fstat(NULL, &req, (uv_file)fd, NULL) < 0)
        MVM_exception_throw_adhoc(tc, "Failed to stat file: %s", uv_strerror(req.result));
    return req.statbuf;
}

MVMint64 MVM_file_stat(MVMThreadContext *tc, void *f, MVMint64 status, MVMint32 use_lstat, MVMint32 use_fstat) {
    MVMint64  r        = -1;
    uv_stat_t statbuf;

    switch (status) {
        case MVM_STAT_EXISTS:
            r = MVM_file_exists(tc, f, use_lstat, use_fstat);
            break;
        case MVM_STAT_FILESIZE:
            statbuf = use_fstat ? file_info_fd(tc, *(int *)f) : file_info(tc, (MVMString *)f, use_lstat);
            r       = statbuf.st_size;
            break;
        case MVM_STAT_ISDIR:
            statbuf = use_fstat ? file_info_fd(tc, *(int *)f) : file_info(tc, (MVMString *)f, use_lstat);
            r       = (statbuf.st_mode & S_IFMT) == S_IFDIR;
            break;
        case MVM_STAT_ISREG:
            statbuf = use_fstat ? file_info_fd(tc, *(int *)f) : file_info(tc, (MVMString *)f, use_lstat);
            r       = (statbuf.st_mode & S_IFMT) == S_IFREG;
            break;
#ifdef _WIN32
        case MVM_STAT_ISDEV:
            statbuf = use_fstat ? file_info_fd(tc, *(int *)f) : file_info(tc, (MVMString *)f, use_lstat);
            r       = statbuf.st_mode & S_IFMT == S_IFCHR;
            break;
#else
        case MVM_STAT_ISDEV:
            statbuf = use_fstat ? file_info_fd(tc, *(int *)f) : file_info(tc, (MVMString *)f, use_lstat);
            r       = (statbuf.st_mode & S_IFMT) == S_IFCHR || (statbuf.st_mode & S_IFMT) == S_IFBLK;
            break;
#endif
        case MVM_STAT_CREATETIME:
            statbuf = use_fstat ? file_info_fd(tc, *(int *)f) : file_info(tc, (MVMString *)f, use_lstat);
            r       = statbuf.st_birthtim.tv_sec;
            break;
        case MVM_STAT_ACCESSTIME:
            statbuf = use_fstat ? file_info_fd(tc, *(int *)f) : file_info(tc, (MVMString *)f, use_lstat);
            r       = statbuf.st_atim.tv_sec;
            break;
        case MVM_STAT_MODIFYTIME:
            statbuf = use_fstat ? file_info_fd(tc, *(int *)f) : file_info(tc, (MVMString *)f, use_lstat);
            r       = statbuf.st_mtim.tv_sec;
            break;
        case MVM_STAT_CHANGETIME:
            statbuf = use_fstat ? file_info_fd(tc, *(int *)f) : file_info(tc, (MVMString *)f, use_lstat);
            r       = statbuf.st_ctim.tv_sec;
            break;
/*        case MVM_STAT_BACKUPTIME:
            statbuf = use_fstat ? file_info_fd(tc, *(int *)f) : file_info(tc, (MVMString *)f, use_lstat);
            r       = -1;
            break;
            */
        case MVM_STAT_UID:
            statbuf = use_fstat ? file_info_fd(tc, *(int *)f) : file_info(tc, (MVMString *)f, use_lstat);
            r       = statbuf.st_uid;
            break;
        case MVM_STAT_GID:
            statbuf = use_fstat ? file_info_fd(tc, *(int *)f) : file_info(tc, (MVMString *)f, use_lstat);
            r       = statbuf.st_gid;
            break;
        case MVM_STAT_ISLNK:
            statbuf = use_fstat ? file_info_fd(tc, *(int *)f) : file_info(tc, (MVMString *)f, 1);
            r       = (statbuf.st_mode & S_IFMT) == S_IFLNK;
            break;
        case MVM_STAT_PLATFORM_DEV:
            statbuf = use_fstat ? file_info_fd(tc, *(int *)f) : file_info(tc, (MVMString *)f, use_lstat);
            r       = statbuf.st_dev;
            break;
        case MVM_STAT_PLATFORM_INODE:
            statbuf = use_fstat ? file_info_fd(tc, *(int *)f) : file_info(tc, (MVMString *)f, use_lstat);
            r       = statbuf.st_ino;
            break;
        case MVM_STAT_PLATFORM_MODE:
            statbuf = use_fstat ? file_info_fd(tc, *(int *)f) : file_info(tc, (MVMString *)f, use_lstat);
            r       = statbuf.st_mode;
            break;
        case MVM_STAT_PLATFORM_NLINKS:
            statbuf = use_fstat ? file_info_fd(tc, *(int *)f) : file_info(tc, (MVMString *)f, use_lstat);
            r       = statbuf.st_nlink;
            break;
        case MVM_STAT_PLATFORM_DEVTYPE:
            statbuf = use_fstat ? file_info_fd(tc, *(int *)f) : file_info(tc, (MVMString *)f, use_lstat);
            r       = statbuf.st_rdev;
            break;
        case MVM_STAT_PLATFORM_BLOCKSIZE:
            statbuf = use_fstat ? file_info_fd(tc, *(int *)f) : file_info(tc, (MVMString *)f, use_lstat);
            r       = statbuf.st_blksize;
            break;
        case MVM_STAT_PLATFORM_BLOCKS:
            statbuf = use_fstat ? file_info_fd(tc, *(int *)f) : file_info(tc, (MVMString *)f, use_lstat);
            r       = statbuf.st_blocks;
            break;
    }

    return r;
}

MVMnum64 MVM_file_time(MVMThreadContext *tc, void *f, MVMint64 status, MVMint32 use_lstat, MVMint32 use_fstat) {
    uv_stat_t     statbuf = use_fstat ? file_info_fd(tc, *(int *)f) : file_info(tc, (MVMString *)f, use_lstat);
    uv_timespec_t ts;

    switch (status) {
        case MVM_STAT_CREATETIME: ts = statbuf.st_birthtim; break;
        case MVM_STAT_MODIFYTIME: ts = statbuf.st_mtim; break;
        case MVM_STAT_ACCESSTIME: ts = statbuf.st_atim; break;
        case MVM_STAT_CHANGETIME: ts = statbuf.st_ctim; break;
        default: return -1;
    }

    return ts.tv_sec + 1e-9 * (MVMnum64)ts.tv_nsec;
}

/* copy a file from one to another */
void MVM_file_copy(MVMThreadContext *tc, MVMString *src, MVMString * dest) {
    char * const a = MVM_string_utf8_c8_encode_C_string(tc, src);
    char * const b = MVM_string_utf8_c8_encode_C_string(tc, dest);
    uv_fs_t req;

    if(uv_fs_copyfile(NULL, &req, a, b, 0, NULL) < 0) {
        MVM_free(a);
        MVM_free(b);
        MVM_exception_throw_adhoc(tc, "Failed to copy file: %s", uv_strerror(req.result));
    }

    MVM_free(a);
    MVM_free(b);
}

/* rename one file to another. */
void MVM_file_rename(MVMThreadContext *tc, MVMString *src, MVMString *dest) {
    char * const a = MVM_string_utf8_c8_encode_C_string(tc, src);
    char * const b = MVM_string_utf8_c8_encode_C_string(tc, dest);
    uv_fs_t req;

    if(uv_fs_rename(NULL, &req, a, b, NULL) < 0 ) {
        MVM_free(a);
        MVM_free(b);
        MVM_exception_throw_adhoc(tc, "Failed to rename file: %s", uv_strerror(req.result));
    }

    MVM_free(a);
    MVM_free(b);
}

void MVM_file_delete(MVMThreadContext *tc, MVMString *f) {
    uv_fs_t req;
    char * const a = MVM_string_utf8_c8_encode_C_string(tc, f);

#ifdef _WIN32
    const int r = MVM_platform_unlink(a);

    if( r < 0 && errno != ENOENT) {
        MVM_free(a);
        MVM_exception_throw_adhoc(tc, "Failed to delete file: %d", errno);
    }

#else
    const int r = uv_fs_unlink(NULL, &req, a, NULL);

    if( r < 0 && r != UV_ENOENT) {
        MVM_free(a);
        MVM_exception_throw_adhoc(tc, "Failed to delete file: %s", uv_strerror(req.result));
    }

#endif
    MVM_free(a);
}

void MVM_file_chmod(MVMThreadContext *tc, MVMString *f, MVMint64 flag) {
    char * const a = MVM_string_utf8_c8_encode_C_string(tc, f);
    uv_fs_t req;

    if(uv_fs_chmod(NULL, &req, a, flag, NULL) < 0 ) {
        MVM_free(a);
        MVM_exception_throw_adhoc(tc, "Failed to set permissions on path: %s", uv_strerror(req.result));
    }

    MVM_free(a);
}

MVMint64 MVM_file_exists(MVMThreadContext *tc, void *f, MVMint32 use_lstat, MVMint32 use_fstat) {
    uv_fs_t  req;
    MVMint64 result;

    if (use_fstat)
        result = uv_fs_fstat(NULL, &req, *(int *)f, NULL) < 0 ? 0 : 1;
    else {
        char * const a = MVM_string_utf8_c8_encode_C_string(tc, (MVMString *)f);
        result = (use_lstat
          ? uv_fs_lstat(NULL, &req, a, NULL)
          :  uv_fs_stat(NULL, &req, a, NULL)
        ) < 0 ? 0 : 1;
        MVM_free(a);
    }

    return result;
}

#ifdef _WIN32
#define FILE_IS(name, rwx) \
    MVMint64 MVM_file_is ## name (MVMThreadContext *tc, void *f, MVMint32 use_lstat, MVMint32 use_fstat) { \
        if (!MVM_file_exists(tc, f, use_lstat, use_fstat)) { \
            return 0; \
        } else { \
            uv_stat_t statbuf = use_fstat ? file_info_fd(tc, *(int *)f) : file_info(tc, (MVMString *)f, use_lstat); \
            MVMint64 r = (statbuf.st_mode & S_I ## rwx ); \
            return r ? 1 : 0; \
        } \
    }
FILE_IS(readable, READ)
FILE_IS(writable, WRITE)
MVMint64 MVM_file_isexecutable(MVMThreadContext *tc, void *f, MVMint32 use_lstat, MVMint32 use_fstat) {
    if (!MVM_file_exists(tc, f, use_lstat, use_fstat))
        return 0;
    else {
        MVMint64  r       = 0;
        uv_stat_t statbuf = use_fstat ? file_info_fd(tc, *(int *)f) : file_info(tc, (MVMString *)f, use_lstat);
        if ((statbuf.st_mode & S_IFMT) == S_IFDIR) {
            return 1;
        } else if (!use_fstat) {
            /* true if fileext is in PATHEXT=.COM;.EXE;.BAT;.CMD;.VBS;.VBE;.JS;.JSE;.WSF;.WSH;.MSC */
            MVMString *dot = MVM_string_ascii_decode_nt(tc, tc->instance->VMString, ".");
            MVMROOT(tc, dot, {
                MVMint64 n = MVM_string_index_from_end(tc, f, dot, 0);
                if (n >= 0) {
                    MVMString *fileext = MVM_string_substring(tc, f, n, -1);
                    char      *ext     = MVM_string_utf8_c8_encode_C_string(tc, fileext);
                    char      *pext    = getenv("PATHEXT");
                    int        plen    = strlen(pext);
                    int        i;
                    for (i = 0; i < plen; i++) {
                        if (0 == stricmp(ext, pext++)) {
                             r = 1;
                             break;
                        }
                    }
                    MVM_free(ext);
                    MVM_free(pext);
                }
            });
        }

        return r;
    }
}
#else
#define FILE_IS(name, rwx) \
    MVMint64 MVM_file_is ## name (MVMThreadContext *tc, void *f, MVMint32 use_lstat, MVMint32 use_fstat) { \
        if (!MVM_file_exists(tc, f, use_lstat, use_fstat)) \
            return 0; \
        else { \
            uv_stat_t statbuf = use_fstat ? file_info_fd(tc, *(int *)f) : file_info(tc, (MVMString *)f, use_lstat); \
            MVMint64 r = (statbuf.st_mode & S_I ## rwx ## OTH) \
                      || (statbuf.st_uid == geteuid() && (statbuf.st_mode & S_I ## rwx ## USR)) \
                      || (statbuf.st_uid == getegid() && (statbuf.st_mode & S_I ## rwx ## GRP)); \
            return r ? 1 : 0; \
        } \
    }
FILE_IS(readable, R)
FILE_IS(writable, W)
FILE_IS(executable, X)
#endif

/* Get a MoarVM file handle representing one of the standard streams */
MVMObject * MVM_file_get_stdstream(MVMThreadContext *tc, MVMint32 descriptor) {
    return MVM_file_handle_from_fd(tc, descriptor);
}

/* Takes a filename and prepends any --libpath value we have, if it's not an
 * absolute path. */
MVMString * MVM_file_in_libpath(MVMThreadContext *tc, MVMString *orig) {
    const char **lib_path = tc->instance->lib_path;
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&orig);
    if (lib_path) {
        /* We actually have a lib_path to consider. See if the filename is
         * absolute (XXX wants a platform abstraction, and doing better). */
        char *orig_cstr = MVM_string_utf8_c8_encode_C_string(tc, orig);
        int  absolute   = orig_cstr[0] == '/' || orig_cstr[0] == '\\' ||
                          (orig_cstr[1] == ':' && orig_cstr[2] == '\\');
        if (absolute) {
            /* Nothing more to do; we have an absolute path. */
            MVM_free(orig_cstr);
            MVM_gc_root_temp_pop(tc); /* orig */
            return orig;
        }
        else {
            MVMString *result = NULL;
            int lib_path_i = 0;
            MVM_gc_root_temp_push(tc, (MVMCollectable **)&result);
            while (lib_path[lib_path_i]) {
                /* Concatenate libpath with filename. */
                size_t lib_path_len = strlen(lib_path[lib_path_i]);
                size_t orig_len     = strlen(orig_cstr);
                int    need_sep     = lib_path[lib_path_i][lib_path_len - 1] != '/' &&
                                      lib_path[lib_path_i][lib_path_len - 1] != '\\';
                int    new_len      = lib_path_len + (need_sep ? 1 : 0) + orig_len;
                char * new_path     = MVM_malloc(new_len);
                memcpy(new_path, lib_path[lib_path_i], lib_path_len);
                if (need_sep) {
                    new_path[lib_path_len] = '/';
                    memcpy(new_path + lib_path_len + 1, orig_cstr, orig_len);
                }
                else {
                    memcpy(new_path + lib_path_len, orig_cstr, orig_len);
                }
                result = MVM_string_utf8_c8_decode(tc, tc->instance->VMString, new_path, new_len);
                MVM_free(new_path);
                if (!MVM_file_exists(tc, result, 1, 0))
                    result = orig;
                else {
                    MVM_free(orig_cstr);
                    MVM_gc_root_temp_pop_n(tc, 2); /* orig and result */
                    return result;
                }
                lib_path_i++;
            }
            if (!result || !MVM_file_exists(tc, result, 1, 0))
                result = orig;
            MVM_free(orig_cstr);
            MVM_gc_root_temp_pop_n(tc, 2); /* orig and result */
            return result;
        }
    }
    else {
        /* No libpath, so just hand back the original name. */
        MVM_gc_root_temp_pop(tc); /* orig */
        return orig;
    }
}

void MVM_file_link(MVMThreadContext *tc, MVMString *oldpath, MVMString *newpath) {
    uv_fs_t req;
    char * const oldpath_s = MVM_string_utf8_c8_encode_C_string(tc, oldpath);
    char * const newpath_s = MVM_string_utf8_c8_encode_C_string(tc, newpath);

    if (uv_fs_link(NULL, &req, oldpath_s, newpath_s, NULL)) {
        MVM_free(oldpath_s);
        MVM_free(newpath_s);
        MVM_exception_throw_adhoc(tc, "Failed to link file: %s", uv_strerror(req.result));
    }

    MVM_free(oldpath_s);
    MVM_free(newpath_s);
}

void MVM_file_symlink(MVMThreadContext *tc, MVMString *oldpath, MVMString *newpath) {
    uv_fs_t req;
    char * const oldpath_s = MVM_string_utf8_c8_encode_C_string(tc, oldpath);
    char * const newpath_s = MVM_string_utf8_c8_encode_C_string(tc, newpath);

    if (uv_fs_symlink(NULL, &req, oldpath_s, newpath_s, 0, NULL)) {
        MVM_free(oldpath_s);
        MVM_free(newpath_s);
        MVM_exception_throw_adhoc(tc, "Failed to symlink file: %s", uv_strerror(req.result));
    }

    MVM_free(oldpath_s);
    MVM_free(newpath_s);
}

MVMString * MVM_file_readlink(MVMThreadContext *tc, MVMString *path) {
    uv_fs_t req;
    MVMString *result;

    char * const path_s = MVM_string_utf8_c8_encode_C_string(tc, path);
    if (uv_fs_readlink(NULL, &req, path_s, NULL) < 0) {
        MVM_free(path_s);
        MVM_exception_throw_adhoc(tc, "Failed to readlink file: %s", uv_strerror(req.result));
    }

    MVM_free(path_s);
    result = MVM_string_utf8_c8_decode(tc, tc->instance->VMString, req.ptr, strlen(req.ptr));
    MVM_free(req.ptr);

    return result;
}
