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

static apr_finfo_t MVM_file_info(MVMThreadContext *tc, MVMString *filename, apr_int32_t wanted) {
    apr_status_t rv;
    apr_pool_t *tmp_pool;
    apr_file_t *file_handle;
    apr_finfo_t finfo;

    char *fname = MVM_string_utf8_encode_C_string(tc, filename);

    /* need a temporary pool */
    if ((rv = apr_pool_create(&tmp_pool, POOL(tc))) != APR_SUCCESS) {
        free(fname);
        MVM_exception_throw_apr_error(tc, rv, "Open file failed to create pool: ");
    }

    if ((rv = apr_file_open(&file_handle, (const char *)fname, APR_FOPEN_READ, APR_OS_DEFAULT, tmp_pool)) != APR_SUCCESS) {
        free(fname);
        apr_pool_destroy(tmp_pool);
        MVM_exception_throw_apr_error(tc, rv, "Failed to open file: ");
    }

    free(fname);

    if((rv = apr_file_info_get(&finfo, wanted, file_handle)) != APR_SUCCESS) {
        MVM_exception_throw_apr_error(tc, rv, "Failed to stat file: ");
    }

    if ((rv = apr_file_close(file_handle)) != APR_SUCCESS) {
        MVM_exception_throw_apr_error(tc, rv, "Failed to close filehandle: ");
    }

    return finfo;
}

MVMint64 MVM_file_stat(MVMThreadContext *tc, MVMString *fn, MVMint64 status) {
    MVMint64 r = -1;

    switch (status) {
        case MVM_stat_exists:             r = MVM_file_exists(tc, fn); break;
        case MVM_stat_filesize:           r = MVM_file_info(tc, fn, APR_FINFO_SIZE).size; break;
        case MVM_stat_isdir:              r = MVM_file_info(tc, fn, APR_FINFO_TYPE).filetype & APR_DIR ? 1 : 0; break;
        case MVM_stat_isreg:              r = MVM_file_info(tc, fn, APR_FINFO_TYPE).filetype & APR_REG ? 1 : 0; break;
        case MVM_stat_isdev:              r = MVM_file_info(tc, fn, APR_FINFO_TYPE).filetype & (APR_CHR|APR_BLK) ? 1 : 0; break;
        case MVM_stat_createtime:         r = MVM_file_info(tc, fn, APR_FINFO_CTIME).ctime; break;
        case MVM_stat_accesstime:         r = MVM_file_info(tc, fn, APR_FINFO_ATIME).atime; break;
        case MVM_stat_modifytime:         r = MVM_file_info(tc, fn, APR_FINFO_MTIME).mtime; break;
        case MVM_stat_changetime:         r = MVM_file_info(tc, fn, APR_FINFO_CTIME).ctime; break;
        case MVM_stat_backuptime:         r = -1; break;
        case MVM_stat_uid:                r = MVM_file_info(tc, fn, APR_FINFO_USER).user; break;
        case MVM_stat_gid:                r = MVM_file_info(tc, fn, APR_FINFO_GROUP).group; break;
        case MVM_stat_islnk:              r = MVM_file_info(tc, fn, APR_FINFO_TYPE).filetype & APR_LNK ? 1 : 0; break;
        case MVM_stat_platform_dev:       r = MVM_file_info(tc, fn, APR_FINFO_DEV).device; break;
        case MVM_stat_platform_inode:     r = MVM_file_info(tc, fn, APR_FINFO_INODE).inode; break;
        case MVM_stat_platform_mode:      r = MVM_file_info(tc, fn, APR_FINFO_PROT).protection; break;
        case MVM_stat_platform_nlinks:    r = MVM_file_info(tc, fn, APR_FINFO_NLINK).nlink; break;
        case MVM_stat_platform_devtype:   r = -1; break;
        case MVM_stat_platform_blocksize: r = MVM_file_info(tc, fn, APR_FINFO_CSIZE).csize; break;
        case MVM_stat_platform_blocks:    r = -1; break;
        default: break;
    }

    return r;
}

char * MVM_file_get_full_path(MVMThreadContext *tc, apr_pool_t *tmp_pool, char *path) {
    apr_status_t rv;
    char *rootpath, *cwd;

    /* determine whether the given path is absolute */
    rv = apr_filepath_root((const char **)&rootpath, (const char **)&path, 0, tmp_pool);

    if (rv != APR_SUCCESS) {
        /* path is relative so needs cwd prepended */
        rv = apr_filepath_get(&cwd, 0, tmp_pool);
        return apr_pstrcat(tmp_pool, cwd, "/", path, NULL);
    }
    /* the path is already absolute */
    return apr_pstrcat(tmp_pool, path, NULL);
}

/* copy a file from one to another. */
void MVM_file_copy(MVMThreadContext *tc, MVMString *src, MVMString *dest) {
    apr_status_t rv;
    char *a, *b, *afull, *bfull;
    MVMuint32 len;
    apr_pool_t *tmp_pool;

    /* need a temporary pool */
    if ((rv = apr_pool_create(&tmp_pool, POOL(tc))) != APR_SUCCESS) {
        MVM_exception_throw_apr_error(tc, rv, "Failed to copy file: ");
    }

    afull = MVM_file_get_full_path(tc, tmp_pool, a = MVM_string_utf8_encode_C_string(tc, src));
    bfull = MVM_file_get_full_path(tc, tmp_pool, b = MVM_string_utf8_encode_C_string(tc, dest));
    free(a); free(b);

    if ((rv = apr_file_copy((const char *)afull, (const char *)bfull,
            0, tmp_pool)) != APR_SUCCESS) {
        apr_pool_destroy(tmp_pool);
        MVM_exception_throw_apr_error(tc, rv, "Failed to copy file: ");
    }
    apr_pool_destroy(tmp_pool);
}

/* append one file to another. */
void MVM_file_append(MVMThreadContext *tc, MVMString *src, MVMString *dest) {
    apr_status_t rv;
    char *a, *b, *afull, *bfull;
    MVMuint32 len;
    apr_pool_t *tmp_pool;

    /* need a temporary pool */
    if ((rv = apr_pool_create(&tmp_pool, POOL(tc))) != APR_SUCCESS) {
        MVM_exception_throw_apr_error(tc, rv, "Failed to append file: ");
    }

    afull = MVM_file_get_full_path(tc, tmp_pool, a = MVM_string_utf8_encode_C_string(tc, src));
    bfull = MVM_file_get_full_path(tc, tmp_pool, b = MVM_string_utf8_encode_C_string(tc, dest));
    free(a); free(b);

    if ((rv = apr_file_append((const char *)afull, (const char *)bfull,
            APR_FPROT_FILE_SOURCE_PERMS, tmp_pool)) != APR_SUCCESS) {
        apr_pool_destroy(tmp_pool);
        MVM_exception_throw_apr_error(tc, rv, "Failed to append file: ");
    }
    /* note: destroying the pool deallocates afull, bfull */
    apr_pool_destroy(tmp_pool);
}

/* rename one file to another. */
void MVM_file_rename(MVMThreadContext *tc, MVMString *src, MVMString *dest) {
    apr_status_t rv;
    char *a, *b, *afull, *bfull;
    MVMuint32 len;
    apr_pool_t *tmp_pool;

    /* need a temporary pool */
    if ((rv = apr_pool_create(&tmp_pool, POOL(tc))) != APR_SUCCESS) {
        MVM_exception_throw_apr_error(tc, rv, "Failed to rename file: ");
    }

    afull = MVM_file_get_full_path(tc, tmp_pool, a = MVM_string_utf8_encode_C_string(tc, src));
    bfull = MVM_file_get_full_path(tc, tmp_pool, b = MVM_string_utf8_encode_C_string(tc, dest));
    free(a); free(b);

    if ((rv = apr_file_rename((const char *)afull, (const char *)bfull, tmp_pool)) != APR_SUCCESS) {
        apr_pool_destroy(tmp_pool);
        MVM_exception_throw_apr_error(tc, rv, "Failed to rename file: ");
    }
    apr_pool_destroy(tmp_pool);
}

void MVM_file_delete(MVMThreadContext *tc, MVMString *f) {
    apr_status_t rv;
    char *a;
    apr_pool_t *tmp_pool;

    /* need a temporary pool */
    if ((rv = apr_pool_create(&tmp_pool, POOL(tc))) != APR_SUCCESS) {
        MVM_exception_throw_apr_error(tc, rv, "Failed to delete file: ");
    }

    a = MVM_string_utf8_encode_C_string(tc, f);

    /* 720002 means file wasn't there on windows, 2 on linux...  */
    /* TODO find defines for these and make it os-specific */
    if ((rv = apr_file_remove((const char *)a, tmp_pool)) != APR_SUCCESS && rv != 720002 && rv != 2) {
        free(a);
        apr_pool_destroy(tmp_pool);
        MVM_exception_throw_apr_error(tc, rv, "Failed to delete file: ");
    }
    free(a);
    apr_pool_destroy(tmp_pool);
}

/* set permissions.  see
 * http://apr.apache.org/docs/apr/1.4/group__apr__file__permissions.html
 * XXX TODO: accept bits by perl format instead...? */
void MVM_file_chmod(MVMThreadContext *tc, MVMString *f, MVMint64 flag) {
    apr_status_t rv;
    char *a;

    a = MVM_string_utf8_encode_C_string(tc, f);

    if ((rv = apr_file_perms_set((const char *)a, (apr_fileperms_t)flag)) != APR_SUCCESS) {
        free(a);
        MVM_exception_throw_apr_error(tc, rv, "Failed to set permissions on path: ");
    }
    free(a);
}

MVMint64 MVM_file_exists(MVMThreadContext *tc, MVMString *f) {
    apr_status_t rv;
    char *a;
    apr_finfo_t  stat_info;
    apr_pool_t *tmp_pool;
    MVMint64 result;

    /* need a temporary pool */
    if ((rv = apr_pool_create(&tmp_pool, POOL(tc))) != APR_SUCCESS) {
        MVM_exception_throw_apr_error(tc, rv, "Failed to exists file: ");
    }

    a = MVM_string_utf8_encode_C_string(tc, f);

    result = ((rv = apr_stat(&stat_info, (const char *)a, APR_FINFO_SIZE, tmp_pool)) == APR_SUCCESS)
        ? 1 : 0;
    free(a);
    apr_pool_destroy(tmp_pool);
    return result;
}

/* open a filehandle; takes a type object */
MVMObject * MVM_file_open_fh(MVMThreadContext *tc, MVMString *filename, MVMString *mode) {
    MVMOSHandle *result;
    apr_status_t rv;
    apr_pool_t *tmp_pool;
    apr_file_t *file_handle;
    apr_int32_t flag;
    MVMObject *type_object = tc->instance->boot_types->BOOTIO;
    char *fname = MVM_string_utf8_encode_C_string(tc, filename);
    char *fmode;

    /* need a temporary pool */
    if ((rv = apr_pool_create(&tmp_pool, POOL(tc))) != APR_SUCCESS) {
        free(fname);
        MVM_exception_throw_apr_error(tc, rv, "Open file failed to create pool: ");
    }

    fmode = MVM_string_utf8_encode_C_string(tc, mode);

    /* generate apr compatible open mode flags */
    if (0 == strcmp("r", fmode))
        flag = APR_FOPEN_READ;
    else if (0 == strcmp("w", fmode))
        flag = APR_FOPEN_WRITE|APR_FOPEN_CREATE|APR_FOPEN_TRUNCATE;
    else if (0 == strcmp("wa", fmode))
        flag = APR_FOPEN_WRITE|APR_FOPEN_CREATE|APR_FOPEN_APPEND;
    else
        MVM_exception_throw_adhoc(tc, "invalid open mode: %d", fmode);

    /* try to open the file */
    if ((rv = apr_file_open(&file_handle, (const char *)fname, flag, APR_OS_DEFAULT, tmp_pool)) != APR_SUCCESS) {
        free(fname);
        free(fmode);
        apr_pool_destroy(tmp_pool);
        MVM_exception_throw_apr_error(tc, rv, "Failed to open file: ");
    }

    /* initialize the object */
    result = (MVMOSHandle *)REPR(type_object)->allocate(tc, STABLE(type_object));

    result->body.file_handle = file_handle;
    result->body.handle_type = MVM_OSHANDLE_FILE;
    result->body.mem_pool = tmp_pool;
    result->body.encoding_type = MVM_encoding_type_utf8;

    free(fname);
    free(fmode);
    return (MVMObject *)result;
}

void MVM_file_close_fh(MVMThreadContext *tc, MVMObject *oshandle) {
    apr_status_t rv;
    MVMOSHandle *handle;

    verify_filehandle_type(tc, oshandle, &handle, "close filehandle");

    if ((rv = apr_file_close(handle->body.file_handle)) != APR_SUCCESS) {
        MVM_exception_throw_apr_error(tc, rv, "Failed to close filehandle: ");
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
    apr_status_t rv;
    MVMOSHandle *handle;

    verify_filehandle_type(tc, oshandle, &handle, "seek in filehandle");

    if ((rv = apr_file_seek(handle->body.file_handle, (apr_seek_where_t)flag, (apr_off_t *)&offset)) != APR_SUCCESS) {
        MVM_exception_throw_apr_error(tc, rv, "Failed to seek in filehandle: ");
    }
}

/* tells position within file */
MVMint64 MVM_file_tell_fh(MVMThreadContext *tc, MVMObject *oshandle) {
    apr_status_t rv;
    MVMOSHandle *handle;
    MVMint64 offset = 0;

    verify_filehandle_type(tc, oshandle, &handle, "tell in filehandle");

    if ((rv = apr_file_seek(handle->body.file_handle, APR_CUR, (apr_off_t *)&offset)) != APR_SUCCESS) {
        MVM_exception_throw_apr_error(tc, rv, "Failed to tell position of filehandle: ");
    }

    return offset;
}

/* locks a filehandle */
MVMint64 MVM_file_lock(MVMThreadContext *tc, MVMObject *oshandle, MVMint64 flag) {
    apr_status_t rv;
    MVMOSHandle *handle;
    int locktype = (int)flag;

    verify_filehandle_type(tc, oshandle, &handle, "lock filehandle");

    if ((rv = apr_file_lock(handle->body.file_handle, locktype)) != APR_SUCCESS) {
        /* XXX this really should check what type of error was returned */
        if (locktype & APR_FLOCK_NONBLOCK) return 0;
        MVM_exception_throw_apr_error(tc, rv, "Failed to lock filehandle: ");
    }
    return 1;
}

/* unlocks a filehandle */
void MVM_file_unlock(MVMThreadContext *tc, MVMObject *oshandle) {
    apr_status_t rv;
    MVMOSHandle *handle;

    verify_filehandle_type(tc, oshandle, &handle, "unlock filehandle");

    if ((rv = apr_file_unlock(handle->body.file_handle)) != APR_SUCCESS) {
        MVM_exception_throw_apr_error(tc, rv, "Failed to unlock filehandle: ");
    }
}

/* flushes a possibly-buffered filehandle (such as stdout) */
void MVM_file_flush(MVMThreadContext *tc, MVMObject *oshandle) {
    apr_status_t rv;
    MVMOSHandle *handle;

    verify_filehandle_type(tc, oshandle, &handle, "flush filehandle");

    if ((rv = apr_file_flush(handle->body.file_handle)) != APR_SUCCESS) {
        MVM_exception_throw_apr_error(tc, rv, "Failed to flush filehandle: ");
    }
}

/* syncs a filehandle (Transfer all file modified data and metadata to disk.) */
void MVM_file_sync(MVMThreadContext *tc, MVMObject *oshandle) {
    apr_status_t rv;
    MVMOSHandle *handle;

    verify_filehandle_type(tc, oshandle, &handle, "sync filehandle");

    if ((rv = apr_file_sync(handle->body.file_handle)) != APR_SUCCESS) {
        MVM_exception_throw_apr_error(tc, rv, "Failed to sync filehandle: ");
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
    apr_status_t rv;
    MVMOSHandle *handle;

    verify_filehandle_type(tc, oshandle, &handle, "truncate filehandle");

    if ((rv = apr_file_trunc(handle->body.file_handle, (apr_off_t)offset)) != APR_SUCCESS) {
        MVM_exception_throw_apr_error(tc, rv, "Failed to truncate filehandle: ");
    }
}

/* return an OSHandle representing one of the standard streams */
static MVMObject * MVM_file_get_stdstream(MVMThreadContext *tc, MVMuint8 type) {
    MVMOSHandle *result;
    apr_file_t  *handle;
    apr_status_t rv;
    MVMObject *type_object = tc->instance->boot_types->BOOTIO;

    result = (MVMOSHandle *)REPR(type_object)->allocate(tc, STABLE(type_object));

    /* need a temporary pool */
    if ((rv = apr_pool_create(&result->body.mem_pool, NULL)) != APR_SUCCESS) {
        MVM_exception_throw_apr_error(tc, rv, "get_stream failed to create pool: ");
    }

    switch(type) {
        case 0:
            apr_file_open_stdin(&handle, result->body.mem_pool);
            break;
        case 1:
            apr_file_open_stdout(&handle, result->body.mem_pool);
            break;
        case 2:
            apr_file_open_stderr(&handle, result->body.mem_pool);
            break;
    }
    result->body.file_handle = handle;
    result->body.handle_type = MVM_OSHANDLE_FILE;
    result->body.encoding_type = MVM_encoding_type_utf8;

    return (MVMObject *)result;
}

MVMint64 MVM_file_eof(MVMThreadContext *tc, MVMObject *oshandle) {
    MVMOSHandle *handle;

    verify_filehandle_type(tc, oshandle, &handle, "check eof");

    return apr_file_eof(handle->body.file_handle) == APR_EOF ? 1 : 0;
}

MVMObject * MVM_file_get_stdin(MVMThreadContext *tc) {
    return MVM_file_get_stdstream(tc, 0);
}

MVMObject * MVM_file_get_stdout(MVMThreadContext *tc) {
    return MVM_file_get_stdstream(tc, 1);
}

MVMObject * MVM_file_get_stderr(MVMThreadContext *tc) {
    return MVM_file_get_stdstream(tc, 2);
}

void MVM_file_set_encoding(MVMThreadContext *tc, MVMObject *oshandle, MVMString *encoding_name) {
    MVMOSHandle *handle;
    MVMuint8 encoding_flag = MVM_find_encoding_by_name(tc, encoding_name);

    verify_filehandle_type(tc, oshandle, &handle, "setencoding");

    handle->body.encoding_type = encoding_flag;
}
