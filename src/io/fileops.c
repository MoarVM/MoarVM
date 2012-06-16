#include "moarvm.h"

#define POOL(tc) (*(tc->interp_cu))->pool

/* cache the oshandle repr */
static MVMObject *anon_oshandle_type;
MVMObject * MVM_file_get_anon_oshandle_type(MVMThreadContext *tc) {
    if (!anon_oshandle_type) {
        /* fake up an anonymous type using MVMOSHandle REPR */
        anon_oshandle_type = MVM_repr_get_by_name(tc, MVM_string_ascii_decode_nt(tc,
                tc->instance->boot_types->BOOTStr, "MVMOSHandle"))->type_object_for(tc,
                REPR(tc->instance->KnowHOW)->allocate(tc, STABLE(tc->instance->KnowHOW)));
    }
    return anon_oshandle_type;
}

static void verify_filehandle_type(MVMThreadContext *tc, MVMObject *oshandle, MVMOSHandle **handle, const char *msg) {

    /* work on only MVMOSHandle of type MVM_OSHANDLE_FILE */
    if (REPR(oshandle)->ID != MVM_REPR_ID_MVMOSHandle) {
        MVM_exception_throw_adhoc(tc, "%s requires an object with REPR MVMOSHandle");
    }
    *handle = (MVMOSHandle *)oshandle;
    if ((*handle)->body.handle_type != MVM_OSHANDLE_FILE) {
        MVM_exception_throw_adhoc(tc, "%s requires an MVMOSHandle of type file handle");
    }
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
    const char *a;
    apr_pool_t *tmp_pool;
    
    /* need a temporary pool */
    if ((rv = apr_pool_create(&tmp_pool, POOL(tc))) != APR_SUCCESS) {
        MVM_exception_throw_apr_error(tc, rv, "Failed to delete file: ");
    }
    
    a = (const char *) MVM_string_utf8_encode_C_string(tc, f);
    
    /* 720002 means file wasn't there on windows, 2 on linux...  */
    /* TODO find defines for these and make it os-specific */
    if ((rv = apr_file_remove(a, tmp_pool)) != APR_SUCCESS && rv != 720002 && rv != 2) {
        apr_pool_destroy(tmp_pool);
        MVM_exception_throw_apr_error(tc, rv, "Failed to delete file: ");
    }
    apr_pool_destroy(tmp_pool);
}

/* set permissions.  see
 * http://apr.apache.org/docs/apr/1.4/group__apr__file__permissions.html 
 * XXX TODO: accept bits by perl format instead...? */
void MVM_file_chmod(MVMThreadContext *tc, MVMString *f, MVMint64 flag) {
    apr_status_t rv;
    const char *a;
    
    a = (const char *) MVM_string_utf8_encode_C_string(tc, f);
    
    if ((rv = apr_file_perms_set(a, (apr_fileperms_t)flag)) != APR_SUCCESS) {
        MVM_exception_throw_apr_error(tc, rv, "Failed to set permissions on path: ");
    }
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
MVMObject * MVM_file_open_fh(MVMThreadContext *tc, MVMObject *type_object, MVMString *filename, MVMint64 flag) {
    MVMOSHandle *result;
    apr_status_t rv;
    apr_pool_t *tmp_pool;
    apr_file_t *file_handle;
    char *fname = MVM_string_utf8_encode_C_string(tc, filename);
    
    if (REPR(type_object)->ID != MVM_REPR_ID_MVMOSHandle || IS_CONCRETE(type_object)) {
        MVM_exception_throw_adhoc(tc, "Open file needs a type object with MVMOSHandle REPR");
    }
    
    /* need a temporary pool */
    if ((rv = apr_pool_create(&tmp_pool, POOL(tc))) != APR_SUCCESS) {
        free(fname);
        MVM_exception_throw_apr_error(tc, rv, "Open file failed to create pool: ");
    }
    
    /* try to open the file */
    if ((rv = apr_file_open(&file_handle, (const char *)fname, flag, APR_OS_DEFAULT, tmp_pool)) != APR_SUCCESS) {
        free(fname);
        apr_pool_destroy(tmp_pool);
        MVM_exception_throw_apr_error(tc, rv, "Failed to open file: ");
    }
    
    /* initialize the object */
    result = (MVMOSHandle *)REPR(type_object)->allocate(tc, STABLE(type_object));
    
    result->body.file_handle = file_handle;
    result->body.handle_type = MVM_OSHANDLE_FILE;
    result->body.mem_pool = tmp_pool;
    
    free(fname);
    
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

/* reads a string from a filehandle.  Assumes utf8 for now */
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
    
    result = MVM_string_utf8_decode(tc, tc->instance->boot_types->BOOTStr, buf, bytes_read);
    
    free(buf);
    
    return result;
}

/* read all of a file into a string */
MVMString * MVM_file_slurp(MVMThreadContext *tc, MVMString *filename) {
    MVMString *result;
    apr_status_t rv;
    apr_file_t *fp;
    apr_finfo_t finfo;
    apr_mmap_t *mmap;
    char *fname = MVM_string_utf8_encode_C_string(tc, filename);
    apr_pool_t *tmp_pool;
    
    /* need a temporary pool */
    if ((rv = apr_pool_create(&tmp_pool, POOL(tc))) != APR_SUCCESS) {
        MVM_exception_throw_apr_error(tc, rv, "Slurp failed to create pool: ");
    }
    
    /* TODO detect encoding (ucs4, latin1, utf8 (including ascii/ansi), utf16).
     * Currently assume utf8. */
    
    if ((rv = apr_file_open(&fp, fname, APR_READ, APR_OS_DEFAULT, tmp_pool)) != APR_SUCCESS) {
        free(fname);
        apr_pool_destroy(tmp_pool);
        MVM_exception_throw_apr_error(tc, rv, "Slurp failed to open file: ");
    }
    
    free(fname);
    
    if ((rv = apr_file_info_get(&finfo, APR_FINFO_SIZE, fp)) != APR_SUCCESS) {
        apr_pool_destroy(tmp_pool);
        MVM_exception_throw_apr_error(tc, rv, "Slurp failed to get info about file: ");
    }
    if ((rv = apr_mmap_create(&mmap, fp, 0, finfo.size, APR_MMAP_READ, tmp_pool)) != APR_SUCCESS) {
        apr_pool_destroy(tmp_pool);
        MVM_exception_throw_apr_error(tc, rv, "Slurp failed to mmap file: ");
    }
    
    /* no longer need the filehandle */
    apr_file_close(fp);
    
    /* convert the mmap to a MVMString */
    result = MVM_string_utf8_decode(tc, tc->instance->boot_types->BOOTStr, mmap->mm, finfo.size);
    
    /* delete the mmap */
    apr_mmap_delete(mmap);
    apr_pool_destroy(tmp_pool);
    
    return result;
}

/* writes a string to a filehandle.  XXX writes only utf8 for now. */
void MVM_file_write_fhs(MVMThreadContext *tc, MVMObject *oshandle, MVMString *str, MVMint64 start, MVMint64 length) {
    apr_status_t rv;
    MVMuint8 *output;
    MVMuint64 output_size;
    apr_size_t bytes_written;
    MVMOSHandle *handle;
    
    verify_filehandle_type(tc, oshandle, &handle, "write to filehandle");
    
    if (length < 0)
        length = str->body.graphs - start;
    else if (start + length > str->body.graphs)
        MVM_exception_throw_adhoc(tc, "write to filehandle start + length past end of string");
    
    output = MVM_string_utf8_encode_substr(tc, str, &output_size, start, length);
    bytes_written = (apr_size_t)output_size;
    if ((rv = apr_file_write(handle->body.file_handle, (const void *)output, &bytes_written)) != APR_SUCCESS) {
        free(output);
        MVM_exception_throw_apr_error(tc, rv, "Failed to write bytes to filehandle: tried to write %u bytes, wrote %u bytes: ", output_size, bytes_written);
    }
    free(output);
}

/* writes a string to a file, overwriting it if necessary */
void MVM_file_spew(MVMThreadContext *tc, MVMString *output, MVMString *filename) {
    MVMObject *fh = MVM_file_open_fh(tc, MVM_file_get_anon_oshandle_type(tc), filename,
        (MVMint64)(APR_FOPEN_TRUNCATE | APR_FOPEN_WRITE | APR_FOPEN_CREATE | APR_FOPEN_BINARY));
    
    MVM_file_write_fhs(tc, fh, output, 0, output->body.graphs);
    
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
static MVMObject * MVM_file_get_stdstream(MVMThreadContext *tc, MVMObject *type_object, MVMuint8 type) {
    MVMOSHandle *result;
    apr_file_t  *handle;
    apr_status_t rv;
    
    if (REPR(type_object)->ID != MVM_REPR_ID_MVMOSHandle || IS_CONCRETE(type_object)) {
        MVM_exception_throw_adhoc(tc, "Open stream needs a type object with MVMOSHandle REPR");
    }
    
    result = (MVMOSHandle *)REPR(type_object)->allocate(tc, STABLE(type_object));
    
    /* need a temporary pool */
    if ((rv = apr_pool_create(&result->body.mem_pool, NULL)) != APR_SUCCESS) {
        /* GC free? */
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
    
    return (MVMObject *)result;
}

MVMint64 MVM_file_eof(MVMThreadContext *tc, MVMObject *oshandle) {
    apr_status_t rv;
    MVMOSHandle *handle;
    
    verify_filehandle_type(tc, oshandle, &handle, "check eof");
    
    return apr_file_eof(handle->body.file_handle) == APR_EOF ? 1 : 0;
}

MVMObject * MVM_file_get_stdin(MVMThreadContext *tc, MVMObject *type_object) {
    return MVM_file_get_stdstream(tc, type_object, 0);
}

MVMObject * MVM_file_get_stdout(MVMThreadContext *tc, MVMObject *type_object) {
    return MVM_file_get_stdstream(tc, type_object, 1);
}

MVMObject * MVM_file_get_stderr(MVMThreadContext *tc, MVMObject *type_object) {
    return MVM_file_get_stdstream(tc, type_object, 2);
}
