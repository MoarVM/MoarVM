/* Gets time since the epoch in nanoseconds.
 * In principle, may return 0 on error.
 */
MVMuint64 MVM_platform_now(void);

/* Tries to sleep for at least the requested number
 * of nanoseconds.
 */
void MVM_platform_sleep(MVMnum64 second);
void MVM_platform_nanosleep(MVMuint64 nanos);

void MVM_platform_decodelocaltime(MVMThreadContext *tc, MVMint64 time, MVMint64 decoded[]);
