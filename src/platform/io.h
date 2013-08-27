#if defined _WIN32
MVMint64 MVM_platform_lseek(int fd, MVMint64 offset, int origin);
#else
#define MVM_platform_lseek lseek
#endif
