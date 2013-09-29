#if defined _WIN32
MVMint64 MVM_platform_lseek(int fd, MVMint64 offset, int origin);
MVMint64 MVM_platform_unlink(const char *pathname);
#else
#define MVM_platform_lseek lseek
#define MVM_platform_unlink unlink
#endif
