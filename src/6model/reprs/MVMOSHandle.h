/* Representation used by VM-level OS handles. */
struct MVMOSHandleBody {
    uv_handle_t *handle;

    MVMuint8 tty_type; /* We need it untill libuv supports uv_file_t,
                        * they said will support it soon. */

    /* see MVMOSHandleTypes */
    MVMuint8 handle_type;
    MVMuint8 encoding_type;
    apr_pool_t *mem_pool;

    union {
        apr_file_t   *file_handle;
        apr_dir_t    *dir_handle;
        apr_socket_t *socket;
    };

};
struct MVMOSHandle {
    MVMObject common;
    MVMOSHandleBody body;
};

typedef enum {
    MVM_STDIN  = 0,
    MVM_STDOUT = 1,
    MVM_STDERR = 2
} MVMSTDHandleType;

typedef enum {
    MVM_OSHANDLE_UNINIT = UV_UNKNOWN_HANDLE,
    MVM_OSHANDLE_FILE   = UV_FILE,
    MVM_OSHANDLE_DIR    = 2,
    MVM_OSHANDLE_SOCKET = 3
} MVMOSHandleTypes;

/* Function for REPR setup. */
MVMREPROps * MVMOSHandle_initialize(MVMThreadContext *tc);
