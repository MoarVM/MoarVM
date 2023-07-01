#include "moar.h"
#include "platform/io.h"

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

static MVMint64 file_info_with_error(MVMThreadContext *tc, uv_stat_t* stat, MVMString *filename, MVMint32 use_lstat) {
    char * const a = MVM_string_utf8_c8_encode_C_string(tc, filename);
    uv_fs_t req;

    MVMint64 res = use_lstat
      ? uv_fs_lstat(NULL, &req, a, NULL)
      :  uv_fs_stat(NULL, &req, a, NULL);
    *stat = req.statbuf;

    MVM_free(a);
    return res;
}

MVMint64 MVM_file_info_with_error(MVMThreadContext *tc, uv_stat_t* stat, MVMString *filename, MVMint32 use_lstat) {
    return file_info_with_error(tc, stat, filename, use_lstat);
}

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

uv_stat_t MVM_file_info(MVMThreadContext *tc, MVMString *filename, MVMint32 use_lstat) {
    return file_info(tc, filename, use_lstat);
}

MVMint64 MVM_file_stat(MVMThreadContext *tc, MVMString *filename, MVMint64 status, MVMint32 use_lstat) {
    MVMint64 r = -1;

    switch (status) {

        case MVM_STAT_EXISTS:             r = MVM_file_exists(tc, filename, use_lstat); break;

        case MVM_STAT_FILESIZE: {
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

                r = req.statbuf.st_size;
                break;
            }

        case MVM_STAT_ISDIR:              r = (file_info(tc, filename, use_lstat).st_mode & S_IFMT) == S_IFDIR; break;

        case MVM_STAT_ISREG:              r = (file_info(tc, filename, use_lstat).st_mode & S_IFMT) == S_IFREG; break;

        case MVM_STAT_ISDEV: {
            const int mode = file_info(tc, filename, use_lstat).st_mode;
#ifdef _WIN32
            r = (mode & S_IFMT) == S_IFCHR;
#else
            r = (mode & S_IFMT) == S_IFCHR || (mode & S_IFMT) == S_IFBLK;
#endif
            break;
        }

        case MVM_STAT_CREATETIME:         r = file_info(tc, filename, use_lstat).st_birthtim.tv_sec; break;

        case MVM_STAT_ACCESSTIME:         r = file_info(tc, filename, use_lstat).st_atim.tv_sec; break;

        case MVM_STAT_MODIFYTIME:         r = file_info(tc, filename, use_lstat).st_mtim.tv_sec; break;

        case MVM_STAT_CHANGETIME:         r = file_info(tc, filename, use_lstat).st_ctim.tv_sec; break;

/*        case MVM_STAT_BACKUPTIME:         r = -1; break;  */

        case MVM_STAT_UID:                r = file_info(tc, filename, use_lstat).st_uid; break;

        case MVM_STAT_GID:                r = file_info(tc, filename, use_lstat).st_gid; break;

        case MVM_STAT_ISLNK:              r = (file_info(tc, filename, 1).st_mode & S_IFMT) == S_IFLNK; break;

        case MVM_STAT_PLATFORM_DEV:       r = file_info(tc, filename, use_lstat).st_dev; break;

        case MVM_STAT_PLATFORM_INODE:     r = file_info(tc, filename, use_lstat).st_ino; break;

        case MVM_STAT_PLATFORM_MODE:      r = file_info(tc, filename, use_lstat).st_mode; break;

        case MVM_STAT_PLATFORM_NLINKS:    r = file_info(tc, filename, use_lstat).st_nlink; break;

        case MVM_STAT_PLATFORM_DEVTYPE:   r = file_info(tc, filename, use_lstat).st_rdev; break;

        case MVM_STAT_PLATFORM_BLOCKSIZE: r = file_info(tc, filename, use_lstat).st_blksize; break;

        case MVM_STAT_PLATFORM_BLOCKS:    r = file_info(tc, filename, use_lstat).st_blocks; break;

        default: break;
    }

    return r;
}

MVMnum64 MVM_file_time(MVMThreadContext *tc, MVMString *filename, MVMint64 status, MVMint32 use_lstat) {
    uv_stat_t statbuf = file_info(tc, filename, use_lstat);
    uv_timespec_t ts;

    switch(status) {
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
    char * const a = MVM_string_utf8_c8_encode_C_string(tc, f);

#ifdef _WIN32
    const int r = MVM_platform_unlink(a);

    if( r < 0 && errno != ENOENT) {
        MVM_free(a);
        MVM_exception_throw_adhoc(tc, "Failed to delete file: %d", errno);
    }

#else
    uv_fs_t req;
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

void MVM_file_chown(MVMThreadContext *tc, MVMString *f, MVMuint64 uid, MVMuint64 gid) {
    char * const a = MVM_string_utf8_c8_encode_C_string(tc, f);
    uv_fs_t req;

    if(uv_fs_chown(NULL, &req, a, uid, gid, NULL) < 0 ) {
        MVM_free(a);
        MVM_exception_throw_adhoc(tc, "Failed to set owner/group on path: %s", uv_strerror(req.result));
    }

    MVM_free(a);
}

MVMint64 MVM_file_exists(MVMThreadContext *tc, MVMString *f, MVMint32 use_lstat) {
    uv_fs_t req;
    char * const a = MVM_string_utf8_c8_encode_C_string(tc, f);
    const MVMint64 result = (use_lstat
      ? uv_fs_lstat(NULL, &req, a, NULL)
      :  uv_fs_stat(NULL, &req, a, NULL)
    ) < 0 ? 0 : 1;

    MVM_free(a);

    return result;
}

#ifdef _WIN32
#define FILE_IS(name, rwx) \
    MVMint64 MVM_file_is ## name (MVMThreadContext *tc, MVMString *filename, MVMint32 use_lstat) { \
        uv_stat_t statbuf; \
        if (file_info_with_error(tc, &statbuf, filename, use_lstat) < 0) { \
            return 0; \
        } \
        else { \
            MVMint64 r = (statbuf.st_mode & S_I ## rwx ); \
            return r ? 1 : 0; \
        } \
    }
FILE_IS(readable, READ)
FILE_IS(writable, WRITE)
MVMint64 MVM_file_isexecutable(MVMThreadContext *tc, MVMString *filename, MVMint32 use_lstat) {
    MVMint64 r = 0;
    uv_stat_t statbuf;
    if (file_info_with_error(tc, &statbuf, filename, use_lstat) < 0)
        return 0;
    else if ((statbuf.st_mode & S_IFMT) == S_IFDIR)
        return 1;
    else {
        /* true if fileext is in PATHEXT=.COM;.EXE;.BAT;.CMD;.VBS;.VBE;.JS;.JSE;.WSF;.WSH;.MSC */
        MVMint64 n = MVM_string_index_from_end(tc, filename, tc->instance->str_consts.dot, 0);
        if (n >= 0) {
            MVMString *fileext = MVM_string_substring(tc, filename, n, -1);
            char *ext  = MVM_string_utf8_c8_encode_C_string(tc, fileext);
            char *pext = getenv("PATHEXT");
            int plen   = strlen(pext);
            int i;
            for (i = 0; i < plen; i++) {
                if (0 == stricmp(ext, pext++)) {
                     r = 1;
                     break;
                }
            }
            MVM_free(ext);
        }
    }
    return r;
}
#else

static int are_we_group_member(MVMThreadContext *tc, gid_t group) {
    int len;
    gid_t *gids;
    int res;
    int i;
    /* Check the user group. */
    if (getegid() == group)
        return 1;
    /* Check the supplementary groups. */
    len  = getgroups(0, NULL);
    if (len == 0)
        return 0;
    gids = MVM_malloc(len * sizeof(gid_t));
    res = getgroups(len, gids);
    if (res < 0) {
        MVM_free(gids);
        MVM_exception_throw_adhoc(tc, "Failed to retrieve groups: %s", strerror(errno));
    }
    res = 0;
    for (i = 0; i < len; i++) {
        if (gids[i] == group) {
            res = 1;
            break;
        }
    }
    MVM_free(gids);
    return res;
}
MVMint64 MVM_are_we_group_member(MVMThreadContext *tc, gid_t group) {
    return (MVMint64)are_we_group_member(tc, group);
}

#define FILE_IS(name, rwx) \
    MVMint64 MVM_file_is ## name (MVMThreadContext *tc, MVMString *filename, MVMint32 use_lstat) { \
        uv_stat_t statbuf; \
        if (file_info_with_error(tc, &statbuf, filename, use_lstat) < 0) \
            return 0; \
        else { \
            MVMint64 r = (statbuf.st_mode & S_I ## rwx ## OTH) \
                      || (statbuf.st_uid == geteuid() && (statbuf.st_mode & S_I ## rwx ## USR)) \
                      || (geteuid() == 0) \
                      || (are_we_group_member(tc, statbuf.st_gid) && (statbuf.st_mode & S_I ## rwx ## GRP)); \
            return r ? 1 : 0; \
        } \
    }
FILE_IS(readable, R)
FILE_IS(writable, W)
    MVMint64 MVM_file_isexecutable(MVMThreadContext *tc, MVMString *filename, MVMint32 use_lstat) {
        uv_stat_t statbuf;
        if (file_info_with_error(tc, &statbuf, filename, use_lstat) < 0)
            return 0;
        else {
            MVMint64 r = (statbuf.st_mode & S_IXOTH)
                      || (statbuf.st_uid == geteuid() && (statbuf.st_mode & S_IXUSR))
                      || (are_we_group_member(tc, statbuf.st_gid) && (statbuf.st_mode & S_IXGRP))
                      || (geteuid() == 0 && (statbuf.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH)));
            return r ? 1 : 0;
        }
    }
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
                if (!MVM_file_exists(tc, result, 1))
                    result = orig;
                else {
                    MVM_free(orig_cstr);
                    MVM_gc_root_temp_pop_n(tc, 2); /* orig and result */
                    return result;
                }
                lib_path_i++;
            }
            if (!result || !MVM_file_exists(tc, result, 1))
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
    uv_fs_req_cleanup(&req);

    return result;
}
