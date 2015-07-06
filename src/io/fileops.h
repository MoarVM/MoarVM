#define MVM_FILE_FLOCK_SHARED        1       /* Shared lock. Read lock */
#define MVM_FILE_FLOCK_EXCLUSIVE     2       /* Exclusive lock. Write lock. */
#define MVM_FILE_FLOCK_TYPEMASK      0x000F  /* a mask of lock type */
#define MVM_FILE_FLOCK_NONBLOCK      0x0010  /* asynchronous block during
                                              * locking the file */
#define MVM_STAT_EXISTS              0
#define MVM_STAT_FILESIZE            1
#define MVM_STAT_ISDIR               2
#define MVM_STAT_ISREG               3
#define MVM_STAT_ISDEV               4
#define MVM_STAT_CREATETIME          5
#define MVM_STAT_ACCESSTIME          6
#define MVM_STAT_MODIFYTIME          7
#define MVM_STAT_CHANGETIME          8
#define MVM_STAT_BACKUPTIME          9
#define MVM_STAT_UID                10
#define MVM_STAT_GID                11
#define MVM_STAT_ISLNK              12
#define MVM_STAT_PLATFORM_DEV       -1
#define MVM_STAT_PLATFORM_INODE     -2
#define MVM_STAT_PLATFORM_MODE      -3
#define MVM_STAT_PLATFORM_NLINKS    -4
#define MVM_STAT_PLATFORM_DEVTYPE   -5
#define MVM_STAT_PLATFORM_BLOCKSIZE -6
#define MVM_STAT_PLATFORM_BLOCKS    -7

MVMint64 MVM_file_stat(MVMThreadContext *tc, MVMString *filename, MVMint64 status, MVMint32 use_lstat);
void MVM_file_copy(MVMThreadContext *tc, MVMString *src, MVMString *dest);
void MVM_file_rename(MVMThreadContext *tc, MVMString *src, MVMString *dest);
void MVM_file_delete(MVMThreadContext *tc, MVMString *f);
void MVM_file_chmod(MVMThreadContext *tc, MVMString *f, MVMint64 flag);
MVMint64 MVM_file_exists(MVMThreadContext *tc, MVMString *f, MVMint32 use_lstat);
MVMint64 MVM_file_isreadable(MVMThreadContext *tc, MVMString *filename, MVMint32 use_lstat);
MVMint64 MVM_file_iswritable(MVMThreadContext *tc, MVMString *filename, MVMint32 use_lstat);
MVMint64 MVM_file_isexecutable(MVMThreadContext *tc, MVMString *filename, MVMint32 use_lstat);
MVMString * MVM_file_slurp(MVMThreadContext *tc, MVMString *filename, MVMString *encoding);
void MVM_file_spew(MVMThreadContext *tc, MVMString *output, MVMString *filename, MVMString *encoding);
MVMObject * MVM_file_get_stdstream(MVMThreadContext *tc, MVMuint8 type, MVMuint8 readable);
MVMString * MVM_file_in_libpath(MVMThreadContext *tc, MVMString *orig);
void MVM_file_link(MVMThreadContext *tc, MVMString *oldpath, MVMString *newpath);
void MVM_file_symlink(MVMThreadContext *tc, MVMString *oldpath, MVMString *newpath);
MVMString * MVM_file_readlink(MVMThreadContext *tc, MVMString *path);
