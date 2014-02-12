#include "moar.h"

/* Here we implement synchronous file I/O. It's done using libuv's file I/O
 * functions, without specifying callbacks, thus easily giving synchronous
 * behavior. */
 
#ifndef _WIN32
#include <sys/types.h>
#include <unistd.h>
#define DEFAULT_MODE 0x0FFF
#else
#include <fcntl.h>
#define O_CREAT  _O_CREAT
#define O_RDONLY _O_RDONLY
#define O_WRONLY _O_WRONLY
#define O_TRUNC  _O_TRUNC
#define DEFAULT_MODE _S_IWRITE /* work around sucky libuv defaults */
#endif

/* Data that we keep for a file-based handle. */
typedef struct {
    /* libuv file descriptor. */
    uv_file fd;

    /* The filename we opened, as a C string. */
    char *filename;

    /* The encoding we're using. */
    MVMint64 encoding;
} MVMIOFileData;

/* Closes the file. */
static void close(MVMThreadContext *tc, MVMOSHandle *h) {
    MVMIOFileData *data = (MVMIOFileData *)h->body.data;
    uv_fs_t req;
    if (uv_fs_close(tc->loop, &req, data->fd, NULL) < 0) {
        data->fd = -1;
        MVM_exception_throw_adhoc(tc, "Failed to close filehandle: %s", uv_strerror(req.result));
    }
    data->fd = -1;
}

/* Sets the encoding used for string-based I/O. */
static void set_encoding(MVMThreadContext *tc, MVMOSHandle *h, MVMint64 encoding) {
    MVMIOFileData *data = (MVMIOFileData *)h->body.data;
    data->encoding = encoding;
}

/* Seek to the specified position in the file. */
static void seek(MVMThreadContext *tc, MVMOSHandle *h, MVMint64 offset, MVMint64 whence) {
    MVMIOFileData *data = (MVMIOFileData *)h->body.data;
    if (MVM_platform_lseek(data->fd, offset, whence) == -1)
        MVM_exception_throw_adhoc(tc, "Failed to seek in filehandle: %d", errno);
}

/* Get curernt position in the file. */
static MVMint64 tell(MVMThreadContext *tc, MVMOSHandle *h) {
    MVMIOFileData *data = (MVMIOFileData *)h->body.data;
    MVMint64 r;
    if ((r = MVM_platform_lseek(data->fd, 0, SEEK_CUR)) == -1)
        MVM_exception_throw_adhoc(tc, "Failed to seek in filehandle: %d", errno);
    return r;
}

/* Set the line separator. */
static void set_separator(MVMThreadContext *tc, MVMOSHandle *h, MVMString *sep) {
    MVM_exception_throw_adhoc(tc, "set_separator NYI on file handles");
}

/* Reads a single line from the file handle. May serve it from a buffer, if we
 * already read enough data. */
static MVMString * read_line(MVMThreadContext *tc, MVMOSHandle *h) {
    MVMIOFileData *data       = (MVMIOFileData *)h->body.data;
    MVMint32       bytes_read = 0;
    MVMint32       bufsize    = 128;
    char          *buf        = malloc(bufsize);
    MVMString     *result;
    uv_fs_t        req;
    MVMint32       res;
    char           ch;

    while ((res = uv_fs_read(tc->loop, &req, data->fd, &ch, 1, -1, NULL)) > 0) {
        if (bytes_read == bufsize) {
            bufsize *= 2;
            buf = realloc(buf, bufsize);
        }
        buf[bytes_read] = ch;
        bytes_read++;
        if (ch == 10)
            break;
    }

    if (res < 0) {
        free(buf);
        MVM_exception_throw_adhoc(tc, "readline from filehandle failed: %s",
            uv_strerror(req.result));
    }

    result = MVM_string_decode(tc, tc->instance->VMString, buf, bytes_read, data->encoding);
    free(buf);
    return result;
}

/* Reads the file from the current position to the end into a string. */
static MVMString * slurp(MVMThreadContext *tc, MVMOSHandle *h) {
    MVMIOFileData *data = (MVMIOFileData *)h->body.data;
    MVMString *result;
    MVMint64 length;
    MVMint64 bytes_read = 0;
    uv_fs_t req;
    char *buf;

    if (uv_fs_fstat(tc->loop, &req, data->fd, NULL) < 0) {
        MVM_exception_throw_adhoc(tc, "slurp from filehandle failed: %s", uv_strerror(req.result));
    }

    length = req.statbuf.st_size;

    if (length > 0) {
        buf = malloc(length);

        bytes_read = uv_fs_read(tc->loop, &req, data->fd, buf, length, -1, NULL);
        if (bytes_read < 0) {
            free(buf);
            MVM_exception_throw_adhoc(tc, "slurp from filehandle failed: %s", uv_strerror(req.result));
        }

        result = MVM_string_decode(tc, tc->instance->VMString, buf, bytes_read, data->encoding);
        free(buf);
    }
    else {
        result = (MVMString *)REPR(tc->instance->VMString)->allocate(tc, STABLE(tc->instance->VMString));
    }
    
    return result;
}

/* Gets the specified number of characters from the file. */
static MVMString * read_chars(MVMThreadContext *tc, MVMOSHandle *h, MVMint64 chars) {
    MVM_exception_throw_adhoc(tc, "read_chars NYI on file handles");
}

/* Reads the specified number of bytes into a the supplied buffer, returing
 * the number actually read.. */
static MVMint64 read_bytes(MVMThreadContext *tc, MVMOSHandle *h, char **buf, MVMint64 bytes) {
    MVMIOFileData *data = (MVMIOFileData *)h->body.data;
    MVMint64 bytes_read;
    uv_fs_t  req;
    *buf = malloc(bytes);
    bytes_read = uv_fs_read(tc->loop, &req, data->fd, *buf, bytes, -1, NULL);
    if (bytes_read < 0) {
        free(*buf);
        *buf = NULL;
        MVM_exception_throw_adhoc(tc, "Read from filehandle failed: %s", uv_strerror(req.result));
    }
    return bytes_read;
}

/* Checks if the end of file has been reached. */
static MVMint64 eof(MVMThreadContext *tc, MVMOSHandle *h) {
    MVMIOFileData *data = (MVMIOFileData *)h->body.data;
    MVMint64 r;
    MVMint64 seek_pos;
    uv_fs_t  req;
    if ((r = uv_fs_lstat(tc->loop, &req, data->filename, NULL)) == -1)
        MVM_exception_throw_adhoc(tc, "Failed to stat in filehandle: %d", errno);
    if ((seek_pos = MVM_platform_lseek(data->fd, 0, SEEK_CUR)) == -1)
        MVM_exception_throw_adhoc(tc, "Failed to seek in filehandle: %d", errno);
    return req.statbuf.st_size == seek_pos;
}

/* Writes the specified string to the file handle, maybe with a newline. */
static MVMint64 write_str(MVMThreadContext *tc, MVMOSHandle *h, MVMString *str, MVMint64 newline) {
    MVMIOFileData *data = (MVMIOFileData *)h->body.data;
    MVMuint8 *output;
    MVMint64 output_size, bytes_written;
    uv_fs_t req;

    output = MVM_string_encode(tc, str, 0, -1, &output_size, data->encoding);
    bytes_written = uv_fs_write(tc->loop, &req, data->fd, (const void *)output, output_size, -1, NULL);
    if (bytes_written < 0) {
        free(output);
        MVM_exception_throw_adhoc(tc, "Failed to write bytes to filehandle: %s", uv_strerror(req.result));
    }
    free(output);

    if (newline) {
        if (uv_fs_write(tc->loop, &req, data->fd, "\n", 1, -1, NULL) < 0)
            MVM_exception_throw_adhoc(tc, "Failed to write newline to filehandle: %s", uv_strerror(req.result));
        bytes_written++;
    }

    return bytes_written;
}

/* Writes the specified bytes to the file handle. */
static MVMint64 write_bytes(MVMThreadContext *tc, MVMOSHandle *h, char *buf, MVMint64 bytes) {
    MVMIOFileData *data = (MVMIOFileData *)h->body.data;
    uv_fs_t  req;
    MVMint64 bytes_written;
    bytes_written = uv_fs_write(tc->loop, &req, data->fd, (const void *)buf, bytes, -1, NULL);
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

/* Frees data associated with the handle. */
void gc_free(MVMThreadContext *tc, void *d) {
    MVMIOFileData *data = (MVMIOFileData *)d;
    if (data) {
        if (data->filename)
            free(data->filename);
        free(data);
    }
}

/* IO ops table, populated with functions. */
static MVMIOClosable     closable      = { close };
static MVMIOEncodable    encodable     = { set_encoding };
static MVMIOSyncReadable sync_readable = { set_separator, read_line, slurp, read_chars, read_bytes, eof };
static MVMIOSyncWritable sync_writable = { write_str, write_bytes, flush };
static MVMIOSeekable     seekable      = { seek, tell };
static MVMIOOps op_table = {
    &closable,
    &encodable,
    &sync_readable,
    &sync_writable,
    &seekable,
    NULL,
    NULL,
    NULL,
    gc_free
};

/* Opens a file, returning a synchronous file handle. */
MVMObject * MVM_file_open_fh(MVMThreadContext *tc, MVMString *filename, MVMString *mode) {
    char          * const fname  = MVM_string_utf8_encode_C_string(tc, filename);
    char          * const fmode  = MVM_string_utf8_encode_C_string(tc, mode);
    MVMOSHandle   * const result = (MVMOSHandle *)MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTIO);
    MVMIOFileData * const data   = calloc(1, sizeof(MVMIOFileData));
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
        free(fname);
        MVM_exception_throw_adhoc(tc, "Invalid open mode: %d", fmode);
    }
    free(fmode);

    /* Try to open the file. */
    if ((fd = uv_fs_open(tc->loop, &req, (const char *)fname, flag, DEFAULT_MODE, NULL)) < 0) {
        free(fname);
        MVM_exception_throw_adhoc(tc, "Failed to open file: %s", uv_strerror(req.result));
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
    MVMIOFileData * const data   = calloc(1, sizeof(MVMIOFileData));
    data->fd          = fd;
    data->encoding    = MVM_encoding_type_utf8;
    result->body.ops  = &op_table;
    result->body.data = data;
    return (MVMObject *)result;
}
