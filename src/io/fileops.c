#include "moarvm.h"

#define POOL(tc) (*(tc->interp_cu))->pool

/* cache the oshandle repr */
static MVMREPROps *oshandle_repr;
static MVMREPROps * get_oshandle_repr(MVMThreadContext *tc) {
    if (!oshandle_repr)
        oshandle_repr = MVM_repr_get_by_name(tc, MVM_string_ascii_decode_nt(tc,
                tc->instance->boot_types->BOOTStr, "MVMOSHandle"));
    return oshandle_repr;
}

void MVM_file_copy(MVMThreadContext *tc, MVMString *src, MVMString *dest) {
    apr_status_t rv;
    const char *a, *b;
    
    a = (const char *) MVM_string_utf8_encode_C_string(tc, src);
    b = (const char *) MVM_string_utf8_encode_C_string(tc, dest);
    
    if ((rv = apr_file_copy(a, b, APR_FILE_SOURCE_PERMS, POOL(tc))) != APR_SUCCESS) {
        MVM_exception_throw_apr_error(tc, rv, "Failed to copy '%s' to '%s': ", a, b);
    }
}

void MVM_file_delete(MVMThreadContext *tc, MVMString *f) {
    apr_status_t rv;
    const char *a;
    
    a = (const char *) MVM_string_utf8_encode_C_string(tc, f);
    
    /* 720002 means file wasn't there on windows, 2 on linux...  */
    /* TODO find defines for these and make it os-specific */
    if ((rv = apr_file_remove(a, POOL(tc))) != APR_SUCCESS && rv != 720002 && rv != 2) {
        MVM_exception_throw_apr_error(tc, rv, "Failed to delete '%s': ", a);
    }
}

MVMint64 MVM_file_exists(MVMThreadContext *tc, MVMString *f) {
    apr_status_t rv;
    const char *a;
    apr_finfo_t  stat_info;
    
    a = (const char *) MVM_string_utf8_encode_C_string(tc, f);
    
    return ((rv = apr_stat(&stat_info, a, APR_FINFO_SIZE, POOL(tc))) == APR_SUCCESS)
        ? 1 : 0;
}

/* read all of a file into a string */
MVMString * MVM_file_slurp(MVMThreadContext *tc, MVMString *filename) {
    MVMString *result;
    apr_status_t rv;
    apr_file_t *fp;
    apr_finfo_t finfo;
    apr_mmap_t *mmap;
    char *fname = MVM_string_utf8_encode_C_string(tc, filename);
    apr_pool_t *mp = POOL(tc);
    
    /* TODO detect encoding (ucs4, latin1, utf8 (including ascii/ansi), utf16).
     * Currently assume utf8. */
    
    if ((rv = apr_file_open(&fp, fname, APR_READ, APR_OS_DEFAULT, mp)) != APR_SUCCESS) {
        MVM_exception_throw_apr_error(tc, rv, "Slurp failed to open file '%s': ", fname);
    }
    if ((rv = apr_file_info_get(&finfo, APR_FINFO_SIZE, fp)) != APR_SUCCESS) {
        MVM_exception_throw_apr_error(tc, rv, "Slurp failed to get info about file '%s': ", fname);
    }
    if ((rv = apr_mmap_create(&mmap, fp, 0, finfo.size, APR_MMAP_READ, mp)) != APR_SUCCESS) {
        MVM_exception_throw_apr_error(tc, rv, "Slurp failed to mmap file '%s': ", fname);
    }
    
    /* no longer need the filehandle */
    apr_file_close(fp);
    
    /* convert the mmap to a MVMString */
    result = MVM_string_utf8_decode(tc, (MVMObject *)filename, mmap->mm, finfo.size);
    
    /* delete the mmap */
    apr_mmap_delete(mmap);
    
    return result;
}

/* writes a string to a filehandle.  XXX writes only utf8 for now. */
void MVM_file_write_fhs(MVMThreadContext *tc, MVMObject *oshandle, MVMString *str, MVMint64 start, MVMint64 length) {
    apr_status_t rv;
    MVMuint8 *output;
    MVMuint64 output_size;
    apr_size_t bytes_written;
    /* XXX TODO must check REPR and handle type of this object */
    MVMOSHandle *handle = (MVMOSHandle *)oshandle;
    
    if (length < 0)
        length = str->body.graphs;
    else if (start + length > str->body.graphs)
        MVM_exception_throw_adhoc(tc, "write to filehandle start + length past end of string");
    
    output = MVM_string_utf8_encode_substr(tc, str, &output_size, start, length);
    bytes_written = (apr_size_t) output_size;
    if ((rv = apr_file_write(handle->body.file_handle, (const void *)output, &bytes_written)) != APR_SUCCESS) {
        free(output);
        MVM_exception_throw_apr_error(tc, rv, "Failed to write bytes to filehandle: tried to write %u, wrote %u: ", output_size, bytes_written);
    }
    free(output);
}

/* return an OSHandle representing stdout */
MVMObject * MVM_file_get_stdout(MVMThreadContext *tc) {
    MVMOSHandle *result = (MVMOSHandle *)get_oshandle_repr(tc)->allocate(tc, NULL);
    apr_file_t  *handle;
    
    apr_file_open_stdout(&handle, POOL(tc));
    result->body.file_handle = handle;
    
    return (MVMObject *)result;
}