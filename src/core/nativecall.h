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
#define MVM_NATIVECALL_ARG_TYPE_MASK       62

/* Flag for whether we should free a string after passing it or not. */
#define MVM_NATIVECALL_ARG_NO_FREE_STR     0
#define MVM_NATIVECALL_ARG_FREE_STR        1
#define MVM_NATIVECALL_ARG_FREE_STR_MASK   1
#define MVM_NATIVECALL_ARG_NO_RW           0
#define MVM_NATIVECALL_ARG_RW              256
#define MVM_NATIVECALL_ARG_RW_MASK         256

/* Native callback entry. Hung off MVMNativeCallbackCacheHead, which is
 * a hash owned by the ThreadContext. All MVMNativeCallbacks in a linked
 * list have the same cuid, which is the key to the CacheHead hash.
 */
struct MVMNativeCallback {
    /* The dyncall callback object. */
    DCCallback *cb;

    /* The routine that we will call. */
    MVMObject *target;

    /* Thread context we expect to run the callback on. */
    MVMThreadContext *tc;

    /* Return and argument type flags. */
    MVMint16 *typeinfos;

    /* Return and argument types themselves. */
    MVMObject **types;

    /* The number of entries in typeinfos/types. */
    MVMint32 num_types;

    /* The MoarVM callsite object for this call. */
    MVMCallsite *cs;

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
void MVM_nativecall_build(MVMThreadContext *tc, MVMObject *site, MVMString *lib,
    MVMString *sym, MVMString *conv, MVMObject *arg_spec, MVMObject *ret_spec);
MVMObject * MVM_nativecall_invoke(MVMThreadContext *tc, MVMObject *res_type,
    MVMObject *site, MVMObject *args);
MVMObject * MVM_nativecall_global(MVMThreadContext *tc, MVMString *lib, MVMString *sym,
    MVMObject *target_spec, MVMObject *target_type);
MVMObject * MVM_nativecall_cast(MVMThreadContext *tc, MVMObject *target_spec,
    MVMObject *res_type, MVMObject *obj);
MVMint64 MVM_nativecall_sizeof(MVMThreadContext *tc, MVMObject *obj);
void MVM_nativecall_refresh(MVMThreadContext *tc, MVMObject *cthingy);
MVMObject * MVM_nativecall_make_cstruct(MVMThreadContext *tc, MVMObject *type, void *cstruct);
MVMObject * MVM_nativecall_make_cunion(MVMThreadContext *tc, MVMObject *type, void *cunion);
MVMObject * MVM_nativecall_make_cpointer(MVMThreadContext *tc, MVMObject *type, void *ptr);
MVMObject * MVM_nativecall_make_carray(MVMThreadContext *tc, MVMObject *type, void *carray);
