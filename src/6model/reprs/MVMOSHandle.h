#ifndef _WIN32
#include <dirent.h>
#endif

/* Representation used by VM-level OS handles. */
struct MVMOSHandleBody {
    /* see MVMOSHandleTypes */
    MVMuint8 type;
    union {
        struct
        {
          uv_handle_t *handle;
          void          *data;
          MVMint32     length;
        };
        uv_file          fd;
#ifdef _WIN32
        struct {
            wchar_t   *dir_name;
            HANDLE   dir_handle;
        };
#else
        DIR     *dir_handle;
#endif
    };
    MVMuint8        eof;
    MVMuint8 encoding_type;
};
struct MVMOSHandle {
    MVMObject common;
    MVMOSHandleBody body;
};

typedef enum {
   MVM_OSHANDLE_UNINIT = 0,
   MVM_OSHANDLE_HANDLE = 1,
   MVM_OSHANDLE_FD     = 2,
   MVM_OSHANDLE_DIR    = 3,
   MVM_OSHANDLE_TCP    = 4,
   MVM_OSHANDLE_UDP    = 5,
   MVM_OSHANDLE_SOCKET = 6 /* XXX: not need after fully port to libuv */
}  MVMOSHandleTypes;

/* Function for REPR setup. */
MVMREPROps * MVMOSHandle_initialize(MVMThreadContext *tc);
