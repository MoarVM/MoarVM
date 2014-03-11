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

MVMObject * MVM_nativecall_invoke(MVMThreadContext *tc, MVMObject *ret_type,
        MVMObject *site, MVMObject *args) {
    MVM_exception_throw_adhoc(tc, "nativecallinvoke NYI");
}

void MVM_nativecall_refresh(MVMThreadContext *tc, MVMObject *cthingy) {
    MVM_exception_throw_adhoc(tc, "nativecallrefresh NYI");
}
