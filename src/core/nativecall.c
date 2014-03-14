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
            free(ctypename);
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
    else if (strcmp(ctypename, "cpointer") == 0)
        result = MVM_NATIVECALL_ARG_CPOINTER;
    else if (strcmp(ctypename, "carray") == 0)
        result = MVM_NATIVECALL_ARG_CARRAY;
    else if (strcmp(ctypename, "callback") == 0)
        result = MVM_NATIVECALL_ARG_CALLBACK;
    else
        MVM_exception_throw_adhoc(tc, "Unknown type '%s' used for native call", ctypename);
    free(ctypename);
    return result;
}

/* Maps a calling convention name to an ID. */
static MVMint16 get_calling_convention(MVMThreadContext *tc, MVMString *name) {
    MVMint16 result = DC_CALL_C_DEFAULT;
    if (name && NUM_GRAPHS(name) > 0) {
        char *cname = MVM_string_utf8_encode_C_string(tc, name);
        if (strcmp(cname, "cdecl") == 0)
            result = DC_CALL_C_X86_CDECL;
        else if (strcmp(cname, "stdcall") == 0)
            result = DC_CALL_C_X86_WIN32_STD;
        else if (strcmp(cname, "stdcall") == 0)
            result = DC_CALL_C_X64_WIN64;
        else
            MVM_exception_throw_adhoc(tc,
                "Unknown calling convention '%s' used for native call", cname);
        free(cname);
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
        case MVM_NATIVECALL_ARG_CALLBACK:
            return 'p';
        default:
            return '\0';
    }
}

MVMObject * make_int_result(MVMThreadContext *tc, MVMObject *type, MVMint64 value) {
    return type ? MVM_repr_box_int(tc, type, value) : NULL;
}

MVMObject * make_num_result(MVMThreadContext *tc, MVMObject *type, MVMnum64 value) {
    return type ? MVM_repr_box_num(tc, type, value) : NULL;
}

MVMObject * make_str_result(MVMThreadContext *tc, MVMObject *type, MVMint16 ret_type, char *cstring) {
    MVMObject *result = type;
    if (cstring && type) {
        MVMString *value;
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
        result = MVM_repr_box_str(tc, type, value);
        if (ret_type & MVM_NATIVECALL_ARG_FREE_STR)
            free(cstring);
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
                "Native call expected return type with CStruct representation, but got something else");
        result = REPR(type)->allocate(tc, STABLE(type));
        ((MVMCStruct *)result)->body.cstruct = cstruct;
        if (repr_data->num_child_objs)
            ((MVMCStruct *)result)->body.child_objs = calloc(repr_data->num_child_objs, sizeof(MVMObject *));
    }
    return result;
}

/* Constructs a boxed result using a CPointer REPR type. */
MVMObject * MVM_nativecall_make_cpointer(MVMThreadContext *tc, MVMObject *type, void *ptr) {
    MVMObject *result = type;
    if (ptr && type) {
        if (REPR(type)->ID != MVM_REPR_ID_MVMCPointer)
            MVM_exception_throw_adhoc(tc,
                "Native call expected return type with CPointer representation, but got something else");
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
                "Native call expected return type with CArray representation, but got something else");
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
        if (REPR(value)->ID == MVM_REPR_ID_MVMCStr)
            *free = 0; /* Manually managed. */
        else if (free && type & MVM_NATIVECALL_ARG_FREE_STR_MASK)
            *free = 1;
        else
            *free = 0;

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
            "Native call expected object with CStruct representation, but got something else");
}

static void * unmarshal_cpointer(MVMThreadContext *tc, MVMObject *value) {
    if (!IS_CONCRETE(value))
        return NULL;
    else if (REPR(value)->ID == MVM_REPR_ID_MVMCPointer)
        return ((MVMCPointer *)value)->body.ptr;
    else
        MVM_exception_throw_adhoc(tc,
            "Native call expected object with CPointer representation, but got something else");
}

static void * unmarshal_carray(MVMThreadContext *tc, MVMObject *value) {
    if (!IS_CONCRETE(value))
        return NULL;
    else if (REPR(value)->ID == MVM_REPR_ID_MVMCArray)
        return ((MVMCArray *)value)->body.storage;
    else
        MVM_exception_throw_adhoc(tc,
            "Native call expected object with CArray representation, but got something else");
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
        free(sym_name);
        MVM_exception_throw_adhoc(tc, "Cannot locate native library '%s'", lib_name);
    }

    /* Try to locate the symbol. */
    body->entry_point = dlFindSymbol(body->lib_handle, sym_name);
    if (!body->entry_point)
        MVM_exception_throw_adhoc(tc, "Cannot locate symbol '%s' in native library '%s'",
            sym_name, lib_name);
    free(sym_name);

    /* Set calling convention, if any. */
    body->convention = get_calling_convention(tc, conv);

    /* Transform each of the args info structures into a flag. */
    body->num_args  = MVM_repr_elems(tc, arg_info);
    body->arg_types = malloc(sizeof(MVMint16) * (body->num_args ? body->num_args : 1));
    body->arg_info  = malloc(sizeof(MVMObject *) * (body->num_args ? body->num_args : 1));
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

    /* Create and set up call VM. */
    DCCallVM *vm = dcNewCallVM(8192);
    dcMode(vm, body->convention);

    /* Process arguments. */
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
                            free_strs = (char**)malloc(num_args * sizeof(char *));
                        free_strs[num_strs] = str;
                        num_strs++;
                    }
                    dcArgPointer(vm, str);
                }
                break;
            case MVM_NATIVECALL_ARG_CSTRUCT:
                dcArgPointer(vm, unmarshal_cstruct(tc, value));
                break;
            case MVM_NATIVECALL_ARG_CPOINTER:
                dcArgPointer(vm, unmarshal_cpointer(tc, value));
                break;
            case MVM_NATIVECALL_ARG_CARRAY:
                dcArgPointer(vm, unmarshal_carray(tc, value));
                break;
            case MVM_NATIVECALL_ARG_CALLBACK:
                MVM_exception_throw_adhoc(tc, "passing callback NYI");
                break;
            default:
                MVM_exception_throw_adhoc(tc, "Internal error: unhandled dyncall argument type");
        }
    }

    /* Call and process return values. */
    MVMROOT(tc, args, {
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
            case MVM_NATIVECALL_ARG_CPOINTER:
                result = MVM_nativecall_make_cpointer(tc, res_type, dcCallPointer(vm, body->entry_point));
                break;
            case MVM_NATIVECALL_ARG_CARRAY:
                result = MVM_nativecall_make_carray(tc, res_type, dcCallPointer(vm, body->entry_point));
                break;
            /* XXX Port the rest. */
            default:
                MVM_exception_throw_adhoc(tc, "Internal error: unhandled dyncall return type");
        }
    });

    /* Perform CArray/CStruct write barriers. */
    for (i = 0; i < num_args; i++)
        MVM_nativecall_refresh(tc, MVM_repr_at_pos_o(tc, args, i));

    /* Free any memory that we need to. */
    if (free_strs) {
        for (i = 0; i < num_strs; i++)
            free(free_strs[i]);
        free(free_strs);
    }

    /* Finally, free call VM. */
    dcFree(vm);

    return result;
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
}
