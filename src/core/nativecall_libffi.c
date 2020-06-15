#include "moar.h"

//~ ffi_type * MVM_nativecall_get_ffi_type(MVMThreadContext *tc, MVMuint64 type_id, void **values, MVMuint64 offset) {
ffi_type * MVM_nativecall_get_ffi_type(MVMThreadContext *tc, MVMuint64 type_id) {
    if ((type_id & MVM_NATIVECALL_ARG_RW_MASK) == MVM_NATIVECALL_ARG_RW)
        return &ffi_type_pointer;

    switch (type_id & MVM_NATIVECALL_ARG_TYPE_MASK) {
        case MVM_NATIVECALL_ARG_CHAR:
            return &ffi_type_schar;
        case MVM_NATIVECALL_ARG_SHORT:
            return &ffi_type_sshort;
        case MVM_NATIVECALL_ARG_INT:
            return &ffi_type_sint;
        case MVM_NATIVECALL_ARG_LONG:
            return &ffi_type_slong;
        case MVM_NATIVECALL_ARG_LONGLONG:
            return &ffi_type_sint64; /* XXX ffi_type_slonglong not defined */
        case MVM_NATIVECALL_ARG_FLOAT:
            return &ffi_type_float;
        case MVM_NATIVECALL_ARG_DOUBLE:
            return &ffi_type_double;
        case MVM_NATIVECALL_ARG_ASCIISTR:
        case MVM_NATIVECALL_ARG_UTF8STR:
        case MVM_NATIVECALL_ARG_UTF16STR:
        case MVM_NATIVECALL_ARG_CPPSTRUCT:
        case MVM_NATIVECALL_ARG_CSTRUCT:
        case MVM_NATIVECALL_ARG_CPOINTER:
        case MVM_NATIVECALL_ARG_CARRAY:
        case MVM_NATIVECALL_ARG_CUNION:
        case MVM_NATIVECALL_ARG_VMARRAY:
        case MVM_NATIVECALL_ARG_CALLBACK:
            return &ffi_type_pointer;
        case MVM_NATIVECALL_ARG_UCHAR:
            return &ffi_type_uchar;
        case MVM_NATIVECALL_ARG_USHORT:
            return &ffi_type_ushort;
        case MVM_NATIVECALL_ARG_UINT:
            return &ffi_type_uint;
        case MVM_NATIVECALL_ARG_ULONG:
            return &ffi_type_ulong;
        case MVM_NATIVECALL_ARG_ULONGLONG:
            return &ffi_type_uint64; /* XXX ffi_type_ulonglong not defined */
        default:
            return &ffi_type_void;
    }
}


/* Maps a calling convention name to an ID. */
ffi_abi MVM_nativecall_get_calling_convention(MVMThreadContext *tc, MVMString *name) {
    ffi_abi result = FFI_DEFAULT_ABI;
    //~ if (name && MVM_string_graphs(tc, name) > 0) {
        //~ char *cname = MVM_string_utf8_encode_C_string(tc, name);
        //~ if (strcmp(cname, "cdecl") == 0)
            //~ result = DC_CALL_C_X86_CDECL;
        //~ else if (strcmp(cname, "stdcall") == 0)
            //~ result = DC_CALL_C_X86_WIN32_STD;
        //~ else if (strcmp(cname, "stdcall") == 0)
            //~ result = DC_CALL_C_X64_WIN64;
        //~ else {
            //~ char *waste[] = { cname, NULL };
            //~ MVM_exception_throw_adhoc_free(tc, waste,
                //~ "Unknown calling convention '%s' used for native call", cname);
        //~ }
        //~ MVM_free(cname);
    //~ }
    return result;
}

/* Sets up a callback, caching the information to avoid duplicate work. */
//~ static char callback_handler(DCCallback *cb, DCArgs *args, DCValue *result, MVMNativeCallback *data);
static void callback_handler(ffi_cif *cif, void *cb_result, void **cb_args, void *data);
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
        MVMObject   *typehash;
        MVMint64     num_info, i;
        MVMNativeCallback *callback_data;
        /* cb is a piece of executable memory we obtain from libffi. */
        void *cb;
        ffi_cif *cif;
        ffi_closure *closure;
        ffi_status status;

        num_info = MVM_repr_elems(tc, sig_info);

        /* We'll also build up a MoarVM callsite as we go. */
        cs                 = MVM_calloc(1, sizeof(MVMCallsite));
        cs->flag_count     = num_info - 1;
        cs->arg_flags      = MVM_malloc(cs->flag_count * sizeof(MVMCallsiteEntry));
        cs->arg_count      = num_info - 1;
        cs->num_pos        = num_info - 1;
        cs->has_flattening = 0;
        cs->is_interned    = 0;
        cs->with_invocant  = NULL;

        callback_data                = MVM_malloc(sizeof(MVMNativeCallback));
        callback_data->num_types     = num_info;
        callback_data->typeinfos     = MVM_malloc(num_info * sizeof(MVMint16));
        callback_data->types         = MVM_malloc(num_info * sizeof(MVMObject *));
        callback_data->next          = NULL;
        cif                          = (ffi_cif *)MVM_malloc(sizeof(ffi_cif));
        callback_data->convention    = FFI_DEFAULT_ABI;
        callback_data->ffi_arg_types = MVM_malloc(sizeof(ffi_type *) * (cs->arg_count ? cs->arg_count : 1));

        /* Collect information about the return type. */
        typehash                    = MVM_repr_at_pos_o(tc, sig_info, 0);
        callback_data->types[0]     = MVM_repr_at_key_o(tc, typehash, tc->instance->str_consts.typeobj);
        callback_data->typeinfos[0] = MVM_nativecall_get_arg_type(tc, typehash, 1);
        callback_data->ffi_ret_type = MVM_nativecall_get_ffi_type(tc, callback_data->typeinfos[0]);

        for (i = 1; i < num_info; i++) {
            typehash = MVM_repr_at_pos_o(tc, sig_info, i);
            callback_data->types[i]             = MVM_repr_at_key_o(tc, typehash, tc->instance->str_consts.typeobj);
            callback_data->typeinfos[i]         = MVM_nativecall_get_arg_type(tc, typehash, 0) & ~MVM_NATIVECALL_ARG_FREE_STR;
            callback_data->ffi_arg_types[i - 1] = MVM_nativecall_get_ffi_type(tc, callback_data->typeinfos[i]);
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
        status                   = ffi_prep_cif(cif, callback_data->convention, (unsigned int)cs->arg_count,
            callback_data->ffi_ret_type, callback_data->ffi_arg_types);

        closure                  = ffi_closure_alloc(sizeof(ffi_closure), &cb);
        if (!closure)
            MVM_panic(1, "Unable to allocate memory for callback closure");
        ffi_prep_closure_loc(closure, cif, callback_handler, callback_data, cb);
        callback_data->cb        = cb;

        /* Now insert the MVMCallback into the linked list. */
        *callback_data_handle    = callback_data;
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
static void callback_handler(ffi_cif *cif, void *cb_result, void **cb_args, void *cb_data) {
    CallbackInvokeData cid;
    MVMint32 num_roots, i;
    MVMRegister res;
    MVMRegister *args;
    MVMNativeCallback *data = (MVMNativeCallback *)cb_data;
    void           **values = MVM_malloc(sizeof(void *) * (data->cs->arg_count ? data->cs->arg_count : 1));
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
                args[i - 1].i64 = *(signed char *)cb_args[i - 1];
                break;
            case MVM_NATIVECALL_ARG_SHORT:
                args[i - 1].i64 = *(signed short *)cb_args[i - 1];
                break;
            case MVM_NATIVECALL_ARG_INT:
                args[i - 1].i64 = *(signed int *)cb_args[i - 1];
                break;
            case MVM_NATIVECALL_ARG_LONG:
                args[i - 1].i64 = *(signed long *)cb_args[i - 1];
                break;
            case MVM_NATIVECALL_ARG_LONGLONG:
                args[i - 1].i64 = *(signed long long *)cb_args[i - 1];
                break;
            case MVM_NATIVECALL_ARG_FLOAT:
                args[i - 1].n64 = *(float *)cb_args[i - 1];
                break;
            case MVM_NATIVECALL_ARG_DOUBLE:
                args[i - 1].n64 = *(double *)cb_args[i - 1];
                break;
            case MVM_NATIVECALL_ARG_ASCIISTR:
            case MVM_NATIVECALL_ARG_UTF8STR:
            case MVM_NATIVECALL_ARG_UTF16STR:
                args[i - 1].o = MVM_nativecall_make_str(tc, type, typeinfo, *(char **)cb_args[i - 1]);
                MVM_gc_root_temp_push(tc, (MVMCollectable **)&(args[i - 1].o));
                num_roots++;
                break;
            case MVM_NATIVECALL_ARG_CSTRUCT:
                args[i - 1].o = MVM_nativecall_make_cstruct(tc, type, *(void **)cb_args[i - 1]);
                MVM_gc_root_temp_push(tc, (MVMCollectable **)&(args[i - 1].o));
                num_roots++;
                break;
            case MVM_NATIVECALL_ARG_CPPSTRUCT:
                args[i - 1].o = MVM_nativecall_make_cppstruct(tc, type, *(void **)cb_args[i - 1]);
                MVM_gc_root_temp_push(tc, (MVMCollectable **)&(args[i - 1].o));
                num_roots++;
                break;
            case MVM_NATIVECALL_ARG_CPOINTER:
                args[i - 1].o = MVM_nativecall_make_cpointer(tc, type, *(void **)cb_args[i - 1]);
                MVM_gc_root_temp_push(tc, (MVMCollectable **)&(args[i - 1].o));
                num_roots++;
                break;
            case MVM_NATIVECALL_ARG_CARRAY:
                args[i - 1].o = MVM_nativecall_make_carray(tc, type, *(void **)cb_args[i - 1]);
                MVM_gc_root_temp_push(tc, (MVMCollectable **)&(args[i - 1].o));
                num_roots++;
                break;
            case MVM_NATIVECALL_ARG_CUNION:
                args[i - 1].o = MVM_nativecall_make_cunion(tc, type, *(void **)cb_args[i - 1]);
                MVM_gc_root_temp_push(tc, (MVMCollectable **)&(args[i - 1].o));
                num_roots++;
                break;
            case MVM_NATIVECALL_ARG_CALLBACK:
                /* TODO: A callback -return- value means that we have a C method
                * that needs to be wrapped similarly to a is native(...) Perl 6
                * sub. */
                /* XXX do something with the function pointer: *(void **)cb_args[i - 1] */
                args[i - 1].o = type;
                MVM_gc_root_temp_push(tc, (MVMCollectable **)&(args[i - 1].o));
                num_roots++;
                break;
            case MVM_NATIVECALL_ARG_UCHAR:
                args[i - 1].i64 = *(unsigned char *)cb_args[i - 1];
                break;
            case MVM_NATIVECALL_ARG_USHORT:
                args[i - 1].i64 = *(unsigned short *)cb_args[i - 1];
                break;
            case MVM_NATIVECALL_ARG_UINT:
                args[i - 1].i64 = *(unsigned int *)cb_args[i - 1];
                break;
            case MVM_NATIVECALL_ARG_ULONG:
                args[i - 1].i64 = *(unsigned long *)cb_args[i - 1];
                break;
            case MVM_NATIVECALL_ARG_ULONGLONG:
                args[i - 1].i64 = *(unsigned long long *)cb_args[i - 1];
                break;
            default:
                MVM_telemetry_interval_stop(tc, interval_id, "nativecall callback handler failed");
                MVM_exception_throw_adhoc(tc,
                    "Internal error: unhandled libffi callback argument type");
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
            *(ffi_sarg *)cb_result = MVM_nativecall_unmarshal_char(tc, res.o);
            break;
        case MVM_NATIVECALL_ARG_SHORT:
            *(ffi_sarg *)cb_result = MVM_nativecall_unmarshal_short(tc, res.o);
            break;
        case MVM_NATIVECALL_ARG_INT:
            *(ffi_sarg *)cb_result = MVM_nativecall_unmarshal_int(tc, res.o);
            break;
        case MVM_NATIVECALL_ARG_LONG:
            *(ffi_sarg *)cb_result = MVM_nativecall_unmarshal_long(tc, res.o);
            break;
        case MVM_NATIVECALL_ARG_LONGLONG:
            *(signed long long *)cb_result = MVM_nativecall_unmarshal_longlong(tc, res.o);
            break;
        case MVM_NATIVECALL_ARG_FLOAT:
            *(float *)cb_result = MVM_nativecall_unmarshal_float(tc, res.o);
            break;
        case MVM_NATIVECALL_ARG_DOUBLE:
            *(double *)cb_result = MVM_nativecall_unmarshal_double(tc, res.o);
            break;
        case MVM_NATIVECALL_ARG_ASCIISTR:
        case MVM_NATIVECALL_ARG_UTF8STR:
        case MVM_NATIVECALL_ARG_UTF16STR:
            *(void **)cb_result = MVM_nativecall_unmarshal_string(tc, res.o, data->typeinfos[0], NULL, MVM_NATIVECALL_UNMARSHAL_KIND_RETURN);
            break;
        case MVM_NATIVECALL_ARG_CSTRUCT:
            *(void **)cb_result = MVM_nativecall_unmarshal_cstruct(tc, res.o, MVM_NATIVECALL_UNMARSHAL_KIND_RETURN);
            break;
        case MVM_NATIVECALL_ARG_CPPSTRUCT:
            *(void **)cb_result = MVM_nativecall_unmarshal_cppstruct(tc, res.o, MVM_NATIVECALL_UNMARSHAL_KIND_RETURN);
            break;
        case MVM_NATIVECALL_ARG_CPOINTER:
            *(void **)cb_result = MVM_nativecall_unmarshal_cpointer(tc, res.o, MVM_NATIVECALL_UNMARSHAL_KIND_RETURN);
            break;
        case MVM_NATIVECALL_ARG_CARRAY:
            *(void **)cb_result = MVM_nativecall_unmarshal_carray(tc, res.o, MVM_NATIVECALL_UNMARSHAL_KIND_RETURN);
            break;
        case MVM_NATIVECALL_ARG_CUNION:
            *(void **)cb_result = MVM_nativecall_unmarshal_cunion(tc, res.o, MVM_NATIVECALL_UNMARSHAL_KIND_RETURN);
            break;
        case MVM_NATIVECALL_ARG_VMARRAY:
            *(void **)cb_result = MVM_nativecall_unmarshal_vmarray(tc, res.o, MVM_NATIVECALL_UNMARSHAL_KIND_RETURN);
            break;
        case MVM_NATIVECALL_ARG_CALLBACK:
            *(void **)cb_result = unmarshal_callback(tc, res.o, data->types[0]);
            break;
        case MVM_NATIVECALL_ARG_UCHAR:
            *(ffi_arg *)cb_result = MVM_nativecall_unmarshal_uchar(tc, res.o);
            break;
        case MVM_NATIVECALL_ARG_USHORT:
            *(ffi_arg *)cb_result = MVM_nativecall_unmarshal_ushort(tc, res.o);
            break;
        case MVM_NATIVECALL_ARG_UINT:
            *(ffi_arg *)cb_result = MVM_nativecall_unmarshal_uint(tc, res.o);
            break;
        case MVM_NATIVECALL_ARG_ULONG:
            *(ffi_arg *)cb_result = MVM_nativecall_unmarshal_ulong(tc, res.o);
            break;
        case MVM_NATIVECALL_ARG_ULONGLONG:
            *(unsigned long long *)cb_result = MVM_nativecall_unmarshal_ulonglong(tc, res.o);
            break;
        default:
                MVM_telemetry_interval_stop(tc, interval_id, "nativecall callback handler failed");
            MVM_exception_throw_adhoc(tc,
                "Internal error: unhandled libffi callback return type");
    }

    /* Clean up. */
    MVM_gc_root_temp_pop_n(tc, num_roots);
    MVM_free(args);

    /* Re-block GC if needed, so other threads will be able to collect. */
    if (was_blocked)
        MVM_gc_mark_thread_blocked(tc);

    MVM_telemetry_interval_stop(tc, interval_id, "nativecall callback handler");
}

#define handle_arg(what, cont_X, dc_type, reg_slot, unmarshal_fun) do { \
    MVMRegister r; \
    if ((arg_types[i] & MVM_NATIVECALL_ARG_RW_MASK) == MVM_NATIVECALL_ARG_RW) { \
        if (MVM_6model_container_is ## cont_X(tc, value)) { \
            MVM_6model_container_de ## cont_X(tc, value, &r); \
            values[i]                       = MVM_malloc(sizeof(void *)); \
            *(void **)values[i]             = MVM_malloc(sizeof(dc_type)); \
            *(dc_type *)*(void **)values[i] = (dc_type)r. reg_slot ; \
        } \
        else \
            MVM_exception_throw_adhoc(tc, \
                "Native call expected argument %d to reference a native %s, but got %s", \
                i, what, REPR(value)->name); \
    } \
    else { \
        values[i] = MVM_malloc(sizeof(dc_type)); \
        if (value && IS_CONCRETE(value) && STABLE(value)->container_spec) { \
            STABLE(value)->container_spec->fetch(tc, value, &r); \
            *(dc_type *)values[i] = unmarshal_fun(tc, r.o); \
        } \
        else { \
            *(dc_type *)values[i] = unmarshal_fun(tc, value); \
        } \
    } \
} while (0)

#define handle_ret(tc, c_type, ffi_type, make_fun) do { \
    if (sizeof(c_type) < sizeof(ffi_type)) { \
        ffi_type ret; \
        ffi_call(&cif, entry_point, &ret, values); \
        MVM_gc_mark_thread_unblocked(tc); \
        result = make_fun(tc, res_type, (c_type)ret); \
    } \
    else { \
        c_type ret; \
        ffi_call(&cif, entry_point, &ret, values); \
        MVM_gc_mark_thread_unblocked(tc); \
        result = make_fun(tc, res_type, ret); \
    } \
} while (0)

MVMObject * MVM_nativecall_invoke(MVMThreadContext *tc, MVMObject *res_type,
        MVMObject *site, MVMObject *args) {
    MVMObject     *result = NULL;
    char      **free_strs = NULL;
    MVMint16     num_strs = 0;
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
    void    **values      = MVM_malloc(sizeof(void *) * (num_args ? num_args : 1));

    unsigned int interval_id;

    ffi_cif cif;
    ffi_status status  = ffi_prep_cif(&cif, body->convention, (unsigned int)num_args, body->ffi_ret_type, body->ffi_arg_types);

    interval_id = MVM_telemetry_interval_start(tc, "nativecall invoke");
    MVM_telemetry_interval_annotate((uintptr_t)entry_point, interval_id, "nc entrypoint");

    /* Process arguments. */
    for (i = 0; i < num_args; i++) {
        MVMObject *value = MVM_repr_at_pos_o(tc, args, i);
        switch (arg_types[i] & MVM_NATIVECALL_ARG_TYPE_MASK) {
            case MVM_NATIVECALL_ARG_CHAR:
                handle_arg("integer", cont_i, signed char, i64, MVM_nativecall_unmarshal_char);
                break;
            case MVM_NATIVECALL_ARG_SHORT:
                handle_arg("integer", cont_i, signed short, i64, MVM_nativecall_unmarshal_short);
                break;
            case MVM_NATIVECALL_ARG_INT:
                handle_arg("integer", cont_i, signed int, i64, MVM_nativecall_unmarshal_int);
                break;
            case MVM_NATIVECALL_ARG_LONG:
                handle_arg("integer", cont_i, signed long, i64, MVM_nativecall_unmarshal_long);
                break;
            case MVM_NATIVECALL_ARG_LONGLONG:
                handle_arg("integer", cont_i, signed long long, i64, MVM_nativecall_unmarshal_longlong);
                break;
            case MVM_NATIVECALL_ARG_FLOAT:
                handle_arg("number", cont_n, float, n64, MVM_nativecall_unmarshal_float);
                break;
            case MVM_NATIVECALL_ARG_DOUBLE:
                handle_arg("number", cont_n, double, n64, MVM_nativecall_unmarshal_double);
                break;
            case MVM_NATIVECALL_ARG_ASCIISTR:
            case MVM_NATIVECALL_ARG_UTF8STR:
            case MVM_NATIVECALL_ARG_UTF16STR: {
                MVMint16 free = 0;
                char *str = MVM_nativecall_unmarshal_string(tc, value, arg_types[i], &free, i);
                if (free) {
                    if (!free_strs)
                        free_strs = (char**)MVM_malloc(num_args * sizeof(char *));
                    free_strs[num_strs] = str;
                    num_strs++;
                }
                values[i]           = MVM_malloc(sizeof(void *));
                *(void **)values[i] = str;
                break;
            }
            case MVM_NATIVECALL_ARG_CSTRUCT:
                values[i]           = MVM_malloc(sizeof(void *));
                *(void **)values[i] = MVM_nativecall_unmarshal_cstruct(tc, value, i);
                break;
            case MVM_NATIVECALL_ARG_CPPSTRUCT: {
                /* We need to allocate the struct (THIS) for C++ constructor before passing it along. */
                if (i == 0 && !IS_CONCRETE(value)) {
                    MVMCPPStructREPRData *repr_data = (MVMCPPStructREPRData *)STABLE(res_type)->REPR_data;
                    /* Allocate a full byte aligned area where the C++ structure fits into. */
                    void *ptr           = MVM_malloc(repr_data->struct_size > 0 ? repr_data->struct_size : 1);
                    result              = MVM_nativecall_make_cppstruct(tc, res_type, ptr);
                    values[i]           = MVM_malloc(sizeof(void *));
                    *(void **)values[i] = ptr;
                }
                else {
                    values[i]           = MVM_malloc(sizeof(void *));
                    *(void **)values[i] = MVM_nativecall_unmarshal_cppstruct(tc, value, i);
                }
                break;
            }
            case MVM_NATIVECALL_ARG_CPOINTER:
                if ((arg_types[i] & MVM_NATIVECALL_ARG_RW_MASK) == MVM_NATIVECALL_ARG_RW) {
                    values[i]                     = MVM_malloc(sizeof(void *));
                    *(void **)values[i]           = MVM_malloc(sizeof(void *));
                    *(void **)*(void **)values[i] = (void *)MVM_nativecall_unmarshal_cpointer(tc, value, i);
                }
                else {
                    values[i]           = MVM_malloc(sizeof(void *));
                    *(void **)values[i] = MVM_nativecall_unmarshal_cpointer(tc, value, i);
                }
                break;
            case MVM_NATIVECALL_ARG_CARRAY:
                values[i]           = MVM_malloc(sizeof(void *));
                *(void **)values[i] = MVM_nativecall_unmarshal_carray(tc, value, i);
                break;
            case MVM_NATIVECALL_ARG_CUNION:
                values[i]           = MVM_malloc(sizeof(void *));
                *(void **)values[i] = MVM_nativecall_unmarshal_cunion(tc, value, i);
                break;
            case MVM_NATIVECALL_ARG_VMARRAY:
                values[i]           = MVM_malloc(sizeof(void *));
                *(void **)values[i] = MVM_nativecall_unmarshal_vmarray(tc, value, i);
                break;
            case MVM_NATIVECALL_ARG_CALLBACK:
                values[i]           = MVM_malloc(sizeof(void *));
                *(void **)values[i] = unmarshal_callback(tc, value, body->arg_info[i]);
                break;
            case MVM_NATIVECALL_ARG_UCHAR:
                handle_arg("integer", cont_i, unsigned char, i64, MVM_nativecall_unmarshal_uchar);
                break;
            case MVM_NATIVECALL_ARG_USHORT:
                handle_arg("integer", cont_i, unsigned short, i64, MVM_nativecall_unmarshal_ushort);
                break;
            case MVM_NATIVECALL_ARG_UINT:
                handle_arg("integer", cont_i, unsigned int, i64, MVM_nativecall_unmarshal_uint);
                break;
            case MVM_NATIVECALL_ARG_ULONG:
                handle_arg("integer", cont_i, unsigned long, i64, MVM_nativecall_unmarshal_ulong);
                break;
            case MVM_NATIVECALL_ARG_ULONGLONG:
                handle_arg("integer", cont_i, unsigned long long, i64, MVM_nativecall_unmarshal_ulonglong);
                break;
            default:
                MVM_telemetry_interval_stop(tc, interval_id, "nativecall invoke failed");
                MVM_exception_throw_adhoc(tc, "Internal error: unhandled libffi argument type");
        }
    }


    MVMROOT2(tc, args, res_type, {
        MVM_gc_mark_thread_blocked(tc);
        if (result) {
            /* We are calling a C++ constructor so we hand back the invocant (THIS) we recorded earlier. */
            void *ret; // We are not going to use it, but we need to pass it to libffi.
            ffi_call(&cif, entry_point, &ret, values);
            MVM_gc_mark_thread_unblocked(tc);
        }
        else {
            /* Process return values. */
            switch (ret_type & MVM_NATIVECALL_ARG_TYPE_MASK) {
                case MVM_NATIVECALL_ARG_VOID: {
                    void *ret;
                    ffi_call(&cif, entry_point, &ret, values);
                    MVM_gc_mark_thread_unblocked(tc);
                    result = res_type;
                    break;
                }
                case MVM_NATIVECALL_ARG_CHAR:
                    handle_ret(tc, signed char, ffi_sarg, MVM_nativecall_make_int);
                    break;
                case MVM_NATIVECALL_ARG_SHORT:
                    handle_ret(tc, signed short, ffi_sarg, MVM_nativecall_make_int);
                    break;
                case MVM_NATIVECALL_ARG_INT:
                    handle_ret(tc, signed int, ffi_sarg, MVM_nativecall_make_int);
                    break;
                case MVM_NATIVECALL_ARG_LONG:
                    handle_ret(tc, signed long, ffi_sarg, MVM_nativecall_make_int);
                    break;
                case MVM_NATIVECALL_ARG_LONGLONG:
                    handle_ret(tc, signed long long, ffi_sarg, MVM_nativecall_make_int);
                    break;
                case MVM_NATIVECALL_ARG_FLOAT: {
                    float ret;
                    ffi_call(&cif, entry_point, &ret, values);
                    MVM_gc_mark_thread_unblocked(tc);
                    result = MVM_nativecall_make_num(tc, res_type, ret);
                    break;
                }
                case MVM_NATIVECALL_ARG_DOUBLE: {
                    double ret;
                    ffi_call(&cif, entry_point, &ret, values);
                    MVM_gc_mark_thread_unblocked(tc);
                    result = MVM_nativecall_make_num(tc, res_type, ret);
                    break;
                }
                case MVM_NATIVECALL_ARG_ASCIISTR:
                case MVM_NATIVECALL_ARG_UTF8STR:
                case MVM_NATIVECALL_ARG_UTF16STR: {
                    char *ret;
                    ffi_call(&cif, entry_point, &ret, values);
                    MVM_gc_mark_thread_unblocked(tc);
                    result = MVM_nativecall_make_str(tc, res_type, body->ret_type, ret);
                    break;
                }
                case MVM_NATIVECALL_ARG_CSTRUCT:
                    handle_ret(tc, void *, ffi_arg, MVM_nativecall_make_cstruct);
                    break;
                case MVM_NATIVECALL_ARG_CPPSTRUCT:
                    handle_ret(tc, void *, ffi_arg, MVM_nativecall_make_cppstruct);
                    break;
                case MVM_NATIVECALL_ARG_CPOINTER:
                    handle_ret(tc, void *, ffi_arg, MVM_nativecall_make_cpointer);
                    break;
                case MVM_NATIVECALL_ARG_CARRAY:
                    handle_ret(tc, void *, ffi_arg, MVM_nativecall_make_carray);
                    break;
                case MVM_NATIVECALL_ARG_CUNION:
                    handle_ret(tc, void *, ffi_arg, MVM_nativecall_make_cunion);
                    break;
                case MVM_NATIVECALL_ARG_CALLBACK: {
                    /* TODO: A callback -return- value means that we have a C method
                    * that needs to be wrapped similarly to a is native(...) Perl 6
                    * sub. */
                    void *ret;
                    ffi_call(&cif, entry_point, &ret, values);
                    MVM_gc_mark_thread_unblocked(tc);
                    /* XXX do something with the function pointer: ret */
                    result = res_type;
                    break;
                }
                case MVM_NATIVECALL_ARG_UCHAR:
                    handle_ret(tc, unsigned char, ffi_arg, MVM_nativecall_make_int);
                    break;
                case MVM_NATIVECALL_ARG_USHORT:
                    handle_ret(tc, unsigned short, ffi_arg, MVM_nativecall_make_int);
                    break;
                case MVM_NATIVECALL_ARG_UINT:
                    handle_ret(tc, unsigned int, ffi_arg, MVM_nativecall_make_int);
                    break;
                case MVM_NATIVECALL_ARG_ULONG:
                    handle_ret(tc, unsigned long, ffi_arg, MVM_nativecall_make_int);
                    break;
                case MVM_NATIVECALL_ARG_ULONGLONG:
                    handle_ret(tc, unsigned long long, ffi_arg, MVM_nativecall_make_int);
                    break;
                default:
                    MVM_gc_mark_thread_unblocked(tc);
                    MVM_telemetry_interval_stop(tc, interval_id, "nativecall invoke failed");
                    MVM_exception_throw_adhoc(tc, "Internal error: unhandled libffi return type");
            }
        }
    });

    for (i = 0; i < num_args; i++) {
        MVMObject *value = MVM_repr_at_pos_o(tc, args, i);
        if ((arg_types[i] & MVM_NATIVECALL_ARG_RW_MASK) == MVM_NATIVECALL_ARG_RW) {
            switch (arg_types[i] & MVM_NATIVECALL_ARG_TYPE_MASK) {
                case MVM_NATIVECALL_ARG_CHAR:
                    MVM_6model_container_assign_i(tc, value, (MVMint64)*(signed char *)*(void **)values[i]);
                    break;
                case MVM_NATIVECALL_ARG_SHORT:
                    MVM_6model_container_assign_i(tc, value, (MVMint64)*(signed short *)*(void **)values[i]);
                    break;
                case MVM_NATIVECALL_ARG_INT:
                    MVM_6model_container_assign_i(tc, value, (MVMint64)*(signed int *)*(void **)values[i]);
                    break;
                case MVM_NATIVECALL_ARG_LONG:
                    MVM_6model_container_assign_i(tc, value, (MVMint64)*(signed long *)*(void **)values[i]);
                    break;
                case MVM_NATIVECALL_ARG_LONGLONG:
                    MVM_6model_container_assign_i(tc, value, (MVMint64)*(signed long long *)*(void **)values[i]);
                    break;
                case MVM_NATIVECALL_ARG_FLOAT:
                    MVM_6model_container_assign_n(tc, value, (MVMnum64)*(float *)*(void **)values[i]);
                    break;
                case MVM_NATIVECALL_ARG_DOUBLE:
                    MVM_6model_container_assign_n(tc, value, (MVMnum64)*(double *)*(void **)values[i]);
                    break;
                case MVM_NATIVECALL_ARG_UCHAR:
                    MVM_6model_container_assign_i(tc, value, (MVMint64)*(unsigned char *)*(void **)values[i]);
                    break;
                case MVM_NATIVECALL_ARG_USHORT:
                    MVM_6model_container_assign_i(tc, value, (MVMint64)*(unsigned short *)*(void **)values[i]);
                    break;
                case MVM_NATIVECALL_ARG_UINT:
                    MVM_6model_container_assign_i(tc, value, (MVMint64)*(unsigned int *)*(void **)values[i]);
                    break;
                case MVM_NATIVECALL_ARG_ULONG:
                    MVM_6model_container_assign_i(tc, value, (MVMint64)*(unsigned long *)*(void **)values[i]);
                    break;
                case MVM_NATIVECALL_ARG_ULONGLONG:
                    MVM_6model_container_assign_i(tc, value, (MVMint64)*(unsigned long long *)*(void **)values[i]);
                    break;
                case MVM_NATIVECALL_ARG_CPOINTER:
                    REPR(value)->box_funcs.set_int(tc, STABLE(value), value, OBJECT_BODY(value),
                        (MVMint64)(uintptr_t)*(void **)*(void **)values[i]);
                    break;
                default:
                    MVM_telemetry_interval_stop(tc, interval_id, "nativecall invoke failed");
                    MVM_exception_throw_adhoc(tc, "Internal error: unhandled libffi argument type");
            }
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

    if (values)
        MVM_free(values);

    MVM_telemetry_interval_stop(tc, interval_id, "nativecall invoke");

    return result;
}
