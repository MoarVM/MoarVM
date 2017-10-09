#include "moar.h"
#include "platform/io.h"

#ifndef _WIN32
#include <sys/types.h>
#include <unistd.h>
#define DEFAULT_MODE 0x01B6
typedef struct stat STAT;
#else
#include <fcntl.h>
#include <errno.h>
#define O_CREAT  _O_CREAT
#define O_RDONLY _O_RDONLY
#define O_WRONLY _O_WRONLY
#define O_TRUNC  _O_TRUNC
#define O_EXCL   _O_EXCL
#define O_RDWR   _O_RDWR
#define DEFAULT_MODE _S_IWRITE
#define open _open
#define close _close
#define read _read
#define write _write
#define isatty _isatty
#define ftruncate _chsize
#define fstat _fstat
typedef struct _stat STAT;
#endif

/* Data that we keep for a file-based handle. */
typedef struct {
    /* File descriptor. */
    int fd;

    /* Is it seekable? */
    short seekable;

    /* Is it know to be writable? */
    short known_writable;

    /* How many bytes have we read/written? Used to fake tell on handles that
     * are not seekable. */
    MVMint64 byte_position;

    /* Did read already report EOF? */
    int eof_reported;

    /* Output buffer, for buffered output. */
    char *output_buffer;

    /* Size of the output buffer, for buffered output; 0 if not buffering. */
    size_t output_buffer_size;

    /* How much of the output buffer has been used so far. */
    size_t output_buffer_used;
} MVMIOFileData;

/* Checks if the file is a TTY. */
static MVMint64 is_tty(MVMThreadContext *tc, MVMOSHandle *h) {
    MVMIOFileData *data = (MVMIOFileData *)h->body.data;
    return isatty(data->fd);
}

/* Gets the file descriptor. */
static MVMint64 mvm_fileno(MVMThreadContext *tc, MVMOSHandle *h) {
    MVMIOFileData *data = (MVMIOFileData *)h->body.data;
    return (MVMint64)data->fd;
}

/* Performs a write, either because a buffer filled or because we are not
 * buffering output. */
static void perform_write(MVMThreadContext *tc, MVMIOFileData *data, char *buf, MVMint64 bytes) {
    MVMint64 bytes_written = 0;
    MVM_gc_mark_thread_blocked(tc);
    while (bytes > 0) {
        int r = write(data->fd, buf, (int)bytes);
        if (r == -1) {
            int save_errno = errno;
            MVM_gc_mark_thread_unblocked(tc);
            MVM_exception_throw_adhoc(tc, "Failed to write bytes to filehandle: %s",
                strerror(save_errno));
        }
        bytes_written += r;
        buf += r;
        bytes -= r;
    }
    MVM_gc_mark_thread_unblocked(tc);
    data->byte_position += bytes_written;
    data->known_writable = 1;
}

/* Flushes any existing output buffer and clears use back to 0. */
static void flush_output_buffer(MVMThreadContext *tc, MVMIOFileData *data) {
    if (data->output_buffer_used) {
        perform_write(tc, data, data->output_buffer, data->output_buffer_used);
        data->output_buffer_used = 0;
    }
}

/* Seek to the specified position in the file. */
static void seek(MVMThreadContext *tc, MVMOSHandle *h, MVMint64 offset, MVMint64 whence) {
    MVMIOFileData *data = (MVMIOFileData *)h->body.data;
    if (!data->seekable)
        MVM_exception_throw_adhoc(tc, "It is not possible to seek this kind of handle");
    flush_output_buffer(tc, data);
    if (MVM_platform_lseek(data->fd, offset, whence) == -1)
        MVM_exception_throw_adhoc(tc, "Failed to seek in filehandle: %d", errno);
}

/* Get current position in the file. */
static MVMint64 mvm_tell(MVMThreadContext *tc, MVMOSHandle *h) {
    MVMIOFileData *data = (MVMIOFileData *)h->body.data;
    flush_output_buffer(tc, data);
    if (data->seekable) {
        MVMint64 r;
        if ((r = MVM_platform_lseek(data->fd, 0, SEEK_CUR)) == -1)
            MVM_exception_throw_adhoc(tc, "Failed to tell in filehandle: %d", errno);
        return r;
    }
    else {
        return data->byte_position;
    }
}

/* Reads the specified number of bytes into a the supplied buffer, returning
 * the number actually read. */
static MVMint64 read_bytes(MVMThreadContext *tc, MVMOSHandle *h, char **buf_out, MVMint64 bytes) {
    MVMIOFileData *data = (MVMIOFileData *)h->body.data;
    char *buf = MVM_malloc(bytes);
    unsigned int interval_id = MVM_telemetry_interval_start(tc, "syncfile.read_to_buffer");
    MVMint32 bytes_read;
#ifdef _WIN32
    /* Can only perform relatively small reads from a Windows console;
     * trying to do larger ones gives back ENOMEM, most likely due to
     * limitations of the Windows console subsystem. */
    if (bytes > 16387 && _isatty(data->fd))
        bytes = 16387;
#endif
    flush_output_buffer(tc, data);
    MVM_gc_mark_thread_blocked(tc);
    if ((bytes_read = read(data->fd, buf, bytes)) == -1) {
        int save_errno = errno;
        MVM_free(buf);
        MVM_gc_mark_thread_unblocked(tc);
        MVM_exception_throw_adhoc(tc, "Reading from filehandle failed: %s",
            strerror(save_errno));
    }
    *buf_out = buf;
    MVM_gc_mark_thread_unblocked(tc);
    MVM_telemetry_interval_annotate(bytes_read, interval_id, "read this many bytes");
    MVM_telemetry_interval_stop(tc, interval_id, "syncfile.read_to_buffer");
    data->byte_position += bytes_read;
    if (bytes_read == 0 && bytes != 0)
        data->eof_reported = 1;
    return bytes_read;
}

/* Checks if the end of file has been reached. */
static MVMint64 mvm_eof(MVMThreadContext *tc, MVMOSHandle *h) {
    MVMIOFileData *data = (MVMIOFileData *)h->body.data;
    if (data->seekable) {
        MVMint64 seek_pos;
        STAT statbuf;
        if (fstat(data->fd, &statbuf) == -1)
            MVM_exception_throw_adhoc(tc, "Failed to stat file descriptor: %s",
                strerror(errno));
        if ((seek_pos = MVM_platform_lseek(data->fd, 0, SEEK_CUR)) == -1)
            MVM_exception_throw_adhoc(tc, "Failed to seek in filehandle: %d", errno);
        /* Comparison with seek_pos for some special files, like those in /proc,
         * which file size is 0 can be false. In that case, we fall back to check
         * file size to detect EOF. */
        return statbuf.st_size == seek_pos || statbuf.st_size == 0;
    }
    else {
        return data->eof_reported;
    }
}

/* Sets the output buffer size; if <= 0, means no buffering. Flushes any
 * existing buffer before changing. */
static void set_buffer_size(MVMThreadContext *tc, MVMOSHandle *h, MVMint64 size) {
    MVMIOFileData *data = (MVMIOFileData *)h->body.data;

    /* Flush and clear up any existing output buffer. */
    flush_output_buffer(tc, data);
    MVM_free(data->output_buffer);

    /* Set up new buffer if needed. */
    if (size > 0) {
        data->output_buffer_size = size;
        data->output_buffer = MVM_malloc(size);
    }
    else {
        data->output_buffer_size = 0;
        data->output_buffer = NULL;
    }
}

/* Writes the specified bytes to the file handle. */
static MVMint64 write_bytes(MVMThreadContext *tc, MVMOSHandle *h, char *buf, MVMint64 bytes) {
    MVMIOFileData *data = (MVMIOFileData *)h->body.data;
    if (data->output_buffer_size && data->known_writable) {
        /* If we can't fit it on the end of the buffer, flush the buffer. */
        if (data->output_buffer_used + bytes > data->output_buffer_size)
            flush_output_buffer(tc, data);

        /* If we can fit it in the buffer now, memcpy it there, and we're
         * done. */
        if (bytes < data->output_buffer_size) {
            memcpy(data->output_buffer + data->output_buffer_used, buf, bytes);
            data->output_buffer_used += bytes;
            return bytes;
        }
    }
    perform_write(tc, data, buf, bytes);
    return bytes;
}

/* Flushes the file handle. */
static void flush(MVMThreadContext *tc, MVMOSHandle *h, MVMint32 sync){
    MVMIOFileData *data = (MVMIOFileData *)h->body.data;
    flush_output_buffer(tc, data);
    if (sync) {
        if (MVM_platform_fsync(data->fd) == -1) {
            /* If this is something that can't be flushed, we let that pass. */
            if (errno != EROFS && errno != EINVAL)
                MVM_exception_throw_adhoc(tc, "Failed to flush filehandle: %s", strerror(errno));
        }
    }
}

/* Truncates the file handle. */
static void truncatefh(MVMThreadContext *tc, MVMOSHandle *h, MVMint64 bytes) {
    MVMIOFileData *data = (MVMIOFileData *)h->body.data;
    if (ftruncate(data->fd, bytes) == -1)
        MVM_exception_throw_adhoc(tc, "Failed to truncate filehandle: %s", strerror(errno));
}

/* Closes the file. */
static MVMint64 closefh(MVMThreadContext *tc, MVMOSHandle *h) {
    MVMIOFileData *data = (MVMIOFileData *)h->body.data;
    if (data->fd != -1) {
        int r;
        flush_output_buffer(tc, data);
        MVM_free(data->output_buffer);
        data->output_buffer = NULL;
        r = close(data->fd);
        data->fd = -1;
        if (r == -1)
            MVM_exception_throw_adhoc(tc, "Failed to close filehandle: %s", strerror(errno));
    }
    return 0;
}

/* Locks a file. */
static MVMint64 lock(MVMThreadContext *tc, MVMOSHandle *h, MVMint64 flag) {
    MVMIOFileData *data = (MVMIOFileData *)h->body.data;

#ifdef _WIN32

    const DWORD len = 0xffffffff;
    const HANDLE hf = (HANDLE)_get_osfhandle(data->fd);
    OVERLAPPED offset;

    if (hf == INVALID_HANDLE_VALUE) {
        MVM_exception_throw_adhoc(tc, "Failed to lock filehandle: bad file descriptor");
    }

    flag = ((flag & MVM_FILE_FLOCK_NONBLOCK) ? LOCKFILE_FAIL_IMMEDIATELY : 0)
          + ((flag & MVM_FILE_FLOCK_TYPEMASK) == MVM_FILE_FLOCK_SHARED
                                       ? 0 : LOCKFILE_EXCLUSIVE_LOCK);

    memset (&offset, 0, sizeof(offset));
    MVM_gc_mark_thread_blocked(tc);
    if (LockFileEx(hf, flag, 0, len, len, &offset)) {
        MVM_gc_mark_thread_unblocked(tc);
        return 1;
    }
    MVM_gc_mark_thread_unblocked(tc);

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
        MVM_gc_mark_thread_blocked(tc);
        r = fcntl(fd, fc, &l);
        MVM_gc_mark_thread_unblocked(tc);
    } while (r == -1 && errno == EINTR);

    if (r == -1) {
        MVM_exception_throw_adhoc(tc, "Failed to lock filehandle: %d", errno);
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
    MVM_gc_mark_thread_blocked(tc);
    if (UnlockFileEx(hf, 0, len, len, &offset)) {
        MVM_gc_mark_thread_unblocked(tc);
        return;
    }
    MVM_gc_mark_thread_unblocked(tc);

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
        MVM_gc_mark_thread_blocked(tc);
        r = fcntl(fd, F_SETLKW, &l);
        MVM_gc_mark_thread_unblocked(tc);
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
        MVM_free(data->output_buffer);
        MVM_free(data);
    }
}

/* IO ops table, populated with functions. */
static const MVMIOClosable      closable      = { closefh };
static const MVMIOSyncReadable  sync_readable = { read_bytes, mvm_eof };
static const MVMIOSyncWritable  sync_writable = { write_bytes, flush, truncatefh };
static const MVMIOSeekable      seekable      = { seek, mvm_tell };
static const MVMIOLockable      lockable      = { lock, unlock };
static const MVMIOIntrospection introspection = { is_tty, mvm_fileno };

static const MVMIOOps op_table = {
    &closable,
    &sync_readable,
    &sync_writable,
    NULL,
    NULL,
    NULL,
    &seekable,
    NULL,
    NULL,
    &lockable,
    &introspection,
    &set_buffer_size,
    NULL,
    gc_free
};

/* Builds POSIX flag from mode string. */
static int resolve_open_mode(int *flag, const char *cp) {
    switch (*cp++) {
        case 'r': *flag = O_RDONLY; break;
        case '-': *flag = O_WRONLY; break;
        case '+': *flag = O_RDWR;   break;

        /* alias for "-c" or "-ct" if by itself */
        case 'w':
        *flag = *cp ? O_WRONLY | O_CREAT : O_WRONLY | O_CREAT | O_TRUNC;
        break;

        default:
        return 0;
    }

    for (;;) switch (*cp++) {
        case 0:
        return 1;

        case 'a': *flag |= O_APPEND; break;
        case 'c': *flag |= O_CREAT;  break;
        case 't': *flag |= O_TRUNC;  break;
        case 'x': *flag |= O_EXCL;   break;

        default:
        return 0;
    }
}

/* Opens a file, returning a synchronous file handle. */
MVMObject * MVM_file_open_fh(MVMThreadContext *tc, MVMString *filename, MVMString *mode) {
    char * const fname = MVM_string_utf8_c8_encode_C_string(tc, filename);
    int fd;
    int flag;
    STAT statbuf;

    /* Resolve mode description to flags. */
    char * const fmode  = MVM_string_utf8_encode_C_string(tc, mode);
    if (!resolve_open_mode(&flag, fmode)) {
        char *waste[] = { fname, fmode, NULL };
        MVM_exception_throw_adhoc_free(tc, waste,
            "Invalid open mode for file %s: %s", fname, fmode);
    }
    MVM_free(fmode);

    /* Try to open the file. */
#ifdef _WIN32
    flag |= _O_BINARY;
#endif
    if ((fd = open((const char *)fname, flag, DEFAULT_MODE)) == -1) {
        char *waste[] = { fname, NULL };
        const char *err = strerror(errno);
        MVM_exception_throw_adhoc_free(tc, waste, "Failed to open file %s: %s", fname, err);
    }

    /*  Check that we didn't open a directory by accident.
        If fstat fails, just move on: Most of the documented error cases should
        already have triggered when opening the file, and we can't do anything
        about the others; a failure also does not necessarily imply that the
        file descriptor cannot be used for reading/writing. */
    if (fstat(fd, &statbuf) == 0 && (statbuf.st_mode & S_IFMT) == S_IFDIR) {
        char *waste[] = { fname, NULL };
        if (close(fd) == -1) {
            const char *err = strerror(errno);
            MVM_exception_throw_adhoc_free(tc, waste,
                "Tried to open directory %s, which we failed to close: %s",
                fname, err);
        }
        MVM_exception_throw_adhoc_free(tc, waste, "Tried to open directory %s", fname);
    }

    /* Set up handle. */
    MVM_free(fname);
    {
        MVMIOFileData * const data   = MVM_calloc(1, sizeof(MVMIOFileData));
        MVMOSHandle   * const result = (MVMOSHandle *)MVM_repr_alloc_init(tc,
            tc->instance->boot_types.BOOTIO);
        data->fd          = fd;
        data->seekable    = MVM_platform_lseek(fd, 0, SEEK_CUR) != -1;
        result->body.ops  = &op_table;
        result->body.data = data;
        return (MVMObject *)result;
    }
}

/* Opens a file, returning a synchronous file handle. */
MVMObject * MVM_file_handle_from_fd(MVMThreadContext *tc, int fd) {
    MVMOSHandle   * const result = (MVMOSHandle *)MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTIO);
    MVMIOFileData * const data   = MVM_calloc(1, sizeof(MVMIOFileData));
    data->fd          = fd;
    data->seekable    = MVM_platform_lseek(fd, 0, SEEK_CUR) != -1;
    result->body.ops  = &op_table;
    result->body.data = data;
#ifdef _WIN32
    _setmode(fd, _O_BINARY);
#endif
    return (MVMObject *)result;
}
