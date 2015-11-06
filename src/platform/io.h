#if defined _WIN32
MVMint64 MVM_platform_lseek(int fd, MVMint64 offset, int origin);
MVMint64 MVM_platform_unlink(const char *pathname);
#define MVM_platform_open _open
#define MVM_platform_read _read
#define MVM_platform_write _write
#define MVM_platform_close _close
#else
#define MVM_platform_lseek lseek
#define MVM_platform_unlink unlink
#define MVM_platform_open open
#define MVM_platform_read read
#define MVM_platform_write write
#define MVM_platform_close close
#endif
