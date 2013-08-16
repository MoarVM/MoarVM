/* Represents a compilation unit; essentially, the runtime representation
 * of a MAST::CompUnit. It may be mapped in from a file, created in memory
 * or something else. */
typedef struct _MVMCompUnit MVMCompUnit;

MVMCompUnit * MVM_cu_map_from_file(MVMThreadContext *tc, char *filename);
