#include "moarvm.h"

#define POOL(tc) (*(tc->interp_cu))->pool

static void verify_dirhandle_type(MVMThreadContext *tc, MVMObject *oshandle, MVMOSHandle **handle, const char *msg) {

    /* work on only MVMOSHandle of type MVM_OSHANDLE_DIR */
    if (REPR(oshandle)->ID != MVM_REPR_ID_MVMOSHandle) {
        MVM_exception_throw_adhoc(tc, "%s requires an object with REPR MVMOSHandle");
    }
    *handle = (MVMOSHandle *)oshandle;
    if ((*handle)->body.handle_type != MVM_OSHANDLE_DIR) {
        MVM_exception_throw_adhoc(tc, "%s requires an MVMOSHandle of type dir handle");
    }
}

/* create a directory recursively */
void MVM_dir_mkdir(MVMThreadContext *tc, MVMString *f) {
    apr_status_t rv;
    apr_pool_t *tmp_pool;
    const char *a;
    
    /* need a temporary pool */
    if ((rv = apr_pool_create(&tmp_pool, POOL(tc))) != APR_SUCCESS) {
        MVM_exception_throw_apr_error(tc, rv, "Failed to mkdir: ");
    }
    
    a = (const char *) MVM_string_utf8_encode_C_string(tc, f);
    
    if ((rv = apr_dir_make_recursive(a, APR_FPROT_OS_DEFAULT, tmp_pool)) != APR_SUCCESS) {
        apr_pool_destroy(tmp_pool);
        MVM_exception_throw_apr_error(tc, rv, "Failed to mkdir: ");
    }
    apr_pool_destroy(tmp_pool);
}

/* remove a directory recursively */
void MVM_dir_rmdir(MVMThreadContext *tc, MVMString *f) {
    apr_status_t rv;
    apr_pool_t *tmp_pool;
    const char *a;
    
    /* need a temporary pool */
    if ((rv = apr_pool_create(&tmp_pool, POOL(tc))) != APR_SUCCESS) {
        MVM_exception_throw_apr_error(tc, rv, "Failed to rmdir: ");
    }
    
    a = (const char *) MVM_string_utf8_encode_C_string(tc, f);
    
    if ((rv = apr_dir_remove(a, tmp_pool)) != APR_SUCCESS) {
        apr_pool_destroy(tmp_pool);
        MVM_exception_throw_apr_error(tc, rv, "Failed to rmdir: ");
    }
    apr_pool_destroy(tmp_pool);
}

/* open a filehandle; takes a type object */
MVMObject * MVM_dir_open(MVMThreadContext *tc, MVMObject *type_object, MVMString *dirname) {
    MVMOSHandle *result;
    apr_status_t rv;
    apr_pool_t *tmp_pool;
    apr_dir_t *dir_handle;
    char *dname = MVM_string_utf8_encode_C_string(tc, dirname);
    
    if (REPR(type_object)->ID != MVM_REPR_ID_MVMOSHandle || IS_CONCRETE(type_object)) {
        MVM_exception_throw_adhoc(tc, "Open dir needs a type object with MVMOSHandle REPR");
    }
    
    /* need a temporary pool */
    if ((rv = apr_pool_create(&tmp_pool, POOL(tc))) != APR_SUCCESS) {
        MVM_exception_throw_apr_error(tc, rv, "Open dir failed to create pool: ");
    }
    
    /* try to open the dir */
    if ((rv = apr_dir_open(&dir_handle, (const char *)dname, tmp_pool)) != APR_SUCCESS) {
        MVM_exception_throw_apr_error(tc, rv, "Failed to open dir: ");
    }
    
    /* initialize the object */
    result = (MVMOSHandle *)REPR(type_object)->allocate(tc, STABLE(type_object));
    
    result->body.dir_handle = dir_handle;
    result->body.handle_type = MVM_OSHANDLE_DIR;
    result->body.mem_pool = tmp_pool;
    
    return (MVMObject *)result;
}

/* reads a directory entry from a directory.  Assumes utf8 for now */
MVMString * MVM_dir_read(MVMThreadContext *tc, MVMObject *oshandle) {
    MVMString *result;
    apr_status_t rv;
    MVMOSHandle *handle;
    apr_finfo_t *finfo = (apr_finfo_t *)malloc(sizeof(apr_finfo_t));
    
    verify_dirhandle_type(tc, oshandle, &handle, "read from dirhandle");
    
    if ((rv = apr_dir_read(finfo, APR_FINFO_NAME, handle->body.dir_handle)) != APR_SUCCESS && rv != APR_ENOENT) {
        MVM_exception_throw_apr_error(tc, rv, "read from dirhandle failed: ");
    }
    
    if (rv == APR_ENOENT) { /* no more entries in the directory */
        /* XXX TODO: reference some process global empty string instead of creating one */
        result = MVM_string_utf8_decode(tc, tc->instance->boot_types->BOOTStr, "", 0);
    }
    else {
        result = MVM_string_utf8_decode(tc, tc->instance->boot_types->BOOTStr, (char *)finfo->name, strlen(finfo->name));
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
