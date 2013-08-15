#include "moarvm.h"

#define POOL(tc) (*(tc->interp_cu))->pool

static void verify_filehandle_type(MVMThreadContext *tc, MVMObject *oshandle, MVMOSHandle **handle, const char *msg) {
    /* work on only MVMOSHandle of type MVM_OSHANDLE_FILE */
    if (REPR(oshandle)->ID != MVM_REPR_ID_MVMOSHandle) {
        MVM_exception_throw_adhoc(tc, "%s requires an object with REPR MVMOSHandle", msg);
    }
    *handle = (MVMOSHandle *)oshandle;
    if ((*handle)->body.handle_type != MVM_OSHANDLE_FILE) {
        MVM_exception_throw_adhoc(tc, "%s requires an MVMOSHandle of type file handle", msg);
    }
}

static uv_stat_t file_info(MVMThreadContext *tc, MVMString *filename) {
    uv_fs_t req;
    char * const   a = MVM_string_utf8_encode_C_string(tc, filename);
    const uv_file fd = uv_fs_open(tc->loop, &req, (const char *)a, O_RDONLY, 0, NULL);

    if (fd >= 0
        && uv_fs_stat(tc->loop, &req, a, NULL) >= 0) {
        free(a);
        if (uv_fs_close(tc->loop, &req, fd, NULL) < 0) {
            MVM_exception_throw_adhoc(tc, "Failed to close file: %s", uv_strerror(req.result));
        }
        return req.statbuf;
    }

    free(a);
    MVM_exception_throw_adhoc(tc, "Failed to stat file: %s", uv_strerror(req.result));
}

MVMint64 MVM_file_stat(MVMThreadContext *tc, MVMString *filename, MVMint64 status) {
    MVMint64 r = -1;

    switch (status) {
        case MVM_stat_exists:             r = MVM_file_exists(tc, filename); break;
        case MVM_stat_filesize:           r = file_info(tc, filename).st_size; break;
        case MVM_stat_isdir:              r = file_info(tc, filename).st_mode & S_IFMT == S_IFDIR; break;
        case MVM_stat_isreg:              r = file_info(tc, filename).st_mode & S_IFMT == S_IFREG; break;
        case MVM_stat_isdev: {
            const int mode = file_info(tc, filename).st_mode;
#ifdef _WIN32
            r = mode & S_IFMT == S_IFCHR;
#else
            r = mode & S_IFMT == S_IFCHR || mode & S_IFMT == S_IFBLK;
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
        case MVM_stat_islnk:              r = file_info(tc, filename).st_mode & S_IFMT == S_IFLNK; break;
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
    const uv_file out_fd = uv_fs_open(tc->loop, &req, (const char *)b, O_CREAT| O_WRONLY | O_TRUNC, 0, NULL);

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
        free(a); free(b);
        MVM_exception_throw_adhoc(tc, "Failed to rename file: %s", uv_strerror(req.result));
    }

    free(a); free(b);
}

void MVM_file_delete(MVMThreadContext *tc, MVMString *f) {
    char * const a = MVM_string_utf8_encode_C_string(tc, f);
    uv_fs_t req;

    if(uv_fs_unlink(tc->loop, &req, a, NULL) < 0 ) {
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
    char * const a = MVM_string_utf8_encode_C_string(tc, f);
    uv_fs_t req;
    const MVMint64 result = uv_fs_stat(tc->loop, &req, a, NULL) < 0 ? 0 : 1;

    free(a);

    return result;
}

/* open a filehandle; takes a type object */
MVMObject * MVM_file_open_fh(MVMThreadContext *tc, MVMString *filename, MVMString *mode) {
    MVMObject * const type_object = tc->instance->boot_types->BOOTIO;
    MVMOSHandle    * const result = (MVMOSHandle *)REPR(type_object)->allocate(tc, STABLE(type_object));
    char            * const fname = MVM_string_utf8_encode_C_string(tc, filename);
    char            * const fmode = MVM_string_utf8_encode_C_string(tc, mode);
    uv_fs_t req;
    int flag;

    if (0 == strcmp("r", fmode))
        flag = O_RDONLY;
    else if (0 == strcmp("w", fmode))
        flag = O_CREAT| O_WRONLY | O_TRUNC;
    else if (0 == strcmp("wa", fmode))
        flag = O_CREAT | O_WRONLY | O_APPEND;
    else {
        free(fmode);
        MVM_exception_throw_adhoc(tc, "Invalid open mode: %d", fmode);
    }

    free(fmode);

    if ((result->body.fd = uv_fs_open(tc->loop, &req, (const char *)fname, flag, 0, NULL)) < 0) {
        free(fname);
        MVM_exception_throw_adhoc(tc, "Failed to open file: %s", uv_strerror(req.result));
    }

    free(fname);

    result->body.type = MVM_OSHANDLE_FD;
    result->body.encoding_type = MVM_encoding_type_utf8;

    return (MVMObject *)result;
}

void MVM_file_close_fh(MVMThreadContext *tc, MVMObject *oshandle) {
    MVMOSHandle *handle;
    uv_fs_t req;

    verify_filehandle_type(tc, oshandle, &handle, "close filehandle");

    if (uv_fs_close(tc->loop, &req, handle->body.fd, NULL) < 0) {
        MVM_exception_throw_adhoc(tc, "Failed to close filehandle: %s", uv_strerror(req.result));
    }
}

/* reads a line from a filehandle. */
MVMString * MVM_file_readline_fh(MVMThreadContext *tc, MVMObject *oshandle) {
    MVMString *result;
    apr_status_t rv;
    MVMOSHandle *handle;
    char ch;
    char *buf;
    apr_off_t offset = 0;
    apr_off_t fetched = 0;
    apr_off_t bytes_read = 0;

    verify_filehandle_type(tc, oshandle, &handle, "readline from filehandle");

    if ((rv = apr_file_seek(handle->body.file_handle, APR_CUR, &offset)) != APR_SUCCESS) {
        MVM_exception_throw_apr_error(tc, rv, "Failed to tell position of filehandle in readline(1): ");
    }

    while (apr_file_getc(&ch, handle->body.file_handle) == APR_SUCCESS && ch != 10 && ch != 13) {
        bytes_read++;
    }

    /* have a look if it is a windows newline, and step back if not. */
    if (ch == 13 && apr_file_getc(&ch, handle->body.file_handle) == APR_SUCCESS && ch != 10) {
        fetched--;
    }

    if ((rv = apr_file_seek(handle->body.file_handle, APR_CUR, &fetched)) != APR_SUCCESS) {
        MVM_exception_throw_apr_error(tc, rv, "Failed to tell position of filehandle in readline(2): ");
    }

    if ((rv = apr_file_seek(handle->body.file_handle, APR_SET, &offset)) != APR_SUCCESS) {
        MVM_exception_throw_apr_error(tc, rv, "Failed to tell position of filehandle in readline(3)");
    }

    buf = malloc((int)(bytes_read + 1));

    if ((rv = apr_file_read(handle->body.file_handle, buf, &bytes_read)) != APR_SUCCESS) {
        free(buf);
        MVM_exception_throw_apr_error(tc, rv, "readline from filehandle failed: ");
    }

    if ((rv = apr_file_seek(handle->body.file_handle, APR_SET, &fetched)) != APR_SUCCESS) {
        MVM_exception_throw_apr_error(tc, rv, "Failed to tell position of filehandle in readline(4)");
    }
                                               /* XXX should this take a type object? */
    result = MVM_decode_C_buffer_to_string(tc, tc->instance->VMString, buf, bytes_read, handle->body.encoding_type);
    free(buf);

    return result;
}

/* reads a string from a filehandle. */
MVMString * MVM_file_read_fhs(MVMThreadContext *tc, MVMObject *oshandle, MVMint64 length) {
    MVMString *result;
    apr_status_t rv;
    MVMOSHandle *handle;
    char *buf;
    MVMint64 bytes_read;

    /* XXX TODO length currently means bytes. alter it to mean graphemes. */
    /* XXX TODO handle length == -1 to mean read to EOF */

    verify_filehandle_type(tc, oshandle, &handle, "read from filehandle");

    if (length < 1 || length > 99999999) {
        MVM_exception_throw_adhoc(tc, "read from filehandle length out of range");
    }

    buf = malloc(length);
    bytes_read = length;

    if ((rv = apr_file_read(handle->body.file_handle, buf, (apr_size_t *)&bytes_read)) != APR_SUCCESS) {
        free(buf);
        MVM_exception_throw_apr_error(tc, rv, "read from filehandle failed: ");
    }
                                               /* XXX should this take a type object? */
    result = MVM_decode_C_buffer_to_string(tc, tc->instance->VMString, buf, bytes_read, handle->body.encoding_type);

    free(buf);

    return result;
}

/* read all of a filehandle into a string. */
MVMString * MVM_file_readall_fh(MVMThreadContext *tc, MVMObject *oshandle) {
    MVMString *result;
    apr_status_t rv;
    MVMOSHandle *handle;
    apr_finfo_t finfo;
    apr_pool_t *tmp_pool;
    char *buf;
    MVMint64 bytes_read;

    /* XXX TODO length currently means bytes. alter it to mean graphemes. */
    /* XXX TODO handle length == -1 to mean read to EOF */

    verify_filehandle_type(tc, oshandle, &handle, "Readall from filehandle");

    ENCODING_VALID(handle->body.encoding_type);

    /* need a temporary pool */
    if ((rv = apr_pool_create(&tmp_pool, POOL(tc))) != APR_SUCCESS) {
        MVM_exception_throw_apr_error(tc, rv, "Readall failed to create pool: ");
    }

    if ((rv = apr_file_info_get(&finfo, APR_FINFO_SIZE, handle->body.file_handle)) != APR_SUCCESS) {
        apr_pool_destroy(tmp_pool);
        MVM_exception_throw_apr_error(tc, rv, "Readall failed to get info about file: ");
    }
    apr_pool_destroy(tmp_pool);

    if (finfo.size > 0) {
        buf = malloc(finfo.size);
        bytes_read = finfo.size;

        if ((rv = apr_file_read(handle->body.file_handle, buf, (apr_size_t *)&bytes_read)) != APR_SUCCESS) {
            free(buf);
            MVM_exception_throw_apr_error(tc, rv, "Readall from filehandle failed: ");
        }
                                                   /* XXX should this take a type object? */
        result = MVM_decode_C_buffer_to_string(tc, tc->instance->VMString, buf, bytes_read, handle->body.encoding_type);
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

/* writes a string to a filehandle. */
MVMint64 MVM_file_write_fhs(MVMThreadContext *tc, MVMObject *oshandle, MVMString *str) {
    apr_status_t rv;
    MVMuint8 *output;
    MVMuint64 output_size;
    apr_size_t bytes_written;
    MVMOSHandle *handle;

    verify_filehandle_type(tc, oshandle, &handle, "write to filehandle");

    output = MVM_encode_string_to_C_buffer(tc, str, 0, -1, &output_size, handle->body.encoding_type);
    bytes_written = (apr_size_t)output_size;
    if ((rv = apr_file_write(handle->body.file_handle, (const void *)output, &bytes_written)) != APR_SUCCESS) {
        free(output);
        MVM_exception_throw_apr_error(tc, rv, "Failed to write bytes to filehandle: tried to write %u bytes, wrote %u bytes: ", output_size, bytes_written);
    }
    free(output);

    return (MVMint64)bytes_written;
}

/* writes a string to a file, overwriting it if necessary */
void MVM_file_spew(MVMThreadContext *tc, MVMString *output, MVMString *filename, MVMString *encoding) {
    MVMString *mode = MVM_string_utf8_decode(tc, tc->instance->VMString, "w", 1);
    MVMObject *fh = MVM_file_open_fh(tc, filename, mode);
    MVM_file_set_encoding(tc, fh, encoding);
    MVM_file_write_fhs(tc, fh, output);
    MVM_file_close_fh(tc, fh);
    /* XXX need to GC free the filehandle? */
}

/* seeks in a filehandle */
void MVM_file_seek(MVMThreadContext *tc, MVMObject *oshandle, MVMint64 offset, MVMint64 flag) {
    MVMOSHandle *handle;
    uv_fs_t req;

    verify_filehandle_type(tc, oshandle, &handle, "seek in filehandle");

    if (uv_fs_seek(tc->loop, &req, handle->body.fd, offset, flag, NULL) < 0) {
        MVM_exception_throw_adhoc(tc, "Failed to seek in filehandle: %s", uv_strerror(req.result));
    }
}

/* tells position within file */
MVMint64 MVM_file_tell_fh(MVMThreadContext *tc, MVMObject *oshandle) {
    MVMint64 r;
    MVMOSHandle *handle;
    uv_fs_t req;

    verify_filehandle_type(tc, oshandle, &handle, "tell in filehandle");

    if ((r = uv_fs_seek(tc->loop, &req, handle->body.fd, 0, SEEK_CUR, NULL)) < 0) {
        MVM_exception_throw_adhoc(tc, "Failed to tell position of filehandle: %s", uv_strerror(req.result));
    }

    return r;
}

/* locks a filehandle */
MVMint64 MVM_file_lock(MVMThreadContext *tc, MVMObject *oshandle, MVMint64 flag) {
    MVMOSHandle *handle;
    uv_fs_t req;

    verify_filehandle_type(tc, oshandle, &handle, "lock filehandle");

    if(uv_fs_lock(tc->loop, &req, handle->body.fd, flag, NULL) < 0 ) {
        MVM_exception_throw_adhoc(tc, "Failed to unlock filehandle: %s", uv_strerror(req.result));
    }

    return 1;
}

/* unlocks a filehandle */
void MVM_file_unlock(MVMThreadContext *tc, MVMObject *oshandle) {
    MVMOSHandle *handle;
    uv_fs_t req;

    verify_filehandle_type(tc, oshandle, &handle, "unlock filehandle");

    if(uv_fs_unlock(tc->loop, &req, handle->body.fd, NULL) < 0 ) {
        MVM_exception_throw_adhoc(tc, "Failed to unlock filehandle: %s", uv_strerror(req.result));
    }
}

/* flushes a possibly-buffered filehandle (such as stdout) */
void MVM_file_flush(MVMThreadContext *tc, MVMObject *oshandle) {
    MVMOSHandle *handle;
    uv_fs_t req;

    verify_filehandle_type(tc, oshandle, &handle, "flush filehandle");

    if(uv_fs_flush(tc->loop, &req, NULL) < 0 ) {
        MVM_exception_throw_adhoc(tc, "Failed to flush filehandle: %s", uv_strerror(req.result));
    }
}

/* syncs a filehandle (Transfer all file modified data and metadata to disk.) */
void MVM_file_sync(MVMThreadContext *tc, MVMObject *oshandle) {
    MVMOSHandle *handle;
    uv_fs_t req;

    verify_filehandle_type(tc, oshandle, &handle, "sync filehandle");

    if(uv_fs_fsync(tc->loop, &req, handle->body.fd, NULL) < 0 ) {
        MVM_exception_throw_adhoc(tc, "Failed to sync filehandle: %s", uv_strerror(req.result));
    }
}

/* creates a pipe between two filehandles */
/* XXX TODO: this needs to stash references to each other in each other,
 * that are understood by the GC, in case one goes out of scope but the
 * other doesn't. Also, there's not really a way to avoid a memory leak
 * when creating a pipe using one of the handle's mem_pools. */
void MVM_file_pipe(MVMThreadContext *tc, MVMObject *oshandle1, MVMObject *oshandle2) {
    apr_status_t rv;
    MVMOSHandle *handle1;
    MVMOSHandle *handle2;

    verify_filehandle_type(tc, oshandle1, &handle1, "pipe filehandles");
    verify_filehandle_type(tc, oshandle2, &handle2, "pipe filehandles");

    if ((rv = apr_file_pipe_create(&handle1->body.file_handle, &handle2->body.file_handle, handle1->body.mem_pool)) != APR_SUCCESS) {
        MVM_exception_throw_apr_error(tc, rv, "Failed to pipe filehandles: ");
    }
}

/* syncs a filehandle (Transfer all file modified data and metadata to disk.) */
void MVM_file_truncate(MVMThreadContext *tc, MVMObject *oshandle, MVMint64 offset) {
    MVMOSHandle *handle;
    uv_fs_t req;

    verify_filehandle_type(tc, oshandle, &handle, "truncate filehandle");

    if(uv_fs_ftruncate(tc->loop, &req, handle->body.fd, offset, NULL) < 0 ) {
        MVM_exception_throw_adhoc(tc, "Failed to truncate filehandle: %s", uv_strerror(req.result));
    }
}

/* return an OSHandle representing one of the standard streams */
static MVMObject * MVM_file_get_stdstream(MVMThreadContext *tc, MVMuint8 type, MVMuint8 readable) {
    MVMObject * const type_object = tc->instance->boot_types->BOOTIO;
    MVMOSHandle * const    result = (MVMOSHandle *)REPR(type_object)->allocate(tc, STABLE(type_object));
    MVMOSHandleBody * const body  = &result->body;
    uv_fs_t req;

    switch(uv_guess_handle(type)) {
        case UV_TTY: {
            uv_tty_t *handle = malloc(sizeof(uv_tty_t));
            uv_tty_init(tc->loop, handle, type, readable);
            body->handle = (uv_handle_t *)handle;
            body->type = MVM_OSHANDLE_HANDLE;
            break;
        }
        case UV_FILE:
            body->fd   = type;
            body->type = MVM_OSHANDLE_FD;
            break;
        case UV_NAMED_PIPE: {
            uv_pipe_t *handle = malloc(sizeof(uv_pipe_t));
            uv_pipe_init(tc->loop, handle, 0);
            uv_pipe_open(handle, type);
            body->handle = (uv_handle_t *)handle;
            body->type = MVM_OSHANDLE_HANDLE;
            break;
        }
        default:
            MVM_exception_throw_adhoc(tc, "get_stream failed, unsupported std handle");
            break;
    }

    return (MVMObject *)result;
}

MVMint64 MVM_file_eof(MVMThreadContext *tc, MVMObject *oshandle) {
    MVMOSHandle *handle;

    verify_filehandle_type(tc, oshandle, &handle, "check eof");

    return apr_file_eof(handle->body.file_handle) == APR_EOF ? 1 : 0;
}

MVMObject * MVM_file_get_stdin(MVMThreadContext *tc) {
    return MVM_file_get_stdstream(tc, 0, 1);
}

MVMObject * MVM_file_get_stdout(MVMThreadContext *tc) {
    return MVM_file_get_stdstream(tc, 1, 0);
}

MVMObject * MVM_file_get_stderr(MVMThreadContext *tc) {
    return MVM_file_get_stdstream(tc, 2, 0);
}

void MVM_file_set_encoding(MVMThreadContext *tc, MVMObject *oshandle, MVMString *encoding_name) {
    MVMOSHandle *handle;
    MVMuint8 encoding_flag = MVM_find_encoding_by_name(tc, encoding_name);

    verify_filehandle_type(tc, oshandle, &handle, "setencoding");

    handle->body.encoding_type = encoding_flag;
}
