#include "moar.h"
#include "platform/io.h"

/* Here we implement synchronous file I/O. It's done using libuv's file I/O
 * functions, without specifying callbacks, thus easily giving synchronous
 * behavior. */

#ifndef _WIN32
#include <sys/types.h>
#include <unistd.h>
#define DEFAULT_MODE 0x01B6
#else
#include <fcntl.h>
#define O_CREAT  _O_CREAT
#define O_RDONLY _O_RDONLY
#define O_WRONLY _O_WRONLY
#define O_TRUNC  _O_TRUNC
#define DEFAULT_MODE _S_IWRITE /* work around sucky libuv defaults */
#endif

/* Number of bytes we pull in at a time to the buffer. */
#define CHUNK_SIZE 32768

/* Data that we keep for a file-based handle. */
typedef struct {
    /* libuv file descriptor. */
    uv_file fd;

    /* The filename we opened, as a C string. */
    char *filename;

    /* The encoding we're using. */
    MVMint64 encoding;

    /* Decode stream, for turning bytes from disk into strings. */
    MVMDecodeStream *ds;

    /* Current separator codepoint. */
    MVMGrapheme32 sep;
} MVMIOFileData;

/* Closes the file. */
static MVMint64 closefh(MVMThreadContext *tc, MVMOSHandle *h) {
    MVMIOFileData *data = (MVMIOFileData *)h->body.data;
    uv_fs_t req;
    if (data->ds) {
        MVM_string_decodestream_destory(tc, data->ds);
        data->ds = NULL;
    }
    if (uv_fs_close(tc->loop, &req, data->fd, NULL) < 0) {
        data->fd = -1;
        MVM_exception_throw_adhoc(tc, "Failed to close filehandle: %s", uv_strerror(req.result));
    }
    data->fd = -1;
    return 0;
}

/* Sets the encoding used for string-based I/O. */
static void set_encoding(MVMThreadContext *tc, MVMOSHandle *h, MVMint64 encoding) {
    MVMIOFileData *data = (MVMIOFileData *)h->body.data;
    if (data->ds)
        MVM_exception_throw_adhoc(tc, "Too late to change handle encoding");
    data->encoding = encoding;
}

/* Seek to the specified position in the file. */
static void seek(MVMThreadContext *tc, MVMOSHandle *h, MVMint64 offset, MVMint64 whence) {
    MVMIOFileData *data = (MVMIOFileData *)h->body.data;
    MVMint64 r;

    if (data->ds) {
        /* We'll start over from a new position. */
        MVM_string_decodestream_destory(tc, data->ds);
        data->ds = NULL;
    }

    /* Seek, then get absolute position for new decodestream. */
    if (MVM_platform_lseek(data->fd, offset, whence) == -1)
        MVM_exception_throw_adhoc(tc, "Failed to seek in filehandle: %d", errno);
    if ((r = MVM_platform_lseek(data->fd, 0, SEEK_CUR)) == -1)
        MVM_exception_throw_adhoc(tc, "Failed to seek in filehandle: %d", errno);
    data->ds = MVM_string_decodestream_create(tc, data->encoding, r);
}

/* Get curernt position in the file. */
static MVMint64 mvm_tell(MVMThreadContext *tc, MVMOSHandle *h) {
    MVMIOFileData *data = (MVMIOFileData *)h->body.data;
    return data->ds ? MVM_string_decodestream_tell_bytes(tc, data->ds) : 0;
}

/* Set the line separator. */
static void set_separator(MVMThreadContext *tc, MVMOSHandle *h, MVMString *sep) {
    MVMIOFileData *data = (MVMIOFileData *)h->body.data;
    data->sep = (MVMGrapheme32)MVM_string_get_grapheme_at(tc, sep,
        MVM_string_graphs(tc, sep) - 1);
}

/* Read a bunch of bytes into the current decode stream. */
static MVMint32 read_to_buffer(MVMThreadContext *tc, MVMIOFileData *data, MVMint32 bytes) {
    char *buf         = MVM_malloc(bytes);
    uv_buf_t read_buf = uv_buf_init(buf, bytes);
    uv_fs_t req;
    MVMint32 read;
    if ((read = uv_fs_read(tc->loop, &req, data->fd, &read_buf, 1, -1, NULL)) < 0) {
        MVM_free(buf);
        MVM_exception_throw_adhoc(tc, "Reading from filehandle failed: %s",
            uv_strerror(req.result));
    }
    MVM_string_decodestream_add_bytes(tc, data->ds, buf, read);
    return read;
}

/* Ensures we have a decode stream, creating it if we're missing one. */
static void ensure_decode_stream(MVMThreadContext *tc, MVMIOFileData *data) {
    if (!data->ds)
        data->ds = MVM_string_decodestream_create(tc, data->encoding, 0);
}

/* Reads a single line from the file handle. May serve it from a buffer, if we
 * already read enough data. */
static MVMString * read_line(MVMThreadContext *tc, MVMOSHandle *h) {
    MVMIOFileData *data = (MVMIOFileData *)h->body.data;
    ensure_decode_stream(tc, data);

    /* Pull data until we can read a line. */
    do {
        MVMString *line = MVM_string_decodestream_get_until_sep(tc, data->ds, data->sep);
        if (line != NULL)
            return line;
    } while (read_to_buffer(tc, data, CHUNK_SIZE) > 0);

    /* Reached end of file, or last (non-termianted) line. */
    return MVM_string_decodestream_get_all(tc, data->ds);
}

/* Reads the file from the current position to the end into a string. */
static MVMString * slurp(MVMThreadContext *tc, MVMOSHandle *h) {
    MVMIOFileData *data = (MVMIOFileData *)h->body.data;
    uv_fs_t req;
    ensure_decode_stream(tc, data);

    /* Typically we're slurping an entire file, so just request the bytes
     * until the end; repeat to ensure we get 'em all. */
    if (uv_fs_fstat(tc->loop, &req, data->fd, NULL) < 0) {
        MVM_exception_throw_adhoc(tc, "slurp from filehandle failed: %s", uv_strerror(req.result));
    }
    while (read_to_buffer(tc, data, req.statbuf.st_size) > 0)
        ;
    return MVM_string_decodestream_get_all(tc, data->ds);
}

/* Gets the specified number of characters from the file. */
static MVMString * read_chars(MVMThreadContext *tc, MVMOSHandle *h, MVMint64 chars) {
    MVMIOFileData *data = (MVMIOFileData *)h->body.data;
    ensure_decode_stream(tc, data);

    /* Pull data until we can read the chars we want. */
    do {
        MVMString *result = MVM_string_decodestream_get_chars(tc, data->ds, chars);
        if (result != NULL)
            return result;
    } while (read_to_buffer(tc, data, CHUNK_SIZE) > 0);

    /* Reached end of file, so just take what we have. */
    return MVM_string_decodestream_get_all(tc, data->ds);
}

/* Reads the specified number of bytes into a the supplied buffer, returing
 * the number actually read. */
static MVMint64 read_bytes(MVMThreadContext *tc, MVMOSHandle *h, char **buf, MVMint64 bytes) {
    MVMIOFileData *data = (MVMIOFileData *)h->body.data;
    ensure_decode_stream(tc, data);

    /* Keep requesting bytes until we have enough in the buffer or we hit
     * end of file. */
    while (!MVM_string_decodestream_have_bytes(tc, data->ds, bytes)) {
        if (read_to_buffer(tc, data, bytes) <= 0)
            break;
    }

    /* Read as many as we can, up to the limit. */
    return MVM_string_decodestream_bytes_to_buf(tc, data->ds, buf, bytes);
}

/* Checks if the end of file has been reached. */
static MVMint64 mvm_eof(MVMThreadContext *tc, MVMOSHandle *h) {
    MVMIOFileData *data = (MVMIOFileData *)h->body.data;
    MVMint64 seek_pos;
    uv_fs_t  req;
    if (data->ds && !MVM_string_decodestream_is_empty(tc, data->ds))
        return 0;
    if (uv_fs_fstat(tc->loop, &req, data->fd, NULL) == -1) {
        MVM_exception_throw_adhoc(tc, "Failed to stat file descriptor: %s", uv_strerror(req.result));
    }
    if ((seek_pos = MVM_platform_lseek(data->fd, 0, SEEK_CUR)) == -1)
        MVM_exception_throw_adhoc(tc, "Failed to seek in filehandle: %d", errno);
    return req.statbuf.st_size == seek_pos;
}

/* Writes the specified string to the file handle, maybe with a newline. */
static MVMint64 write_str(MVMThreadContext *tc, MVMOSHandle *h, MVMString *str, MVMint64 newline) {
    MVMIOFileData *data = (MVMIOFileData *)h->body.data;
    MVMuint64 output_size;
    MVMint64 bytes_written;
    char *output = MVM_string_encode(tc, str, 0, -1, &output_size, data->encoding);
    uv_buf_t write_buf  = uv_buf_init(output, output_size);
    uv_fs_t req;

    bytes_written = uv_fs_write(tc->loop, &req, data->fd, &write_buf, 1, -1, NULL);
    if (bytes_written < 0) {
        MVM_free(output);
        MVM_exception_throw_adhoc(tc, "Failed to write bytes to filehandle: %s", uv_strerror(req.result));
    }
    MVM_free(output);

    if (newline) {
        uv_buf_t nl = uv_buf_init("\n", 1);
        if (uv_fs_write(tc->loop, &req, data->fd, &nl, 1, -1, NULL) < 0)
            MVM_exception_throw_adhoc(tc, "Failed to write newline to filehandle: %s", uv_strerror(req.result));
        bytes_written++;
    }

    return bytes_written;
}

/* Writes the specified bytes to the file handle. */
static MVMint64 write_bytes(MVMThreadContext *tc, MVMOSHandle *h, char *buf, MVMint64 bytes) {
    MVMIOFileData *data = (MVMIOFileData *)h->body.data;
    uv_buf_t write_buf  = uv_buf_init(buf, bytes);
    uv_fs_t  req;
    MVMint64 bytes_written;
    bytes_written = uv_fs_write(tc->loop, &req, data->fd, &write_buf, 1, -1, NULL);
    if (bytes_written < 0)
        MVM_exception_throw_adhoc(tc, "Failed to write bytes to filehandle: %s", uv_strerror(req.result));
    return bytes_written;
}

/* Flushes the file handle. */
static void flush(MVMThreadContext *tc, MVMOSHandle *h){
    MVMIOFileData *data = (MVMIOFileData *)h->body.data;
    uv_fs_t req;
    if (uv_fs_fsync(tc->loop, &req, data->fd, NULL) < 0 )
        MVM_exception_throw_adhoc(tc, "Failed to flush filehandle: %s", uv_strerror(req.result));
}

/* Truncates the file handle. */
static void truncatefh(MVMThreadContext *tc, MVMOSHandle *h, MVMint64 bytes) {
    MVMIOFileData *data = (MVMIOFileData *)h->body.data;
    uv_fs_t req;
    if(uv_fs_ftruncate(tc->loop, &req, data->fd, bytes, NULL) < 0 )
        MVM_exception_throw_adhoc(tc, "Failed to truncate filehandle: %s", uv_strerror(req.result));
}

/* Locks a file. */
static MVMint64 lock(MVMThreadContext *tc, MVMOSHandle *h, MVMint64 flag) {
    MVMIOFileData *data = (MVMIOFileData *)h->body.data;

#ifdef _WIN32

    const DWORD len = 0xffffffff;
    const HANDLE hf = (HANDLE)_get_osfhandle(data->fd);
    OVERLAPPED offset;

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

    MVM_exception_throw_adhoc(tc, "Failed to lock filehandle: %d", GetLastError());

    return 0;

#else

    struct flock l;
    ssize_t r;
    int fc;
    const int fd = data->fd;

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
        MVM_exception_throw_adhoc(tc, "Failed to lock filehandle: %d", errno);
        return 0;
    }

    return 1;
#endif
}

/* Unlocks a file. */
static void unlock(MVMThreadContext *tc, MVMOSHandle *h) {
    MVMIOFileData *data = (MVMIOFileData *)h->body.data;

#ifdef _WIN32

    const DWORD len = 0xffffffff;
    const HANDLE hf = (HANDLE)_get_osfhandle(data->fd);
    OVERLAPPED offset;

    if (hf == INVALID_HANDLE_VALUE) {
        MVM_exception_throw_adhoc(tc, "Failed to seek in filehandle: bad file descriptor");
    }

    memset (&offset, 0, sizeof(offset));
    if (UnlockFileEx(hf, 0, len, len, &offset)) {
        return;
    }

    MVM_exception_throw_adhoc(tc, "Failed to unlock filehandle: %d", GetLastError());
#else

    struct flock l;
    ssize_t r;
    const int fd = data->fd;

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

/* Frees data associated with the handle. */
static void gc_free(MVMThreadContext *tc, MVMObject *h, void *d) {
    MVMIOFileData *data = (MVMIOFileData *)d;
    if (data) {
        if (data->ds)
            MVM_string_decodestream_destory(tc, data->ds);
        if (data->filename)
            MVM_free(data->filename);
        MVM_free(data);
    }
}

/* IO ops table, populated with functions. */
static const MVMIOClosable     closable      = { closefh };
static const MVMIOEncodable    encodable     = { set_encoding };
static const MVMIOSyncReadable sync_readable = { set_separator, read_line, slurp, read_chars, read_bytes, mvm_eof };
static const MVMIOSyncWritable sync_writable = { write_str, write_bytes, flush, truncatefh };
static const MVMIOSeekable     seekable      = { seek, mvm_tell };
static const MVMIOLockable     lockable      = { lock, unlock };
static const MVMIOOps op_table = {
    &closable,
    &encodable,
    &sync_readable,
    &sync_writable,
    NULL,
    NULL,
    &seekable,
    NULL,
    NULL,
    &lockable,
    NULL,
    gc_free
};

/* Opens a file, returning a synchronous file handle. */
MVMObject * MVM_file_open_fh(MVMThreadContext *tc, MVMString *filename, MVMString *mode) {
    char          * const fname  = MVM_string_utf8_encode_C_string(tc, filename);
    char          * const fmode  = MVM_string_utf8_encode_C_string(tc, mode);
    MVMOSHandle   * const result = (MVMOSHandle *)MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTIO);
    MVMIOFileData * const data   = MVM_calloc(1, sizeof(MVMIOFileData));
    uv_fs_t req;
    uv_file fd;

    /* Resolve mode description to flags. */
    int flag;
    if (0 == strcmp("r", fmode))
        flag = O_RDONLY;
    else if (0 == strcmp("w", fmode))
        flag = O_CREAT| O_WRONLY | O_TRUNC;
    else if (0 == strcmp("wa", fmode))
        flag = O_CREAT | O_WRONLY | O_APPEND;
    else {
        MVM_free(fname);
        MVM_exception_throw_adhoc(tc, "Invalid open mode: %s", fmode);
    }
    MVM_free(fmode);

    /* Try to open the file. */
    if ((fd = uv_fs_open(tc->loop, &req, (const char *)fname, flag, DEFAULT_MODE, NULL)) < 0) {
        MVM_exception_throw_adhoc(tc, "Failed to open file %s: %s", fname, uv_strerror(req.result));
    }

    /* Set up handle. */
    data->fd          = fd;
    data->filename    = fname;
    data->encoding    = MVM_encoding_type_utf8;
    result->body.ops  = &op_table;
    result->body.data = data;

    return (MVMObject *)result;
}

/* Opens a file, returning a synchronous file handle. */
MVMObject * MVM_file_handle_from_fd(MVMThreadContext *tc, uv_file fd) {
    MVMOSHandle   * const result = (MVMOSHandle *)MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTIO);
    MVMIOFileData * const data   = MVM_calloc(1, sizeof(MVMIOFileData));
    data->fd          = fd;
    data->encoding    = MVM_encoding_type_utf8;
    result->body.ops  = &op_table;
    result->body.data = data;
    return (MVMObject *)result;
}
