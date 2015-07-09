#include "moar.h"
#ifndef _WIN32
#include <dlfcn.h>
#endif

/* Grabs a NativeCall body. */
MVMNativeCallBody * MVM_nativecall_get_nc_body(MVMThreadContext *tc, MVMObject *obj) {
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

/* Gets the flag for whether to free a string after a call or not. */
static MVMint16 get_rw_flag(MVMThreadContext *tc, MVMObject *info) {
    MVMString *flag = tc->instance->str_consts.rw;
    if (MVM_repr_exists_key(tc, info, flag)) {
        if (MVM_repr_get_int(tc, MVM_repr_at_key_o(tc, info, flag)))
            return MVM_NATIVECALL_ARG_RW;
    }
    return MVM_NATIVECALL_ARG_NO_RW;
}

/* Takes a hash describing a type hands back an argument type code. */
MVMint16 MVM_nativecall_get_arg_type(MVMThreadContext *tc, MVMObject *info, MVMint16 is_return) {
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
        result = MVM_NATIVECALL_ARG_CHAR | get_rw_flag(tc, info);
    else if (strcmp(ctypename, "short") == 0)
        result = MVM_NATIVECALL_ARG_SHORT | get_rw_flag(tc, info);
    else if (strcmp(ctypename, "int") == 0)
        result = MVM_NATIVECALL_ARG_INT | get_rw_flag(tc, info);
    else if (strcmp(ctypename, "long") == 0)
        result = MVM_NATIVECALL_ARG_LONG | get_rw_flag(tc, info);
    else if (strcmp(ctypename, "longlong") == 0)
        result = MVM_NATIVECALL_ARG_LONGLONG | get_rw_flag(tc, info);
    else if (strcmp(ctypename, "uchar") == 0)
        result = MVM_NATIVECALL_ARG_UCHAR | get_rw_flag(tc, info);
    else if (strcmp(ctypename, "ushort") == 0)
        result = MVM_NATIVECALL_ARG_USHORT | get_rw_flag(tc, info);
    else if (strcmp(ctypename, "uint") == 0)
        result = MVM_NATIVECALL_ARG_UINT | get_rw_flag(tc, info);
    else if (strcmp(ctypename, "ulong") == 0)
        result = MVM_NATIVECALL_ARG_ULONG | get_rw_flag(tc, info);
    else if (strcmp(ctypename, "ulonglong") == 0)
        result = MVM_NATIVECALL_ARG_ULONGLONG | get_rw_flag(tc, info);
    else if (strcmp(ctypename, "float") == 0)
        result = MVM_NATIVECALL_ARG_FLOAT | get_rw_flag(tc, info);
    else if (strcmp(ctypename, "double") == 0)
        result = MVM_NATIVECALL_ARG_DOUBLE | get_rw_flag(tc, info);
    else if (strcmp(ctypename, "asciistr") == 0)
        result = MVM_NATIVECALL_ARG_ASCIISTR | get_str_free_flag(tc, info);
    else if (strcmp(ctypename, "utf8str") == 0)
        result = MVM_NATIVECALL_ARG_UTF8STR | get_str_free_flag(tc, info);
    else if (strcmp(ctypename, "utf16str") == 0)
        result = MVM_NATIVECALL_ARG_UTF16STR | get_str_free_flag(tc, info);
    else if (strcmp(ctypename, "cstruct") == 0)
        result = MVM_NATIVECALL_ARG_CSTRUCT;
    else if (strcmp(ctypename, "cpointer") == 0)
        result = MVM_NATIVECALL_ARG_CPOINTER;
    else if (strcmp(ctypename, "carray") == 0)
        result = MVM_NATIVECALL_ARG_CARRAY;
    else if (strcmp(ctypename, "cunion") == 0)
        result = MVM_NATIVECALL_ARG_CUNION;
    else if (strcmp(ctypename, "vmarray") == 0)
        result = MVM_NATIVECALL_ARG_VMARRAY;
    else if (strcmp(ctypename, "callback") == 0)
        result = MVM_NATIVECALL_ARG_CALLBACK;
    else {
        char *waste[] = { ctypename, NULL };
        MVM_exception_throw_adhoc_free(tc, waste, "Unknown type '%s' used for native call", ctypename);
    }
    MVM_free(ctypename);
    return result;
}

MVMObject * MVM_nativecall_make_int(MVMThreadContext *tc, MVMObject *type, MVMint64 value) {
    return type ? MVM_repr_box_int(tc, type, value) : NULL;
}

MVMObject * MVM_nativecall_make_uint(MVMThreadContext *tc, MVMObject *type, MVMuint64 value) {
    return type ? MVM_repr_box_int(tc, type, (MVMint64)value) : NULL;
}

MVMObject * MVM_nativecall_make_num(MVMThreadContext *tc, MVMObject *type, MVMnum64 value) {
    return type ? MVM_repr_box_num(tc, type, value) : NULL;
}

MVMObject * MVM_nativecall_make_str(MVMThreadContext *tc, MVMObject *type, MVMint16 ret_type, char *cstring) {
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

/* Constructs a boxed result using a CUnion REPR type. */
MVMObject * MVM_nativecall_make_cunion(MVMThreadContext *tc, MVMObject *type, void *cunion) {
    MVMObject *result = type;
    if (cunion && type) {
        MVMCUnionREPRData *repr_data = (MVMCUnionREPRData *)STABLE(type)->REPR_data;
        if (REPR(type)->ID != MVM_REPR_ID_MVMCUnion)
            MVM_exception_throw_adhoc(tc,
                "Native call expected return type with CUnion representation, but got a %s", REPR(type)->name);
        result = REPR(type)->allocate(tc, STABLE(type));
        ((MVMCUnion *)result)->body.cunion = cunion;
        if (repr_data->num_child_objs)
            ((MVMCUnion *)result)->body.child_objs = MVM_calloc(repr_data->num_child_objs, sizeof(MVMObject *));
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

signed char MVM_nativecall_unmarshal_char(MVMThreadContext *tc, MVMObject *value) {
    return (signed char)MVM_repr_get_int(tc, value);
}

signed short MVM_nativecall_unmarshal_short(MVMThreadContext *tc, MVMObject *value) {
    return (signed short)MVM_repr_get_int(tc, value);
}

signed int MVM_nativecall_unmarshal_int(MVMThreadContext *tc, MVMObject *value) {
    return (signed int)MVM_repr_get_int(tc, value);
}

signed long MVM_nativecall_unmarshal_long(MVMThreadContext *tc, MVMObject *value) {
    return (signed long)MVM_repr_get_int(tc, value);
}

signed long long MVM_nativecall_unmarshal_longlong(MVMThreadContext *tc, MVMObject *value) {
    return (signed long long)MVM_repr_get_int(tc, value);
}

unsigned char MVM_nativecall_unmarshal_uchar(MVMThreadContext *tc, MVMObject *value) {
    return (unsigned char)MVM_repr_get_int(tc, value);
}

unsigned short MVM_nativecall_unmarshal_ushort(MVMThreadContext *tc, MVMObject *value) {
    return (unsigned short)MVM_repr_get_int(tc, value);
}

unsigned int MVM_nativecall_unmarshal_uint(MVMThreadContext *tc, MVMObject *value) {
    return (unsigned int)MVM_repr_get_int(tc, value);
}

unsigned long MVM_nativecall_unmarshal_ulong(MVMThreadContext *tc, MVMObject *value) {
    return (unsigned long)MVM_repr_get_int(tc, value);
}

unsigned long long MVM_nativecall_unmarshal_ulonglong(MVMThreadContext *tc, MVMObject *value) {
    return (unsigned long long)MVM_repr_get_int(tc, value);
}

float MVM_nativecall_unmarshal_float(MVMThreadContext *tc, MVMObject *value) {
    return (float)MVM_repr_get_num(tc, value);
}

double MVM_nativecall_unmarshal_double(MVMThreadContext *tc, MVMObject *value) {
    return (double)MVM_repr_get_num(tc, value);
}

char * MVM_nativecall_unmarshal_string(MVMThreadContext *tc, MVMObject *value, MVMint16 type, MVMint16 *free) {
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

void * MVM_nativecall_unmarshal_cstruct(MVMThreadContext *tc, MVMObject *value) {
    if (!IS_CONCRETE(value))
        return NULL;
    else if (REPR(value)->ID == MVM_REPR_ID_MVMCStruct)
        return ((MVMCStruct *)value)->body.cstruct;
    else
        MVM_exception_throw_adhoc(tc,
            "Native call expected return type with CStruct representation, but got a %s", REPR(value)->name);
}

void * MVM_nativecall_unmarshal_cpointer(MVMThreadContext *tc, MVMObject *value) {
    if (!IS_CONCRETE(value))
        return NULL;
    else if (REPR(value)->ID == MVM_REPR_ID_MVMCPointer)
        return ((MVMCPointer *)value)->body.ptr;
    else
        MVM_exception_throw_adhoc(tc,
            "Native call expected return type with CPointer representation, but got a %s", REPR(value)->name);
}

void * MVM_nativecall_unmarshal_carray(MVMThreadContext *tc, MVMObject *value) {
    if (!IS_CONCRETE(value))
        return NULL;
    else if (REPR(value)->ID == MVM_REPR_ID_MVMCArray)
        return ((MVMCArray *)value)->body.storage;
    else
        MVM_exception_throw_adhoc(tc,
            "Native call expected return type with CArray representation, but got a %s", REPR(value)->name);
}

void * MVM_nativecall_unmarshal_vmarray(MVMThreadContext *tc, MVMObject *value) {
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

void * MVM_nativecall_unmarshal_cunion(MVMThreadContext *tc, MVMObject *value) {
    if (!IS_CONCRETE(value))
        return NULL;
    else if (REPR(value)->ID == MVM_REPR_ID_MVMCUnion)
        return ((MVMCUnion *)value)->body.cunion;
    else
        MVM_exception_throw_adhoc(tc,
            "Native call expected return type with CUnion representation, but got a %s", REPR(value)->name);
}

#ifdef _WIN32
static const char *dlerror(void)
{
    static char buf[32];
    DWORD dw = GetLastError();
    if (dw == 0)
        return NULL;
    sprintf(buf, "error 0x%x", (unsigned int)dw);
    return buf;
}
#endif

/* Builds up a native call site out of the supplied arguments. */
void MVM_nativecall_build(MVMThreadContext *tc, MVMObject *site, MVMString *lib,
        MVMString *sym, MVMString *conv, MVMObject *arg_info, MVMObject *ret_info) {
    char *lib_name = MVM_string_utf8_encode_C_string(tc, lib);
    char *sym_name = MVM_string_utf8_encode_C_string(tc, sym);
    MVMint16 i;

    /* Initialize the object; grab native call part of its body. */
    MVMNativeCallBody *body = MVM_nativecall_get_nc_body(tc, site);

    /* Try to load the library. */
    body->lib_name = lib_name;
    body->lib_handle = MVM_nativecall_load_lib(strlen(lib_name) ? lib_name : NULL);

    if (!body->lib_handle) {
        MVM_free(sym_name);
        char *waste[] = { lib_name, NULL };
        MVM_exception_throw_adhoc_free(tc, waste, "Cannot locate native library '%s': %s", lib_name, dlerror());
    }

    /* Try to locate the symbol. */
    body->entry_point = MVM_nativecall_find_sym(body->lib_handle, sym_name);
    if (!body->entry_point) {
        char *waste[] = { sym_name, lib_name, NULL };
        MVM_exception_throw_adhoc_free(tc, waste, "Cannot locate symbol '%s' in native library '%s'",
            sym_name, lib_name);
    }
    MVM_free(sym_name);

    /* Set calling convention, if any. */
    body->convention = MVM_nativecall_get_calling_convention(tc, conv);

    /* Transform each of the args info structures into a flag. */
    body->num_args  = MVM_repr_elems(tc, arg_info);
    body->arg_types = MVM_malloc(sizeof(MVMint16) * (body->num_args ? body->num_args : 1));
    body->arg_info  = MVM_malloc(sizeof(MVMObject *) * (body->num_args ? body->num_args : 1));
#ifdef HAVE_LIBFFI
    body->ffi_arg_types = MVM_malloc(sizeof(ffi_type *) * (body->num_args ? body->num_args : 1));
#endif
    for (i = 0; i < body->num_args; i++) {
        MVMObject *info = MVM_repr_at_pos_o(tc, arg_info, i);
        body->arg_types[i] = MVM_nativecall_get_arg_type(tc, info, 0);
#ifdef HAVE_LIBFFI
        body->ffi_arg_types[i] = MVM_nativecall_get_ffi_type(tc, body->arg_types[i]);
#endif
        if(body->arg_types[i] == MVM_NATIVECALL_ARG_CALLBACK) {
            MVM_ASSIGN_REF(tc, &(site->header), body->arg_info[i],
                MVM_repr_at_key_o(tc, info, tc->instance->str_consts.callback_args));
        }
        else {
            body->arg_info[i]  = NULL;
        }
    }

    /* Transform return argument type info a flag. */
    body->ret_type     = MVM_nativecall_get_arg_type(tc, ret_info, 1);
#ifdef HAVE_LIBFFI
    body->ffi_ret_type = MVM_nativecall_get_ffi_type(tc, body->ret_type);
#endif
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

                        result = MVM_nativecall_make_int(tc, target_type, value);
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

                        result = MVM_nativecall_make_num(tc, target_type, value);
                    }
                    else if(ss->can_box & MVM_STORAGE_SPEC_CAN_BOX_STR) {
                        result = MVM_nativecall_make_str(tc, target_type, MVM_NATIVECALL_ARG_UTF8STR,
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
                    result = MVM_nativecall_make_int(tc, target_type, value);
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

                    result = MVM_nativecall_make_num(tc, target_type, value);
                    break;
                }
                case MVM_REPR_ID_MVMCStr:
                case MVM_REPR_ID_P6str:
                    result = MVM_nativecall_make_str(tc, target_type, MVM_NATIVECALL_ARG_UTF8STR,
                        (char *)cpointer_body);
                    break;
                case MVM_REPR_ID_MVMCStruct:
                    result = MVM_nativecall_make_cstruct(tc, target_type, (void *)cpointer_body);
                    break;
                case MVM_REPR_ID_MVMCUnion:
                    result = MVM_nativecall_make_cunion(tc, target_type, (void *)cpointer_body);
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
    lib_handle = MVM_nativecall_load_lib(strlen(lib_name) ? lib_name : NULL);
    if (!lib_handle) {
        MVM_free(sym_name);
        char *waste[] = { lib_name, NULL };
        MVM_exception_throw_adhoc_free(tc, waste, "Cannot locate native library '%s': %s", lib_name, dlerror());
    }

    /* Try to locate the symbol. */
    entry_point = MVM_nativecall_find_sym(lib_handle, sym_name);
    if (!entry_point) {
        char *waste[] = { sym_name, lib_name, NULL };
        MVM_exception_throw_adhoc_free(tc, waste, "Cannot locate symbol '%s' in native library '%s'",
            sym_name, lib_name);
    }
    MVM_free(sym_name);
    MVM_free(lib_name);

    if (REPR(target_type)->ID == MVM_REPR_ID_MVMCStr
    ||  REPR(target_type)->ID == MVM_REPR_ID_P6str
    || (REPR(target_type)->ID == MVM_REPR_ID_P6opaque
        && REPR(target_spec)->get_storage_spec(tc, STABLE(target_spec))->can_box & MVM_STORAGE_SPEC_CAN_BOX_STR)) {
        entry_point = *(void **)entry_point;
    }

    ret = nativecall_cast(tc, target_spec, target_type, entry_point);
    MVM_nativecall_free_lib(lib_handle);
    return ret;
}

MVMObject * MVM_nativecall_cast(MVMThreadContext *tc, MVMObject *target_spec, MVMObject *target_type, MVMObject *source) {
    void *data_body;

    if (!source)
        return target_type;

    if (REPR(source)->ID == MVM_REPR_ID_MVMCStruct)
        data_body = MVM_nativecall_unmarshal_cstruct(tc, source);
    else if (REPR(source)->ID == MVM_REPR_ID_MVMCUnion)
        data_body = MVM_nativecall_unmarshal_cunion(tc, source);
    else if (REPR(source)->ID == MVM_REPR_ID_MVMCPointer)
        data_body = MVM_nativecall_unmarshal_cpointer(tc, source);
    else if (REPR(source)->ID == MVM_REPR_ID_MVMCArray)
        data_body = MVM_nativecall_unmarshal_carray(tc, source);
    else if (REPR(source)->ID == MVM_REPR_ID_MVMArray)
        data_body = MVM_nativecall_unmarshal_vmarray(tc, source);
    else
        MVM_exception_throw_adhoc(tc,
            "Native call expected return type with CPointer, CStruct, CArray, or VMArray representation, but got a %s", REPR(source)->name);
    return nativecall_cast(tc, target_spec, target_type, data_body);
}

MVMint64 MVM_nativecall_sizeof(MVMThreadContext *tc, MVMObject *obj) {
    if (REPR(obj)->ID == MVM_REPR_ID_MVMCStruct)
        return ((MVMCStructREPRData *)STABLE(obj)->REPR_data)->struct_size;
    else if (REPR(obj)->ID == MVM_REPR_ID_MVMCUnion)
        return ((MVMCUnionREPRData *)STABLE(obj)->REPR_data)->struct_size;
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
                    case MVM_CARRAY_ELEM_KIND_CUNION:
                        objptr = ((MVMCUnionBody *)OBJECT_BODY(body->child_objs[i]))->cunion;
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
                    case MVM_CSTRUCT_ATTR_CUNION:
                        objptr = (MVMCUnionBody *)OBJECT_BODY(body->child_objs[slot]);
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
}
