#include "moar.h"
#include "platform/io.h"

#ifndef _WIN32
#include <sys/wait.h>
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

#if MVM_HAS_READLINE
#ifdef __cplusplus
extern "C" {
#endif
    char *readline(const char *);
    void add_history(const char*);
#ifdef __cplusplus
}
#endif
#else
#include <linenoise.h>
#endif

static uv_stat_t file_info(MVMThreadContext *tc, MVMString *filename) {
    char * const a = MVM_string_utf8_encode_C_string(tc, filename);
    uv_fs_t req;

    uv_mutex_lock((uv_mutex_t *) tc->loop->data);
    if (uv_fs_lstat(tc->loop, &req, a, NULL) < 0) {
        uv_mutex_unlock((uv_mutex_t *) tc->loop->data);
        MVM_free(a);
        MVM_exception_throw_adhoc(tc, "Failed to stat file: %s", uv_strerror(req.result));
    }
    uv_mutex_unlock((uv_mutex_t *) tc->loop->data);

    MVM_free(a);
    return req.statbuf;
}

MVMint64 MVM_file_stat(MVMThreadContext *tc, MVMString *filename, MVMint64 status) {
    MVMint64 r = -1;

    switch (status) {
        case MVM_STAT_EXISTS:             r = MVM_file_exists(tc, filename); break;
        case MVM_STAT_FILESIZE: {
                char * const a = MVM_string_utf8_encode_C_string(tc, filename);
                uv_fs_t req;

                uv_mutex_lock((uv_mutex_t *) tc->loop->data);
                if (uv_fs_stat(tc->loop, &req, a, NULL) < 0) {
                    uv_mutex_unlock((uv_mutex_t *) tc->loop->data);
                    MVM_free(a);
                    MVM_exception_throw_adhoc(tc, "Failed to stat file: %s", uv_strerror(req.result));
                }
                uv_mutex_unlock((uv_mutex_t *) tc->loop->data);
                MVM_free(a);

                r = req.statbuf.st_size;
                break;
            }
        case MVM_STAT_ISDIR:              r = (file_info(tc, filename).st_mode & S_IFMT) == S_IFDIR; break;
        case MVM_STAT_ISREG:              r = (file_info(tc, filename).st_mode & S_IFMT) == S_IFREG; break;
        case MVM_STAT_ISDEV: {
            const int mode = file_info(tc, filename).st_mode;
#ifdef _WIN32
            r = mode & S_IFMT == S_IFCHR;
#else
            r = (mode & S_IFMT) == S_IFCHR || (mode & S_IFMT) == S_IFBLK;
#endif
            break;
        }
        case MVM_STAT_CREATETIME:         r = file_info(tc, filename).st_ctim.tv_sec; break;
        case MVM_STAT_ACCESSTIME:         r = file_info(tc, filename).st_atim.tv_sec; break;
        case MVM_STAT_MODIFYTIME:         r = file_info(tc, filename).st_mtim.tv_sec; break;
        case MVM_STAT_CHANGETIME:         r = file_info(tc, filename).st_ctim.tv_sec; break;
        case MVM_STAT_BACKUPTIME:         r = -1; break;
        case MVM_STAT_UID:                r = file_info(tc, filename).st_uid; break;
        case MVM_STAT_GID:                r = file_info(tc, filename).st_gid; break;
        case MVM_STAT_ISLNK:              r = (file_info(tc, filename).st_mode & S_IFMT) == S_IFLNK; break;
        case MVM_STAT_PLATFORM_DEV:       r = file_info(tc, filename).st_dev; break;
        case MVM_STAT_PLATFORM_INODE:     r = file_info(tc, filename).st_ino; break;
        case MVM_STAT_PLATFORM_MODE:      r = file_info(tc, filename).st_mode; break;
        case MVM_STAT_PLATFORM_NLINKS:    r = file_info(tc, filename).st_nlink; break;
        case MVM_STAT_PLATFORM_DEVTYPE:   r = file_info(tc, filename).st_rdev; break;
        case MVM_STAT_PLATFORM_BLOCKSIZE: r = file_info(tc, filename).st_blksize; break;
        case MVM_STAT_PLATFORM_BLOCKS:    r = file_info(tc, filename).st_blocks; break;
        default: break;
    }

    return r;
}

/* copy a file from one to another. */
void MVM_file_copy(MVMThreadContext *tc, MVMString *src, MVMString *dest) {
    uv_fs_t req;
    char *       const a = MVM_string_utf8_encode_C_string(tc, src);
    uv_mutex_lock((uv_mutex_t *) tc->loop->data);
    const uv_file  in_fd = uv_fs_open(tc->loop, &req, (const char *)a, O_RDONLY, 0, NULL);

    if (in_fd >= 0 && uv_fs_stat(tc->loop, &req, a, NULL) >= 0) {
        char *       const b = MVM_string_utf8_encode_C_string(tc, dest);
        const uv_file out_fd = uv_fs_open(tc->loop, &req, (const char *)b, O_WRONLY | O_CREAT | O_TRUNC, DEFAULT_MODE, NULL);
        MVM_free(a);

        if (out_fd >= 0
        && uv_fs_sendfile(tc->loop, &req, out_fd, in_fd, 0, req.statbuf.st_size, NULL) >= 0) {
            MVM_free(b);

            if (uv_fs_close(tc->loop, &req, in_fd, NULL) < 0) {
                uv_fs_close(tc->loop, &req, out_fd, NULL); /* should close out_fd before throw. */
                uv_mutex_unlock((uv_mutex_t *) tc->loop->data);
                MVM_exception_throw_adhoc(tc, "Failed to close file: %s", uv_strerror(req.result));
            }

            if (uv_fs_close(tc->loop, &req, out_fd, NULL) < 0) {
                uv_mutex_unlock((uv_mutex_t *) tc->loop->data);
                MVM_exception_throw_adhoc(tc, "Failed to close file: %s", uv_strerror(req.result));
            }

            uv_mutex_unlock((uv_mutex_t *) tc->loop->data);
            return;
        }
        else
            MVM_free(b);
    }
    else
        MVM_free(a);

    uv_mutex_unlock((uv_mutex_t *) tc->loop->data);
    MVM_exception_throw_adhoc(tc, "Failed to copy file: %s", uv_strerror(req.result));
}

/* rename one file to another. */
void MVM_file_rename(MVMThreadContext *tc, MVMString *src, MVMString *dest) {
    char * const a = MVM_string_utf8_encode_C_string(tc, src);
    char * const b = MVM_string_utf8_encode_C_string(tc, dest);
    uv_fs_t req;

    uv_mutex_lock((uv_mutex_t *) tc->loop->data);
    if(uv_fs_rename(tc->loop, &req, a, b, NULL) < 0 ) {
        uv_mutex_unlock((uv_mutex_t *) tc->loop->data);
        MVM_free(a);
        MVM_free(b);
        MVM_exception_throw_adhoc(tc, "Failed to rename file: %s", uv_strerror(req.result));
    }
    uv_mutex_unlock((uv_mutex_t *) tc->loop->data);

    MVM_free(a);
    MVM_free(b);
}

void MVM_file_delete(MVMThreadContext *tc, MVMString *f) {
    uv_fs_t req;
    char * const a = MVM_string_utf8_encode_C_string(tc, f);

#ifdef _WIN32
    const int r = MVM_platform_unlink(a);

    if( r < 0 && r != ENOENT) {
        MVM_free(a);
        MVM_exception_throw_adhoc(tc, "Failed to delete file: %d", errno);
    }

#else
    uv_mutex_lock((uv_mutex_t *) tc->loop->data);
    const int r = uv_fs_unlink(tc->loop, &req, a, NULL);
    uv_mutex_unlock((uv_mutex_t *) tc->loop->data);

    if( r < 0 && r != UV_ENOENT) {
        MVM_free(a);
        MVM_exception_throw_adhoc(tc, "Failed to delete file: %s", uv_strerror(req.result));
    }

#endif
    MVM_free(a);
}

void MVM_file_chmod(MVMThreadContext *tc, MVMString *f, MVMint64 flag) {
    char * const a = MVM_string_utf8_encode_C_string(tc, f);
    uv_fs_t req;

    uv_mutex_lock((uv_mutex_t *) tc->loop->data);
    if(uv_fs_chmod(tc->loop, &req, a, flag, NULL) < 0 ) {
        uv_mutex_unlock((uv_mutex_t *) tc->loop->data);
        MVM_free(a);
        MVM_exception_throw_adhoc(tc, "Failed to set permissions on path: %s", uv_strerror(req.result));
    }
    uv_mutex_unlock((uv_mutex_t *) tc->loop->data);

    MVM_free(a);
}

MVMint64 MVM_file_exists(MVMThreadContext *tc, MVMString *f) {
    uv_fs_t req;
    char * const a = MVM_string_utf8_encode_C_string(tc, f);
    uv_mutex_lock((uv_mutex_t *) tc->loop->data);
    const MVMint64 result = uv_fs_stat(tc->loop, &req, a, NULL) < 0 ? 0 : 1;
    uv_mutex_unlock((uv_mutex_t *) tc->loop->data);

    MVM_free(a);

    return result;
}

#ifdef _WIN32
#define FILE_IS(name, rwx) \
    MVMint64 MVM_file_is ## name (MVMThreadContext *tc, MVMString *filename) { \
        if (!MVM_file_exists(tc, filename)) \
            return 0; \
        else { \
            uv_stat_t statbuf = file_info(tc, filename); \
            MVMint64 r = (statbuf.st_mode & S_I ## rwx ); \
            return r ? 1 : 0; \
        } \
    }
FILE_IS(readable, READ)
FILE_IS(writable, WRITE)
MVMint64 MVM_file_isexecutable(MVMThreadContext *tc, MVMString *filename) {
    if (!MVM_file_exists(tc, filename))
        return 0;
    else {
        MVMint64 r = 0;
        uv_stat_t statbuf = file_info(tc, filename);
        if ((statbuf.st_mode & S_IFMT) == S_IFDIR)
            return 1;
        else {
            // true if fileext is in PATHEXT=.COM;.EXE;.BAT;.CMD;.VBS;.VBE;.JS;.JSE;.WSF;.WSH;.MSC
            MVMString *dot = MVM_string_ascii_decode_nt(tc, tc->instance->VMString, ".");
            MVMROOT(tc, dot, {
                MVMint64 n = MVM_string_index_from_end(tc, filename, dot, 0);
                if (n >= 0) {
                    MVMString *fileext = MVM_string_substring(tc, filename, n, -1);
                    char *ext  = MVM_string_utf8_encode_C_string(tc, fileext);
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
                    MVM_free(pext);
                }
            });
        }
        return r;
    }
}
#else
#define FILE_IS(name, rwx) \
    MVMint64 MVM_file_is ## name (MVMThreadContext *tc, MVMString *filename) { \
        if (!MVM_file_exists(tc, filename)) \
            return 0; \
        else { \
            uv_stat_t statbuf = file_info(tc, filename); \
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

/* Readline interactive; needs a re-visit soon. */
MVMString * MVM_file_readline_interactive_fh(MVMThreadContext *tc, MVMObject *oshandle, MVMString *prompt) {
    MVMString *return_str = NULL;
    char *line;
    char * const prompt_str = MVM_string_utf8_encode_C_string(tc, prompt);
    MVMOSHandle *h = (MVMOSHandle *) oshandle;
    MVMIOSyncStreamData *data = (MVMIOSyncStreamData *)h->body.data;

#if MVM_HAS_READLINE
    line = readline(prompt_str);

    MVM_free(prompt_str);

    if (line) {
        if (*line)
            add_history(line);

        return_str = MVM_string_decode(tc, tc->instance->VMString, line, strlen(line), MVM_encoding_type_utf8);

        MVM_free(line);
    } else {
        data->eof = 1;
    }

#else /* !MVM_HAS_READLINE */
    line = linenoise(prompt_str);

    MVM_free(prompt_str);

    if (line) {
        if (*line) {
            linenoiseHistoryAdd(line);
        }

        return_str = MVM_string_decode(tc, tc->instance->VMString, line, strlen(line), MVM_encoding_type_utf8);

        MVM_free(line);
    } else {
        data->eof = 1;
    }

#endif /* MVM_HAS_READLINE */

    return return_str;
}

/* Read all of a file into a string. */
MVMString * MVM_file_slurp(MVMThreadContext *tc, MVMString *filename, MVMString *encoding) {
    MVMString *mode = MVM_string_utf8_decode(tc, tc->instance->VMString, "r", 1);
    MVMObject *oshandle = (MVMObject *)MVM_file_open_fh(tc, filename, mode);
    MVMString *result;
    MVM_io_set_encoding(tc, oshandle, encoding);
    result = MVM_io_slurp(tc, oshandle);
    MVM_io_close(tc, oshandle);
    return result;
}

/* Writes a string to a file, overwriting it if necessary */
void MVM_file_spew(MVMThreadContext *tc, MVMString *output, MVMString *filename, MVMString *encoding) {
    MVMString *mode = MVM_string_utf8_decode(tc, tc->instance->VMString, "w", 1);
    MVMObject *fh = MVM_file_open_fh(tc, filename, mode);
    MVM_io_set_encoding(tc, fh, encoding);
    MVM_io_write_string(tc, fh, output, 0);
    MVM_io_close(tc, fh);
}

/* return an OSHandle representing one of the standard streams */
MVMObject * MVM_file_get_stdstream(MVMThreadContext *tc, MVMuint8 type, MVMuint8 readable) {
    switch(uv_guess_handle(type)) {
        case UV_TTY: {
            uv_tty_t * const handle = MVM_malloc(sizeof(uv_tty_t));
            uv_mutex_lock((uv_mutex_t *) tc->loop->data);
            uv_tty_init(tc->loop, handle, type, readable);
            uv_mutex_unlock((uv_mutex_t *) tc->loop->data);
#ifdef _WIN32
            uv_stream_set_blocking((uv_stream_t *)handle, 1);
#else
            ((uv_stream_t *)handle)->flags = 0x80; /* UV_STREAM_BLOCKING */
#endif
            return MVM_io_syncstream_from_uvstream(tc, (uv_stream_t *)handle);
        }
        case UV_FILE:
            return MVM_file_handle_from_fd(tc, type);
        case UV_NAMED_PIPE: {
            uv_pipe_t * const handle = MVM_malloc(sizeof(uv_pipe_t));
            uv_mutex_lock((uv_mutex_t *) tc->loop->data);
            uv_pipe_init(tc->loop, handle, 0);
            uv_mutex_unlock((uv_mutex_t *) tc->loop->data);
#ifdef _WIN32
            uv_stream_set_blocking((uv_stream_t *)handle, 1);
#else
            ((uv_stream_t *)handle)->flags = 0x80; /* UV_STREAM_BLOCKING */
#endif
            uv_pipe_open(handle, type);
            return MVM_io_syncstream_from_uvstream(tc, (uv_stream_t *)handle);
        }
        default:
            MVM_exception_throw_adhoc(tc, "get_stream failed, unsupported std handle");
    }
}

/* Takes a filename and prepends any --libpath value we have, if it's not an
 * absolute path. */
MVMString * MVM_file_in_libpath(MVMThreadContext *tc, MVMString *orig) {
    const char **lib_path = tc->instance->lib_path;
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&orig);
    if (lib_path) {
        /* We actually have a lib_path to consider. See if the filename is
         * absolute (XXX wants a platform abstraction, and doing better). */
        char *orig_cstr = MVM_string_utf8_encode_C_string(tc, orig);
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
                result = MVM_string_utf8_decode(tc, tc->instance->VMString, new_path, new_len);
                MVM_free(new_path);
                if (!MVM_file_exists(tc, result))
                    result = orig;
                else {
                    MVM_free(orig_cstr);
                    MVM_gc_root_temp_pop_n(tc, 2); /* orig and result */
                    return result;
                }
                lib_path_i++;
            }
            if (!result || !MVM_file_exists(tc, result))
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
    char * const oldpath_s = MVM_string_utf8_encode_C_string(tc, oldpath);
    char * const newpath_s = MVM_string_utf8_encode_C_string(tc, newpath);

    uv_mutex_lock((uv_mutex_t *) tc->loop->data);
    if (uv_fs_link(tc->loop, &req, oldpath_s, newpath_s, NULL)) {
        uv_mutex_unlock((uv_mutex_t *) tc->loop->data);
        MVM_free(oldpath_s);
        MVM_free(newpath_s);
        MVM_exception_throw_adhoc(tc, "Failed to link file: %s", uv_strerror(req.result));
    }
    uv_mutex_unlock((uv_mutex_t *) tc->loop->data);

    MVM_free(oldpath_s);
    MVM_free(newpath_s);
}

void MVM_file_symlink(MVMThreadContext *tc, MVMString *oldpath, MVMString *newpath) {
    uv_fs_t req;
    char * const oldpath_s = MVM_string_utf8_encode_C_string(tc, oldpath);
    char * const newpath_s = MVM_string_utf8_encode_C_string(tc, newpath);

    uv_mutex_lock((uv_mutex_t *) tc->loop->data);
    if (uv_fs_symlink(tc->loop, &req, oldpath_s, newpath_s, 0, NULL)) {
        uv_mutex_unlock((uv_mutex_t *) tc->loop->data);
        MVM_free(oldpath_s);
        MVM_free(newpath_s);
        MVM_exception_throw_adhoc(tc, "Failed to symlink file: %s", uv_strerror(req.result));
    }
    uv_mutex_unlock((uv_mutex_t *) tc->loop->data);

    MVM_free(oldpath_s);
    MVM_free(newpath_s);
}
