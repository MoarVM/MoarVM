#if defined _WIN32
MVMint64 MVM_platform_lseek(int fd, MVMint64 offset, int origin);
MVMint64 MVM_platform_unlink(const char *pathname);
int MVM_platform_fsync(int fd);
#else
#define MVM_platform_lseek lseek
#define MVM_platform_unlink unlink
#define MVM_platform_fsync fsync
#endif

#if defined(__APPLE__) || defined(__Darwin__)
short MVM_platform_is_fd_seekable(int fd);
#else
#define MVM_platform_is_fd_seekable(x) (MVM_platform_lseek((x), 0, SEEK_CUR) != -1)
#endif
