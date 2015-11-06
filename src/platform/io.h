#if defined _WIN32
MVMint64 MVM_platform_lseek(int fd, MVMint64 offset, int origin);
MVMint64 MVM_platform_unlink(const char *pathname);
MVMint64 MVM_platform_size_from_fd(int fd);
#define MVM_platform_open _open
#define MVM_platform_read _read
#define MVM_platform_write _write
#define MVM_platform_close _close
#define MVM_platform_ensure_binary_fd(fd) _setmode(fd, _O_BINARY)
#define MVM_platform_add_binary_flag(flag) (flag) |= _O_BINARY
#define MVM_platform_isatty _isatty
#else
#define MVM_platform_lseek lseek
#define MVM_platform_unlink unlink
#define MVM_platform_open open
#define MVM_platform_read read
#define MVM_platform_write write
#define MVM_platform_close close
#define MVM_platform_ensure_binary_fd(fd) /* no-op on POSIX */
#define MVM_platform_add_binary_flag(flag) /* no-op on POSIX */
#define MVM_platform_isatty isatty
#endif
