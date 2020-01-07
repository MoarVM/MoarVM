#if defined _WIN32
MVMint64 MVM_platform_lseek(int fd, MVMint64 offset, int origin);
MVMint64 MVM_platform_unlink(const char *pathname);
int MVM_platform_fsync(int fd);
int MVM_platform_open(const char *pathname, int flags, ...);
FILE *MVM_platform_fopen(const char *pathname, const char *mode);
#else
#define MVM_platform_lseek lseek
#define MVM_platform_unlink unlink
#define MVM_platform_fsync fsync
#define MVM_platform_open open
#define MVM_platform_fopen fopen
#endif

#if defined(__APPLE__) || defined(__Darwin__)
short MVM_platform_is_fd_seekable(int fd);
#else
#define MVM_platform_is_fd_seekable(x) (MVM_platform_lseek((x), 0, SEEK_CUR) != -1)
#endif
