#include <moar.h>
#include <platform/io.h>

MVMint64 MVM_platform_size_from_fd(int fd) {
    struct stat sbuf;
    return fstat(fd, &sbuf) ? -1 : sbuf.st_size;
}
