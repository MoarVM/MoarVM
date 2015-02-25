#include "moar.h"

/* Grabs a NativeCall body. */
static MVMNativeCallBody * get_nc_body(MVMThreadContext *tc, MVMObject *obj) {
    if (REPR(obj)->ID == MVM_REPR_ID_MVMNativeCall)
        return (MVMNativeCallBody *)OBJECT_BODY(obj);
    else
        return (MVMNativeCallBody *)REPR(obj)->box_funcs.get_boxed_ref(tc,
            STABLE(obj), obj, OBJECT_BODY(obj), MVM_REPR_ID_MVMNativeCall);
}

/* Gets the flag for whether to free a string after a call or not. */
static MVMint16 get_str_free_flag(MVMThreadContext *tc, MVMObject *info) {
    MVMString *flag = tc->instance->str_consts.free_str;
    if (MVM_repr_exists_key(tc, info, flag))
        if (!MVM_repr_get_int(tc, MVM_repr_at_key_o(tc, info, flag)))
            return MVM_NATIVECALL_ARG_NO_FREE_STR;
    return MVM_NATIVECALL_ARG_FREE_STR;
}

/* Takes a hash describing a type hands back an argument type code. */
static MVMint16 get_arg_type(MVMThreadContext *tc, MVMObject *info, MVMint16 is_return) {
    MVMString *typename = MVM_repr_get_str(tc, MVM_repr_at_key_o(tc, info,
        tc->instance->str_consts.type));
    char *ctypename = MVM_string_utf8_encode_C_string(tc, typename);
    MVMint16 result;
    if (strcmp(ctypename, "void") == 0) {
        if (!is_return) {
            MVM_free(ctypename);
            MVM_exception_throw_adhoc(tc,
                "Cannot use 'void' type except for on native call return values");
        }
        result = MVM_NATIVECALL_ARG_VOID;
    }
    else if (strcmp(ctypename, "char") == 0)
        result = MVM_NATIVECALL_ARG_CHAR;
    else if (strcmp(ctypename, "short") == 0)
        result = MVM_NATIVECALL_ARG_SHORT;
    else if (strcmp(ctypename, "int") == 0)
        result = MVM_NATIVECALL_ARG_INT;
    else if (strcmp(ctypename, "long") == 0)
        result = MVM_NATIVECALL_ARG_LONG;
    else if (strcmp(ctypename, "longlong") == 0)
        result = MVM_NATIVECALL_ARG_LONGLONG;
    else if (strcmp(ctypename, "float") == 0)
        result = MVM_NATIVECALL_ARG_FLOAT;
    else if (strcmp(ctypename, "double") == 0)
        result = MVM_NATIVECALL_ARG_DOUBLE;
    else if (strcmp(ctypename, "asciistr") == 0)
        result = MVM_NATIVECALL_ARG_ASCIISTR | get_str_free_flag(tc, info);
    else if (strcmp(ctypename, "utf8str") == 0)
        result = MVM_NATIVECALL_ARG_UTF8STR | get_str_free_flag(tc, info);
    else if (strcmp(ctypename, "utf16str") == 0)
        result = MVM_NATIVECALL_ARG_UTF16STR | get_str_free_flag(tc, info);
    else if (strcmp(ctypename, "cstruct") == 0)
        result = MVM_NATIVECALL_ARG_CSTRUCT;
    else if (strcmp(ctypename, "cppstruct") == 0)
        result = MVM_NATIVECALL_ARG_CPPSTRUCT;
    else if (strcmp(ctypename, "cpointer") == 0)
        result = MVM_NATIVECALL_ARG_CPOINTER;
    else if (strcmp(ctypename, "carray") == 0)
        result = MVM_NATIVECALL_ARG_CARRAY;
    else if (strcmp(ctypename, "vmarray") == 0)
        result = MVM_NATIVECALL_ARG_VMARRAY;
    else if (strcmp(ctypename, "callback") == 0)
        result = MVM_NATIVECALL_ARG_CALLBACK;
    else
        MVM_exception_throw_adhoc(tc, "Unknown type '%s' used for native call", ctypename);
    MVM_free(ctypename);
    return result;
}

/* Maps a calling convention name to an ID. */
static MVMint16 get_calling_convention(MVMThreadContext *tc, MVMString *name) {
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
        else
            MVM_exception_throw_adhoc(tc,
                "Unknown calling convention '%s' used for native call", cname);
        MVM_free(cname);
    }
    return result;
}

/* Map argument type ID to dyncall character ID. */
static char get_signature_char(MVMint16 type_id) {
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
        case MVM_NATIVECALL_ARG_CPOINTER:
        case MVM_NATIVECALL_ARG_CARRAY:
        case MVM_NATIVECALL_ARG_VMARRAY:
        case MVM_NATIVECALL_ARG_CALLBACK:
            return 'p';
        default:
            return '\0';
    }
}

static MVMObject * make_int_result(MVMThreadContext *tc, MVMObject *type, MVMint64 value) {
    return type ? MVM_repr_box_int(tc, type, value) : NULL;
}

static MVMObject * make_num_result(MVMThreadContext *tc, MVMObject *type, MVMnum64 value) {
    return type ? MVM_repr_box_num(tc, type, value) : NULL;
}

static MVMObject * make_str_result(MVMThreadContext *tc, MVMObject *type, MVMint16 ret_type, char *cstring) {
    MVMObject *result = type;
    if (cstring && type) {
        MVMString *value;

        MVM_gc_root_temp_push(tc, (MVMCollectable **)&type);

        switch (ret_type & MVM_NATIVECALL_ARG_TYPE_MASK) {
            case MVM_NATIVECALL_ARG_ASCIISTR:
                value = MVM_string_ascii_decode(tc, tc->instance->VMString, cstring, strlen(cstring));
                break;
            case MVM_NATIVECALL_ARG_UTF8STR:
                value = MVM_string_utf8_decode(tc, tc->instance->VMString, cstring, strlen(cstring));
                break;
            case MVM_NATIVECALL_ARG_UTF16STR:
                value = MVM_string_utf16_decode(tc, tc->instance->VMString, cstring, strlen(cstring));
                break;
            default:
                MVM_exception_throw_adhoc(tc, "Internal error: unhandled encoding");
        }

        MVM_gc_root_temp_pop(tc);
        result = MVM_repr_box_str(tc, type, value);
        if (ret_type & MVM_NATIVECALL_ARG_FREE_STR)
            MVM_free(cstring);
    }

    return result;
}

/* Constructs a boxed result using a CStruct REPR type. */
MVMObject * MVM_nativecall_make_cstruct(MVMThreadContext *tc, MVMObject *type, void *cstruct) {
    MVMObject *result = type;
    if (cstruct && type) {
        MVMCStructREPRData *repr_data = (MVMCStructREPRData *)STABLE(type)->REPR_data;
        if (REPR(type)->ID != MVM_REPR_ID_MVMCStruct)
            MVM_exception_throw_adhoc(tc,
                "Native call expected return type with CStruct representation, but got a %s", REPR(type)->name);
        result = REPR(type)->allocate(tc, STABLE(type));
        ((MVMCStruct *)result)->body.cstruct = cstruct;
        if (repr_data->num_child_objs)
            ((MVMCStruct *)result)->body.child_objs = MVM_calloc(repr_data->num_child_objs, sizeof(MVMObject *));
    }
    return result;
}

MVMObject * MVM_nativecall_make_cppstruct(MVMThreadContext *tc, MVMObject *type, void *cppstruct) {
    MVMObject *result = type;
    if (cppstruct && type) {
        MVMCPPStructREPRData *repr_data = (MVMCPPStructREPRData *)STABLE(type)->REPR_data;
        if (REPR(type)->ID != MVM_REPR_ID_MVMCPPStruct)
            MVM_exception_throw_adhoc(tc,
                "Native call expected return type with CPPStruct representation, but got a %s", REPR(type)->name);
        result = REPR(type)->allocate(tc, STABLE(type));
        ((MVMCPPStruct *)result)->body.cppstruct = cppstruct;
        if (repr_data->num_child_objs)
            ((MVMCPPStruct *)result)->body.child_objs = MVM_calloc(repr_data->num_child_objs, sizeof(MVMObject *));
    }
    return result;
}

/* Constructs a boxed result using a CPointer REPR type. */
MVMObject * MVM_nativecall_make_cpointer(MVMThreadContext *tc, MVMObject *type, void *ptr) {
    MVMObject *result = type;
    if (ptr && type) {
        if (REPR(type)->ID != MVM_REPR_ID_MVMCPointer)
            MVM_exception_throw_adhoc(tc,
                "Native call expected return type with CPointer representation, but got a %s", REPR(type)->name);
        result = REPR(type)->allocate(tc, STABLE(type));
        ((MVMCPointer *)result)->body.ptr = ptr;
    }
    return result;
}

/* Constructs a boxed result using a CArray REPR type. */
MVMObject * MVM_nativecall_make_carray(MVMThreadContext *tc, MVMObject *type, void *carray) {
    MVMObject *result = type;
    if (carray && type) {
        if (REPR(type)->ID != MVM_REPR_ID_MVMCArray)
            MVM_exception_throw_adhoc(tc,
                "Native call expected return type with CArray representation, but got a %s", REPR(type)->name);
        result = REPR(type)->allocate(tc, STABLE(type));
        ((MVMCArray *)result)->body.storage = carray;
    }
    return result;
}

static DCchar unmarshal_char(MVMThreadContext *tc, MVMObject *value) {
    return (DCchar)MVM_repr_get_int(tc, value);
}

static DCshort unmarshal_short(MVMThreadContext *tc, MVMObject *value) {
    return (DCshort)MVM_repr_get_int(tc, value);
}

static DCint unmarshal_int(MVMThreadContext *tc, MVMObject *value) {
    return (DCint)MVM_repr_get_int(tc, value);
}

static DClong unmarshal_long(MVMThreadContext *tc, MVMObject *value) {
    return (DClong)MVM_repr_get_int(tc, value);
}

static DClonglong unmarshal_longlong(MVMThreadContext *tc, MVMObject *value) {
    return (DClonglong)MVM_repr_get_int(tc, value);
}

static DCfloat unmarshal_float(MVMThreadContext *tc, MVMObject *value) {
    return (DCfloat)MVM_repr_get_num(tc, value);
}

static DCdouble unmarshal_double(MVMThreadContext *tc, MVMObject *value) {
    return (DCdouble)MVM_repr_get_num(tc, value);
}

static char * unmarshal_string(MVMThreadContext *tc, MVMObject *value, MVMint16 type, MVMint16 *free) {
    if (IS_CONCRETE(value)) {
        MVMString *value_str = MVM_repr_get_str(tc, value);

        /* Encode string. */
        char *str;
        switch (type & MVM_NATIVECALL_ARG_TYPE_MASK) {
            case MVM_NATIVECALL_ARG_ASCIISTR:
                str = MVM_string_ascii_encode_any(tc, value_str);
                break;
            case MVM_NATIVECALL_ARG_UTF16STR:
                str = MVM_string_utf16_encode(tc, value_str);
                break;
            default:
                str = MVM_string_utf8_encode_C_string(tc, value_str);
        }

        /* Set whether to free it or not. */
        if (free) {
            if (REPR(value)->ID == MVM_REPR_ID_MVMCStr)
                *free = 0; /* Manually managed. */
            else if (free && type & MVM_NATIVECALL_ARG_FREE_STR_MASK)
                *free = 1;
            else
                *free = 0;
        }

        return str;
    }
    else {
        return NULL;
    }
}

static void * unmarshal_cstruct(MVMThreadContext *tc, MVMObject *value) {
    if (!IS_CONCRETE(value))
        return NULL;
    else if (REPR(value)->ID == MVM_REPR_ID_MVMCStruct)
        return ((MVMCStruct *)value)->body.cstruct;
    else
        MVM_exception_throw_adhoc(tc,
            "Native call expected return type with CStruct representation, but got a %s", REPR(value)->name);
}

static void * unmarshal_cppstruct(MVMThreadContext *tc, MVMObject *value) {
    if (!IS_CONCRETE(value))
        return NULL;
    else if (REPR(value)->ID == MVM_REPR_ID_MVMCPPStruct)
        return ((MVMCPPStruct *)value)->body.cppstruct;
    else
        MVM_exception_throw_adhoc(tc,
            "Native call expected return type with CPPStruct representation, but got a %s", REPR(value)->name);
}

static void * unmarshal_cpointer(MVMThreadContext *tc, MVMObject *value) {
    if (!IS_CONCRETE(value))
        return NULL;
    else if (REPR(value)->ID == MVM_REPR_ID_MVMCPointer)
        return ((MVMCPointer *)value)->body.ptr;
    else
        MVM_exception_throw_adhoc(tc,
            "Native call expected return type with CPointer representation, but got a %s", REPR(value)->name);
}

static void * unmarshal_carray(MVMThreadContext *tc, MVMObject *value) {
    if (!IS_CONCRETE(value))
        return NULL;
    else if (REPR(value)->ID == MVM_REPR_ID_MVMCArray)
        return ((MVMCArray *)value)->body.storage;
    else
        MVM_exception_throw_adhoc(tc,
            "Native call expected return type with CArray representation, but got a %s", REPR(value)->name);
}

static void * unmarshal_vmarray(MVMThreadContext *tc, MVMObject *value) {
    if (!IS_CONCRETE(value))
        return NULL;
    else if (REPR(value)->ID == MVM_REPR_ID_MVMArray) {
        MVMArrayBody *body          = &((MVMArray *)value)->body;
        MVMArrayREPRData *repr_data = (MVMArrayREPRData *)STABLE(value)->REPR_data;
        size_t start_pos            = body->start * repr_data->elem_size;
        return ((char *)body->slots.any) + start_pos;
    }
    else
        MVM_exception_throw_adhoc(tc,
            "Native call expected object with Array representation, but got a %s", REPR(value)->name);
}

/* Sets up a callback, caching the information to avoid duplicate work. */
static char callback_handler(DCCallback *cb, DCArgs *args, DCValue *result, MVMNativeCallback *data);
static void * unmarshal_callback(MVMThreadContext *tc, MVMObject *callback, MVMObject *sig_info) {
    MVMNativeCallbackCacheHead *callback_data_head = NULL;
    MVMNativeCallback **callback_data_handle;
    MVMString          *cuid;

    if (!IS_CONCRETE(callback))
        return NULL;

    /* Try to locate existing cached callback info. */
    callback = MVM_frame_find_invokee(tc, callback, NULL);
    cuid     = ((MVMCode *)callback)->body.sf->body.cuuid;
    MVM_string_flatten(tc, cuid);
    MVM_HASH_GET(tc, tc->native_callback_cache, cuid, callback_data_head);

    if (!callback_data_head) {
        callback_data_head = MVM_malloc(sizeof(MVMNativeCallbackCacheHead));
        callback_data_head->head = NULL;

        MVM_HASH_BIND(tc, tc->native_callback_cache, cuid, callback_data_head);
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
        cs                 = MVM_malloc(sizeof(MVMCallsite));
        cs->arg_flags      = MVM_malloc(num_info * sizeof(MVMCallsiteEntry));
        cs->arg_count      = num_info - 1;
        cs->num_pos        = num_info - 1;
        cs->has_flattening = 0;
        cs->is_interned    = 0;
        cs->with_invocant  = NULL;

        typehash = MVM_repr_at_pos_o(tc, sig_info, 0);
        callback_data->types[0] = MVM_repr_at_key_o(tc, typehash,
            tc->instance->str_consts.typeobj);
        callback_data->typeinfos[0] = get_arg_type(tc, typehash, 1);
        signature[num_info] = get_signature_char(callback_data->typeinfos[0]);
        for (i = 1; i < num_info; i++) {
            typehash = MVM_repr_at_pos_o(tc, sig_info, i);
            callback_data->types[i] = MVM_repr_at_key_o(tc, typehash,
                tc->instance->str_consts.typeobj);
            callback_data->typeinfos[i] = get_arg_type(tc, typehash, 0) & ~MVM_NATIVECALL_ARG_FREE_STR;
            signature[i - 1] = get_signature_char(callback_data->typeinfos[i]);
            switch (callback_data->typeinfos[i] & MVM_NATIVECALL_ARG_TYPE_MASK) {
                case MVM_NATIVECALL_ARG_CHAR:
                case MVM_NATIVECALL_ARG_SHORT:
                case MVM_NATIVECALL_ARG_INT:
                case MVM_NATIVECALL_ARG_LONG:
                case MVM_NATIVECALL_ARG_LONGLONG:
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

        MVM_callsite_try_intern(tc, &cs);

        callback_data->tc        = tc;
        callback_data->cs        = cs;
        callback_data->target    = callback;
        callback_data->cb        = dcbNewCallback(signature, (DCCallbackHandler *)&callback_handler, callback_data);

        /* Now insert the MVMCallback into the linked list. */
        *callback_data_handle = callback_data;
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
    MVMRegister res;

    /* Build a callsite and arguments buffer. */
    MVMRegister *args = MVM_malloc(data->num_types * sizeof(MVMRegister));
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
                args[i - 1].o = make_str_result(data->tc, type, typeinfo,
                    (char *)dcbArgPointer(cb_args));
                break;
            case MVM_NATIVECALL_ARG_CSTRUCT:
                args[i - 1].o = MVM_nativecall_make_cstruct(data->tc, type,
                    dcbArgPointer(cb_args));
                break;
            case MVM_NATIVECALL_ARG_CPOINTER:
                args[i - 1].o = MVM_nativecall_make_cpointer(data->tc, type,
                    dcbArgPointer(cb_args));
                break;
            case MVM_NATIVECALL_ARG_CARRAY:
                args[i - 1].o = MVM_nativecall_make_carray(data->tc, type,
                    dcbArgPointer(cb_args));
                break;
            case MVM_NATIVECALL_ARG_CALLBACK:
                /* TODO: A callback -return- value means that we have a C method
                * that needs to be wrapped similarly to a is native(...) Perl 6
                * sub. */
                dcbArgPointer(cb_args);
                args[i - 1].o = type;
            default:
                MVM_exception_throw_adhoc(data->tc,
                    "Internal error: unhandled dyncall callback argument type");
        }
    }

    /* Call into a nested interpreter (since we already are in one). Need to
     * save a bunch of state around each side of this. */
    cid.invokee = data->target;
    cid.args    = args;
    cid.cs      = data->cs;
    {
        MVMThreadContext *tc = data->tc;
        MVMuint8 **backup_interp_cur_op         = tc->interp_cur_op;
        MVMuint8 **backup_interp_bytecode_start = tc->interp_bytecode_start;
        MVMRegister **backup_interp_reg_base    = tc->interp_reg_base;
        MVMCompUnit **backup_interp_cu          = tc->interp_cu;
        MVMFrame *backup_cur_frame              = tc->cur_frame;
        MVMFrame *backup_thread_entry_frame     = tc->thread_entry_frame;
        MVMuint32 backup_mark                   = MVM_gc_root_temp_mark(tc);
        jmp_buf backup_interp_jump;
        memcpy(backup_interp_jump, tc->interp_jump, sizeof(jmp_buf));

        tc->cur_frame->return_value = &res;
        tc->cur_frame->return_type  = MVM_RETURN_OBJ;
        MVM_interp_run(tc, &callback_invoke, &cid);

        tc->interp_cur_op         = backup_interp_cur_op;
        tc->interp_bytecode_start = backup_interp_bytecode_start;
        tc->interp_reg_base       = backup_interp_reg_base;
        tc->interp_cu             = backup_interp_cu;
        tc->cur_frame             = backup_cur_frame;
        tc->thread_entry_frame    = backup_thread_entry_frame;
        memcpy(tc->interp_jump, backup_interp_jump, sizeof(jmp_buf));
        MVM_gc_root_temp_mark_reset(tc, backup_mark);
    }

    /* Handle return value. */
    if (res.o) {
        MVMContainerSpec const *contspec = STABLE(res.o)->container_spec;
        if (contspec && contspec->fetch_never_invokes)
            contspec->fetch(data->tc, res.o, &res);
    }
    switch (data->typeinfos[0] & MVM_NATIVECALL_ARG_TYPE_MASK) {
        case MVM_NATIVECALL_ARG_VOID:
            break;
        case MVM_NATIVECALL_ARG_CHAR:
            cb_result->c = unmarshal_char(data->tc, res.o);
            break;
        case MVM_NATIVECALL_ARG_SHORT:
            cb_result->s = unmarshal_short(data->tc, res.o);
            break;
        case MVM_NATIVECALL_ARG_INT:
            cb_result->i = unmarshal_int(data->tc, res.o);
            break;
        case MVM_NATIVECALL_ARG_LONG:
            cb_result->j = unmarshal_long(data->tc, res.o);
            break;
        case MVM_NATIVECALL_ARG_LONGLONG:
            cb_result->l = unmarshal_longlong(data->tc, res.o);
            break;
        case MVM_NATIVECALL_ARG_FLOAT:
            cb_result->f = unmarshal_float(data->tc, res.o);
            break;
        case MVM_NATIVECALL_ARG_DOUBLE:
            cb_result->d = unmarshal_double(data->tc, res.o);
            break;
        case MVM_NATIVECALL_ARG_ASCIISTR:
        case MVM_NATIVECALL_ARG_UTF8STR:
        case MVM_NATIVECALL_ARG_UTF16STR:
            cb_result->Z = unmarshal_string(data->tc, res.o, data->typeinfos[0], NULL);
            break;
        case MVM_NATIVECALL_ARG_CSTRUCT:
            cb_result->p = unmarshal_cstruct(data->tc, res.o);
            break;
        case MVM_NATIVECALL_ARG_CPOINTER:
            cb_result->p = unmarshal_cpointer(data->tc, res.o);
            break;
        case MVM_NATIVECALL_ARG_CARRAY:
            cb_result->p = unmarshal_carray(data->tc, res.o);
            break;
        case MVM_NATIVECALL_ARG_VMARRAY:
            cb_result->p = unmarshal_vmarray(data->tc, res.o);
            break;
        case MVM_NATIVECALL_ARG_CALLBACK:
            cb_result->p = unmarshal_callback(data->tc, res.o, data->types[0]);
            break;
        default:
            MVM_exception_throw_adhoc(data->tc,
                "Internal error: unhandled dyncall callback return type");
    }

    /* Clean up. */
    MVM_free(args);

    /* Indicate what we're producing as a result. */
    return get_signature_char(data->typeinfos[0]);
}

/* Builds up a native call site out of the supplied arguments. */
void MVM_nativecall_build(MVMThreadContext *tc, MVMObject *site, MVMString *lib,
        MVMString *sym, MVMString *conv, MVMObject *arg_info, MVMObject *ret_info) {
    char *lib_name = MVM_string_utf8_encode_C_string(tc, lib);
    char *sym_name = MVM_string_utf8_encode_C_string(tc, sym);
    MVMint16 i;

    /* Initialize the object; grab native call part of its body. */
    MVMNativeCallBody *body = get_nc_body(tc, site);

    /* Try to load the library. */
    body->lib_name = lib_name;
    body->lib_handle = dlLoadLibrary(strlen(lib_name) ? lib_name : NULL);
    if (!body->lib_handle) {
        MVM_free(sym_name);
        MVM_exception_throw_adhoc(tc, "Cannot locate native library '%s'", lib_name);
    }

    /* Try to locate the symbol. */
    body->entry_point = dlFindSymbol(body->lib_handle, sym_name);
    if (!body->entry_point)
        MVM_exception_throw_adhoc(tc, "Cannot locate symbol '%s' in native library '%s'",
            sym_name, lib_name);
    MVM_free(sym_name);

    /* Set calling convention, if any. */
    body->convention = get_calling_convention(tc, conv);

    /* Transform each of the args info structures into a flag. */
    body->num_args  = MVM_repr_elems(tc, arg_info);
    body->arg_types = MVM_malloc(sizeof(MVMint16) * (body->num_args ? body->num_args : 1));
    body->arg_info  = MVM_malloc(sizeof(MVMObject *) * (body->num_args ? body->num_args : 1));
    for (i = 0; i < body->num_args; i++) {
        MVMObject *info = MVM_repr_at_pos_o(tc, arg_info, i);
        body->arg_types[i] = get_arg_type(tc, info, 0);
        if(body->arg_types[i] == MVM_NATIVECALL_ARG_CALLBACK) {
            MVM_ASSIGN_REF(tc, &(site->header), body->arg_info[i],
                MVM_repr_at_key_o(tc, info, tc->instance->str_consts.callback_args));
        }
        else {
            body->arg_info[i]  = NULL;
        }
    }

    /* Transform return argument type info a flag. */
    body->ret_type = get_arg_type(tc, ret_info, 1);
}

MVMObject * MVM_nativecall_invoke(MVMThreadContext *tc, MVMObject *res_type,
        MVMObject *site, MVMObject *args) {
    MVMObject  *result = NULL;
    char      **free_strs = NULL;
    MVMint16    num_strs  = 0;
    MVMint16    i;

    /* Get native call body, so we can locate the call info. Read out all we
     * shall need, since later we may allocate a result and and move it. */
    MVMNativeCallBody *body = get_nc_body(tc, site);
    MVMint16  num_args    = body->num_args;
    MVMint16 *arg_types   = body->arg_types;
    MVMint16  ret_type    = body->ret_type;
    void     *entry_point = body->entry_point;
    void *ptr = NULL;

    /* Create and set up call VM. */
    DCCallVM *vm = dcNewCallVM(8192);
    dcMode(vm, body->convention);

    /* Process arguments. */
    MVMROOT(tc, result, {
    for (i = 0; i < num_args; i++) {
        MVMObject *value = MVM_repr_at_pos_o(tc, args, i);
        switch (arg_types[i] & MVM_NATIVECALL_ARG_TYPE_MASK) {
            case MVM_NATIVECALL_ARG_CHAR:
                dcArgChar(vm, unmarshal_char(tc, value));
                break;
            case MVM_NATIVECALL_ARG_SHORT:
                dcArgShort(vm, unmarshal_short(tc, value));
                break;
            case MVM_NATIVECALL_ARG_INT:
                dcArgInt(vm, unmarshal_int(tc, value));
                break;
            case MVM_NATIVECALL_ARG_LONG:
                dcArgLong(vm, unmarshal_long(tc, value));
                break;
            case MVM_NATIVECALL_ARG_LONGLONG:
                dcArgLongLong(vm, unmarshal_longlong(tc, value));
                break;
            case MVM_NATIVECALL_ARG_FLOAT:
                dcArgFloat(vm, unmarshal_float(tc, value));
                break;
            case MVM_NATIVECALL_ARG_DOUBLE:
                dcArgDouble(vm, unmarshal_double(tc, value));
                break;
            case MVM_NATIVECALL_ARG_ASCIISTR:
            case MVM_NATIVECALL_ARG_UTF8STR:
            case MVM_NATIVECALL_ARG_UTF16STR:
                {
                    MVMint16 free;
                    char *str = unmarshal_string(tc, value, arg_types[i], &free);
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
                dcArgPointer(vm, unmarshal_cstruct(tc, value));
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
                        dcArgPointer(vm, unmarshal_cppstruct(tc, value));
                    }
                }
                break;
            case MVM_NATIVECALL_ARG_CPOINTER:
                dcArgPointer(vm, unmarshal_cpointer(tc, value));
                break;
            case MVM_NATIVECALL_ARG_CARRAY:
                dcArgPointer(vm, unmarshal_carray(tc, value));
                break;
            case MVM_NATIVECALL_ARG_VMARRAY:
                dcArgPointer(vm, unmarshal_vmarray(tc, value));
                break;
            case MVM_NATIVECALL_ARG_CALLBACK:
                dcArgPointer(vm, unmarshal_callback(tc, value, body->arg_info[i]));
                break;
            default:
                MVM_exception_throw_adhoc(tc, "Internal error: unhandled dyncall argument type");
        }
    }
    });

    if (result) {
        /* We are calling a C++ constructor so we hand back the invocant (THIS) we recorded earlier. */
        dcCallVoid(vm, body->entry_point);
    }
    else {
        /* Call and process return values. */
        MVMROOT(tc, args, {
        MVMROOT(tc, res_type, {
            switch (ret_type & MVM_NATIVECALL_ARG_TYPE_MASK) {
                case MVM_NATIVECALL_ARG_VOID:
                    dcCallVoid(vm, entry_point);
                    result = res_type;
                    break;
                case MVM_NATIVECALL_ARG_CHAR:
                    result = make_int_result(tc, res_type, dcCallChar(vm, entry_point));
                    break;
                case MVM_NATIVECALL_ARG_SHORT:
                    result = make_int_result(tc, res_type, dcCallShort(vm, entry_point));
                    break;
                case MVM_NATIVECALL_ARG_INT:
                    result = make_int_result(tc, res_type, dcCallInt(vm, entry_point));
                    break;
                case MVM_NATIVECALL_ARG_LONG:
                    result = make_int_result(tc, res_type, dcCallLong(vm, entry_point));
                    break;
                case MVM_NATIVECALL_ARG_LONGLONG:
                    result = make_int_result(tc, res_type, dcCallLongLong(vm, entry_point));
                    break;
                case MVM_NATIVECALL_ARG_FLOAT:
                    result = make_num_result(tc, res_type, dcCallFloat(vm, entry_point));
                    break;
                case MVM_NATIVECALL_ARG_DOUBLE:
                    result = make_num_result(tc, res_type, dcCallDouble(vm, entry_point));
                    break;
                case MVM_NATIVECALL_ARG_ASCIISTR:
                case MVM_NATIVECALL_ARG_UTF8STR:
                case MVM_NATIVECALL_ARG_UTF16STR:
                    result = make_str_result(tc, res_type, body->ret_type,
                        (char *)dcCallPointer(vm, entry_point));
                    break;
                case MVM_NATIVECALL_ARG_CSTRUCT:
                    result = MVM_nativecall_make_cstruct(tc, res_type, dcCallPointer(vm, body->entry_point));
                    break;
                case MVM_NATIVECALL_ARG_CPPSTRUCT:
                    result = MVM_nativecall_make_cppstruct(tc, res_type, dcCallPointer(vm, body->entry_point));
                    break;
                case MVM_NATIVECALL_ARG_CPOINTER:
                    result = MVM_nativecall_make_cpointer(tc, res_type, dcCallPointer(vm, body->entry_point));
                    break;
                case MVM_NATIVECALL_ARG_CARRAY:
                    result = MVM_nativecall_make_carray(tc, res_type, dcCallPointer(vm, body->entry_point));
                    break;
                case MVM_NATIVECALL_ARG_CALLBACK:
                    /* TODO: A callback -return- value means that we have a C method
                    * that needs to be wrapped similarly to a is native(...) Perl 6
                    * sub. */
                    dcCallPointer(vm, body->entry_point);
                    result = res_type;
                default:
                    MVM_exception_throw_adhoc(tc, "Internal error: unhandled dyncall return type");
            }
        });
        });
    }

    /* Perform CArray/CStruct write barriers. */
    for (i = 0; i < num_args; i++)
        MVM_nativecall_refresh(tc, MVM_repr_at_pos_o(tc, args, i));

    /* Free any memory that we need to. */
    if (free_strs) {
        for (i = 0; i < num_strs; i++)
            MVM_free(free_strs[i]);
        MVM_free(free_strs);
    }

    /* Finally, free call VM. */
    dcFree(vm);

    return result;
}

static MVMObject * nativecall_cast(MVMThreadContext *tc, MVMObject *target_spec, MVMObject *target_type, void *cpointer_body) {
    MVMObject *result = NULL;

    MVMROOT(tc, target_spec, {
        MVMROOT(tc, target_type, {
            switch (REPR(target_type)->ID) {
                case MVM_REPR_ID_P6opaque: {
                    const MVMStorageSpec *ss = REPR(target_spec)->get_storage_spec(tc, STABLE(target_spec));
                    if(ss->can_box & MVM_STORAGE_SPEC_CAN_BOX_INT) {
                        MVMint64 value = 0;
                        switch(ss->bits) {
                            case 8:
                                value = *(MVMint8 *)cpointer_body;
                                break;
                            case 16:
                                value = *(MVMint16 *)cpointer_body;
                                break;
                            case 32:
                                value = *(MVMint32 *)cpointer_body;
                                break;
                            case 64:
                            default:
                                value = *(MVMint64 *)cpointer_body;
                        }

                        result = make_int_result(tc, target_type, value);
                    }
                    else if(ss->can_box & MVM_STORAGE_SPEC_CAN_BOX_NUM) {
                        MVMnum64 value;

                        switch(ss->bits) {
                            case 32:
                                value = *(MVMnum32 *)cpointer_body;
                                break;
                            case 64:
                            default:
                                value = *(MVMnum64 *)cpointer_body;
                        }

                        result = make_num_result(tc, target_type, value);
                    }
                    else if(ss->can_box & MVM_STORAGE_SPEC_CAN_BOX_STR) {
                        result = make_str_result(tc, target_type, MVM_NATIVECALL_ARG_UTF8STR,
                        (char *)cpointer_body);
                    }
                    break;
                }
                case MVM_REPR_ID_P6int: {
                    const MVMStorageSpec *ss = REPR(target_spec)->get_storage_spec(tc, STABLE(target_spec));
                    MVMint64 value;
                    switch(ss->bits) {
                        case 8:
                            value = *(MVMint8 *)cpointer_body;
                            break;
                        case 16:
                            value = *(MVMint16 *)cpointer_body;
                            break;
                        case 32:
                            value = *(MVMint32 *)cpointer_body;
                            break;
                        case 64:
                        default:
                            value = *(MVMint64 *)cpointer_body;
                    }
                    result = make_int_result(tc, target_type, value);
                    break;
                }
                case MVM_REPR_ID_P6num: {
                    const MVMStorageSpec *ss = REPR(target_spec)->get_storage_spec(tc, STABLE(target_spec));
                    MVMnum64 value;

                    switch(ss->bits) {
                        case 32:
                            value = *(MVMnum32 *)cpointer_body;
                            break;
                        case 64:
                        default:
                            value = *(MVMnum64 *)cpointer_body;
                    }

                    result = make_num_result(tc, target_type, value);
                    break;
                }
                case MVM_REPR_ID_MVMCStr:
                case MVM_REPR_ID_P6str:
                    result = make_str_result(tc, target_type, MVM_NATIVECALL_ARG_UTF8STR,
                        (char *)cpointer_body);
                    break;
                case MVM_REPR_ID_MVMCStruct:
                    result = MVM_nativecall_make_cstruct(tc, target_type, (void *)cpointer_body);
                    break;
                case MVM_REPR_ID_MVMCPointer:
                    result = MVM_nativecall_make_cpointer(tc, target_type, (void *)cpointer_body);
                    break;
                case MVM_REPR_ID_MVMCArray: {
                    result = MVM_nativecall_make_carray(tc, target_type, (void *)cpointer_body);
                    break;
                }
                default:
                    MVM_exception_throw_adhoc(tc, "Internal error: unhandled target type");
            }
        });
    });

    return result;
}

MVMObject * MVM_nativecall_global(MVMThreadContext *tc, MVMString *lib, MVMString *sym, MVMObject *target_spec, MVMObject *target_type) {
    char *lib_name = MVM_string_utf8_encode_C_string(tc, lib);
    char *sym_name = MVM_string_utf8_encode_C_string(tc, sym);
    DLLib *lib_handle;
    void *entry_point;
    MVMObject *ret = NULL;

    /* Try to load the library. */
    lib_handle = dlLoadLibrary(strlen(lib_name) ? lib_name : NULL);
    if (!lib_handle) {
        MVM_free(sym_name);
        MVM_exception_throw_adhoc(tc, "Cannot locate native library '%s'", lib_name);
    }

    /* Try to locate the symbol. */
    entry_point = dlFindSymbol(lib_handle, sym_name);
    if (!entry_point)
        MVM_exception_throw_adhoc(tc, "Cannot locate symbol '%s' in native library '%s'",
            sym_name, lib_name);
    MVM_free(sym_name);
    MVM_free(lib_name);

    if (REPR(target_type)->ID == MVM_REPR_ID_MVMCStr
    ||  REPR(target_type)->ID == MVM_REPR_ID_P6str
    || (REPR(target_type)->ID == MVM_REPR_ID_P6opaque
        && REPR(target_spec)->get_storage_spec(tc, STABLE(target_spec))->can_box & MVM_STORAGE_SPEC_CAN_BOX_STR)) {
        entry_point = *(void **)entry_point;
    }

    ret = nativecall_cast(tc, target_spec, target_type, entry_point);
    dlFreeLibrary(lib_handle);
    return ret;
}

MVMObject * MVM_nativecall_cast(MVMThreadContext *tc, MVMObject *target_spec, MVMObject *target_type, MVMObject *source) {
    void *data_body;

    if (!source)
        return target_type;

    if (REPR(source)->ID == MVM_REPR_ID_MVMCStruct) {
        data_body = unmarshal_cstruct(tc, source);
    } else if (REPR(source)->ID == MVM_REPR_ID_MVMCPointer) {
        data_body = unmarshal_cpointer(tc, source);
    } else if (REPR(source)->ID == MVM_REPR_ID_MVMCArray) {
        data_body = unmarshal_carray(tc, source);
    } else {
        MVM_exception_throw_adhoc(tc,
            "Native call expected return type with CPointer, CStruct, or CArray representation, but got a %s", REPR(source)->name);
    }
    return nativecall_cast(tc, target_spec, target_type, data_body);
}

MVMint64 MVM_nativecall_sizeof(MVMThreadContext *tc, MVMObject *obj) {
    if (REPR(obj)->ID == MVM_REPR_ID_MVMCStruct)
        return ((MVMCStructREPRData *)STABLE(obj)->REPR_data)->struct_size;
    else if (REPR(obj)->ID == MVM_REPR_ID_MVMCPPStruct)
        return ((MVMCPPStructREPRData *)STABLE(obj)->REPR_data)->struct_size;
    else if (REPR(obj)->ID == MVM_REPR_ID_P6int)
        return ((MVMP6intREPRData *)STABLE(obj)->REPR_data)->bits / 8;
    else if (REPR(obj)->ID == MVM_REPR_ID_P6num)
        return ((MVMP6numREPRData *)STABLE(obj)->REPR_data)->bits / 8;
    else if (REPR(obj)->ID == MVM_REPR_ID_MVMCPointer
          || REPR(obj)->ID == MVM_REPR_ID_MVMCArray
          || REPR(obj)->ID == MVM_REPR_ID_MVMCStr
          || REPR(obj)->ID == MVM_REPR_ID_P6str)
        return sizeof(void *);
    else
        MVM_exception_throw_adhoc(tc,
            "NativeCall op sizeof expected type with CPointer, CStruct, CArray, P6int or P6num representation, but got a %s",
            REPR(obj)->name);
}

/* Write-barriers a dyncall object so that delayed changes to the C-side of
 * objects are propagated to the HLL side. All CArray and CStruct arguments to
 * C functions are write-barriered automatically, so this should be necessary
 * only in the rarest of cases. */
void MVM_nativecall_refresh(MVMThreadContext *tc, MVMObject *cthingy) {
    if (!IS_CONCRETE(cthingy))
        return;
    if (REPR(cthingy)->ID == MVM_REPR_ID_MVMCArray) {
        MVMCArrayBody      *body      = (MVMCArrayBody *)OBJECT_BODY(cthingy);
        MVMCArrayREPRData  *repr_data = (MVMCArrayREPRData *)STABLE(cthingy)->REPR_data;
        void              **storage   = (void **) body->storage;
        MVMint64            i;

        /* No need to check for numbers. They're stored directly in the array. */
        if (repr_data->elem_kind == MVM_CARRAY_ELEM_KIND_NUMERIC)
            return;

        for (i = 0; i < body->elems; i++) {
            void *cptr;   /* The pointer in the C storage. */
            void *objptr; /* The pointer in the object representing the C object. */

            /* Ignore elements where we haven't generated an object. */
            if (!body->child_objs[i])
                continue;

            cptr = storage[i];
            if (IS_CONCRETE(body->child_objs[i])) {
                switch (repr_data->elem_kind) {
                    case MVM_CARRAY_ELEM_KIND_CARRAY:
                        objptr = ((MVMCArrayBody *)OBJECT_BODY(body->child_objs[i]))->storage;
                        break;
                    case MVM_CARRAY_ELEM_KIND_CPOINTER:
                        objptr = ((MVMCPointerBody *)OBJECT_BODY(body->child_objs[i]))->ptr;
                        break;
                    case MVM_CARRAY_ELEM_KIND_CSTRUCT:
                        objptr = ((MVMCStructBody *)OBJECT_BODY(body->child_objs[i]))->cstruct;
                        break;
                    case MVM_CARRAY_ELEM_KIND_STRING:
                        objptr = NULL; /* TODO */
                        break;
                    default:
                        MVM_exception_throw_adhoc(tc,
                            "Fatal error: bad elem_kind (%d) in CArray write barrier",
                            repr_data->elem_kind);
                }
            }
            else {
                objptr = NULL;
            }

            if (objptr != cptr)
                body->child_objs[i] = NULL;
            else
                MVM_nativecall_refresh(tc, body->child_objs[i]);
        }
    }
    else if (REPR(cthingy)->ID == MVM_REPR_ID_MVMCStruct) {
        MVMCStructBody     *body      = (MVMCStructBody *)OBJECT_BODY(cthingy);
        MVMCStructREPRData *repr_data = (MVMCStructREPRData *)STABLE(cthingy)->REPR_data;
        char               *storage   = (char *) body->cstruct;
        MVMint64            i;

        for (i = 0; i < repr_data->num_attributes; i++) {
            MVMint32 kind = repr_data->attribute_locations[i] & MVM_CSTRUCT_ATTR_MASK;
            MVMint32 slot = repr_data->attribute_locations[i] >> MVM_CSTRUCT_ATTR_SHIFT;
            void *cptr;   /* The pointer in the C storage. */
            void *objptr; /* The pointer in the object representing the C object. */

            if (kind == MVM_CSTRUCT_ATTR_IN_STRUCT || !body->child_objs[slot])
                continue;

            cptr = *((void **)(storage + repr_data->struct_offsets[i]));
            if (IS_CONCRETE(body->child_objs[slot])) {
                switch (kind) {
                    case MVM_CSTRUCT_ATTR_CARRAY:
                        objptr = ((MVMCArrayBody *)OBJECT_BODY(body->child_objs[slot]))->storage;
                        break;
                    case MVM_CSTRUCT_ATTR_CPTR:
                        objptr = ((MVMCPointerBody *)OBJECT_BODY(body->child_objs[slot]))->ptr;
                        break;
                    case MVM_CSTRUCT_ATTR_CSTRUCT:
                        objptr = (MVMCStructBody *)OBJECT_BODY(body->child_objs[slot]);
                        break;
                    case MVM_CSTRUCT_ATTR_STRING:
                        objptr = NULL;
                        break;
                    default:
                        MVM_exception_throw_adhoc(tc,
                            "Fatal error: bad kind (%d) in CStruct write barrier",
                            kind);
                }
            }
            else {
                objptr = NULL;
            }

            if (objptr != cptr)
                body->child_objs[slot] = NULL;
            else
                MVM_nativecall_refresh(tc, body->child_objs[slot]);
        }
    }
    else if (REPR(cthingy)->ID == MVM_REPR_ID_MVMCPPStruct) {
        MVMCPPStructBody     *body      = (MVMCPPStructBody *)OBJECT_BODY(cthingy);
        MVMCPPStructREPRData *repr_data = (MVMCPPStructREPRData *)STABLE(cthingy)->REPR_data;
        char                 *storage   = (char *) body->cppstruct;
        MVMint64              i;

        for (i = 0; i < repr_data->num_attributes; i++) {
            MVMint32 kind = repr_data->attribute_locations[i] & MVM_CPPSTRUCT_ATTR_MASK;
            MVMint32 slot = repr_data->attribute_locations[i] >> MVM_CPPSTRUCT_ATTR_SHIFT;
            void *cptr;   /* The pointer in the C storage. */
            void *objptr; /* The pointer in the object representing the C object. */

            if (kind == MVM_CPPSTRUCT_ATTR_IN_STRUCT || !body->child_objs[slot])
                continue;

            cptr = *((void **)(storage + repr_data->struct_offsets[i]));
            if (IS_CONCRETE(body->child_objs[slot])) {
                switch (kind) {
                    case MVM_CPPSTRUCT_ATTR_CARRAY:
                        objptr = ((MVMCArrayBody *)OBJECT_BODY(body->child_objs[slot]))->storage;
                        break;
                    case MVM_CPPSTRUCT_ATTR_CPTR:
                        objptr = ((MVMCPointerBody *)OBJECT_BODY(body->child_objs[slot]))->ptr;
                        break;
                    case MVM_CPPSTRUCT_ATTR_CSTRUCT:
                        objptr = (MVMCStructBody *)OBJECT_BODY(body->child_objs[slot]);
                        break;
                    case MVM_CPPSTRUCT_ATTR_STRING:
                        objptr = NULL;
                        break;
                    default:
                        MVM_exception_throw_adhoc(tc,
                            "Fatal error: bad kind (%d) in CPPStruct write barrier",
                            kind);
                }
            }
            else {
                objptr = NULL;
            }

            if (objptr != cptr)
                body->child_objs[slot] = NULL;
            else
                MVM_nativecall_refresh(tc, body->child_objs[slot]);
        }
    }
}
