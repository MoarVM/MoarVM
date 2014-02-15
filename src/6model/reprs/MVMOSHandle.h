#ifndef _WIN32
#include <dirent.h>
#endif

/* Representation used by VM-level OS handles. */
struct MVMOSHandleBody {
    /* The function table for this handle, determining how it will process
     * various kinds of I/O related operations. */
    MVMIOOps *ops;

    /* Any data a particular set of I/O functions wishes to store. */
    void *data;

    /* XXX Everything below is going away during I/O refactor. */
    /* see MVMOSHandleTypes */
    MVMuint8 type;
    union {
        struct
        {
          uv_handle_t   *handle;
          void            *data;
          MVMint32       length;
        };
#ifdef _WIN32
        struct {
            wchar_t   *dir_name;
            HANDLE   dir_handle;
        };
#else
        DIR     *dir_handle;
#endif
    } u;
    MVMuint8 encoding_type;
};
struct MVMOSHandle {
    MVMObject common;
    MVMOSHandleBody body;
};

typedef enum {
   MVM_OSHANDLE_UNINIT = 0,
   MVM_OSHANDLE_DIR    = 3,
   MVM_OSHANDLE_TCP    = 4,
   MVM_OSHANDLE_UDP    = 5,
}  MVMOSHandleTypes;

/* Function for REPR setup. */
const MVMREPROps * MVMOSHandle_initialize(MVMThreadContext *tc);
