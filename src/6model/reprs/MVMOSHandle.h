/* Representation used by VM-level OS handles. */
struct MVMOSHandleBody {
    MVMuint8 type;
    union {
       uv_handle_t handle;
       uv_req_t    req;
    };

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
   MVM_OSHANDLE_HANDLE = 0,
   MVM_OSHANDLE_REQ    = 1,
};

typedef enum {
    MVM_OSHANDLE_UNINIT = UV_UNKNOWN_HANDLE,
    MVM_OSHANDLE_FILE   = UV_FILE,
    MVM_OSHANDLE_DIR    = 2,
    MVM_OSHANDLE_SOCKET = 3
} MVMOSHandleTypes;

/* Function for REPR setup. */
MVMREPROps * MVMOSHandle_initialize(MVMThreadContext *tc);
