#include "moar.h"

/* Delegatory functions that assert we have a capable handle, then delegate
 * through the IO table to the correct operation. */

static MVMOSHandle * verify_is_handle(MVMThreadContext *tc, MVMObject *oshandle, const char *op) {
    if (REPR(oshandle)->ID != MVM_REPR_ID_MVMOSHandle)
        MVM_exception_throw_adhoc(tc, "%s requires an object with REPR MVMOSHandle", op);
    return (MVMOSHandle *)oshandle;
}

void MVM_io_close(MVMThreadContext *tc, MVMObject *oshandle) {
    MVMOSHandle *handle = verify_is_handle(tc, oshandle, "close");
    if (handle->body.ops->closable)
        handle->body.ops->closable->close(tc, handle);
    else
        MVM_exception_throw_adhoc(tc, "Cannot close this kind of handle");
}

void MVM_io_set_encoding(MVMThreadContext *tc, MVMObject *oshandle, MVMString *encoding_name) {
    MVMOSHandle *handle = verify_is_handle(tc, oshandle, "set encoding");
    MVMROOT(tc, handle, {
        const MVMuint8 encoding_flag = MVM_string_find_encoding(tc, encoding_name);
        if (handle->body.ops->encodable)
            handle->body.ops->encodable->set_encoding(tc, handle, encoding_flag);
        else
            MVM_exception_throw_adhoc(tc, "Cannot set encoding on this kind of handle");
    });
}

void MVM_io_seek(MVMThreadContext *tc, MVMObject *oshandle, MVMint64 offset, MVMint64 flag) {
    MVMOSHandle *handle = verify_is_handle(tc, oshandle, "seek");
    if (handle->body.ops->seekable)
        handle->body.ops->seekable->seek(tc, handle, offset, flag);
    else
        MVM_exception_throw_adhoc(tc, "Cannot seek this kind of handle");
}

MVMint64 MVM_io_tell(MVMThreadContext *tc, MVMObject *oshandle) {
    MVMOSHandle *handle = verify_is_handle(tc, oshandle, "tell");
    if (handle->body.ops->seekable)
        return handle->body.ops->seekable->tell(tc, handle);
    else
        MVM_exception_throw_adhoc(tc, "Cannot tell this kind of handle");
}

MVMString * MVM_io_readline(MVMThreadContext *tc, MVMObject *oshandle) {
    MVMOSHandle *handle = verify_is_handle(tc, oshandle, "readline");
    if (handle->body.ops->sync_readable)
        return handle->body.ops->sync_readable->read_line(tc, handle);
    else
        MVM_exception_throw_adhoc(tc, "Cannot read lines from this kind of handle");
}

MVMString * MVM_io_read_string(MVMThreadContext *tc, MVMObject *oshandle, MVMint64 chars) {
    MVMOSHandle *handle = verify_is_handle(tc, oshandle, "read string");
    if (handle->body.ops->sync_readable)
        return handle->body.ops->sync_readable->read_chars(tc, handle, chars);
    else
        MVM_exception_throw_adhoc(tc, "Cannot read charcaters this kind of handle");
}

void MVM_io_read_bytes(MVMThreadContext *tc, MVMObject *oshandle, MVMObject *result, MVMint64 length) {
    MVMOSHandle *handle = verify_is_handle(tc, oshandle, "read bytes");
    MVMint64 bytes_read;
    uv_fs_t req;
    char *buf;

    /* Ensure the target is in the correct form. */
    if (!IS_CONCRETE(result) || REPR(result)->ID != MVM_REPR_ID_MVMArray)
        MVM_exception_throw_adhoc(tc, "read_fhb requires a native array to write to");
    if (((MVMArrayREPRData *)STABLE(result)->REPR_data)->slot_type != MVM_ARRAY_U8
        && ((MVMArrayREPRData *)STABLE(result)->REPR_data)->slot_type != MVM_ARRAY_I8)
        MVM_exception_throw_adhoc(tc, "read_fhb requires a native array of uint8 or int8");

    if (length < 1 || length > 99999999)
        MVM_exception_throw_adhoc(tc, "read from filehandle length out of range");

    if (handle->body.ops->sync_readable)
        bytes_read = handle->body.ops->sync_readable->read_bytes(tc, handle, &buf, length);
    else
        MVM_exception_throw_adhoc(tc, "Cannot read charcaters this kind of handle");

    /* Stash the data in the VMArray. */
    ((MVMArray *)result)->body.slots.i8 = (MVMint8 *)buf;
    ((MVMArray *)result)->body.start    = 0;
    ((MVMArray *)result)->body.ssize    = bytes_read;
    ((MVMArray *)result)->body.elems    = bytes_read;
}

MVMString * MVM_io_slurp(MVMThreadContext *tc, MVMObject *oshandle) {
    MVMOSHandle *handle = verify_is_handle(tc, oshandle, "slurp");
    if (handle->body.ops->sync_readable)
        return handle->body.ops->sync_readable->slurp(tc, handle);
    else
        MVM_exception_throw_adhoc(tc, "Cannot slurp this kind of handle");
}

MVMint64 MVM_io_write_string(MVMThreadContext *tc, MVMObject *oshandle, MVMString *str, MVMint8 addnl) {
    MVMOSHandle *handle = verify_is_handle(tc, oshandle, "write string");
    if (str == NULL)
        MVM_exception_throw_adhoc(tc, "Failed to write to filehandle: NULL string given");
    if (handle->body.ops->sync_writable)
        return handle->body.ops->sync_writable->write_str(tc, handle, str, addnl);
    else
        MVM_exception_throw_adhoc(tc, "Cannot write a string to this kind of handle");
}

void MVM_io_write_bytes(MVMThreadContext *tc, MVMObject *oshandle, MVMObject *buffer) {
    MVMOSHandle *handle = verify_is_handle(tc, oshandle, "write bytes");
    MVMuint8 *output;
    MVMint64 output_size;
    MVMint64 bytes_written;

    /* Ensure the target is in the correct form. */
    if (!IS_CONCRETE(buffer) || REPR(buffer)->ID != MVM_REPR_ID_MVMArray)
        MVM_exception_throw_adhoc(tc, "write_fhb requires a native array to read from");
    if (((MVMArrayREPRData *)STABLE(buffer)->REPR_data)->slot_type != MVM_ARRAY_U8
        && ((MVMArrayREPRData *)STABLE(buffer)->REPR_data)->slot_type != MVM_ARRAY_I8)
        MVM_exception_throw_adhoc(tc, "write_fhb requires a native array of uint8 or int8");

    output = ((MVMArray *)buffer)->body.slots.i8 + ((MVMArray *)buffer)->body.start;
    output_size = ((MVMArray *)buffer)->body.elems;

    if (handle->body.ops->sync_writable)
        bytes_written = handle->body.ops->sync_writable->write_bytes(tc, handle, output, output_size);
    else
        MVM_exception_throw_adhoc(tc, "Cannot write bytes to this kind of handle");
}

MVMint64 MVM_io_eof(MVMThreadContext *tc, MVMObject *oshandle) {
    MVMOSHandle *handle = verify_is_handle(tc, oshandle, "eof");
    if (handle->body.ops->sync_readable)
        return handle->body.ops->sync_readable->eof(tc, handle);
    else
        MVM_exception_throw_adhoc(tc, "Cannot eof this kind of handle");
}

MVMint64 MVM_io_lock(MVMThreadContext *tc, MVMObject *oshandle, MVMint64 flag) {
    MVMOSHandle *handle = verify_is_handle(tc, oshandle, "lock");
    if (handle->body.ops->lockable)
        return handle->body.ops->lockable->lock(tc, handle, flag);
    else
        MVM_exception_throw_adhoc(tc, "Cannot lock this kind of handle");
}

void MVM_io_unlock(MVMThreadContext *tc, MVMObject *oshandle) {
    MVMOSHandle *handle = verify_is_handle(tc, oshandle, "unlock");
    if (handle->body.ops->lockable)
        handle->body.ops->lockable->unlock(tc, handle);
    else
        MVM_exception_throw_adhoc(tc, "Cannot unlock this kind of handle");
}

void MVM_io_flush(MVMThreadContext *tc, MVMObject *oshandle) {
    MVMOSHandle *handle = verify_is_handle(tc, oshandle, "flush");
    if (handle->body.ops->sync_writable)
        handle->body.ops->sync_writable->flush(tc, handle);
    else
        MVM_exception_throw_adhoc(tc, "Cannot flush this kind of handle");
}

void MVM_io_truncate(MVMThreadContext *tc, MVMObject *oshandle, MVMint64 offset) {
    MVMOSHandle *handle = verify_is_handle(tc, oshandle, "truncate");
    if (handle->body.ops->sync_writable)
        handle->body.ops->sync_writable->truncate(tc, handle, offset);
    else
        MVM_exception_throw_adhoc(tc, "Cannot truncate this kind of handle");
}
