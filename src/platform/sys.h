/* Tries to determine the number of logical CPUs available to the process.
 * May return 0 on error.
 */
MVMuint32 MVM_platform_cpu_count(void);
void MVM_platform_uname(MVMThreadContext *tc, MVMObject *result);
