#include "moar.h"

/* Delegatory functions that assert we have a capable handle, then delegate
 * through the IO table to the correct operation. */

static MVMOSHandle * verify_is_handle(MVMThreadContext *tc, MVMObject *oshandle, const char *op) {
    if (REPR(oshandle)->ID != MVM_REPR_ID_MVMOSHandle)
        MVM_exception_throw_adhoc(tc, "%s requires an object with REPR MVMOSHandle (got %s with REPR %s)", op, STABLE(oshandle)->debug_name, REPR(oshandle)->name);
    return (MVMOSHandle *)oshandle;
}

static uv_mutex_t * acquire_mutex(MVMThreadContext *tc, MVMOSHandle *handle) {
    uv_mutex_t *mutex = handle->body.mutex;
    uv_mutex_lock(mutex);
    MVM_tc_set_ex_release_mutex(tc, mutex);
    return mutex;
}

static void release_mutex(MVMThreadContext *tc, uv_mutex_t *mutex) {
    uv_mutex_unlock(mutex);
    MVM_tc_clear_ex_release_mutex(tc);
}

MVMint64 MVM_io_close(MVMThreadContext *tc, MVMObject *oshandle) {
    MVMOSHandle *handle = verify_is_handle(tc, oshandle, "close");
    if (handle->body.ops->closable) {
        uv_mutex_t *mutex = acquire_mutex(tc, handle);
        MVMint64 ret = handle->body.ops->closable->close(tc, handle);
        release_mutex(tc, mutex);
        return ret;
    }
    else
        MVM_exception_throw_adhoc(tc, "Cannot close this kind of handle");
}

MVMint64 MVM_io_is_tty(MVMThreadContext *tc, MVMObject *oshandle) {
    MVMOSHandle *handle = verify_is_handle(tc, oshandle, "istty");
    /* We need the extra check on is_tty because it is NULL for pipes. */
    if (handle->body.ops->introspection && handle->body.ops->introspection->is_tty) {
        uv_mutex_t *mutex = acquire_mutex(tc, handle);
        MVMint64 ret = handle->body.ops->introspection->is_tty(tc, handle);
        release_mutex(tc, mutex);
        return ret;
    }
    else {
        return 0;
    }
}

MVMint64 MVM_io_fileno(MVMThreadContext *tc, MVMObject *oshandle) {
    MVMOSHandle *handle = verify_is_handle(tc, oshandle, "get native descriptor");
    if (handle->body.ops->introspection) {
        uv_mutex_t *mutex = acquire_mutex(tc, handle);
        MVMint64 ret = handle->body.ops->introspection->native_descriptor(tc, handle);
        release_mutex(tc, mutex);
        return ret;
    }
    else {
        return -1;
    }
}

void MVM_io_set_encoding(MVMThreadContext *tc, MVMObject *oshandle, MVMString *encoding_name) {
    MVMOSHandle *handle = verify_is_handle(tc, oshandle, "set encoding");
    MVMROOT(tc, handle, {
        const MVMuint8 encoding_flag = MVM_string_find_encoding(tc, encoding_name);
        if (handle->body.ops->encodable) {
            uv_mutex_t *mutex = acquire_mutex(tc, handle);
            handle->body.ops->encodable->set_encoding(tc, handle, encoding_flag);
            release_mutex(tc, mutex);
        }
        else
            MVM_exception_throw_adhoc(tc, "Cannot set encoding on this kind of handle");
    });
}

void MVM_io_seek(MVMThreadContext *tc, MVMObject *oshandle, MVMint64 offset, MVMint64 flag) {
    MVMOSHandle *handle = verify_is_handle(tc, oshandle, "seek");
    if (handle->body.ops->seekable) {
        uv_mutex_t *mutex = acquire_mutex(tc, handle);
        handle->body.ops->seekable->seek(tc, handle, offset, flag);
        release_mutex(tc, mutex);
    }
    else
        MVM_exception_throw_adhoc(tc, "Cannot seek this kind of handle");
}

MVMint64 MVM_io_tell(MVMThreadContext *tc, MVMObject *oshandle) {
    MVMOSHandle *handle = verify_is_handle(tc, oshandle, "tell");
    if (handle->body.ops->seekable) {
        uv_mutex_t *mutex = acquire_mutex(tc, handle);
        MVMint64 result = handle->body.ops->seekable->tell(tc, handle);
        release_mutex(tc, mutex);
        return result;
    }
    else
        MVM_exception_throw_adhoc(tc, "Cannot tell this kind of handle");
}

void MVM_io_set_separator(MVMThreadContext *tc, MVMObject *oshandle, MVMString *sep) {
    MVMOSHandle *handle = verify_is_handle(tc, oshandle, "set separator");
    if (handle->body.ops->sync_readable) {
        uv_mutex_t *mutex = acquire_mutex(tc, handle);
        handle->body.ops->sync_readable->set_separator(tc, handle, &sep, 1);
        release_mutex(tc, mutex);
    }
    else
        MVM_exception_throw_adhoc(tc, "Cannot set a separator on this kind of handle");
}

void MVM_io_set_separators(MVMThreadContext *tc, MVMObject *oshandle, MVMObject *seps) {
    MVMOSHandle *handle = verify_is_handle(tc, oshandle, "set separators");
    if (handle->body.ops->sync_readable) {
        MVMint32 is_str_array = REPR(seps)->pos_funcs.get_elem_storage_spec(tc,
            STABLE(seps)).boxed_primitive == MVM_STORAGE_SPEC_BP_STR;
        if (is_str_array) {
            uv_mutex_t *mutex;
            MVMString **c_seps;
            MVMuint64 i;

            MVMuint64 num_seps = MVM_repr_elems(tc, seps);
            if (num_seps > 0xFFFFFF)
                MVM_exception_throw_adhoc(tc, "Too many line separators");
            c_seps = MVM_malloc((num_seps ? num_seps : 1) * sizeof(MVMString *));
            for (i = 0; i < num_seps; i++)
                c_seps[i] = MVM_repr_at_pos_s(tc, seps, i);

            mutex = acquire_mutex(tc, handle);
            handle->body.ops->sync_readable->set_separator(tc, handle, c_seps, (MVMint32)num_seps);
            release_mutex(tc, mutex);

            MVM_free(c_seps);
        }
        else {
            MVM_exception_throw_adhoc(tc, "Set separators requires a native string array");
        }
    }
    else {
        MVM_exception_throw_adhoc(tc, "Cannot set separators on this kind of handle");
    }
}

MVMString * MVM_io_readline(MVMThreadContext *tc, MVMObject *oshandle, MVMint32 chomp) {
    MVMOSHandle *handle = verify_is_handle(tc, oshandle, "readline");
    if (handle->body.ops->sync_readable) {
        uv_mutex_t *mutex = acquire_mutex(tc, handle);
        MVMString *result = handle->body.ops->sync_readable->read_line(tc, handle, chomp);
        release_mutex(tc, mutex);
        return result;
    }
    else
        MVM_exception_throw_adhoc(tc, "Cannot read lines from this kind of handle");
}

MVMString * MVM_io_read_string(MVMThreadContext *tc, MVMObject *oshandle, MVMint64 chars) {
    MVMOSHandle *handle = verify_is_handle(tc, oshandle, "read string");
    if (handle->body.ops->sync_readable) {
        uv_mutex_t *mutex = acquire_mutex(tc, handle);
        MVMString *result = handle->body.ops->sync_readable->read_chars(tc, handle, chars);
        release_mutex(tc, mutex);
        return result;
    }
    else
        MVM_exception_throw_adhoc(tc, "Cannot read characters from this kind of handle");
}

void MVM_io_read_bytes(MVMThreadContext *tc, MVMObject *oshandle, MVMObject *result, MVMint64 length) {
    MVMOSHandle *handle = verify_is_handle(tc, oshandle, "read bytes");
    MVMint64 bytes_read;
    char *buf;

    /* Ensure the target is in the correct form. */
    if (!IS_CONCRETE(result) || REPR(result)->ID != MVM_REPR_ID_VMArray)
        MVM_exception_throw_adhoc(tc, "read_fhb requires a native array to write to");
    if (((MVMArrayREPRData *)STABLE(result)->REPR_data)->slot_type != MVM_ARRAY_U8
        && ((MVMArrayREPRData *)STABLE(result)->REPR_data)->slot_type != MVM_ARRAY_I8)
        MVM_exception_throw_adhoc(tc, "read_fhb requires a native array of uint8 or int8");

    if (length < 1)
        MVM_exception_throw_adhoc(tc, "Out of range: attempted to read %"PRId64" bytes from filehandle", length);

    if (handle->body.ops->sync_readable) {
        MVMROOT(tc, handle, {
        MVMROOT(tc, result, {
            uv_mutex_t *mutex = acquire_mutex(tc, handle);
            bytes_read = handle->body.ops->sync_readable->read_bytes(tc, handle, &buf, length);
            release_mutex(tc, mutex);
        });
        });
    }
    else
        MVM_exception_throw_adhoc(tc, "Cannot read characters from this kind of handle");

    /* Stash the data in the VMArray. */
    ((MVMArray *)result)->body.slots.i8 = (MVMint8 *)buf;
    ((MVMArray *)result)->body.start    = 0;
    ((MVMArray *)result)->body.ssize    = bytes_read;
    ((MVMArray *)result)->body.elems    = bytes_read;
}

MVMString * MVM_io_slurp(MVMThreadContext *tc, MVMObject *oshandle) {
    MVMOSHandle *handle = verify_is_handle(tc, oshandle, "slurp");
    if (handle->body.ops->sync_readable) {
        uv_mutex_t *mutex = acquire_mutex(tc, handle);
        MVMString *result = handle->body.ops->sync_readable->slurp(tc, handle);
        release_mutex(tc, mutex);
        return result;
    }
    else
        MVM_exception_throw_adhoc(tc, "Cannot slurp this kind of handle");
}

MVMint64 MVM_io_write_string(MVMThreadContext *tc, MVMObject *oshandle, MVMString *str, MVMint8 addnl) {
    MVMOSHandle *handle = verify_is_handle(tc, oshandle, "write string");
    if (str == NULL)
        MVM_exception_throw_adhoc(tc, "Failed to write to filehandle: NULL string given");
    if (handle->body.ops->sync_writable) {
        uv_mutex_t *mutex = acquire_mutex(tc, handle);
        MVMint64 result = handle->body.ops->sync_writable->write_str(tc, handle, str, addnl);
        release_mutex(tc, mutex);
        return result;
    }
    else
        MVM_exception_throw_adhoc(tc, "Cannot write a string to this kind of handle");
}

void MVM_io_write_bytes(MVMThreadContext *tc, MVMObject *oshandle, MVMObject *buffer) {
    MVMOSHandle *handle = verify_is_handle(tc, oshandle, "write bytes");
    char *output;
    MVMint64 output_size;

    /* Ensure the target is in the correct form. */
    if (!IS_CONCRETE(buffer) || REPR(buffer)->ID != MVM_REPR_ID_VMArray)
        MVM_exception_throw_adhoc(tc, "write_fhb requires a native array to read from");
    if (((MVMArrayREPRData *)STABLE(buffer)->REPR_data)->slot_type != MVM_ARRAY_U8
        && ((MVMArrayREPRData *)STABLE(buffer)->REPR_data)->slot_type != MVM_ARRAY_I8)
        MVM_exception_throw_adhoc(tc, "write_fhb requires a native array of uint8 or int8");

    output = (char *)(((MVMArray *)buffer)->body.slots.i8 + ((MVMArray *)buffer)->body.start);
    output_size = ((MVMArray *)buffer)->body.elems;

    if (handle->body.ops->sync_writable) {
        uv_mutex_t *mutex = acquire_mutex(tc, handle);
        handle->body.ops->sync_writable->write_bytes(tc, handle, output, output_size);
        release_mutex(tc, mutex);
    }
    else
        MVM_exception_throw_adhoc(tc, "Cannot write bytes to this kind of handle");
}

MVMObject * MVM_io_read_bytes_async(MVMThreadContext *tc, MVMObject *oshandle, MVMObject *queue,
                                    MVMObject *schedulee, MVMObject *buf_type, MVMObject *async_type) {
    MVMOSHandle *handle = verify_is_handle(tc, oshandle, "read bytes asynchronously");
    if (handle->body.ops->async_readable) {
        uv_mutex_t *mutex = acquire_mutex(tc, handle);
        MVMObject *result = (MVMObject *)handle->body.ops->async_readable->read_bytes(tc,
            handle, queue, schedulee, buf_type, async_type);
        release_mutex(tc, mutex);
        return result;
    }
    else
        MVM_exception_throw_adhoc(tc, "Cannot read bytes asynchronously from this kind of handle");
}

MVMObject * MVM_io_write_bytes_async(MVMThreadContext *tc, MVMObject *oshandle, MVMObject *queue,
                                     MVMObject *schedulee, MVMObject *buffer, MVMObject *async_type) {
    MVMOSHandle *handle = verify_is_handle(tc, oshandle, "write buffer asynchronously");
    if (buffer == NULL)
        MVM_exception_throw_adhoc(tc, "Failed to write to filehandle: NULL buffer given");
    if (handle->body.ops->async_writable) {
        uv_mutex_t *mutex = acquire_mutex(tc, handle);
        MVMObject *result = (MVMObject *)handle->body.ops->async_writable->write_bytes(tc,
            handle, queue, schedulee, buffer, async_type);
        release_mutex(tc, mutex);
        return result;
    }
    else
        MVM_exception_throw_adhoc(tc, "Cannot write bytes asynchronously to this kind of handle");
}

MVMObject * MVM_io_write_bytes_to_async(MVMThreadContext *tc, MVMObject *oshandle, MVMObject *queue,
                                        MVMObject *schedulee, MVMObject *buffer, MVMObject *async_type,
                                        MVMString *host, MVMint64 port) {
    MVMOSHandle *handle = verify_is_handle(tc, oshandle, "write buffer asynchronously to destination");
    if (buffer == NULL)
        MVM_exception_throw_adhoc(tc, "Failed to write to filehandle: NULL buffer given");
    if (handle->body.ops->async_writable_to) {
        uv_mutex_t *mutex = acquire_mutex(tc, handle);
        MVMObject *result = (MVMObject *)handle->body.ops->async_writable_to->write_bytes_to(tc,
            handle, queue, schedulee, buffer, async_type, host, port);
        release_mutex(tc, mutex);
        return result;
    }
    else
        MVM_exception_throw_adhoc(tc, "Cannot write bytes to a destination asynchronously to this kind of handle");
}

MVMint64 MVM_io_eof(MVMThreadContext *tc, MVMObject *oshandle) {
    MVMOSHandle *handle = verify_is_handle(tc, oshandle, "eof");
    if (handle->body.ops->sync_readable) {
        uv_mutex_t *mutex = acquire_mutex(tc, handle);
        MVMint64 result = handle->body.ops->sync_readable->eof(tc, handle);
        release_mutex(tc, mutex);
        return result;
    }
    else
        MVM_exception_throw_adhoc(tc, "Cannot eof this kind of handle");
}

MVMint64 MVM_io_lock(MVMThreadContext *tc, MVMObject *oshandle, MVMint64 flag) {
    MVMOSHandle *handle = verify_is_handle(tc, oshandle, "lock");
    if (handle->body.ops->lockable) {
        uv_mutex_t *mutex = acquire_mutex(tc, handle);
        MVMint64 result = handle->body.ops->lockable->lock(tc, handle, flag);
        release_mutex(tc, mutex);
        return result;
    }
    else
        MVM_exception_throw_adhoc(tc, "Cannot lock this kind of handle");
}

void MVM_io_unlock(MVMThreadContext *tc, MVMObject *oshandle) {
    MVMOSHandle *handle = verify_is_handle(tc, oshandle, "unlock");
    if (handle->body.ops->lockable) {
        uv_mutex_t *mutex = acquire_mutex(tc, handle);
        handle->body.ops->lockable->unlock(tc, handle);
        release_mutex(tc, mutex);
    }
    else
        MVM_exception_throw_adhoc(tc, "Cannot unlock this kind of handle");
}

void MVM_io_flush(MVMThreadContext *tc, MVMObject *oshandle) {
    MVMOSHandle *handle = verify_is_handle(tc, oshandle, "flush");
    if (handle->body.ops->sync_writable) {
        uv_mutex_t *mutex = acquire_mutex(tc, handle);
        handle->body.ops->sync_writable->flush(tc, handle);
        release_mutex(tc, mutex);
    }
    else
        MVM_exception_throw_adhoc(tc, "Cannot flush this kind of handle");
}

void MVM_io_truncate(MVMThreadContext *tc, MVMObject *oshandle, MVMint64 offset) {
    MVMOSHandle *handle = verify_is_handle(tc, oshandle, "truncate");
    if (handle->body.ops->sync_writable) {
        uv_mutex_t *mutex = acquire_mutex(tc, handle);
        handle->body.ops->sync_writable->truncate(tc, handle, offset);
        release_mutex(tc, mutex);
    }
    else
        MVM_exception_throw_adhoc(tc, "Cannot truncate this kind of handle");
}

void MVM_io_connect(MVMThreadContext *tc, MVMObject *oshandle, MVMString *host, MVMint64 port) {
    MVMOSHandle *handle = verify_is_handle(tc, oshandle, "connect");
    if (handle->body.ops->sockety) {
        uv_mutex_t *mutex = acquire_mutex(tc, handle);
        handle->body.ops->sockety->connect(tc, handle, host, port);
        release_mutex(tc, mutex);
    }
    else
        MVM_exception_throw_adhoc(tc, "Cannot connect this kind of handle");
}

void MVM_io_bind(MVMThreadContext *tc, MVMObject *oshandle, MVMString *host, MVMint64 port, MVMint32 backlog) {
    MVMOSHandle *handle = verify_is_handle(tc, oshandle, "bind");
    if (handle->body.ops->sockety) {
        uv_mutex_t *mutex = acquire_mutex(tc, handle);
        handle->body.ops->sockety->bind(tc, handle, host, port, backlog);
        release_mutex(tc, mutex);
    }
    else
        MVM_exception_throw_adhoc(tc, "Cannot bind this kind of handle");
}

MVMObject * MVM_io_accept(MVMThreadContext *tc, MVMObject *oshandle) {
    MVMOSHandle *handle = verify_is_handle(tc, oshandle, "accept");
    if (handle->body.ops->sockety) {
        uv_mutex_t *mutex = acquire_mutex(tc, handle);
        MVMObject *result = handle->body.ops->sockety->accept(tc, handle);
        release_mutex(tc, mutex);
        return result;
    }
    else
        MVM_exception_throw_adhoc(tc, "Cannot accept this kind of handle");
}
