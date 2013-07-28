#include "moarvm.h"

#define POOL(tc) (*(tc->interp_cu))->pool

static void verify_dirhandle_type(MVMThreadContext *tc, MVMObject *oshandle, MVMOSHandle **handle, const char *msg) {

    /* work on only MVMOSHandle of type MVM_OSHANDLE_DIR */
    if (REPR(oshandle)->ID != MVM_REPR_ID_MVMOSHandle) {
        MVM_exception_throw_adhoc(tc, "%s requires an object with REPR MVMOSHandle", msg);
    }
    *handle = (MVMOSHandle *)oshandle;
    if ((*handle)->body.handle_type != MVM_OSHANDLE_DIR) {
        MVM_exception_throw_adhoc(tc, "%s requires an MVMOSHandle of type dir handle", msg);
    }
}

/* create a directory recursively */
void MVM_dir_mkdir(MVMThreadContext *tc, MVMString *f, MVMint64 mode) {
    apr_status_t rv;
    apr_pool_t *tmp_pool;
    char *a;

    /* need a temporary pool */
    if ((rv = apr_pool_create(&tmp_pool, POOL(tc))) != APR_SUCCESS) {
        MVM_exception_throw_apr_error(tc, rv, "Failed to mkdir: ");
    }

    a = MVM_string_utf8_encode_C_string(tc, f);

    if ((rv = apr_dir_make_recursive((const char *)a, MVM_get_apr_perms(mode), tmp_pool)) != APR_SUCCESS) {
        free(a);
        apr_pool_destroy(tmp_pool);
        MVM_exception_throw_apr_error(tc, rv, "Failed to mkdir: ");
    }
    free(a);
    apr_pool_destroy(tmp_pool);
}

/* remove a directory recursively */
void MVM_dir_rmdir(MVMThreadContext *tc, MVMString *f) {
    apr_status_t rv;
    apr_pool_t *tmp_pool;
    char *a;

    /* need a temporary pool */
    if ((rv = apr_pool_create(&tmp_pool, POOL(tc))) != APR_SUCCESS) {
        MVM_exception_throw_apr_error(tc, rv, "Failed to rmdir: ");
    }

    a = MVM_string_utf8_encode_C_string(tc, f);

    if ((rv = apr_dir_remove((const char *)a, tmp_pool)) != APR_SUCCESS) {
        free(a);
        apr_pool_destroy(tmp_pool);
        MVM_exception_throw_apr_error(tc, rv, "Failed to rmdir: ");
    }
    free(a);
    apr_pool_destroy(tmp_pool);
}

/* open a filehandle; takes a type object */
MVMObject * MVM_dir_open(MVMThreadContext *tc, MVMObject *type_object, MVMString *dirname, MVMint64 encoding_flag) {
    MVMOSHandle *result;
    apr_status_t rv;
    apr_pool_t *tmp_pool;
    apr_dir_t *dir_handle;
    char *dname = MVM_string_utf8_encode_C_string(tc, dirname);

    ENCODING_VALID(encoding_flag);

    if (REPR(type_object)->ID != MVM_REPR_ID_MVMOSHandle || IS_CONCRETE(type_object)) {
        MVM_exception_throw_adhoc(tc, "Open dir needs a type object with MVMOSHandle REPR");
    }

    /* need a temporary pool */
    if ((rv = apr_pool_create(&tmp_pool, POOL(tc))) != APR_SUCCESS) {
        free(dname);
        MVM_exception_throw_apr_error(tc, rv, "Open dir failed to create pool: ");
    }

    /* try to open the dir */
    if ((rv = apr_dir_open(&dir_handle, (const char *)dname, tmp_pool)) != APR_SUCCESS) {
        free(dname);
        MVM_exception_throw_apr_error(tc, rv, "Failed to open dir: ");
    }

    free(dname);

    /* initialize the object */
    result = (MVMOSHandle *)REPR(type_object)->allocate(tc, STABLE(type_object));

    result->body.dir_handle = dir_handle;
    result->body.handle_type = MVM_OSHANDLE_DIR;
    result->body.mem_pool = tmp_pool;
    result->body.encoding_type = encoding_flag;

    return (MVMObject *)result;
}

/* reads a directory entry from a directory.  Assumes utf8 for now */
MVMString * MVM_dir_read(MVMThreadContext *tc, MVMObject *oshandle) {
    MVMString *result;
    apr_status_t rv;
    MVMOSHandle *handle;
    apr_finfo_t *finfo = (apr_finfo_t *)malloc(sizeof(apr_finfo_t));

    verify_dirhandle_type(tc, oshandle, &handle, "read from dirhandle");

    if ((rv = apr_dir_read(finfo, APR_FINFO_NAME, handle->body.dir_handle)) != APR_SUCCESS && rv != APR_ENOENT && rv != 720018) {
        printf("rv is %d\n", rv);
        MVM_exception_throw_apr_error(tc, rv, "read from dirhandle failed: ");
    }

    /* TODO investigate magic number 720018 */
    if (rv == APR_ENOENT || rv == 720018) { /* no more entries in the directory */
        /* XXX TODO: reference some process global empty string instead of creating one */
        result = MVM_decode_C_buffer_to_string(tc, tc->instance->VMString, "", 0, handle->body.encoding_type);
    }
    else {
        result = MVM_decode_C_buffer_to_string(tc, tc->instance->VMString, (char *)finfo->name, strlen(finfo->name), handle->body.encoding_type);
    }

    free(finfo);

    return result;
}

void MVM_dir_close(MVMThreadContext *tc, MVMObject *oshandle) {
    apr_status_t rv;
    MVMOSHandle *handle;

    verify_dirhandle_type(tc, oshandle, &handle, "close dirhandle");

    if ((rv = apr_dir_close(handle->body.dir_handle)) != APR_SUCCESS) {
        MVM_exception_throw_apr_error(tc, rv, "Failed to close dirhandle: ");
    }
}

void MVM_dir_chdir(MVMThreadContext *tc, MVMString *dir) {
    char *dirstring = MVM_string_utf8_encode_C_string(tc, dir);

    if (chdir((const char *)dirstring) != 0) {
        free(dirstring);
        MVM_exception_throw_adhoc(tc, "chdir failed: %s", strerror(errno));
    }

    free(dirstring);
}
