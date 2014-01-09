#include "moar.h"
#include "platform/io.h"

#ifndef _WIN32
#include <sys/types.h>
#include <unistd.h>
#define DEFAULT_MODE 0x0FFF
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

static void verify_filehandle_type(MVMThreadContext *tc, MVMObject *oshandle, MVMOSHandle **handle, const char *msg) {
    /* work on only MVMOSHandle of type MVM_OSHANDLE_FILE */
    if (REPR(oshandle)->ID != MVM_REPR_ID_MVMOSHandle) {
        MVM_exception_throw_adhoc(tc, "%s requires an object with REPR MVMOSHandle", msg);
    }
    *handle = (MVMOSHandle *)oshandle;
    if ((*handle)->body.type != MVM_OSHANDLE_FD && (*handle)->body.type != MVM_OSHANDLE_HANDLE) {
        MVM_exception_throw_adhoc(tc, "%s requires an MVMOSHandle of type file handle", msg);
    }
}

static uv_stat_t file_info(MVMThreadContext *tc, MVMString *filename) {
    char * const a = MVM_string_utf8_encode_C_string(tc, filename);
    uv_fs_t req;

    if (uv_fs_lstat(tc->loop, &req, a, NULL) < 0) {
        free(a);
        MVM_exception_throw_adhoc(tc, "Failed to stat file: %s", uv_strerror(req.result));
    }

    free(a);
    return req.statbuf;
}

MVMint64 MVM_file_stat(MVMThreadContext *tc, MVMString *filename, MVMint64 status) {
    MVMint64 r = -1;

    switch (status) {
        case MVM_stat_exists:             r = MVM_file_exists(tc, filename); break;
        case MVM_stat_filesize:           r = file_info(tc, filename).st_size; break;
        case MVM_stat_isdir:              r = (file_info(tc, filename).st_mode & S_IFMT) == S_IFDIR; break;
        case MVM_stat_isreg:              r = (file_info(tc, filename).st_mode & S_IFMT) == S_IFREG; break;
        case MVM_stat_isdev: {
            const int mode = file_info(tc, filename).st_mode;
#ifdef _WIN32
            r = mode & S_IFMT == S_IFCHR;
#else
            r = (mode & S_IFMT) == S_IFCHR || (mode & S_IFMT) == S_IFBLK;
#endif
            break;
        }
        case MVM_stat_createtime:         r = file_info(tc, filename).st_ctim.tv_sec; break;
        case MVM_stat_accesstime:         r = file_info(tc, filename).st_atim.tv_sec; break;
        case MVM_stat_modifytime:         r = file_info(tc, filename).st_mtim.tv_sec; break;
        case MVM_stat_changetime:         r = file_info(tc, filename).st_ctim.tv_sec; break;
        case MVM_stat_backuptime:         r = -1; break;
        case MVM_stat_uid:                r = file_info(tc, filename).st_uid; break;
        case MVM_stat_gid:                r = file_info(tc, filename).st_gid; break;
        case MVM_stat_islnk:              r = (file_info(tc, filename).st_mode & S_IFMT) == S_IFLNK; break;
        case MVM_stat_platform_dev:       r = file_info(tc, filename).st_dev; break;
        case MVM_stat_platform_inode:     r = file_info(tc, filename).st_ino; break;
        case MVM_stat_platform_mode:      r = file_info(tc, filename).st_mode; break;
        case MVM_stat_platform_nlinks:    r = file_info(tc, filename).st_nlink; break;
        case MVM_stat_platform_devtype:   r = file_info(tc, filename).st_rdev; break;
        case MVM_stat_platform_blocksize: r = file_info(tc, filename).st_blksize; break;
        case MVM_stat_platform_blocks:    r = file_info(tc, filename).st_blocks; break;
        default: break;
    }

    return r;
}

/* copy a file from one to another. */
void MVM_file_copy(MVMThreadContext *tc, MVMString *src, MVMString *dest) {
    uv_fs_t req;
    char *       const a = MVM_string_utf8_encode_C_string(tc, src);
    char *       const b = MVM_string_utf8_encode_C_string(tc, dest);
    const uv_file  in_fd = uv_fs_open(tc->loop, &req, (const char *)a, O_RDONLY, 0, NULL);
    const uv_file out_fd = uv_fs_open(tc->loop, &req, (const char *)b, O_WRONLY | O_CREAT | O_TRUNC, DEFAULT_MODE, NULL);

    if (in_fd >= 0 && out_fd >= 0
        && uv_fs_stat(tc->loop, &req, a, NULL) >= 0
        && uv_fs_sendfile(tc->loop, &req, out_fd, in_fd, 0, req.statbuf.st_size, NULL) >= 0) {
        free(a);
        free(b);

        if (uv_fs_close(tc->loop, &req, in_fd, NULL) < 0) {
            uv_fs_close(tc->loop, &req, out_fd, NULL); /* should close out_fd before throw. */
            MVM_exception_throw_adhoc(tc, "Failed to close file: %s", uv_strerror(req.result));
        }

        if (uv_fs_close(tc->loop, &req, out_fd, NULL) < 0) {
            MVM_exception_throw_adhoc(tc, "Failed to close file: %s", uv_strerror(req.result));
        }

        return;
    }

    free(a);
    free(b);

    MVM_exception_throw_adhoc(tc, "Failed to copy file: %s", uv_strerror(req.result));
}

/* rename one file to another. */
void MVM_file_rename(MVMThreadContext *tc, MVMString *src, MVMString *dest) {
    char * const a = MVM_string_utf8_encode_C_string(tc, src);
    char * const b = MVM_string_utf8_encode_C_string(tc, dest);
    uv_fs_t req;

    if(uv_fs_rename(tc->loop, &req, a, b, NULL) < 0 ) {
        free(a);
        free(b);
        MVM_exception_throw_adhoc(tc, "Failed to rename file: %s", uv_strerror(req.result));
    }

    free(a);
    free(b);
}

void MVM_file_delete(MVMThreadContext *tc, MVMString *f) {
    uv_fs_t req;
    char * const a = MVM_string_utf8_encode_C_string(tc, f);
    const int    r = uv_fs_unlink(tc->loop, &req, a, NULL);

    if( r < 0 && r != UV_ENOENT) {
        free(a);
        MVM_exception_throw_adhoc(tc, "Failed to delete file: %s", uv_strerror(req.result));
    }

    free(a);
}

void MVM_file_chmod(MVMThreadContext *tc, MVMString *f, MVMint64 flag) {
    char * const a = MVM_string_utf8_encode_C_string(tc, f);
    uv_fs_t req;

    if(uv_fs_chmod(tc->loop, &req, a, flag, NULL) < 0 ) {
        free(a);
        MVM_exception_throw_adhoc(tc, "Failed to set permissions on path: %s", uv_strerror(req.result));
    }

    free(a);
}

MVMint64 MVM_file_exists(MVMThreadContext *tc, MVMString *f) {
    uv_fs_t req;
    char * const a = MVM_string_utf8_encode_C_string(tc, f);
    const MVMint64 result = uv_fs_stat(tc->loop, &req, a, NULL) < 0 ? 0 : 1;

    free(a);

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
                    MVMROOT(tc, fileext, {
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
                        free(ext);
                        free(pext);
                    });
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

/* open a filehandle; takes a type object */
MVMObject * MVM_file_open_fh(MVMThreadContext *tc, MVMString *filename, MVMString *mode) {
    char            * const fname = MVM_string_utf8_encode_C_string(tc, filename);
    char            * const fmode = MVM_string_utf8_encode_C_string(tc, mode);
    MVMOSHandle    * const result = (MVMOSHandle *)MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTIO);
    uv_fs_t req;
    int flag;

    if (0 == strcmp("r", fmode))
        flag = O_RDONLY;
    else if (0 == strcmp("w", fmode))
        flag = O_CREAT| O_WRONLY | O_TRUNC;
    else if (0 == strcmp("wa", fmode))
        flag = O_CREAT | O_WRONLY | O_APPEND;
    else {
        free(fname);
        MVM_exception_throw_adhoc(tc, "Invalid open mode: %d", fmode);
    }

    free(fmode);

    if ((result->body.u.fd = uv_fs_open(tc->loop, &req, (const char *)fname, flag, DEFAULT_MODE, NULL)) < 0) {
        free(fname);
        MVM_exception_throw_adhoc(tc, "Failed to open file: %s", uv_strerror(req.result));
    }

    result->body.filename = fname;
    result->body.type = MVM_OSHANDLE_FD;
    result->body.encoding_type = MVM_encoding_type_utf8;

    return (MVMObject *)result;
}

void MVM_file_close_fh(MVMThreadContext *tc, MVMObject *oshandle) {
    MVMOSHandle *handle;
    uv_fs_t req;

    verify_filehandle_type(tc, oshandle, &handle, "close filehandle");

    MVM_checked_free_null(handle->body.filename);

    if (uv_fs_close(tc->loop, &req, handle->body.u.fd, NULL) < 0) {
        MVM_exception_throw_adhoc(tc, "Failed to close filehandle: %s", uv_strerror(req.result));
    }
}

/* reads a line from a filehandle. */
MVMString * MVM_file_readline_fh(MVMThreadContext *tc, MVMObject *oshandle) {
    MVMint32 bytes_read = 0;
    MVMint32 step_back  = 0; /* total reads = bytes_read + step_back */
    MVMString *result;
    MVMOSHandle *handle;
    uv_fs_t req;
    char ch;
    char *buf;

    verify_filehandle_type(tc, oshandle, &handle, "readline from filehandle");

    while (uv_fs_read(tc->loop, &req, handle->body.u.fd, &ch, 1, -1, NULL) > 0) {
        bytes_read++;

        if (ch == 10 || ch == 13)
            break;
    }

    /* have a look if it is a windows newline. */
    if (ch == 13) {
        if (uv_fs_read(tc->loop, &req, handle->body.u.fd, &ch, 1, -1, NULL) > 0 && ch == 10) {
            bytes_read++;
        } else {
            step_back++;
        }
    }

    MVM_file_seek(tc, oshandle, -bytes_read - step_back, SEEK_CUR);

    buf = malloc(bytes_read);

    if (uv_fs_read(tc->loop, &req, handle->body.u.fd, buf, bytes_read, -1, NULL) < 0) {
        free(buf);
        MVM_exception_throw_adhoc(tc, "readline from filehandle failed: %s", uv_strerror(req.result));
    }

                                               /* XXX should this take a type object? */
    result = MVM_string_decode(tc, tc->instance->VMString, buf, bytes_read, handle->body.encoding_type);
    free(buf);

    return result;
}

/* reads a line from a filehandle. */
MVMString * MVM_file_readline_interactive_fh(MVMThreadContext *tc, MVMObject *oshandle, MVMString *prompt) {
    MVMString *return_str = NULL;
    MVMOSHandle *handle;
    char *line;
    char * const prompt_str = MVM_string_utf8_encode_C_string(tc, prompt);

    verify_filehandle_type(tc, oshandle, &handle, "read from filehandle");

#if MVM_HAS_READLINE
    line = readline(prompt_str);

    free(prompt_str);

    if (line) {
        if (*line)
            add_history(line);

        return_str = MVM_string_decode(tc, tc->instance->VMString, line, strlen(line), handle->body.encoding_type);

        free(line);
    }

#else /* !MVM_HAS_READLINE */
    line = linenoise(prompt_str);

    free(prompt_str);

    if (line) {
        if (*line) {
            linenoiseHistoryAdd(line);
        }

        return_str = MVM_string_decode(tc, tc->instance->VMString, line, strlen(line), handle->body.encoding_type);

        free(line);
    }
#endif /* MVM_HAS_READLINE */

    return return_str;
}

static void tty_on_alloc(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf) {
    const MVMint64 length = ((MVMOSHandle *)handle->data)->body.u.length;

    buf->base = malloc(length);
    buf->len = length;
}

static void tty_on_read(uv_stream_t *handle, ssize_t nread, const uv_buf_t *buf) {
    MVMOSHandle * const oshandle = (MVMOSHandle *)(handle->data);

    oshandle->body.u.data = buf->base;
    oshandle->body.u.length = buf->len;
}

/* reads a string from a filehandle. */
MVMString * MVM_file_read_fhs(MVMThreadContext *tc, MVMObject *oshandle, MVMint64 length) {
    MVMString *result;
    MVMOSHandle *handle;
    MVMint64 bytes_read;
    uv_fs_t req;
    char *buf;

    verify_filehandle_type(tc, oshandle, &handle, "read from filehandle");

    /* XXX TODO length currently means codepoints. Alter it to mean graphemes when we have NFG. */
    if (handle->body.encoding_type == MVM_encoding_type_utf16)
        length *= 2;
    
    /* A length of -1 means to read to EOF */
    if (length == -1) {
        MVMint64 seek_pos;
        MVMint64 r;
        uv_fs_t req;

        if ((r = uv_fs_lstat(tc->loop, &req, handle->body.filename, NULL)) == -1) {
            MVM_exception_throw_adhoc(tc, "Failed to stat in filehandle: %d", errno);
        }

        if ((seek_pos = MVM_platform_lseek(handle->body.u.fd, 0, SEEK_CUR)) == -1) {
            MVM_exception_throw_adhoc(tc, "Failed to seek in filehandle: %d", errno);
        }

        length = req.statbuf.st_size - seek_pos;
    }
    
    /* We must scan how many bytes the requested codepoint count takes. */
    if (handle->body.encoding_type == MVM_encoding_type_utf8) {
        unsigned char ch;
        MVMint64 cp_have  = 0;
        MVMint64 cp_want  = length;
        length            = 0; /* length means bytes from here */

        while (cp_have < cp_want && uv_fs_read(tc->loop, &req, handle->body.u.fd, &ch, 1, -1, NULL) > 0) {
            if (ch >> 7 == 0) {
                length++;
            }
            else if (ch >> 5 == 0b110) {
                length += 2;
                MVM_platform_lseek(handle->body.u.fd, 1, SEEK_CUR);
            }
            else if (ch >> 4 == 0b1110) {
                length += 3;
                MVM_platform_lseek(handle->body.u.fd, 2, SEEK_CUR);
            }
            else if (ch >> 3 == 0b11110) {
                length += 4;
                MVM_platform_lseek(handle->body.u.fd, 3, SEEK_CUR);
            }
            else {
                MVM_exception_throw_adhoc(tc, "Malformed character in UTF-8 string: %#x", ch);
            }
            cp_have++;
        }

        if (length > 0)
            MVM_platform_lseek(handle->body.u.fd, -length, SEEK_CUR);
    }

    if (length < 0 || length > 99999999) {
        MVM_exception_throw_adhoc(tc, "read from filehandle length out of range: %d", length);
    }

    switch (handle->body.type) {
        case MVM_OSHANDLE_HANDLE: {
            MVMOSHandleBody * const body = &handle->body;
            body->u.length = length;
            uv_read_start((uv_stream_t *)body->u.handle, tty_on_alloc, tty_on_read);
            buf = body->u.data;
            bytes_read = body->u.length;
            break;
        }
        case MVM_OSHANDLE_FD:
            buf = malloc(length);
            bytes_read = uv_fs_read(tc->loop, &req, handle->body.u.fd, buf, length, -1, NULL);
            break;
        default:
            break;
    }

    if (bytes_read < 0) {
        free(buf);
        MVM_exception_throw_adhoc(tc, "Read from filehandle failed: %s", uv_strerror(req.result));
    }

                                               /* XXX should this take a type object? */
    result = MVM_string_decode(tc, tc->instance->VMString, buf, bytes_read, handle->body.encoding_type);

    free(buf);

    return result;
}

/* reads a buf from a filehandle. */
void MVM_file_read_fhb(MVMThreadContext *tc, MVMObject *oshandle, MVMObject *result, MVMint64 length) {
    MVMOSHandle *handle;
    MVMint64 bytes_read;
    uv_fs_t req;
    char *buf;

    /* XXX TODO handle length == -1 to mean read to EOF */

    verify_filehandle_type(tc, oshandle, &handle, "read from filehandle");

    /* Ensure the target is in the correct form. */
    if (!IS_CONCRETE(result) || REPR(result)->ID != MVM_REPR_ID_MVMArray)
        MVM_exception_throw_adhoc(tc, "read_fhb requires a native array to write to");
    if (((MVMArrayREPRData *)STABLE(result)->REPR_data)->slot_type != MVM_ARRAY_I8)
        MVM_exception_throw_adhoc(tc, "read_fhb requires a native array of int8");

    if (length < 1 || length > 99999999) {
        MVM_exception_throw_adhoc(tc, "read from filehandle length out of range");
    }

    switch (handle->body.type) {
        case MVM_OSHANDLE_HANDLE: {
            MVMOSHandleBody * const body = &handle->body;
            body->u.length = length;
            uv_read_start((uv_stream_t *)body->u.handle, tty_on_alloc, tty_on_read);
            buf = body->u.data;
            bytes_read = body->u.length;
            break;
        }
        case MVM_OSHANDLE_FD:
            buf = malloc(length);
            bytes_read = uv_fs_read(tc->loop, &req, handle->body.u.fd, buf, length, -1, NULL);
            break;
        default:
            break;
    }

    if (bytes_read < 0) {
        free(buf);
        MVM_exception_throw_adhoc(tc, "Read from filehandle failed: %s", uv_strerror(req.result));
    }

    /* Stash the data in the VMArray. */
    ((MVMArray *)result)->body.slots.i8 = (MVMint8 *)buf;
    ((MVMArray *)result)->body.start    = 0;
    ((MVMArray *)result)->body.ssize    = bytes_read;
    ((MVMArray *)result)->body.elems    = bytes_read;
}

/* read all of a filehandle into a string. */
MVMString * MVM_file_readall_fh(MVMThreadContext *tc, MVMObject *oshandle) {
    MVMString *result;
    MVMOSHandle *handle;
    MVMint64 file_size;
    MVMint64 bytes_read;
    uv_fs_t req;
    char *buf;

    /* XXX TODO length currently means bytes. alter it to mean graphemes. */
    /* XXX TODO handle length == -1 to mean read to EOF */

    verify_filehandle_type(tc, oshandle, &handle, "Readall from filehandle");

    if (uv_fs_fstat(tc->loop, &req, handle->body.u.fd, NULL) < 0) {
        MVM_exception_throw_adhoc(tc, "Readall from filehandle failed: %s", uv_strerror(req.result));
    }

    file_size = req.statbuf.st_size;

    if (file_size > 0) {
        buf = malloc(file_size);

        bytes_read = uv_fs_read(tc->loop, &req, handle->body.u.fd, buf, file_size, -1, NULL);
        if (bytes_read < 0) {
            free(buf);
            MVM_exception_throw_adhoc(tc, "Readall from filehandle failed: %s", uv_strerror(req.result));
        }
                                                   /* XXX should this take a type object? */
        result = MVM_string_decode(tc, tc->instance->VMString, buf, bytes_read, handle->body.encoding_type);
        free(buf);
    }
    else {
        result = (MVMString *)REPR(tc->instance->VMString)->allocate(tc, STABLE(tc->instance->VMString));
    }

    return result;
}

/* read all of a file into a string */
MVMString * MVM_file_slurp(MVMThreadContext *tc, MVMString *filename, MVMString *encoding) {
    MVMString *mode = MVM_string_utf8_decode(tc, tc->instance->VMString, "r", 1);
    MVMObject *oshandle = (MVMObject *)MVM_file_open_fh(tc, filename, mode);
    MVMString *result;
    MVM_file_set_encoding(tc, oshandle, encoding);
    result = MVM_file_readall_fh(tc, oshandle);
    MVM_file_close_fh(tc, oshandle);

    return result;
}

static void write_cb(uv_write_t* req, int status) {
    uv_unref((uv_handle_t *)req);
    free(req);
}

/* writes a string to a filehandle. */
MVMint64 MVM_file_write_fhs(MVMThreadContext *tc, MVMObject *oshandle, MVMString *str, MVMint8 addnl) {
    MVMOSHandle *handle;
    MVMuint8 *output;
    MVMint64 output_size;
    MVMint64 bytes_written;

    if (str == NULL)
        MVM_exception_throw_adhoc(tc, "Failed to write to filehandle: NULL string given");
    verify_filehandle_type(tc, oshandle, &handle, "write to filehandle");

    output = MVM_string_encode(tc, str, 0, -1, &output_size, handle->body.encoding_type);

    if (addnl) {
        output = (MVMuint8 *)realloc(output, ++output_size);
        output[output_size - 1] = '\n';
    }

    switch (handle->body.type) {
        case MVM_OSHANDLE_HANDLE: {
            uv_write_t *req = malloc(sizeof(uv_write_t));
            uv_buf_t buf = uv_buf_init(output, bytes_written = output_size);
            int r;
            if ((r = uv_write(req, (uv_stream_t *)handle->body.u.handle, &buf, 1, write_cb)) < 0) {
                if (req) free(req);
                free(output);
                MVM_exception_throw_adhoc(tc, "Failed to write bytes to filehandle: %s", uv_strerror(r));
            }
            else {
                uv_unref((uv_handle_t *)req);
                uv_run(tc->loop, UV_RUN_DEFAULT);
                free(output);
            }
            break;
        }
        case MVM_OSHANDLE_FD: {
            uv_fs_t req;
            bytes_written = uv_fs_write(tc->loop, &req, handle->body.u.fd, (const void *)output, output_size, -1, NULL);
            if (bytes_written < 0) {
                free(output);
                MVM_exception_throw_adhoc(tc, "Failed to write bytes to filehandle: %s", uv_strerror(req.result));
            }
            free(output);
            break;
        }
        default:
            break;
    }

    return bytes_written;
}

void MVM_file_write_fhb(MVMThreadContext *tc, MVMObject *oshandle, MVMObject *buffer) {
    MVMOSHandle *handle;
    MVMuint8 *output;
    MVMint64 output_size;
    MVMint64 bytes_written;

    verify_filehandle_type(tc, oshandle, &handle, "write to filehandle");

    /* Ensure the target is in the correct form. */
    if (!IS_CONCRETE(buffer) || REPR(buffer)->ID != MVM_REPR_ID_MVMArray)
        MVM_exception_throw_adhoc(tc, "write_fhb requires a native array to read from");
    if (((MVMArrayREPRData *)STABLE(buffer)->REPR_data)->slot_type != MVM_ARRAY_I8)
        MVM_exception_throw_adhoc(tc, "write_fhb requires a native array of int8");

    output = ((MVMArray *)buffer)->body.slots.i8;
    output_size = ((MVMArray *)buffer)->body.elems;

    switch (handle->body.type) {
        case MVM_OSHANDLE_HANDLE: {
            uv_write_t *req = malloc(sizeof(uv_write_t));
            uv_buf_t buf = uv_buf_init(output, bytes_written = output_size);
            int r;

            if ((r = uv_write(req, (uv_stream_t *)handle->body.u.handle, &buf, 1, write_cb)) < 0) {
                free(req);
                MVM_exception_throw_adhoc(tc, "Failed to write bytes to filehandle: %s", uv_strerror(r));
            }
            break;
        }
        case MVM_OSHANDLE_FD: {
            uv_fs_t req;
            bytes_written = uv_fs_write(tc->loop, &req, handle->body.u.fd, (const void *)output, output_size, -1, NULL);
            if (bytes_written < 0) {
                MVM_exception_throw_adhoc(tc, "Failed to write bytes to filehandle: %s", uv_strerror(req.result));
            }
            break;
        }
        default:
            break;
    }
}

/* writes a string to a file, overwriting it if necessary */
void MVM_file_spew(MVMThreadContext *tc, MVMString *output, MVMString *filename, MVMString *encoding) {
    MVMString *mode = MVM_string_utf8_decode(tc, tc->instance->VMString, "w", 1);
    MVMObject *fh = MVM_file_open_fh(tc, filename, mode);
    MVM_file_set_encoding(tc, fh, encoding);
    MVM_file_write_fhs(tc, fh, output, 0);
    MVM_file_close_fh(tc, fh);
    /* XXX need to GC free the filehandle? */
}

/* seeks in a filehandle */
void MVM_file_seek(MVMThreadContext *tc, MVMObject *oshandle, MVMint64 offset, MVMint64 flag) {
    MVMOSHandle *handle;

    verify_filehandle_type(tc, oshandle, &handle, "seek in filehandle");

    if (MVM_platform_lseek(handle->body.u.fd, offset, flag) == -1) {
        MVM_exception_throw_adhoc(tc, "Failed to seek in filehandle: %d", errno);
    }
}

/* tells position within file */
MVMint64 MVM_file_tell_fh(MVMThreadContext *tc, MVMObject *oshandle) {
    MVMOSHandle *handle;
    MVMint64 r;

    verify_filehandle_type(tc, oshandle, &handle, "seek in filehandle");

    if ((r = MVM_platform_lseek(handle->body.u.fd, 0, SEEK_CUR)) == -1) {
        MVM_exception_throw_adhoc(tc, "Failed to seek in filehandle: %d", errno);
    }

    return r;
}

/* locks a filehandle */
MVMint64 MVM_file_lock(MVMThreadContext *tc, MVMObject *oshandle, MVMint64 flag) {
    MVMOSHandle *handle;
#ifdef _WIN32
    const DWORD len = 0xffffffff;
    HANDLE hf;
    OVERLAPPED offset;
#else
  struct flock l;
  ssize_t r;
  int fc;
  const int fd = handle->body.u.fd;
#endif

    verify_filehandle_type(tc, oshandle, &handle, "lock filehandle");

#ifdef _WIN32
    hf = (HANDLE)_get_osfhandle(handle->body.u.fd);
    if (hf == INVALID_HANDLE_VALUE) {
        MVM_exception_throw_adhoc(tc, "Failed to seek in filehandle: bad file descriptor");
    }

    flag = ((flag & MVM_FILE_FLOCK_NONBLOCK) ? LOCKFILE_FAIL_IMMEDIATELY : 0)
          + ((flag & MVM_FILE_FLOCK_TYPEMASK) == MVM_FILE_FLOCK_SHARED
                                       ? 0 : LOCKFILE_EXCLUSIVE_LOCK);

    memset (&offset, 0, sizeof(offset));
    if (LockFileEx(hf, flag, 0, len, len, &offset)) {
        return 1;
    }

    MVM_exception_throw_adhoc(tc, "Failed to unlock filehandle: %d", GetLastError());

    return 0;
#else
    l.l_whence = SEEK_SET;
    l.l_start = 0;
    l.l_len = 0;

    if ((flag & MVM_FILE_FLOCK_TYPEMASK) == MVM_FILE_FLOCK_SHARED)
        l.l_type = F_RDLCK;
    else
        l.l_type = F_WRLCK;

    fc = (flag & MVM_FILE_FLOCK_NONBLOCK) ? F_SETLK : F_SETLKW;

    do {
        r = fcntl(fd, fc, &l);
    } while (r == -1 && errno == EINTR);

    if (r == -1) {
        MVM_exception_throw_adhoc(tc, "Failed to unlock filehandle: %d", errno);
        return 0;
    }

    return 1;
#endif
}

/* unlocks a filehandle */
void MVM_file_unlock(MVMThreadContext *tc, MVMObject *oshandle) {
    MVMOSHandle *handle;
#ifdef _WIN32
    const DWORD len = 0xffffffff;
    HANDLE hf;
    OVERLAPPED offset;
#else
  struct flock l;
  ssize_t r;
  const int fd = handle->body.u.fd;
#endif

    verify_filehandle_type(tc, oshandle, &handle, "unlock filehandle");

#ifdef _WIN32
    hf = (HANDLE)_get_osfhandle(handle->body.u.fd);
    if (hf == INVALID_HANDLE_VALUE) {
        MVM_exception_throw_adhoc(tc, "Failed to seek in filehandle: bad file descriptor");
    }

    memset (&offset, 0, sizeof(offset));
    if (UnlockFileEx(hf, 0, len, len, &offset)) {
        return;
    }

    MVM_exception_throw_adhoc(tc, "Failed to unlock filehandle: %d", GetLastError());
#else

    l.l_whence = SEEK_SET;
    l.l_start = 0;
    l.l_len = 0;
    l.l_type = F_UNLCK;

    do {
        r = fcntl(fd, F_SETLKW, &l);
    } while (r == -1 && errno == EINTR);

    if (r == -1) {
        MVM_exception_throw_adhoc(tc, "Failed to unlock filehandle: %d", errno);
    }
#endif
}

/* syncs a filehandle (Transfer all file modified data and metadata to disk.) */
void MVM_file_sync(MVMThreadContext *tc, MVMObject *oshandle) {
    MVMOSHandle *handle;
    uv_fs_t req;

    verify_filehandle_type(tc, oshandle, &handle, "sync filehandle");

    if(uv_fs_fsync(tc->loop, &req, handle->body.u.fd, NULL) < 0 ) {
        MVM_exception_throw_adhoc(tc, "Failed to sync filehandle: %s", uv_strerror(req.result));
    }
}

/* syncs a filehandle (Transfer all file modified data and metadata to disk.) */
void MVM_file_truncate(MVMThreadContext *tc, MVMObject *oshandle, MVMint64 offset) {
    MVMOSHandle *handle;
    uv_fs_t req;

    verify_filehandle_type(tc, oshandle, &handle, "truncate filehandle");

    if(uv_fs_ftruncate(tc->loop, &req, handle->body.u.fd, offset, NULL) < 0 ) {
        MVM_exception_throw_adhoc(tc, "Failed to truncate filehandle: %s", uv_strerror(req.result));
    }
}

/* return an OSHandle representing one of the standard streams */
MVMObject * MVM_file_get_stdstream(MVMThreadContext *tc, MVMuint8 type, MVMuint8 readable) {
    MVMObject * const type_object = tc->instance->boot_types.BOOTIO;
    MVMOSHandle * const    result = (MVMOSHandle *)REPR(type_object)->allocate(tc, STABLE(type_object));
    MVMOSHandleBody * const body  = &result->body;

    switch(uv_guess_handle(type)) {
        case UV_TTY: {
            uv_tty_t * const handle = malloc(sizeof(uv_tty_t));
            uv_tty_init(tc->loop, handle, type, readable);
#ifdef _WIN32
            uv_stream_set_blocking((uv_stream_t *)handle, 1);
#else
            ((uv_stream_t *)handle)->flags = 0x80; /* UV_STREAM_BLOCKING */
#endif
            body->u.handle = (uv_handle_t *)handle;
            body->u.handle->data = result;       /* this is needed in tty_on_read function. */
            body->type = MVM_OSHANDLE_HANDLE;
            break;
        }
        case UV_FILE:
            body->u.fd     = type;
            body->type     = MVM_OSHANDLE_FD;
            body->filename = NULL;
            break;
        case UV_NAMED_PIPE: {
            uv_pipe_t * const handle = malloc(sizeof(uv_pipe_t));
            uv_pipe_init(tc->loop, handle, 0);
#ifdef _WIN32
            uv_stream_set_blocking((uv_stream_t *)handle, 1);
#else
            ((uv_stream_t *)handle)->flags = 0x80; /* UV_STREAM_BLOCKING */
#endif
            uv_pipe_open(handle, type);
            body->u.handle = (uv_handle_t *)handle;
            body->u.handle->data = result;
            body->type = MVM_OSHANDLE_HANDLE;
            break;
        }
        default:
            MVM_exception_throw_adhoc(tc, "get_stream failed, unsupported std handle");
            break;
    }

    body->encoding_type = MVM_encoding_type_utf8;
    return (MVMObject *)result;
}

MVMint64 MVM_file_eof(MVMThreadContext *tc, MVMObject *oshandle) {
    MVMOSHandle *handle;
    MVMint64 r;
    MVMint64 seek_pos;
    uv_fs_t req;

    verify_filehandle_type(tc, oshandle, &handle, "check eof");

    if ((r = uv_fs_lstat(tc->loop, &req, handle->body.filename, NULL)) == -1) {
        MVM_exception_throw_adhoc(tc, "Failed to stat in filehandle: %d", errno);
    }

    if ((seek_pos = MVM_platform_lseek(handle->body.u.fd, 0, SEEK_CUR)) == -1) {
        MVM_exception_throw_adhoc(tc, "Failed to seek in filehandle: %d", errno);
    }

    return req.statbuf.st_size == seek_pos;
}

void MVM_file_set_encoding(MVMThreadContext *tc, MVMObject *oshandle, MVMString *encoding_name) {
    MVMOSHandle *handle;
    MVMROOT(tc, oshandle, {
            const MVMuint8 encoding_flag = MVM_string_find_encoding(tc, encoding_name);

            verify_filehandle_type(tc, oshandle, &handle, "setencoding");
            handle->body.encoding_type = encoding_flag;
        });
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
                          orig_cstr[1] == ':' && orig_cstr[2] == '\\';
        if (absolute) {
            /* Nothing more to do; we have an absolute path. */
            free(orig_cstr);
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
                char * new_path     = malloc(new_len);
                memcpy(new_path, lib_path[lib_path_i], lib_path_len);
                if (need_sep) {
                    new_path[lib_path_len] = '/';
                    memcpy(new_path + lib_path_len + 1, orig_cstr, orig_len);
                }
                else {
                    memcpy(new_path + lib_path_len, orig_cstr, orig_len);
                }
                result = MVM_string_utf8_decode(tc, tc->instance->VMString, new_path, new_len);
                free(new_path);
                if (!MVM_file_exists(tc, result))
                    result = orig;
                else {
                    MVM_gc_root_temp_pop_n(tc, 2); /* orig and result */
                    return result;
                }
                lib_path_i++;
            }
            if (!result || !MVM_file_exists(tc, result))
                result = orig;
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
