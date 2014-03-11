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
#define MVM_NATIVECALL_ARG_TYPE_MASK       30

/* Flag for whether we should free a string after passing it or not. */
#define MVM_NATIVECALL_ARG_NO_FREE_STR     0
#define MVM_NATIVECALL_ARG_FREE_STR        1
#define MVM_NATIVECALL_ARG_FREE_STR_MASK   1

/* Functions for working with native callsites. */
void MVM_nativecall_build(MVMThreadContext *tc, MVMObject *site, MVMString *lib,
    MVMString *sym, MVMString *conv, MVMObject *arg_spec, MVMObject *ret_spec);
MVMObject * MVM_nativecall_invoke(MVMThreadContext *tc, MVMObject *res_type,
    MVMObject *site, MVMObject *args);
void MVM_nativecall_refresh(MVMThreadContext *tc, MVMObject *cthingy);
