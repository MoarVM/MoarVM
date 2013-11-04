#define MVM_FILE_FLOCK_SHARED        1       /* Shared lock. Read lock */
#define MVM_FILE_FLOCK_EXCLUSIVE     2       /* Exclusive lock. Write lock. */
#define MVM_FILE_FLOCK_TYPEMASK      0x000F  /* a mask of lock type */
#define MVM_FILE_FLOCK_NONBLOCK      0x0010  /* asynchronous block during
                                              * locking the file */
#define MVM_stat_exists              0
#define MVM_stat_filesize            1
#define MVM_stat_isdir               2
#define MVM_stat_isreg               3
#define MVM_stat_isdev               4
#define MVM_stat_createtime          5
#define MVM_stat_accesstime          6
#define MVM_stat_modifytime          7
#define MVM_stat_changetime          8
#define MVM_stat_backuptime          9
#define MVM_stat_uid                10
#define MVM_stat_gid                11
#define MVM_stat_islnk              12
#define MVM_stat_platform_dev       -1
#define MVM_stat_platform_inode     -2
#define MVM_stat_platform_mode      -3
#define MVM_stat_platform_nlinks    -4
#define MVM_stat_platform_devtype   -5
#define MVM_stat_platform_blocksize -6
#define MVM_stat_platform_blocks    -7

MVMint64 MVM_file_stat(MVMThreadContext *tc, MVMString *filename, MVMint64 status);
void MVM_file_copy(MVMThreadContext *tc, MVMString *src, MVMString *dest);
void MVM_file_rename(MVMThreadContext *tc, MVMString *src, MVMString *dest);
void MVM_file_delete(MVMThreadContext *tc, MVMString *f);
void MVM_file_chmod(MVMThreadContext *tc, MVMString *f, MVMint64 flag);
MVMint64 MVM_file_exists(MVMThreadContext *tc, MVMString *f);
MVMint64 MVM_file_isreadable(MVMThreadContext *tc, MVMString *filename);
MVMint64 MVM_file_iswritable(MVMThreadContext *tc, MVMString *filename);
MVMint64 MVM_file_isexecutable(MVMThreadContext *tc, MVMString *filename);
MVMObject * MVM_file_open_fh(MVMThreadContext *tc, MVMString *filename, MVMString *mode);
void MVM_file_close_fh(MVMThreadContext *tc, MVMObject *oshandle);
MVMString * MVM_file_readline_fh(MVMThreadContext *tc, MVMObject *oshandle);
MVMString * MVM_file_readline_interactive_fh(MVMThreadContext *tc, MVMObject *oshandle, MVMString *prompt);
MVMString * MVM_file_read_fhs(MVMThreadContext *tc, MVMObject *oshandle, MVMint64 length);
MVMString * MVM_file_readall_fh(MVMThreadContext *tc, MVMObject *oshandle);
MVMString * MVM_file_slurp(MVMThreadContext *tc, MVMString *filename, MVMString *encoding);
void MVM_file_spew(MVMThreadContext *tc, MVMString *output, MVMString *filename, MVMString *encoding);
MVMint64 MVM_file_write_fhs(MVMThreadContext *tc, MVMObject *oshandle, MVMString *str);
void MVM_file_seek(MVMThreadContext *tc, MVMObject *oshandle, MVMint64 offset, MVMint64 flag);
MVMint64 MVM_file_tell_fh(MVMThreadContext *tc, MVMObject *oshandle);
MVMint64 MVM_file_eof(MVMThreadContext *tc, MVMObject *oshandle);
MVMint64 MVM_file_lock(MVMThreadContext *tc, MVMObject *oshandle, MVMint64 flag);
void MVM_file_unlock(MVMThreadContext *tc, MVMObject *oshandle);
void MVM_file_sync(MVMThreadContext *tc, MVMObject *oshandle);
void MVM_file_truncate(MVMThreadContext *tc, MVMObject *oshandle, MVMint64 offset);
MVMObject * MVM_file_get_stdin(MVMThreadContext *tc);
MVMObject * MVM_file_get_stdout(MVMThreadContext *tc);
MVMObject * MVM_file_get_stderr(MVMThreadContext *tc);
void MVM_file_set_encoding(MVMThreadContext *tc, MVMObject *oshandle, MVMString *encoding_name);