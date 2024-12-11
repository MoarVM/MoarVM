#include "moar.h"

/* Delegatory functions that assert we have a capable handle, then delegate
 * through the IO table to the correct operation. */

static MVMOSHandle * verify_is_handle(MVMThreadContext *tc, MVMObject *oshandle, const char *op) {
    if (REPR(oshandle)->ID != MVM_REPR_ID_MVMOSHandle)
        MVM_exception_throw_adhoc(tc, "%s requires an object with REPR MVMOSHandle (got %s with REPR %s)", op, MVM_6model_get_debug_name(tc, oshandle), REPR(oshandle)->name);
    if (!IS_CONCRETE(oshandle))
        MVM_exception_throw_adhoc(tc, "%s requires a concrete MVMOSHandle, but got a type object", op);
    return (MVMOSHandle *)oshandle;
}

static uv_mutex_t * acquire_mutex(MVMThreadContext *tc, MVMOSHandle *handle) {
    uv_mutex_t *mutex = handle->body.mutex;
    MVM_gc_mark_thread_blocked(tc);
    uv_mutex_lock(mutex);
    MVM_gc_mark_thread_unblocked(tc);
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
        MVMint64 ret;
        MVMROOT(tc, handle) {
            uv_mutex_t *mutex = acquire_mutex(tc, handle);
            ret = handle->body.ops->closable->close(tc, handle);
            release_mutex(tc, mutex);
        }
        return ret;
    }
    else
        MVM_exception_throw_adhoc(tc, "Cannot close this kind of handle");
}

MVMint64 MVM_io_is_tty(MVMThreadContext *tc, MVMObject *oshandle) {
    MVMOSHandle *handle = verify_is_handle(tc, oshandle, "istty");
    /* We need the extra check on is_tty because it is NULL for pipes. */
    if (handle->body.ops->introspection && handle->body.ops->introspection->is_tty) {
        MVMint64 ret;
        MVMROOT(tc, handle) {
            uv_mutex_t *mutex = acquire_mutex(tc, handle);
            ret = handle->body.ops->introspection->is_tty(tc, handle);
            release_mutex(tc, mutex);
        }
        return ret;
    }
    else {
        return 0;
    }
}

MVMint64 MVM_io_fileno(MVMThreadContext *tc, MVMObject *oshandle) {
    MVMOSHandle *handle = verify_is_handle(tc, oshandle, "get native descriptor");
    if (handle->body.ops->introspection) {
        MVMint64 ret;
        MVMROOT(tc, handle) {
            uv_mutex_t *mutex = acquire_mutex(tc, handle);
            ret = handle->body.ops->introspection->native_descriptor(tc, handle);
            release_mutex(tc, mutex);
        }
        return ret;
    }
    else {
        return -1;
    }
}

void MVM_io_seek(MVMThreadContext *tc, MVMObject *oshandle, MVMint64 offset, MVMint64 flag) {
    MVMOSHandle *handle = verify_is_handle(tc, oshandle, "seek");
    if (handle->body.ops->seekable) {
        MVMROOT(tc, handle) {
            uv_mutex_t *mutex = acquire_mutex(tc, handle);
            handle->body.ops->seekable->seek(tc, handle, offset, flag);
            release_mutex(tc, mutex);
        }
    }
    else
        MVM_exception_throw_adhoc(tc, "Cannot seek this kind of handle");
}

MVMint64 MVM_io_tell(MVMThreadContext *tc, MVMObject *oshandle) {
    MVMOSHandle *handle = verify_is_handle(tc, oshandle, "tell");
    if (handle->body.ops->seekable) {
        MVMint64 result;
        MVMROOT(tc, handle) {
            uv_mutex_t *mutex = acquire_mutex(tc, handle);
            result = handle->body.ops->seekable->tell(tc, handle);
            release_mutex(tc, mutex);
        }
        return result;
    }
    else
        MVM_exception_throw_adhoc(tc, "Cannot tell this kind of handle");
}

void MVM_io_read_bytes(MVMThreadContext *tc, MVMObject *oshandle, MVMObject *result, MVMint64 length) {
    MVMOSHandle *handle = verify_is_handle(tc, oshandle, "read bytes");
    MVMuint64 bytes_read;
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
        MVMROOT2(tc, handle, result) {
            uv_mutex_t *mutex = acquire_mutex(tc, handle);
            bytes_read = handle->body.ops->sync_readable->read_bytes(tc, handle, &buf, length);
            release_mutex(tc, mutex);
        }
    }
    else
        MVM_exception_throw_adhoc(tc, "Cannot read characters from this kind of handle");

    /* Stash the data in the VMArray. */
    ((MVMArray *)result)->body.slots.i8 = (MVMint8 *)buf;
    ((MVMArray *)result)->body.start    = 0;
    ((MVMArray *)result)->body.ssize    = bytes_read;
    ((MVMArray *)result)->body.elems    = bytes_read;
}

void MVM_io_write_bytes(MVMThreadContext *tc, MVMObject *oshandle, MVMObject *buffer) {
    MVMOSHandle *handle = verify_is_handle(tc, oshandle, "write bytes");
    char *output;
    MVMuint64 output_size;

    /* Ensure the target is in the correct form. */
    if (!IS_CONCRETE(buffer) || REPR(buffer)->ID != MVM_REPR_ID_VMArray)
        MVM_exception_throw_adhoc(tc, "write_fhb requires a native array to read from");
    if (((MVMArrayREPRData *)STABLE(buffer)->REPR_data)->slot_type == MVM_ARRAY_U8
        || ((MVMArrayREPRData *)STABLE(buffer)->REPR_data)->slot_type == MVM_ARRAY_I8) {
        output_size = ((MVMArray *)buffer)->body.elems;
        output = (char *)(((MVMArray *)buffer)->body.slots.i8 + ((MVMArray *)buffer)->body.start);
    }
    else if (((MVMArrayREPRData *)STABLE(buffer)->REPR_data)->slot_type == MVM_ARRAY_U16
        || ((MVMArrayREPRData *)STABLE(buffer)->REPR_data)->slot_type == MVM_ARRAY_I16) {
        output_size = ((MVMArray *)buffer)->body.elems * sizeof(MVMuint16);
        output = (char *)(((MVMArray *)buffer)->body.slots.i16 + ((MVMArray *)buffer)->body.start);
    }
    else
        MVM_exception_throw_adhoc(tc, "write_fhb requires a native array of uint8, int8, uint16 or int16");

    if (handle->body.ops->sync_writable) {
        MVMROOT(tc, handle) {
            uv_mutex_t *mutex = acquire_mutex(tc, handle);
            handle->body.ops->sync_writable->write_bytes(tc, handle, output, output_size);
            release_mutex(tc, mutex);
        }
    }
    else
        MVM_exception_throw_adhoc(tc, "Cannot write bytes to this kind of handle");
}

void MVM_io_write_bytes_c(MVMThreadContext *tc, MVMObject *oshandle, char *output,
                          MVMuint64 output_size) {
    MVMOSHandle *handle = verify_is_handle(tc, oshandle, "write bytes");
    if (handle->body.ops->sync_writable) {
        MVMROOT(tc, handle) {
            uv_mutex_t *mutex = acquire_mutex(tc, handle);
            handle->body.ops->sync_writable->write_bytes(tc, handle, output, output_size);
            release_mutex(tc, mutex);
        }
    }
    else
        MVM_exception_throw_adhoc(tc, "Cannot write bytes to this kind of handle");
}

MVMObject * MVM_io_read_bytes_async(MVMThreadContext *tc, MVMObject *oshandle, MVMObject *queue,
                                    MVMObject *schedulee, MVMObject *buf_type, MVMObject *async_type) {
    MVMOSHandle *handle = verify_is_handle(tc, oshandle, "read bytes asynchronously");
    if (handle->body.ops->async_readable) {
        MVMObject *result;
        MVMROOT5(tc, queue, schedulee, buf_type, async_type, handle) {
            uv_mutex_t *mutex = acquire_mutex(tc, handle);
            result = (MVMObject *)handle->body.ops->async_readable->read_bytes(tc,
                handle, queue, schedulee, buf_type, async_type);
            release_mutex(tc, mutex);
        }
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
        MVMObject *result;
        MVMROOT5(tc, queue, schedulee, buffer, async_type, handle) {
            uv_mutex_t *mutex = acquire_mutex(tc, handle);
            result = (MVMObject *)handle->body.ops->async_writable->write_bytes(tc,
                handle, queue, schedulee, buffer, async_type);
            release_mutex(tc, mutex);
        }
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
        MVMObject *result;
        MVMROOT6(tc, host, queue, schedulee, buffer, async_type, handle) {
            uv_mutex_t *mutex = acquire_mutex(tc, handle);
            result = (MVMObject *)handle->body.ops->async_writable_to->write_bytes_to(tc,
                handle, queue, schedulee, buffer, async_type, host, port);
            release_mutex(tc, mutex);
        }
        return result;
    }
    else
        MVM_exception_throw_adhoc(tc, "Cannot write bytes to a destination asynchronously to this kind of handle");
}

MVMint64 MVM_io_eof(MVMThreadContext *tc, MVMObject *oshandle) {
    MVMOSHandle *handle = verify_is_handle(tc, oshandle, "eof");
    if (handle->body.ops->sync_readable) {
        MVMint64 result;
        MVMROOT(tc, handle) {
            uv_mutex_t *mutex = acquire_mutex(tc, handle);
            result = handle->body.ops->sync_readable->eof(tc, handle);
            release_mutex(tc, mutex);
        }
        return result;
    }
    else
        MVM_exception_throw_adhoc(tc, "Cannot eof this kind of handle");
}

MVMint64 MVM_io_lock(MVMThreadContext *tc, MVMObject *oshandle, MVMint64 flag) {
    MVMOSHandle *handle = verify_is_handle(tc, oshandle, "lock");
    if (handle->body.ops->lockable) {
        MVMint64 result;
        MVMROOT(tc, handle) {
            uv_mutex_t *mutex = acquire_mutex(tc, handle);
            result = handle->body.ops->lockable->lock(tc, handle, flag);
            release_mutex(tc, mutex);
        }
        return result;
    }
    else
        MVM_exception_throw_adhoc(tc, "Cannot lock this kind of handle");
}

void MVM_io_unlock(MVMThreadContext *tc, MVMObject *oshandle) {
    MVMOSHandle *handle = verify_is_handle(tc, oshandle, "unlock");
    if (handle->body.ops->lockable) {
        MVMROOT(tc, handle) {
            uv_mutex_t *mutex = acquire_mutex(tc, handle);
            handle->body.ops->lockable->unlock(tc, handle);
            release_mutex(tc, mutex);
        }
    }
    else
        MVM_exception_throw_adhoc(tc, "Cannot unlock this kind of handle");
}

void MVM_io_flush(MVMThreadContext *tc, MVMObject *oshandle, MVMint32 sync) {
    MVMOSHandle *handle = verify_is_handle(tc, oshandle, "flush");
    if (handle->body.ops->sync_writable) {
        MVMROOT(tc, handle) {
            uv_mutex_t *mutex = acquire_mutex(tc, handle);
            handle->body.ops->sync_writable->flush(tc, handle, sync);
            release_mutex(tc, mutex);
        }
    }
    else
        MVM_exception_throw_adhoc(tc, "Cannot flush this kind of handle");
}

void MVM_io_truncate(MVMThreadContext *tc, MVMObject *oshandle, MVMint64 offset) {
    MVMOSHandle *handle = verify_is_handle(tc, oshandle, "truncate");
    if (handle->body.ops->sync_writable) {
        MVMROOT(tc, handle) {
            uv_mutex_t *mutex = acquire_mutex(tc, handle);
            handle->body.ops->sync_writable->truncate(tc, handle, offset);
            release_mutex(tc, mutex);
        }
    }
    else
        MVM_exception_throw_adhoc(tc, "Cannot truncate this kind of handle");
}

void MVM_io_connect(MVMThreadContext *tc, MVMObject *oshandle, MVMString *host, MVMint64 port, MVMuint16 family) {
    MVMOSHandle *handle = verify_is_handle(tc, oshandle, "connect");
    if (handle->body.ops->sockety) {
        MVMROOT2(tc, host, handle) {
            uv_mutex_t *mutex = acquire_mutex(tc, handle);
            handle->body.ops->sockety->connect(tc, handle, host, port, family);
            release_mutex(tc, mutex);
        }
    }
    else
        MVM_exception_throw_adhoc(tc, "Cannot connect this kind of handle");
}

void MVM_io_bind(MVMThreadContext *tc, MVMObject *oshandle, MVMString *host, MVMint64 port, MVMuint16 family, MVMint32 backlog) {
    MVMOSHandle *handle = verify_is_handle(tc, oshandle, "bind");
    if (handle->body.ops->sockety) {
        MVMROOT2(tc, host, handle) {
            uv_mutex_t *mutex = acquire_mutex(tc, handle);
            handle->body.ops->sockety->bind(tc, handle, host, port, family, backlog);
            release_mutex(tc, mutex);
        }
    }
    else
        MVM_exception_throw_adhoc(tc, "Cannot bind this kind of handle");
}

MVMint64 MVM_io_getport(MVMThreadContext *tc, MVMObject *oshandle) {
    MVMOSHandle *handle = verify_is_handle(tc, oshandle, "getport");
    if (handle->body.ops->sockety) {
        MVMint64 result;
        MVMROOT(tc, handle) {
            uv_mutex_t *mutex = acquire_mutex(tc, handle);
            result = handle->body.ops->sockety->getport(tc, handle);
            release_mutex(tc, mutex);
        }
        return result;
    }
    else
        MVM_exception_throw_adhoc(tc, "Cannot getport for this kind of handle");
}

MVMObject * MVM_io_accept(MVMThreadContext *tc, MVMObject *oshandle) {
    MVMOSHandle *handle = verify_is_handle(tc, oshandle, "accept");
    if (handle->body.ops->sockety) {
        MVMObject *result;
        MVMROOT(tc, handle) {
            uv_mutex_t *mutex = acquire_mutex(tc, handle);
            result = handle->body.ops->sockety->accept(tc, handle);
            release_mutex(tc, mutex);
        }
        return result;
    }
    else
        MVM_exception_throw_adhoc(tc, "Cannot accept this kind of handle");
}

void MVM_io_set_buffer_size(MVMThreadContext *tc, MVMObject *oshandle, MVMint64 size) {
    MVMOSHandle *handle = verify_is_handle(tc, oshandle, "set buffer size");
    if (handle->body.ops->set_buffer_size) {
        MVMROOT(tc, handle) {
            uv_mutex_t *mutex = acquire_mutex(tc, handle);
            handle->body.ops->set_buffer_size(tc, handle, size);
            release_mutex(tc, mutex);
        }
    }
    else
        MVM_exception_throw_adhoc(tc, "Cannot set buffer size on this kind of handle");
}

MVMObject * MVM_io_get_async_task_handle(MVMThreadContext *tc, MVMObject *oshandle) {
    MVMOSHandle *handle = verify_is_handle(tc, oshandle, "get async task handle");
    if (handle->body.ops->get_async_task_handle) {
        MVMObject *ath;
        MVMROOT(tc, handle) {
            uv_mutex_t *mutex = acquire_mutex(tc, handle);
            ath = handle->body.ops->get_async_task_handle(tc, handle);
            release_mutex(tc, mutex);
        }
        return ath;
    }
    else
        MVM_exception_throw_adhoc(tc, "Cannot get async task handle from this kind of handle");
}

void MVM_io_flush_standard_handles(MVMThreadContext *tc) {
    MVM_io_flush(tc, tc->instance->stdout_handle, 0);
    MVM_io_flush(tc, tc->instance->stderr_handle, 0);
}

#ifdef _WIN32
int MVM_set_std_handle_to_nul(FILE *file, int fd, BOOL read, int std_handle_type) {
    /* Found on https://docs.microsoft.com/en-us/cpp/c-runtime-library/reference/get-osfhandle?view=vs-2019:

       "When stdin, stdout, and stderr aren't associated with a stream (for example, in a Windows
       application without a console window), the file descriptor values for these streams are
       returned from _fileno as the special value -2. Similarly, if you use a 0, 1, or 2 as the
       file descriptor parameter instead of the result of a call to _fileno, _get_osfhandle also
       returns the special value -2 when the file descriptor is not associated with a stream, and
       does not set errno. However, this is not a valid file handle value, and subsequent calls
       that attempt to use it are likely to fail."

       See https://jdebp.eu/FGA/redirecting-standard-io.html
           https://stackoverflow.com/a/50358201 (Especially the comments of Eryk Sun)
    */
    FILE* stream;
    HANDLE new_handle;

    if (_fileno(file) != -2 || _get_osfhandle(fd) != -2)
        // The handles are initialized. Don't touch!
        return 1;

    /* FD 1 is in an error state (_get_osfhandle(1) == -2). Close it. The FD number is up for grabs
        after this call. */
    if (_close(fd) != 0)
        return 0;

    /* FILE *stdout is in an error state (_fileno(stdout) == -2). Reopen it to a "NUL:" file. This
        will take the next free FD number. So it's important to call this sequentially for FD 0, 1
        and 2. */
    if (freopen_s(&stream, "NUL:", read ? "r" : "w", file) != 0)
        return 0;

    /* Set the underlying Windows handle as the STD handler. */
    new_handle = (HANDLE)_get_osfhandle(fd);
    if (!SetStdHandle(std_handle_type, new_handle))
        return 0;

    return 1;
}
#endif

char* MVM_platform_path(MVMThreadContext *tc, MVMString *path) {
    // Convert the MVMString to a C string first.
    char *original_path = MVM_string_utf8_c8_encode_C_string(tc, path);

#ifdef _WIN32
    /* Add prefix if:
       * It is an absolute path
       * It doesn't already start with \\?\
       * It is at least MAX_PATH in length
    */
    if (is_absolute_path(original_path) == 1 && strlen(original_path) > MAX_PATH && strncmp(original_path, "\\\\?\\", 4) != 0) {

        // Allocate memory for the new path. Add extra space for "\\?\" and the null terminator.
        size_t new_length = strlen(original_path) + 4 + 1;

        char *new_path = (char *)MVM_malloc(new_length);

        // Copy "\\?\" prefix.
        strcpy(new_path, "\\\\?\\");

        // Copy the rest of the path, converting '/' to '\'.
        char *new_path_ptr = new_path + 4;
        char *original_path_ptr = original_path;
        while (*original_path_ptr != '\0') {
            *new_path_ptr++ = (*original_path_ptr == '/') ? '\\' : *original_path_ptr;
            original_path_ptr++;
        }

        // Null terminate the new string.
        *new_path_ptr = '\0';

        // Free the original path string and use new path.
        MVM_free(original_path);
        original_path = new_path;
    }
#endif

    return original_path;
}

int is_absolute_path(const char *path) {
    if (path == NULL || strlen(path) == 0) {
        return 0;
    }

    if (path[0] == '/' || path[0] == '\\') {
        return 1;
    }

#ifdef _WIN32
    if (strlen(path) >= 3 && path[1] == ':' && path[2] == '\\') {
        return 1;
    }
#endif

    return 0;
}
