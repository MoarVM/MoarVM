/* Representation used by VM-level OS handles. */
typedef struct _MVMOSHandleBody {
    /* see MVMOSHandleTypes */
    MVMuint8 handle_type;
    MVMuint8 encoding_type;
    apr_pool_t *mem_pool;

    union {
        apr_file_t   *file_handle;
        apr_dir_t    *dir_handle;
        apr_socket_t *socket;
    };

} MVMOSHandleBody;
typedef struct _MVMOSHandle {
    MVMObject common;
    MVMOSHandleBody body;
} MVMOSHandle;

typedef enum {
    MVM_OSHANDLE_UNINIT = 0,
    MVM_OSHANDLE_FILE   = 1,
    MVM_OSHANDLE_DIR    = 2,
    MVM_OSHANDLE_SOCKET = 3
} MVMOSHandleTypes;

/* Function for REPR setup. */
MVMREPROps * MVMOSHandle_initialize(MVMThreadContext *tc);
