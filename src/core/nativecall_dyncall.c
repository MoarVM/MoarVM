#include "moar.h"
#ifndef _WIN32
#include <dlfcn.h>
#endif

/* Maps a calling convention name to an ID. */
MVMint16 MVM_nativecall_get_calling_convention(MVMThreadContext *tc, MVMString *name) {
    MVMint16 result = DC_CALL_C_DEFAULT;
    if (name && MVM_string_graphs(tc, name) > 0) {
        char *cname = MVM_string_utf8_encode_C_string(tc, name);
        if (strcmp(cname, "cdecl") == 0)
            result = DC_CALL_C_X86_CDECL;
        else if (strcmp(cname, "stdcall") == 0)
            result = DC_CALL_C_X86_WIN32_STD;
        else if (strcmp(cname, "thisgnu") == 0)
            result = DC_CALL_C_X86_WIN32_THIS_GNU;
        else if (strcmp(cname, "thisms") == 0)
            result = DC_CALL_C_X86_WIN32_THIS_MS;
        else if (strcmp(cname, "stdcall") == 0)
            result = DC_CALL_C_X64_WIN64;
        else {
            char *waste[] = { cname, NULL };
            MVM_exception_throw_adhoc_free(tc, waste,
                "Unknown calling convention '%s' used for native call", cname);
        }
        MVM_free(cname);
    }
    return result;
}

/* Map argument type ID to dyncall character ID. */
static char get_signature_char(MVMint16 type_id) {
    if ( (type_id & MVM_NATIVECALL_ARG_RW_MASK) == MVM_NATIVECALL_ARG_RW)
        return 'p';

    switch (type_id & MVM_NATIVECALL_ARG_TYPE_MASK) {
        case MVM_NATIVECALL_ARG_VOID:
            return 'v';
        case MVM_NATIVECALL_ARG_CHAR:
            return 'c';
        case MVM_NATIVECALL_ARG_SHORT:
            return 's';
        case MVM_NATIVECALL_ARG_INT:
            return 'i';
        case MVM_NATIVECALL_ARG_LONG:
            return 'j';
        case MVM_NATIVECALL_ARG_LONGLONG:
            return 'l';
        case MVM_NATIVECALL_ARG_FLOAT:
            return 'f';
        case MVM_NATIVECALL_ARG_DOUBLE:
            return 'd';
        case MVM_NATIVECALL_ARG_ASCIISTR:
        case MVM_NATIVECALL_ARG_UTF8STR:
        case MVM_NATIVECALL_ARG_UTF16STR:
        case MVM_NATIVECALL_ARG_CSTRUCT:
        case MVM_NATIVECALL_ARG_CPPSTRUCT:
        case MVM_NATIVECALL_ARG_CPOINTER:
        case MVM_NATIVECALL_ARG_CARRAY:
        case MVM_NATIVECALL_ARG_CUNION:
        case MVM_NATIVECALL_ARG_VMARRAY:
        case MVM_NATIVECALL_ARG_CALLBACK:
            return 'p';
        case MVM_NATIVECALL_ARG_UCHAR:
            return 'C';
        case MVM_NATIVECALL_ARG_USHORT:
            return 'S';
        case MVM_NATIVECALL_ARG_UINT:
            return 'I';
        case MVM_NATIVECALL_ARG_ULONG:
            return 'J';
        case MVM_NATIVECALL_ARG_ULONGLONG:
            return 'L';
        default:
            return '\0';
    }
}

/* Sets up a callback, caching the information to avoid duplicate work. */
static char callback_handler(DCCallback *cb, DCArgs *args, DCValue *result, MVMNativeCallback *data);
static void * unmarshal_callback(MVMThreadContext *tc, MVMObject *callback, MVMObject *sig_info) {
    MVMNativeCallback **callback_data_handle;
    MVMString          *cuid;

    if (!IS_CONCRETE(callback))
        return NULL;

    /* Try to locate existing cached callback info. */
    callback = MVM_frame_find_invokee(tc, callback, NULL);
    cuid     = ((MVMCode *)callback)->body.sf->body.cuuid;

    if (!MVM_str_hash_entry_size(tc, &tc->native_callback_cache)) {
        MVM_str_hash_build(tc, &tc->native_callback_cache, sizeof(MVMNativeCallbackCacheHead), 0);
    }

    MVMNativeCallbackCacheHead *callback_data_head
        = MVM_str_hash_lvalue_fetch(tc, &tc->native_callback_cache, cuid);

    if (!callback_data_head->hash_handle.key) {
        /* MVM_str_hash_lvalue_fetch created a new entry. Fill it in: */
        callback_data_head->hash_handle.key = cuid;
        callback_data_head->head = NULL;
    }

    callback_data_handle = &(callback_data_head->head);

    while (*callback_data_handle) {
        if ((*callback_data_handle)->target == callback) /* found it, break */
            break;

        callback_data_handle = &((*callback_data_handle)->next);
    }

    if (!*callback_data_handle) {
        /* First, build the MVMNativeCallback */
        MVMCallsite *cs;
        char        *signature;
        MVMObject   *typehash;
        MVMint64     num_info, i;
        MVMNativeCallback *callback_data;

        num_info                 = MVM_repr_elems(tc, sig_info);
        callback_data            = MVM_malloc(sizeof(MVMNativeCallback));
        callback_data->num_types = num_info;
        callback_data->typeinfos = MVM_malloc(num_info * sizeof(MVMint16));
        callback_data->types     = MVM_malloc(num_info * sizeof(MVMObject *));
        callback_data->next      = NULL;

        /* A dyncall signature looks like this: xxx)x
        * Argument types before the ) and return type after it. Thus,
        * num_info+1 must be NULL (zero-terminated string) and num_info-1
        * must be the ).
        */
        signature = MVM_malloc(num_info + 2);
        signature[num_info + 1] = '\0';
        signature[num_info - 1] = ')';

        /* We'll also build up a MoarVM callsite as we go. */
        cs                 = MVM_calloc(1, sizeof(MVMCallsite));
        cs->flag_count     = num_info - 1;
        cs->arg_flags      = MVM_malloc(cs->flag_count * sizeof(MVMCallsiteEntry));
        cs->arg_count      = num_info - 1;
        cs->num_pos        = num_info - 1;
        cs->has_flattening = 0;
        cs->is_interned    = 0;
        cs->with_invocant  = NULL;

        typehash = MVM_repr_at_pos_o(tc, sig_info, 0);
        callback_data->types[0] = MVM_repr_at_key_o(tc, typehash,
            tc->instance->str_consts.typeobj);
        callback_data->typeinfos[0] = MVM_nativecall_get_arg_type(tc, typehash, 1);
        signature[num_info] = get_signature_char(callback_data->typeinfos[0]);
        for (i = 1; i < num_info; i++) {
            typehash = MVM_repr_at_pos_o(tc, sig_info, i);
            callback_data->types[i] = MVM_repr_at_key_o(tc, typehash,
                tc->instance->str_consts.typeobj);
            callback_data->typeinfos[i] = MVM_nativecall_get_arg_type(tc, typehash, 0) & ~MVM_NATIVECALL_ARG_FREE_STR;
            signature[i - 1] = get_signature_char(callback_data->typeinfos[i]);
            switch (callback_data->typeinfos[i] & MVM_NATIVECALL_ARG_TYPE_MASK) {
                case MVM_NATIVECALL_ARG_CHAR:
                case MVM_NATIVECALL_ARG_SHORT:
                case MVM_NATIVECALL_ARG_INT:
                case MVM_NATIVECALL_ARG_LONG:
                case MVM_NATIVECALL_ARG_LONGLONG:
                    cs->arg_flags[i - 1] = MVM_CALLSITE_ARG_INT;
                    break;
                case MVM_NATIVECALL_ARG_UCHAR:
                case MVM_NATIVECALL_ARG_USHORT:
                case MVM_NATIVECALL_ARG_UINT:
                case MVM_NATIVECALL_ARG_ULONG:
                case MVM_NATIVECALL_ARG_ULONGLONG:
                    /* TODO: should probably be UINT, when we can support that. */
                    cs->arg_flags[i - 1] = MVM_CALLSITE_ARG_INT;
                    break;
                case MVM_NATIVECALL_ARG_FLOAT:
                case MVM_NATIVECALL_ARG_DOUBLE:
                    cs->arg_flags[i - 1] = MVM_CALLSITE_ARG_NUM;
                    break;
                default:
                    cs->arg_flags[i - 1] = MVM_CALLSITE_ARG_OBJ;
                    break;
            }
        }

        MVM_callsite_intern(tc, &cs, 0, 1);

        callback_data->instance  = tc->instance;
        callback_data->cs        = cs;
        callback_data->target    = callback;
        callback_data->cb        = dcbNewCallback(signature, (DCCallbackHandler *)callback_handler, callback_data);
        if (!callback_data->cb)
            MVM_panic(1, "Unable to allocate memory for callback closure");

        /* Now insert the MVMCallback into the linked list. */
        *callback_data_handle = callback_data;
        MVM_free(signature);
    }

    return (*callback_data_handle)->cb;
}

/* Called to handle a callback. */
typedef struct {
    MVMObject   *invokee;
    MVMRegister *args;
    MVMCallsite *cs;
} CallbackInvokeData;
static void callback_invoke(MVMThreadContext *tc, void *data) {
    /* Invoke the coderef, to set up the nested interpreter. */
    CallbackInvokeData *cid = (CallbackInvokeData *)data;
    STABLE(cid->invokee)->invoke(tc, cid->invokee, cid->cs, cid->args);

    /* Ensure we exit interp after callback. */
    tc->thread_entry_frame = tc->cur_frame;
}
static char callback_handler(DCCallback *cb, DCArgs *cb_args, DCValue *cb_result, MVMNativeCallback *data) {
    CallbackInvokeData cid;
    MVMint32 num_roots, i;
    MVMRegister res = {0};
    MVMRegister *args;
    unsigned int interval_id;

    /* Locate the MoarVM thread this callback is being run on. */
    MVMThreadContext *tc = MVM_nativecall_find_thread_context(data->instance);

    /* Unblock GC if needed, so this thread can do work. */
    MVMint32 was_blocked = MVM_gc_is_thread_blocked(tc);
    if (was_blocked)
        MVM_gc_mark_thread_unblocked(tc);

    interval_id = MVM_telemetry_interval_start(tc, "nativecall callback handler");

    /* Build a callsite and arguments buffer. */
    args = MVM_malloc(data->num_types * sizeof(MVMRegister));
    num_roots = 0;
    for (i = 1; i < data->num_types; i++) {
        MVMObject *type     = data->types[i];
        MVMint16   typeinfo = data->typeinfos[i];
        switch (typeinfo & MVM_NATIVECALL_ARG_TYPE_MASK) {
            case MVM_NATIVECALL_ARG_CHAR:
                args[i - 1].i64 = dcbArgChar(cb_args);
                break;
            case MVM_NATIVECALL_ARG_SHORT:
                args[i - 1].i64 = dcbArgShort(cb_args);
                break;
            case MVM_NATIVECALL_ARG_INT:
                args[i - 1].i64 = dcbArgInt(cb_args);
                break;
            case MVM_NATIVECALL_ARG_LONG:
                args[i - 1].i64 = dcbArgLong(cb_args);
                break;
            case MVM_NATIVECALL_ARG_LONGLONG:
                args[i - 1].i64 = dcbArgLongLong(cb_args);
                break;
            case MVM_NATIVECALL_ARG_FLOAT:
                args[i - 1].n64 = dcbArgFloat(cb_args);
                break;
            case MVM_NATIVECALL_ARG_DOUBLE:
                args[i - 1].n64 = dcbArgDouble(cb_args);
                break;
            case MVM_NATIVECALL_ARG_ASCIISTR:
            case MVM_NATIVECALL_ARG_UTF8STR:
            case MVM_NATIVECALL_ARG_UTF16STR:
                args[i - 1].o = MVM_nativecall_make_str(tc, type, typeinfo,
                    (char *)dcbArgPointer(cb_args));
                MVM_gc_root_temp_push(tc, (MVMCollectable **)&(args[i - 1].o));
                num_roots++;
                break;
            case MVM_NATIVECALL_ARG_CSTRUCT:
                args[i - 1].o = MVM_nativecall_make_cstruct(tc, type,
                    dcbArgPointer(cb_args));
                MVM_gc_root_temp_push(tc, (MVMCollectable **)&(args[i - 1].o));
                num_roots++;
                break;
            case MVM_NATIVECALL_ARG_CPPSTRUCT:
                args[i - 1].o = MVM_nativecall_make_cppstruct(tc, type,
                    dcbArgPointer(cb_args));
                MVM_gc_root_temp_push(tc, (MVMCollectable **)&(args[i - 1].o));
                num_roots++;
                break;
            case MVM_NATIVECALL_ARG_CPOINTER:
                args[i - 1].o = MVM_nativecall_make_cpointer(tc, type,
                    dcbArgPointer(cb_args));
                MVM_gc_root_temp_push(tc, (MVMCollectable **)&(args[i - 1].o));
                num_roots++;
                break;
            case MVM_NATIVECALL_ARG_CARRAY:
                args[i - 1].o = MVM_nativecall_make_carray(tc, type,
                    dcbArgPointer(cb_args));
                MVM_gc_root_temp_push(tc, (MVMCollectable **)&(args[i - 1].o));
                num_roots++;
                break;
            case MVM_NATIVECALL_ARG_CUNION:
                args[i - 1].o = MVM_nativecall_make_cunion(tc, type,
                    dcbArgPointer(cb_args));
                MVM_gc_root_temp_push(tc, (MVMCollectable **)&(args[i - 1].o));
                num_roots++;
                break;
            case MVM_NATIVECALL_ARG_CALLBACK:
                /* TODO: A callback -return- value means that we have a C method
                * that needs to be wrapped similarly to a is native(...) Perl 6
                * sub. */
                dcbArgPointer(cb_args);
                args[i - 1].o = type;
                MVM_gc_root_temp_push(tc, (MVMCollectable **)&(args[i - 1].o));
                num_roots++;
                break;
            case MVM_NATIVECALL_ARG_UCHAR:
                args[i - 1].i64 = dcbArgUChar(cb_args);
                break;
            case MVM_NATIVECALL_ARG_USHORT:
                args[i - 1].i64 = dcbArgUShort(cb_args);
                break;
            case MVM_NATIVECALL_ARG_UINT:
                args[i - 1].i64 = dcbArgUInt(cb_args);
                break;
            case MVM_NATIVECALL_ARG_ULONG:
                args[i - 1].i64 = dcbArgULong(cb_args);
                break;
            case MVM_NATIVECALL_ARG_ULONGLONG:
                args[i - 1].i64 = dcbArgULongLong(cb_args);
                break;
            default:
                MVM_free(args);
                MVM_telemetry_interval_stop(tc, interval_id, "nativecall callback handler failed");
                MVM_exception_throw_adhoc(tc,
                    "Internal error: unhandled dyncall callback argument type");
        }
    }

    /* Call into a nested interpreter (since we already are in one). Need to
     * save a bunch of state around each side of this. */
    cid.invokee = data->target;
    cid.args    = args;
    cid.cs      = data->cs;
    MVM_interp_run_nested(tc, callback_invoke, &cid, &res);

    /* Handle return value. */
    if (res.o) {
        MVMContainerSpec const *contspec = STABLE(res.o)->container_spec;
        if (contspec && contspec->fetch_never_invokes)
            contspec->fetch(tc, res.o, &res);
    }
    switch (data->typeinfos[0] & MVM_NATIVECALL_ARG_TYPE_MASK) {
        case MVM_NATIVECALL_ARG_VOID:
            break;
        case MVM_NATIVECALL_ARG_CHAR:
            cb_result->c = (signed char)MVM_nativecall_unmarshal_char(tc, res.o);
            break;
        case MVM_NATIVECALL_ARG_SHORT:
            cb_result->s = MVM_nativecall_unmarshal_short(tc, res.o);
            break;
        case MVM_NATIVECALL_ARG_INT:
            cb_result->i = MVM_nativecall_unmarshal_int(tc, res.o);
            break;
        case MVM_NATIVECALL_ARG_LONG:
            cb_result->j = MVM_nativecall_unmarshal_long(tc, res.o);
            break;
        case MVM_NATIVECALL_ARG_LONGLONG:
            cb_result->l = MVM_nativecall_unmarshal_longlong(tc, res.o);
            break;
        case MVM_NATIVECALL_ARG_FLOAT:
            cb_result->f = MVM_nativecall_unmarshal_float(tc, res.o);
            break;
        case MVM_NATIVECALL_ARG_DOUBLE:
            cb_result->d = MVM_nativecall_unmarshal_double(tc, res.o);
            break;
        case MVM_NATIVECALL_ARG_ASCIISTR:
        case MVM_NATIVECALL_ARG_UTF8STR:
        case MVM_NATIVECALL_ARG_UTF16STR:
            cb_result->Z = MVM_nativecall_unmarshal_string(tc, res.o, data->typeinfos[0], NULL, MVM_NATIVECALL_UNMARSHAL_KIND_GENERIC);
            break;
        case MVM_NATIVECALL_ARG_CSTRUCT:
            cb_result->p = MVM_nativecall_unmarshal_cstruct(tc, res.o, MVM_NATIVECALL_UNMARSHAL_KIND_GENERIC);
            break;
        case MVM_NATIVECALL_ARG_CPPSTRUCT:
            cb_result->p = MVM_nativecall_unmarshal_cppstruct(tc, res.o, MVM_NATIVECALL_UNMARSHAL_KIND_GENERIC);
            break;
        case MVM_NATIVECALL_ARG_CPOINTER:
            cb_result->p = MVM_nativecall_unmarshal_cpointer(tc, res.o, MVM_NATIVECALL_UNMARSHAL_KIND_GENERIC);
            break;
        case MVM_NATIVECALL_ARG_CARRAY:
            cb_result->p = MVM_nativecall_unmarshal_carray(tc, res.o, MVM_NATIVECALL_UNMARSHAL_KIND_GENERIC);
            break;
        case MVM_NATIVECALL_ARG_CUNION:
            cb_result->p = MVM_nativecall_unmarshal_cunion(tc, res.o, MVM_NATIVECALL_UNMARSHAL_KIND_GENERIC);
            break;
        case MVM_NATIVECALL_ARG_VMARRAY:
            cb_result->p = MVM_nativecall_unmarshal_vmarray(tc, res.o, MVM_NATIVECALL_UNMARSHAL_KIND_GENERIC);
            break;
        case MVM_NATIVECALL_ARG_CALLBACK:
            cb_result->p = unmarshal_callback(tc, res.o, data->types[0]);
            break;
        case MVM_NATIVECALL_ARG_UCHAR:
            cb_result->c = MVM_nativecall_unmarshal_uchar(tc, res.o);
            break;
        case MVM_NATIVECALL_ARG_USHORT:
            cb_result->s = MVM_nativecall_unmarshal_ushort(tc, res.o);
            break;
        case MVM_NATIVECALL_ARG_UINT:
            cb_result->i = MVM_nativecall_unmarshal_uint(tc, res.o);
            break;
        case MVM_NATIVECALL_ARG_ULONG:
            cb_result->j = MVM_nativecall_unmarshal_ulong(tc, res.o);
            break;
        case MVM_NATIVECALL_ARG_ULONGLONG:
            cb_result->l = MVM_nativecall_unmarshal_ulonglong(tc, res.o);
            break;
        default:
            MVM_free(args);
            MVM_telemetry_interval_stop(tc, interval_id, "nativecall callback handler failed");
            MVM_exception_throw_adhoc(tc,
                "Internal error: unhandled dyncall callback return type");
    }

    /* Clean up. */
    MVM_gc_root_temp_pop_n(tc, num_roots);
    MVM_free(args);

    /* Re-block GC if needed, so other threads will be able to collect. */
    if (was_blocked)
        MVM_gc_mark_thread_blocked(tc);

    MVM_telemetry_interval_stop(tc, interval_id, "nativecall callback handler");

    /* Indicate what we're producing as a result. */
    return get_signature_char(data->typeinfos[0]);
}

#define handle_arg(what, cont_X, dc_type, reg_slot, dc_fun, unmarshal_fun) do { \
    MVMRegister r; \
    if ((arg_types[i] & MVM_NATIVECALL_ARG_RW_MASK) == MVM_NATIVECALL_ARG_RW) { \
        if (MVM_6model_container_is ## cont_X(tc, value)) { \
            dc_type *rw = (dc_type *)MVM_malloc(sizeof(dc_type)); \
            MVM_6model_container_de ## cont_X(tc, value, &r); \
            *rw = (dc_type)r. reg_slot ; \
            if (!free_rws) \
                free_rws = (void **)MVM_malloc(num_args * sizeof(void *)); \
            free_rws[num_rws] = rw; \
            num_rws++; \
            dcArgPointer(vm, rw); \
        } \
        else \
            MVM_exception_throw_adhoc(tc, \
                "Native call expected argument %d to reference a native %s, but got %s", \
                i, what, REPR(value)->name); \
    } \
    else { \
        if (value && IS_CONCRETE(value) && STABLE(value)->container_spec) { \
            STABLE(value)->container_spec->fetch(tc, value, &r); \
            dc_fun(vm, unmarshal_fun(tc, r.o)); \
        } \
        else { \
            dc_fun(vm, unmarshal_fun(tc, value)); \
        } \
    } \
} while (0)

MVMObject * MVM_nativecall_invoke(MVMThreadContext *tc, MVMObject *res_type,
        MVMObject *site, MVMObject *args) {
    MVMObject  *result = NULL;
    char      **free_strs = NULL;
    void      **free_rws  = NULL;
    MVMint16    num_strs  = 0;
    MVMint16    num_rws   = 0;
    MVMint16    i;

    /* Get native call body, so we can locate the call info. Read out all we
     * shall need, since later we may allocate a result and and move it. */
    MVMNativeCallBody *body = MVM_nativecall_get_nc_body(tc, site);
    if (MVM_UNLIKELY(!body->lib_handle)) {
        MVMROOT3(tc, site, args, res_type, {
            MVM_nativecall_restore_library(tc, body, site);
        });
        body = MVM_nativecall_get_nc_body(tc, site);
    }
    MVMint16  num_args    = body->num_args;
    MVMint16 *arg_types   = body->arg_types;
    MVMint16  ret_type    = body->ret_type;
    void     *entry_point = body->entry_point;
    void     *ptr         = NULL;

    unsigned int interval_id;
    DCCallVM *vm;

    /* Create and set up call VM. */
    vm = dcNewCallVM(8192);
    dcMode(vm, body->convention);
    dcReset(vm);

    interval_id = MVM_telemetry_interval_start(tc, "nativecall invoke");
    MVM_telemetry_interval_annotate((intptr_t)entry_point, interval_id, "nc entrypoint");

    /* Process arguments. */
    for (i = 0; i < num_args; i++) {
        MVMObject *value = MVM_repr_at_pos_o(tc, args, i);
        switch (arg_types[i] & MVM_NATIVECALL_ARG_TYPE_MASK) {
            case MVM_NATIVECALL_ARG_CHAR:
                handle_arg("integer", cont_i, DCchar, i64, dcArgChar, MVM_nativecall_unmarshal_char);
                break;
            case MVM_NATIVECALL_ARG_SHORT:
                handle_arg("integer", cont_i, DCshort, i64, dcArgShort, MVM_nativecall_unmarshal_short);
                break;
            case MVM_NATIVECALL_ARG_INT:
                handle_arg("integer", cont_i, DCint, i64, dcArgInt, MVM_nativecall_unmarshal_int);
                break;
            case MVM_NATIVECALL_ARG_LONG:
                handle_arg("integer", cont_i, DClong, i64, dcArgLong, MVM_nativecall_unmarshal_long);
                break;
            case MVM_NATIVECALL_ARG_LONGLONG:
                handle_arg("integer", cont_i, DClonglong, i64, dcArgLongLong, MVM_nativecall_unmarshal_longlong);
                break;
            case MVM_NATIVECALL_ARG_FLOAT:
                handle_arg("number", cont_n, DCfloat, n64, dcArgFloat, MVM_nativecall_unmarshal_float);
                break;
            case MVM_NATIVECALL_ARG_DOUBLE:
                handle_arg("number", cont_n, DCdouble, n64, dcArgDouble, MVM_nativecall_unmarshal_double);
                break;
            case MVM_NATIVECALL_ARG_ASCIISTR:
            case MVM_NATIVECALL_ARG_UTF8STR:
            case MVM_NATIVECALL_ARG_UTF16STR:
                {
                    MVMint16 free = 0;
                    char *str = MVM_nativecall_unmarshal_string(tc, value, arg_types[i], &free, i);
                    if (free) {
                        if (!free_strs)
                            free_strs = (char**)MVM_malloc(num_args * sizeof(char *));
                        free_strs[num_strs] = str;
                        num_strs++;
                    }
                    dcArgPointer(vm, str);
                }
                break;
            case MVM_NATIVECALL_ARG_CSTRUCT:
                dcArgPointer(vm, MVM_nativecall_unmarshal_cstruct(tc, value, i));
                break;
            case MVM_NATIVECALL_ARG_CPPSTRUCT: {
                    /* We need to allocate the struct (THIS) for C++ constructor before passing it along. */
                    if (i == 0 && !IS_CONCRETE(value)) {
                        MVMCPPStructREPRData *repr_data = (MVMCPPStructREPRData *)STABLE(res_type)->REPR_data;
                        /* Allocate a full byte aligned area where the C++ structure fits into. */
                        ptr    = MVM_malloc(repr_data->struct_size > 0 ? repr_data->struct_size : 1);
                        result = MVM_nativecall_make_cppstruct(tc, res_type, ptr);

                        dcArgPointer(vm, ptr);
                    }
                    else {
                        dcArgPointer(vm, MVM_nativecall_unmarshal_cppstruct(tc, value, i));
                    }
                }
                break;
            case MVM_NATIVECALL_ARG_CPOINTER:
                if ((arg_types[i] & MVM_NATIVECALL_ARG_RW_MASK) == MVM_NATIVECALL_ARG_RW) {
                    DCpointer *rw = (DCpointer *)MVM_malloc(sizeof(DCpointer *));
                    *rw           = (DCpointer)MVM_nativecall_unmarshal_cpointer(tc, value, i);
                    if (!free_rws)
                        free_rws = (void **)MVM_malloc(num_args * sizeof(void *));
                    free_rws[num_rws] = rw;
                    num_rws++;
                    dcArgPointer(vm, rw);
                }
                else {
                    dcArgPointer(vm, MVM_nativecall_unmarshal_cpointer(tc, value, i));
                }
                break;
            case MVM_NATIVECALL_ARG_CARRAY:
                dcArgPointer(vm, MVM_nativecall_unmarshal_carray(tc, value, i));
                break;
            case MVM_NATIVECALL_ARG_CUNION:
                dcArgPointer(vm, MVM_nativecall_unmarshal_cunion(tc, value, i));
                break;
            case MVM_NATIVECALL_ARG_VMARRAY:
                dcArgPointer(vm, MVM_nativecall_unmarshal_vmarray(tc, value, i));
                break;
            case MVM_NATIVECALL_ARG_CALLBACK:
                dcArgPointer(vm, unmarshal_callback(tc, value, body->arg_info[i]));
                break;
            case MVM_NATIVECALL_ARG_UCHAR:
                handle_arg("integer", cont_i, DCuchar, i64, dcArgChar, MVM_nativecall_unmarshal_uchar);
                break;
            case MVM_NATIVECALL_ARG_USHORT:
                handle_arg("integer", cont_i, DCushort, i64, dcArgShort, MVM_nativecall_unmarshal_ushort);
                break;
            case MVM_NATIVECALL_ARG_UINT:
                handle_arg("integer", cont_i, DCuint, i64, dcArgInt, MVM_nativecall_unmarshal_uint);
                break;
            case MVM_NATIVECALL_ARG_ULONG:
                handle_arg("integer", cont_i, DCulong, i64, dcArgLong, MVM_nativecall_unmarshal_ulong);
                break;
            case MVM_NATIVECALL_ARG_ULONGLONG:
                handle_arg("integer", cont_i, DCulonglong, i64, dcArgLongLong, MVM_nativecall_unmarshal_ulonglong);
                break;
            default:
                MVM_telemetry_interval_stop(tc, interval_id, "nativecall invoke failed");
                MVM_exception_throw_adhoc(tc, "Internal error: unhandled dyncall argument type");
        }
    }

    MVMROOT3(tc, args, res_type, result, {
        MVM_gc_mark_thread_blocked(tc);
        if (result) {
            /* We are calling a C++ constructor so we hand back the invocant (THIS) we recorded earlier. */
            dcCallVoid(vm, body->entry_point);
            MVM_gc_mark_thread_unblocked(tc);
        }
        else {
            /* Call and process return values. */
            switch (ret_type & MVM_NATIVECALL_ARG_TYPE_MASK) {
                case MVM_NATIVECALL_ARG_VOID:
                    dcCallVoid(vm, entry_point);
                    MVM_gc_mark_thread_unblocked(tc);
                    result = res_type;
                    break;
                case MVM_NATIVECALL_ARG_CHAR: {
                    MVMint64 native_result = (signed char)dcCallChar(vm, entry_point);
                    MVM_gc_mark_thread_unblocked(tc);
                    result = MVM_nativecall_make_int(tc, res_type, native_result);
                    break;
                }
                case MVM_NATIVECALL_ARG_SHORT: {
                    MVMint64 native_result = dcCallShort(vm, entry_point);
                    MVM_gc_mark_thread_unblocked(tc);
                    result = MVM_nativecall_make_int(tc, res_type, native_result);
                    break;
                }
                case MVM_NATIVECALL_ARG_INT: {
                    MVMint64 native_result = dcCallInt(vm, entry_point);
                    MVM_gc_mark_thread_unblocked(tc);
                    result = MVM_nativecall_make_int(tc, res_type, native_result);
                    break;
                }
                case MVM_NATIVECALL_ARG_LONG: {
                    MVMint64 native_result = dcCallLong(vm, entry_point);
                    MVM_gc_mark_thread_unblocked(tc);
                    result = MVM_nativecall_make_int(tc, res_type, native_result);
                    break;
                }
                case MVM_NATIVECALL_ARG_LONGLONG: {
                    MVMint64 native_result = dcCallLongLong(vm, entry_point);
                    MVM_gc_mark_thread_unblocked(tc);
                    result = MVM_nativecall_make_int(tc, res_type, native_result);
                    break;
                }
                case MVM_NATIVECALL_ARG_FLOAT: {
                    MVMnum64 native_result = dcCallFloat(vm, entry_point);
                    MVM_gc_mark_thread_unblocked(tc);
                    result = MVM_nativecall_make_num(tc, res_type, native_result);
                    break;
                }
                case MVM_NATIVECALL_ARG_DOUBLE: {
                    MVMnum64 native_result = dcCallDouble(vm, entry_point);
                    MVM_gc_mark_thread_unblocked(tc);
                    result = MVM_nativecall_make_num(tc, res_type, native_result);
                    break;
                }
                case MVM_NATIVECALL_ARG_ASCIISTR:
                case MVM_NATIVECALL_ARG_UTF8STR:
                case MVM_NATIVECALL_ARG_UTF16STR: {
                    char *native_result = (char *)dcCallPointer(vm, entry_point);
                    MVM_gc_mark_thread_unblocked(tc);
                    result = MVM_nativecall_make_str(tc, res_type, body->ret_type,
                        native_result);
                    break;
                }
                case MVM_NATIVECALL_ARG_CSTRUCT: {
                    void *native_result = dcCallPointer(vm, body->entry_point);
                    MVM_gc_mark_thread_unblocked(tc);
                    result = MVM_nativecall_make_cstruct(tc, res_type, native_result);
                    break;
                }
                case MVM_NATIVECALL_ARG_CPPSTRUCT: {
                    void *native_result = dcCallPointer(vm, body->entry_point);
                    MVM_gc_mark_thread_unblocked(tc);
                    result = MVM_nativecall_make_cppstruct(tc, res_type, native_result);
                    break;
                }
                case MVM_NATIVECALL_ARG_CPOINTER: {
                    void *native_result = dcCallPointer(vm, body->entry_point);
                    MVM_gc_mark_thread_unblocked(tc);
                    result = MVM_nativecall_make_cpointer(tc, res_type, native_result);
                    break;
                }
                case MVM_NATIVECALL_ARG_CARRAY: {
                    void *native_result = dcCallPointer(vm, body->entry_point);
                    MVM_gc_mark_thread_unblocked(tc);
                    result = MVM_nativecall_make_carray(tc, res_type, native_result);
                    break;
                }
                case MVM_NATIVECALL_ARG_CUNION: {
                    void *native_result = dcCallPointer(vm, body->entry_point);
                    MVM_gc_mark_thread_unblocked(tc);
                    result = MVM_nativecall_make_cunion(tc, res_type, native_result);
                    break;
                }
                case MVM_NATIVECALL_ARG_CALLBACK:
                    /* TODO: A callback -return- value means that we have a C method
                    * that needs to be wrapped similarly to a is native(...) Perl 6
                    * sub. */
                    dcCallPointer(vm, body->entry_point);
                    MVM_gc_mark_thread_unblocked(tc);
                    result = res_type;
                    break;
                case MVM_NATIVECALL_ARG_UCHAR: {
                    MVMuint64 native_result = (DCuchar)dcCallChar(vm, entry_point);
                    MVM_gc_mark_thread_unblocked(tc);
                    result = MVM_nativecall_make_uint(tc, res_type, native_result);
                    break;
                }
                case MVM_NATIVECALL_ARG_USHORT: {
                    MVMuint64 native_result = (DCushort)dcCallShort(vm, entry_point);
                    MVM_gc_mark_thread_unblocked(tc);
                    result = MVM_nativecall_make_uint(tc, res_type, native_result);
                    break;
                }
                case MVM_NATIVECALL_ARG_UINT: {
                    MVMuint64 native_result = (DCuint)dcCallInt(vm, entry_point);
                    MVM_gc_mark_thread_unblocked(tc);
                    result = MVM_nativecall_make_uint(tc, res_type, native_result);
                    break;
                }
                case MVM_NATIVECALL_ARG_ULONG: {
                    MVMuint64 native_result = (DCulong)dcCallLong(vm, entry_point);
                    MVM_gc_mark_thread_unblocked(tc);
                    result = MVM_nativecall_make_uint(tc, res_type, native_result);
                    break;
                }
                case MVM_NATIVECALL_ARG_ULONGLONG: {
                    MVMuint64 native_result = (DCulonglong)dcCallLongLong(vm, entry_point);
                    MVM_gc_mark_thread_unblocked(tc);
                    result = MVM_nativecall_make_uint(tc, res_type, native_result);
                    break;
                }
                default:
                    MVM_gc_mark_thread_unblocked(tc);
                    MVM_telemetry_interval_stop(tc, interval_id, "nativecall invoke failed");
                    MVM_exception_throw_adhoc(tc, "Internal error: unhandled dyncall return type");
            }
        }
    });

    num_rws = 0;
    for (i = 0; i < num_args; i++) {
        MVMObject *value = MVM_repr_at_pos_o(tc, args, i);
        if ((arg_types[i] & MVM_NATIVECALL_ARG_RW_MASK) == MVM_NATIVECALL_ARG_RW) {
            switch (arg_types[i] & MVM_NATIVECALL_ARG_TYPE_MASK) {
                case MVM_NATIVECALL_ARG_CHAR:
                    MVM_6model_container_assign_i(tc, value, (MVMint64)*(DCchar *)free_rws[num_rws]);
                    break;
                case MVM_NATIVECALL_ARG_SHORT:
                    MVM_6model_container_assign_i(tc, value, (MVMint64)*(DCshort *)free_rws[num_rws]);
                    break;
                case MVM_NATIVECALL_ARG_INT:
                    MVM_6model_container_assign_i(tc, value, (MVMint64)*(DCint *)free_rws[num_rws]);
                    break;
                case MVM_NATIVECALL_ARG_LONG:
                    MVM_6model_container_assign_i(tc, value, (MVMint64)*(DClong *)free_rws[num_rws]);
                    break;
                case MVM_NATIVECALL_ARG_LONGLONG:
                    MVM_6model_container_assign_i(tc, value, (MVMint64)*(DClonglong *)free_rws[num_rws]);
                    break;
                case MVM_NATIVECALL_ARG_FLOAT:
                    MVM_6model_container_assign_n(tc, value, (MVMnum64)*(DCfloat *)free_rws[num_rws]);
                    break;
                case MVM_NATIVECALL_ARG_DOUBLE:
                    MVM_6model_container_assign_n(tc, value, (MVMnum64)*(DCdouble *)free_rws[num_rws]);
                    break;
                case MVM_NATIVECALL_ARG_UCHAR:
                    MVM_6model_container_assign_i(tc, value, (MVMint64)*(DCuchar *)free_rws[num_rws]);
                    break;
                case MVM_NATIVECALL_ARG_USHORT:
                    MVM_6model_container_assign_i(tc, value, (MVMint64)*(DCushort *)free_rws[num_rws]);
                    break;
                case MVM_NATIVECALL_ARG_UINT:
                    MVM_6model_container_assign_i(tc, value, (MVMint64)*(DCuint *)free_rws[num_rws]);
                    break;
                case MVM_NATIVECALL_ARG_ULONG:
                    MVM_6model_container_assign_i(tc, value, (MVMint64)*(DCulong *)free_rws[num_rws]);
                    break;
                case MVM_NATIVECALL_ARG_ULONGLONG:
                    MVM_6model_container_assign_i(tc, value, (MVMint64)*(DCulonglong *)free_rws[num_rws]);
                    break;
                case MVM_NATIVECALL_ARG_CPOINTER:
                    REPR(value)->box_funcs.set_int(tc, STABLE(value), value, OBJECT_BODY(value),
                        (MVMint64)*(DCpointer *)free_rws[num_rws]);
                    break;
                default:
                    MVM_telemetry_interval_stop(tc, interval_id, "nativecall invoke failed");
                    MVM_exception_throw_adhoc(tc, "Internal error: unhandled dyncall argument type");
            }
            num_rws++;
        }
        /* Perform CArray/CStruct write barriers. */
        MVM_nativecall_refresh(tc, value);
    }

    /* Free any memory that we need to. */
    if (free_strs) {
        for (i = 0; i < num_strs; i++)
            MVM_free(free_strs[i]);
        MVM_free(free_strs);
    }

    if (free_rws) {
        for (i = 0; i < num_rws; i++)
            MVM_free(free_rws[i]);
        MVM_free(free_rws);
    }

    /* Finally, free call VM. */
    dcFree(vm);

    MVM_telemetry_interval_stop(tc, interval_id, "nativecall invoke");
    return result;
}
