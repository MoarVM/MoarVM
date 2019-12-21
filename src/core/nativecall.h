/* The various native call argument types. */
#define MVM_NATIVECALL_ARG_VOID            0
#define MVM_NATIVECALL_ARG_CHAR            2
#define MVM_NATIVECALL_ARG_SHORT           4
#define MVM_NATIVECALL_ARG_INT             6
#define MVM_NATIVECALL_ARG_LONG            8
#define MVM_NATIVECALL_ARG_LONGLONG        10
#define MVM_NATIVECALL_ARG_FLOAT           12
#define MVM_NATIVECALL_ARG_DOUBLE          14
#define MVM_NATIVECALL_ARG_ASCIISTR        16
#define MVM_NATIVECALL_ARG_UTF8STR         18
#define MVM_NATIVECALL_ARG_UTF16STR        20
#define MVM_NATIVECALL_ARG_CSTRUCT         22
#define MVM_NATIVECALL_ARG_CARRAY          24
#define MVM_NATIVECALL_ARG_CALLBACK        26
#define MVM_NATIVECALL_ARG_CPOINTER        28
#define MVM_NATIVECALL_ARG_VMARRAY         30
#define MVM_NATIVECALL_ARG_UCHAR           32
#define MVM_NATIVECALL_ARG_USHORT          34
#define MVM_NATIVECALL_ARG_UINT            36
#define MVM_NATIVECALL_ARG_ULONG           38
#define MVM_NATIVECALL_ARG_ULONGLONG       40
#define MVM_NATIVECALL_ARG_CUNION          42
#define MVM_NATIVECALL_ARG_CPPSTRUCT       44
#define MVM_NATIVECALL_ARG_TYPE_MASK       62

/* Flag for whether we should free a string after passing it or not. */
#define MVM_NATIVECALL_ARG_NO_FREE_STR     0
#define MVM_NATIVECALL_ARG_FREE_STR        1
#define MVM_NATIVECALL_ARG_FREE_STR_MASK   1
/* Flag for whether we need to refresh a CArray after passing or not. */
#define MVM_NATIVECALL_ARG_NO_REFRESH      0
#define MVM_NATIVECALL_ARG_REFRESH         1
#define MVM_NATIVECALL_ARG_REFRESH_MASK    1
#define MVM_NATIVECALL_ARG_NO_RW           0
#define MVM_NATIVECALL_ARG_RW              256
#define MVM_NATIVECALL_ARG_RW_MASK         256

#define MVM_NATIVECALL_UNMARSHAL_KIND_GENERIC     -1
#define MVM_NATIVECALL_UNMARSHAL_KIND_RETURN      -2
#define MVM_NATIVECALL_UNMARSHAL_KIND_NATIVECAST  -3

/* Native callback entry. Hung off MVMNativeCallbackCacheHead, which is
 * a hash owned by the ThreadContext. All MVMNativeCallbacks in a linked
 * list have the same cuid, which is the key to the CacheHead hash.
 */
struct MVMNativeCallback {
    /* The dyncall/libffi callback object. */
    void *cb;

    /* The routine that we will call. */
    MVMObject *target;

    /* The VM instance. */
    MVMInstance *instance;

    /* Return and argument type flags. */
    MVMint16 *typeinfos;

    /* Return and argument types themselves. */
    MVMObject **types;

    /* The number of entries in typeinfos/types. */
    MVMint32 num_types;

    /* The MoarVM callsite object for this call. */
    MVMCallsite *cs;

#ifdef HAVE_LIBFFI
    ffi_abi     convention;
    ffi_type  **ffi_arg_types;
    ffi_type   *ffi_ret_type;
#endif

    /* The next entry in the linked list */
    MVMNativeCallback *next;
};


/* A hash of nativecall callbacks. Each entry is a linked
 * list of MVMNativeCallback sharing the same cuid.
 * Multiple callbacks with the same cuid get created when
 * closures are taken and need to be differentiated.
 */
struct MVMNativeCallbackCacheHead {
    MVMNativeCallback *head;

    /* The uthash hash handle inline struct. */
    UT_hash_handle hash_handle;
};

/* Functions for working with native callsites. */
MVMNativeCallBody * MVM_nativecall_get_nc_body(MVMThreadContext *tc, MVMObject *obj);
MVMint16 MVM_nativecall_get_arg_type(MVMThreadContext *tc, MVMObject *info, MVMint16 is_return);
MVMint8 MVM_nativecall_build(MVMThreadContext *tc, MVMObject *site, MVMString *lib,
    MVMString *sym, MVMString *conv, MVMObject *arg_spec, MVMObject *ret_spec);
void MVM_nativecall_setup(MVMThreadContext *tc, MVMNativeCallBody *body, unsigned int interval_id);
void MVM_nativecall_restore_library(MVMThreadContext *tc, MVMNativeCallBody *body, MVMObject *root);
MVMObject * MVM_nativecall_invoke(MVMThreadContext *tc, MVMObject *res_type,
    MVMObject *site, MVMObject *args);
void MVM_nativecall_invoke_jit(MVMThreadContext *tc, MVMObject *site);
MVMObject * MVM_nativecall_global(MVMThreadContext *tc, MVMString *lib, MVMString *sym,
    MVMObject *target_spec, MVMObject *target_type);
MVMObject * MVM_nativecall_cast(MVMThreadContext *tc, MVMObject *target_spec,
    MVMObject *res_type, MVMObject *obj);
MVMint64 MVM_nativecall_sizeof(MVMThreadContext *tc, MVMObject *obj);
void MVM_nativecall_refresh(MVMThreadContext *tc, MVMObject *cthingy);

MVMObject * MVM_nativecall_make_cstruct(MVMThreadContext *tc, MVMObject *type, void *cstruct);
MVMObject * MVM_nativecall_make_cppstruct(MVMThreadContext *tc, MVMObject *type, void *cppstruct);
MVMObject * MVM_nativecall_make_cunion(MVMThreadContext *tc, MVMObject *type, void *cunion);
MVMObject * MVM_nativecall_make_cpointer(MVMThreadContext *tc, MVMObject *type, void *ptr);
MVMObject * MVM_nativecall_make_carray(MVMThreadContext *tc, MVMObject *type, void *carray);

MVMObject * MVM_nativecall_make_int(MVMThreadContext *tc, MVMObject *type, MVMint64 value);
MVMObject * MVM_nativecall_make_uint(MVMThreadContext *tc, MVMObject *type, MVMuint64 value);
MVMObject * MVM_nativecall_make_num(MVMThreadContext *tc, MVMObject *type, MVMnum64 value);
MVMObject * MVM_nativecall_make_str(MVMThreadContext *tc, MVMObject *type, MVMint16 ret_type, char *cstring);

signed char         MVM_nativecall_unmarshal_char(MVMThreadContext *tc, MVMObject *value);
signed short        MVM_nativecall_unmarshal_short(MVMThreadContext *tc, MVMObject *value);
signed int          MVM_nativecall_unmarshal_int(MVMThreadContext *tc, MVMObject *value);
signed long         MVM_nativecall_unmarshal_long(MVMThreadContext *tc, MVMObject *value);
signed long long    MVM_nativecall_unmarshal_longlong(MVMThreadContext *tc, MVMObject *value);
unsigned char       MVM_nativecall_unmarshal_uchar(MVMThreadContext *tc, MVMObject *value);
unsigned short      MVM_nativecall_unmarshal_ushort(MVMThreadContext *tc, MVMObject *value);
unsigned int        MVM_nativecall_unmarshal_uint(MVMThreadContext *tc, MVMObject *value);
unsigned long       MVM_nativecall_unmarshal_ulong(MVMThreadContext *tc, MVMObject *value);
unsigned long long  MVM_nativecall_unmarshal_ulonglong(MVMThreadContext *tc, MVMObject *value);
float               MVM_nativecall_unmarshal_float(MVMThreadContext *tc, MVMObject *value);
double              MVM_nativecall_unmarshal_double(MVMThreadContext *tc, MVMObject *value);

char * MVM_nativecall_unmarshal_string(MVMThreadContext *tc, MVMObject *value, MVMint16 type, MVMint16 *free, MVMint16 unmarshal_kind);
void * MVM_nativecall_unmarshal_cstruct(MVMThreadContext *tc, MVMObject *value, MVMint16 unmarshal_kind);
void * MVM_nativecall_unmarshal_cppstruct(MVMThreadContext *tc, MVMObject *value, MVMint16 unmarshal_kind);
void * MVM_nativecall_unmarshal_cpointer(MVMThreadContext *tc, MVMObject *value, MVMint16 unmarshal_kind);
void * MVM_nativecall_unmarshal_carray(MVMThreadContext *tc, MVMObject *value, MVMint16 unmarshal_kind);
void * MVM_nativecall_unmarshal_vmarray(MVMThreadContext *tc, MVMObject *value, MVMint16 unmarshal_kind);
void * MVM_nativecall_unmarshal_cunion(MVMThreadContext *tc, MVMObject *value, MVMint16 unmarshal_kind);
MVMThreadContext * MVM_nativecall_find_thread_context(MVMInstance *instance);
MVMJitGraph *MVM_nativecall_jit_graph_for_caller_code(
    MVMThreadContext   *tc,
    MVMSpeshGraph      *sg,
    MVMNativeCallBody  *body,
    MVMint16            restype,
    MVMint16            dst,
    MVMSpeshIns       **arg_ins
);
