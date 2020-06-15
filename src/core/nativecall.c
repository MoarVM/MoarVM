#include "moar.h"
#ifndef _WIN32
#include <dlfcn.h>
#endif
#include <platform/time.h>

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

/* Gets the flag for whether an arg is rw or not. */
static MVMint16 get_rw_flag(MVMThreadContext *tc, MVMObject *info) {
    MVMString *flag = tc->instance->str_consts.rw;
    if (MVM_repr_exists_key(tc, info, flag)) {
        if (MVM_repr_get_int(tc, MVM_repr_at_key_o(tc, info, flag)))
            return MVM_NATIVECALL_ARG_RW;
    }
    return MVM_NATIVECALL_ARG_NO_RW;
}

/* Gets the flag for whether an arg is rw or not. */
static MVMint16 get_refresh_flag(MVMThreadContext *tc, MVMObject *info) {
    MVMString *typeobj_str = tc->instance->str_consts.typeobj;
    if (MVM_repr_exists_key(tc, info, typeobj_str)) {
        MVMObject *typeobj = MVM_repr_at_key_o(tc, info, typeobj_str);

        if (REPR(typeobj)->ID == MVM_REPR_ID_MVMCArray) {
            MVMCArrayREPRData  *repr_data = (MVMCArrayREPRData *)STABLE(typeobj)->REPR_data;

            /* No need to refresh numbers. They're stored directly in the array. */
            if (repr_data->elem_kind == MVM_CARRAY_ELEM_KIND_NUMERIC)
                return MVM_NATIVECALL_ARG_NO_REFRESH;

            return MVM_NATIVECALL_ARG_REFRESH;
        }
    }

    /* We don't know, so fail safe by assuming we have to refresh */
    return MVM_NATIVECALL_ARG_REFRESH;
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
    else if (strcmp(ctypename, "cppstruct") == 0)
        result = MVM_NATIVECALL_ARG_CPPSTRUCT;
    else if (strcmp(ctypename, "cpointer") == 0)
        result = MVM_NATIVECALL_ARG_CPOINTER | get_rw_flag(tc, info);
    else if (strcmp(ctypename, "carray") == 0)
        result = MVM_NATIVECALL_ARG_CARRAY | get_refresh_flag(tc, info);
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
                "Native call expected return type with CStruct representation, but got a %s (%s)", REPR(type)->name, MVM_6model_get_debug_name(tc, type));
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
                "Native call expected return type with CUnion representation, but got a %s (%s)", REPR(type)->name, MVM_6model_get_debug_name(tc, type));
        result = REPR(type)->allocate(tc, STABLE(type));
        ((MVMCUnion *)result)->body.cunion = cunion;
        if (repr_data->num_child_objs)
            ((MVMCUnion *)result)->body.child_objs = MVM_calloc(repr_data->num_child_objs, sizeof(MVMObject *));
    }
    return result;
}

MVMObject * MVM_nativecall_make_cppstruct(MVMThreadContext *tc, MVMObject *type, void *cppstruct) {
    MVMObject *result = type;
    if (cppstruct && type) {
        MVMCPPStructREPRData *repr_data = (MVMCPPStructREPRData *)STABLE(type)->REPR_data;
        if (REPR(type)->ID != MVM_REPR_ID_MVMCPPStruct)
            MVM_exception_throw_adhoc(tc,
                "Native call expected return type with CPPStruct representation, but got a %s (%s)", REPR(type)->name, MVM_6model_get_debug_name(tc, type));
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
                "Native call expected return type with CPointer representation, but got a %s (%s)", REPR(type)->name, MVM_6model_get_debug_name(tc, type));
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
                "Native call expected return type with CArray representation, but got a %s (%s)", REPR(type)->name, MVM_6model_get_debug_name(tc, type));
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

char * MVM_nativecall_unmarshal_string(MVMThreadContext *tc, MVMObject *value, MVMint16 type, MVMint16 *free, MVMint16 unmarshal_kind) {
    if (IS_CONCRETE(value)) {
        MVMString *value_str = MVM_repr_get_str(tc, value);

        /* Encode string. */
        char *str;
        switch (type & MVM_NATIVECALL_ARG_TYPE_MASK) {
            case MVM_NATIVECALL_ARG_ASCIISTR:
                str = MVM_string_ascii_encode_any(tc, value_str);
                break;
            case MVM_NATIVECALL_ARG_UTF16STR:
                str = MVM_string_utf16_encode(tc, value_str, 0);
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

MVM_NO_RETURN static void unmarshal_error(MVMThreadContext *tc, char *desired_repr, MVMObject *value, MVMint16 unmarshal_kind) MVM_NO_RETURN_ATTRIBUTE;
MVM_NO_RETURN static void unmarshal_error(MVMThreadContext *tc, char *desired_repr, MVMObject *value, MVMint16 unmarshal_kind) {
    if (unmarshal_kind == MVM_NATIVECALL_UNMARSHAL_KIND_GENERIC) {
        MVM_exception_throw_adhoc(tc,
            "NativeCall conversion expected type with %s representation, but got a %s (%s)", desired_repr, REPR(value)->name, MVM_6model_get_debug_name(tc, value));
    }
    else if (unmarshal_kind == MVM_NATIVECALL_UNMARSHAL_KIND_RETURN) {
        MVM_exception_throw_adhoc(tc,
            "Expected return value with %s representation, but got a %s (%s)", desired_repr, REPR(value)->name, MVM_6model_get_debug_name(tc, value));
    }
    else if (unmarshal_kind == MVM_NATIVECALL_UNMARSHAL_KIND_NATIVECAST) {
        MVM_exception_throw_adhoc(tc,
            "NativeCast expected value with %s representation, but got a %s (%s)", desired_repr, REPR(value)->name, MVM_6model_get_debug_name(tc, value));
    }
    else {
        MVM_exception_throw_adhoc(tc,
            "Native call expected argument %d with %s representation, but got a %s (%s)", unmarshal_kind + 1, desired_repr, REPR(value)->name, MVM_6model_get_debug_name(tc, value));
    }
}

void * MVM_nativecall_unmarshal_cstruct(MVMThreadContext *tc, MVMObject *value, MVMint16 unmarshal_kind) {
    if (!IS_CONCRETE(value))
        return NULL;
    else if (REPR(value)->ID == MVM_REPR_ID_MVMCStruct)
        return ((MVMCStruct *)value)->body.cstruct;
    else
        unmarshal_error(tc, "CStruct", value, unmarshal_kind);
}

void * MVM_nativecall_unmarshal_cppstruct(MVMThreadContext *tc, MVMObject *value, MVMint16 unmarshal_kind) {
    if (!IS_CONCRETE(value))
        return NULL;
    else if (REPR(value)->ID == MVM_REPR_ID_MVMCPPStruct)
        return ((MVMCPPStruct *)value)->body.cppstruct;
    else
        unmarshal_error(tc, "CPPStruct", value, unmarshal_kind);
}

void * MVM_nativecall_unmarshal_cpointer(MVMThreadContext *tc, MVMObject *value, MVMint16 unmarshal_kind) {
    if (!IS_CONCRETE(value))
        return NULL;
    else if (REPR(value)->ID == MVM_REPR_ID_MVMCPointer)
        return ((MVMCPointer *)value)->body.ptr;
    else
        unmarshal_error(tc, "CPointer", value, unmarshal_kind);
}

void * MVM_nativecall_unmarshal_carray(MVMThreadContext *tc, MVMObject *value, MVMint16 unmarshal_kind) {
    if (!IS_CONCRETE(value))
        return NULL;
    else if (REPR(value)->ID == MVM_REPR_ID_MVMCArray)
        return ((MVMCArray *)value)->body.storage;
    else
        unmarshal_error(tc, "CArray", value, unmarshal_kind);
}

void * MVM_nativecall_unmarshal_vmarray(MVMThreadContext *tc, MVMObject *value, MVMint16 unmarshal_kind) {
    if (!IS_CONCRETE(value))
        return NULL;
    else if (REPR(value)->ID == MVM_REPR_ID_VMArray) {
        MVMArrayBody *body          = &((MVMArray *)value)->body;
        MVMArrayREPRData *repr_data = (MVMArrayREPRData *)STABLE(value)->REPR_data;
        size_t start_pos            = body->start * repr_data->elem_size;
        return ((char *)body->slots.any) + start_pos;
    }
    else
        unmarshal_error(tc, "VMArray", value, unmarshal_kind);
}

void * MVM_nativecall_unmarshal_cunion(MVMThreadContext *tc, MVMObject *value, MVMint16 unmarshal_kind) {
    if (!IS_CONCRETE(value))
        return NULL;
    else if (REPR(value)->ID == MVM_REPR_ID_MVMCUnion)
        return ((MVMCUnion *)value)->body.cunion;
    else
        unmarshal_error(tc, "CUnion", value, unmarshal_kind);
}

#ifdef _WIN32
static const char *dlerror(void)
{
    static char buf[32];
    DWORD dw = GetLastError();
    if (dw == 0)
        return NULL;
    snprintf(buf, 32, "error 0x%"PRIx32"", (MVMuint32)dw);
    return buf;
}
#endif

void init_c_call_node(MVMThreadContext *tc, MVMSpeshGraph *sg, MVMJitNode *node, void *func_ptr, MVMint16 num_args, MVMJitCallArg *args) {
    node->type = MVM_JIT_NODE_CALL_C;
    node->u.call.func_ptr = func_ptr;
    if (0 < num_args) {
        node->u.call.args = MVM_spesh_alloc(tc, sg, num_args * sizeof(MVMJitCallArg));
        memcpy(node->u.call.args, args, num_args * sizeof(MVMJitCallArg));
    }
    else {
        node->u.call.args = NULL;
    }
    node->u.call.num_args = num_args;
    node->u.call.rv_mode = MVM_JIT_RV_VOID;
    node->u.call.rv_idx  = -1;
}

void save_rv_to_stack(MVMThreadContext *tc, MVMJitNode *node, MVMint32 storage_pos) {
    node->u.call.rv_mode = MVM_JIT_RV_TO_STACK;
    node->u.call.rv_idx = storage_pos;
}

void init_box_call_node(MVMThreadContext *tc, MVMSpeshGraph *sg, MVMJitNode *box_rv_node, void *func_ptr, MVMint16 restype, MVMint16 dst) {
    MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR , { MVM_JIT_INTERP_TC } },
                             { MVM_JIT_REG_DYNIDX, { 2 } },
                             { MVM_JIT_STACK_VALUE, { 0 } }};
    init_c_call_node(tc, sg, box_rv_node, func_ptr, 3, args);
    box_rv_node->next = NULL;
    if (dst == -1) {
        box_rv_node->u.call.rv_mode = MVM_JIT_RV_DYNIDX;
        box_rv_node->u.call.rv_idx = 0;
    }
    else {
        box_rv_node->u.call.args[1].type = MVM_JIT_REG_VAL;
        box_rv_node->u.call.args[1].v.reg = restype;

        box_rv_node->u.call.rv_mode = MVM_JIT_RV_PTR;
        box_rv_node->u.call.rv_idx = dst;
    }
}

MVMJitGraph *MVM_nativecall_jit_graph_for_caller_code(
    MVMThreadContext   *tc,
    MVMSpeshGraph      *sg,
    MVMNativeCallBody  *body,
    MVMint16            restype,
    MVMint16            dst,
    MVMSpeshIns       **arg_ins
) {
    MVMJitGraph *jg = MVM_spesh_alloc(tc, sg, sizeof(MVMJitGraph)); /* will actually calloc */
    MVMJitNode *block_gc_node = MVM_spesh_alloc(tc, sg, sizeof(MVMJitNode));
    MVMJitNode *unblock_gc_node = MVM_spesh_alloc(tc, sg, sizeof(MVMJitNode));
    MVMJitNode *call_node = MVM_spesh_alloc(tc, sg, sizeof(MVMJitNode));
    MVMJitNode *box_rv_node = MVM_spesh_alloc(tc, sg, sizeof(MVMJitNode));
    MVMJitCallArg block_gc_args[] = { { MVM_JIT_INTERP_VAR , { MVM_JIT_INTERP_TC } } };

    jg->sg = sg;
    jg->first_node = block_gc_node;
    init_c_call_node(tc, sg, block_gc_node,   &MVM_gc_mark_thread_blocked, 1, block_gc_args);
    block_gc_node->next = call_node;
    init_c_call_node(tc, sg, call_node, body->entry_point, 0, NULL); /* we handle args manually */
    save_rv_to_stack(tc, call_node, 0);

    init_c_call_node(tc, sg, unblock_gc_node, &MVM_gc_mark_thread_unblocked, 1, block_gc_args);

    call_node->next = unblock_gc_node;
    call_node->u.call.num_args = body->num_args;
    jg->last_node = unblock_gc_node->next = box_rv_node;

    if (0 < body->num_args) {
        MVMuint16 i = 0, str_arg_count = 0;
        call_node->u.call.args = MVM_spesh_alloc(tc, sg, body->num_args * sizeof(MVMJitCallArg));
        for (i = 0; i < body->num_args; i++) {
            if ((body->arg_types[i] & MVM_NATIVECALL_ARG_TYPE_MASK) == MVM_NATIVECALL_ARG_UTF8STR) {
                MVMJitNode *unbox_str_node;
                MVMJitNode *free_str_node;

                if (7 < ++str_arg_count) /* only got 7 empty slots in the stack scratch space */
                    goto fail;

                unbox_str_node = MVM_spesh_alloc(tc, sg, sizeof(MVMJitNode));
                {
                    MVMJitCallArg unbox_str_args[] = {
                        { MVM_JIT_INTERP_VAR , { MVM_JIT_INTERP_TC } },
                        {
                            dst == -1 ? MVM_JIT_ARG_I64 : MVM_JIT_PARAM_I64 ,
                            { dst == -1 ? i : arg_ins[i]->operands[1].reg.orig }
                        }
                    };
                    init_c_call_node(tc, sg, unbox_str_node, &MVM_string_utf8_maybe_encode_C_string, 2, unbox_str_args);
                    save_rv_to_stack(tc, unbox_str_node, str_arg_count);
                }
                unbox_str_node->next = jg->first_node;
                jg->first_node = unbox_str_node;

                call_node->u.call.args[i].type = MVM_JIT_STACK_VALUE;
                call_node->u.call.args[i].v.lit_i64 = str_arg_count;

                if ((body->arg_types[i] & MVM_NATIVECALL_ARG_FREE_STR_MASK) != 0) {
                    MVMJitCallArg free_str_args[] = {
                        { MVM_JIT_STACK_VALUE , { str_arg_count } }
                    };
                    free_str_node = MVM_spesh_alloc(tc, sg, sizeof(MVMJitNode));
                    init_c_call_node(tc, sg, free_str_node, &MVM_free, 1, free_str_args);
                    free_str_node->next = unblock_gc_node->next;
                    unblock_gc_node->next = free_str_node;
                }
            }
        }
        for (i = 0; i < body->num_args; i++) {
            MVMJitArgType arg_type;
            int is_rw = ((body->arg_types[i] & MVM_NATIVECALL_ARG_RW_MASK) == MVM_NATIVECALL_ARG_RW);

            switch (body->arg_types[i] & MVM_NATIVECALL_ARG_TYPE_MASK) {
                case MVM_NATIVECALL_ARG_CHAR:
                case MVM_NATIVECALL_ARG_UCHAR:
                case MVM_NATIVECALL_ARG_SHORT:
                case MVM_NATIVECALL_ARG_USHORT:
                case MVM_NATIVECALL_ARG_INT:
                case MVM_NATIVECALL_ARG_UINT:
                case MVM_NATIVECALL_ARG_LONG:
                case MVM_NATIVECALL_ARG_ULONG:
                case MVM_NATIVECALL_ARG_LONGLONG:
                case MVM_NATIVECALL_ARG_ULONGLONG:
                    arg_type = dst == -1
                        ? is_rw ? MVM_JIT_ARG_I64_RW : MVM_JIT_ARG_I64
                        : is_rw ? MVM_JIT_PARAM_I64_RW : MVM_JIT_PARAM_I64;
                    break;
                case MVM_NATIVECALL_ARG_DOUBLE:
                    if (is_rw) goto fail;
                    arg_type = dst == -1
                        ? MVM_JIT_ARG_DOUBLE
                        : MVM_JIT_PARAM_DOUBLE;
                    break;
                case MVM_NATIVECALL_ARG_CPOINTER:
                    if (is_rw) goto fail;
                    arg_type = dst == -1 ? MVM_JIT_ARG_PTR : MVM_JIT_PARAM_PTR;
                    break;
                case MVM_NATIVECALL_ARG_CARRAY:
                    if (body->arg_types[i] & MVM_NATIVECALL_ARG_REFRESH_MASK) goto fail;
                    if (is_rw) goto fail;
                    arg_type = dst == -1 ? MVM_JIT_ARG_PTR : MVM_JIT_PARAM_PTR;
                    break;
                case MVM_NATIVECALL_ARG_VMARRAY:
                    if (is_rw) goto fail;
                    arg_type = dst == -1 ? MVM_JIT_ARG_VMARRAY : MVM_JIT_PARAM_VMARRAY;
                    break;
                case MVM_NATIVECALL_ARG_UTF8STR:
                    if (is_rw) goto fail;
                    continue; /* already handled */
                default:
                    goto fail;
            }
            call_node->u.call.args[i].type = arg_type;
            call_node->u.call.args[i].v.lit_i64 = dst == -1 ? i : arg_ins[i]->operands[1].reg.orig;
        }
    }

    if (body->ret_type == MVM_NATIVECALL_ARG_CHAR
        || body->ret_type == MVM_NATIVECALL_ARG_UCHAR
        || body->ret_type == MVM_NATIVECALL_ARG_SHORT
        || body->ret_type == MVM_NATIVECALL_ARG_USHORT
        || body->ret_type == MVM_NATIVECALL_ARG_INT
        || body->ret_type == MVM_NATIVECALL_ARG_UINT
        || body->ret_type == MVM_NATIVECALL_ARG_LONG
        || body->ret_type == MVM_NATIVECALL_ARG_ULONG
        || body->ret_type == MVM_NATIVECALL_ARG_LONGLONG
        || body->ret_type == MVM_NATIVECALL_ARG_ULONGLONG
    ) {
        call_node->u.call.rv_type = body->ret_type;
        init_box_call_node(tc, sg, box_rv_node, &MVM_nativecall_make_int, restype, dst);
    }
    else if (body->ret_type == MVM_NATIVECALL_ARG_CPOINTER) {
        init_box_call_node(tc, sg, box_rv_node, &MVM_nativecall_make_cpointer, restype, dst);
    }
    else if (body->ret_type == MVM_NATIVECALL_ARG_UTF8STR) {
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR , { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_DYNIDX, { 2 } },
                                 { MVM_JIT_LITERAL, { MVM_NATIVECALL_ARG_UTF8STR } },
                                 { MVM_JIT_STACK_VALUE, { 0 } }};
        init_c_call_node(tc, sg, box_rv_node, &MVM_nativecall_make_str, 4, args);
        box_rv_node->next = NULL;
        if (dst == -1) {
            box_rv_node->u.call.rv_mode = MVM_JIT_RV_DYNIDX;
            box_rv_node->u.call.rv_idx = 0;
        }
        else {
            box_rv_node->u.call.args[1].type = MVM_JIT_REG_VAL;
            box_rv_node->u.call.args[1].v.reg = restype;

            box_rv_node->u.call.rv_mode = MVM_JIT_RV_PTR;
            box_rv_node->u.call.rv_idx = dst;
        }
    }
    else if (body->ret_type == MVM_NATIVECALL_ARG_VOID) {
        call_node->next = unblock_gc_node;
        unblock_gc_node->next = NULL;
        jg->last_node = unblock_gc_node;
    }
    else {
        goto fail;
    }

    return jg;
fail:
    return NULL;
}

MVMJitCode *create_caller_code(MVMThreadContext *tc, MVMNativeCallBody *body) {
    MVMSpeshGraph *sg = MVM_calloc(1, sizeof(MVMSpeshGraph));
    MVMJitGraph *jg = MVM_nativecall_jit_graph_for_caller_code(tc, sg, body, -1, -1, NULL);
    MVMJitCode *jitcode;
    if (jg != NULL) {
        MVMJitNode *entry_label = MVM_spesh_alloc(tc, sg, sizeof(MVMJitNode));
        entry_label->next = jg->first_node;
        jg->first_node = entry_label;
        jg->num_labels = 1;
        jg->no_trampoline = 1;

        entry_label->type = MVM_JIT_NODE_LABEL;
        entry_label->u.label.name = 0;
        jitcode = MVM_jit_compile_graph(tc, jg);
    }
    else {
        jitcode = NULL;
    }
    MVM_spesh_graph_destroy(tc, sg);
    return jitcode;
}

void MVM_nativecall_setup(MVMThreadContext *tc, MVMNativeCallBody *body, unsigned int interval_id) {
    /* Try to load the library. */
    DLLib *lib_handle = MVM_nativecall_load_lib(body->lib_name[0] ? body->lib_name : NULL);

    if (!lib_handle) {
        char *waste[] = { body->lib_name, NULL };
        MVM_free_null(body->sym_name);
        body->lib_name = NULL;
        if (interval_id)
            MVM_telemetry_interval_stop(tc, interval_id, "error building native call");
        MVM_exception_throw_adhoc_free(tc, waste, "Cannot locate native library '%s': %s", waste[0], dlerror());
    }

    if (!body->entry_point) {
        body->entry_point = MVM_nativecall_find_sym(lib_handle, body->sym_name);
        if (!body->entry_point) {
            char *waste[] = { body->sym_name, body->lib_name, NULL };
            body->sym_name = NULL;
            body->lib_name = NULL;
            if (interval_id)
                MVM_telemetry_interval_stop(tc, interval_id, "error building native call");
            MVM_exception_throw_adhoc_free(tc, waste, "Cannot locate symbol '%s' in native library '%s'",
                waste[0], waste[1]);
        }
    }

    if (tc->instance->jit_enabled) {
        body->jitcode = create_caller_code(tc, body);
    }
    else
        body->jitcode = NULL;

    /* Initialize body->lib_handle as late as possible, so get_int won't report
       that the NativeCall site is set up too early */
    body->lib_handle = lib_handle;
}

/* Builds up a native call site out of the supplied arguments. */
MVMint8 MVM_nativecall_build(MVMThreadContext *tc, MVMObject *site, MVMString *lib,
        MVMString *sym, MVMString *conv, MVMObject *arg_info, MVMObject *ret_info) {
    char *lib_name = MVM_string_utf8_c8_encode_C_string(tc, lib);
    char *sym_name = MVM_string_utf8_c8_encode_C_string(tc, sym);
    MVMint8  keep_sym_name = 0;
    MVMint16 i;

    unsigned int interval_id = MVM_telemetry_interval_start(tc, "building native call");

    MVMObject *entry_point_o = (MVMObject *)MVM_repr_at_key_o(tc, ret_info,
        tc->instance->str_consts.entry_point);
    MVMObject *resolve_lib_name = (MVMObject *)MVM_repr_at_key_o(tc, ret_info,
        tc->instance->str_consts.resolve_lib_name);
    MVMObject *resolve_lib_name_arg = (MVMObject *)MVM_repr_at_key_o(tc, ret_info,
        tc->instance->str_consts.resolve_lib_name_arg);

    /* Initialize the object; grab native call part of its body. */
    MVMNativeCallBody *body = MVM_nativecall_get_nc_body(tc, site);

    body->lib_name = lib_name;
    MVM_ASSIGN_REF(tc, &(site->header), body->resolve_lib_name, resolve_lib_name);
    MVM_ASSIGN_REF(tc, &(site->header), body->resolve_lib_name_arg, resolve_lib_name_arg);

    /* Try to locate the symbol. */
    if (entry_point_o && !MVM_is_null(tc, entry_point_o)) {
        body->entry_point = MVM_nativecall_unmarshal_cpointer(tc, entry_point_o, MVM_NATIVECALL_UNMARSHAL_KIND_GENERIC);
        body->sym_name    = sym_name;
        keep_sym_name     = 1;
    }

    if (!body->entry_point) {
        body->sym_name    = sym_name;
        keep_sym_name     = 1;
    }

    MVM_telemetry_interval_annotate_dynamic((uintptr_t)body->entry_point, interval_id, body->sym_name);

    if (keep_sym_name == 0) {
        MVM_free(sym_name);
    }

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

    MVM_nativecall_setup(tc, body, interval_id);

    MVM_telemetry_interval_stop(tc, interval_id, "nativecall built");

    return body->jitcode != NULL;
}

static MVMObject * nativecall_cast(MVMThreadContext *tc, MVMObject *target_spec, MVMObject *target_type, void *cpointer_body) {
    MVMObject *result = NULL;

    MVMROOT2(tc, target_spec, target_type, {
        switch (REPR(target_type)->ID) {
            case MVM_REPR_ID_P6opaque: {
                const MVMStorageSpec *ss = REPR(target_spec)->get_storage_spec(tc, STABLE(target_spec));
                if(ss->can_box & MVM_STORAGE_SPEC_CAN_BOX_INT) {
                    MVMint64 value = 0;
                    if (ss->is_unsigned)
                        switch(ss->bits) {
                            case 8:
                                value = *(MVMuint8 *)cpointer_body;
                                break;
                            case 16:
                                value = *(MVMuint16 *)cpointer_body;
                                break;
                            case 32:
                                value = *(MVMuint32 *)cpointer_body;
                                break;
                            case 64:
                            default:
                                value = *(MVMuint64 *)cpointer_body;
                        }
                    else
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
                else
                    MVM_exception_throw_adhoc(tc, "Internal error: unhandled target type");

                break;
            }
            case MVM_REPR_ID_P6int: {
                const MVMStorageSpec *ss = REPR(target_spec)->get_storage_spec(tc, STABLE(target_spec));
                MVMint64 value;
                if (ss->is_unsigned)
                    switch(ss->bits) {
                        case 8:
                            value = *(MVMuint8 *)cpointer_body;
                            break;
                        case 16:
                            value = *(MVMuint16 *)cpointer_body;
                            break;
                        case 32:
                            value = *(MVMuint32 *)cpointer_body;
                            break;
                        case 64:
                        default:
                            value = *(MVMuint64 *)cpointer_body;
                    }
                else
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
            case MVM_REPR_ID_MVMCPPStruct:
                result = MVM_nativecall_make_cppstruct(tc, target_type, (void *)cpointer_body);
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

    return result;
}

MVMObject * MVM_nativecall_global(MVMThreadContext *tc, MVMString *lib, MVMString *sym, MVMObject *target_spec, MVMObject *target_type) {
    char *lib_name = MVM_string_utf8_c8_encode_C_string(tc, lib);
    char *sym_name = MVM_string_utf8_c8_encode_C_string(tc, sym);
    DLLib *lib_handle;
    void *entry_point;
    MVMObject *ret = NULL;

    /* Try to load the library. */
    lib_handle = MVM_nativecall_load_lib(lib_name[0] ? lib_name : NULL);
    if (!lib_handle) {
        char *waste[] = { lib_name, NULL };
        MVM_free(sym_name);
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
        data_body = MVM_nativecall_unmarshal_cstruct(tc, source, MVM_NATIVECALL_UNMARSHAL_KIND_NATIVECAST);
    else if (REPR(source)->ID == MVM_REPR_ID_MVMCPPStruct)
        data_body = MVM_nativecall_unmarshal_cppstruct(tc, source, MVM_NATIVECALL_UNMARSHAL_KIND_NATIVECAST);
    else if (REPR(source)->ID == MVM_REPR_ID_MVMCUnion)
        data_body = MVM_nativecall_unmarshal_cunion(tc, source, MVM_NATIVECALL_UNMARSHAL_KIND_NATIVECAST);
    else if (REPR(source)->ID == MVM_REPR_ID_MVMCPointer)
        data_body = MVM_nativecall_unmarshal_cpointer(tc, source, MVM_NATIVECALL_UNMARSHAL_KIND_NATIVECAST);
    else if (REPR(source)->ID == MVM_REPR_ID_MVMCArray)
        data_body = MVM_nativecall_unmarshal_carray(tc, source, MVM_NATIVECALL_UNMARSHAL_KIND_NATIVECAST);
    else if (REPR(source)->ID == MVM_REPR_ID_VMArray)
        data_body = MVM_nativecall_unmarshal_vmarray(tc, source, MVM_NATIVECALL_UNMARSHAL_KIND_NATIVECAST);
    else
        MVM_exception_throw_adhoc(tc,
            "Native call cast expected return type with CPointer, CStruct, CArray, or VMArray representation, but got a %s (%s)", REPR(source)->name, MVM_6model_get_debug_name(tc, source));
    return nativecall_cast(tc, target_spec, target_type, data_body);
}

MVMint64 MVM_nativecall_sizeof(MVMThreadContext *tc, MVMObject *obj) {
    if (REPR(obj)->ID == MVM_REPR_ID_MVMCStruct)
        return ((MVMCStructREPRData *)STABLE(obj)->REPR_data)->struct_size;
    else if (REPR(obj)->ID == MVM_REPR_ID_MVMCPPStruct)
        return ((MVMCPPStructREPRData *)STABLE(obj)->REPR_data)->struct_size;
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
            "NativeCall op sizeof expected type with CPointer, CStruct, CArray, P6int or P6num representation, but got a %s (%s)",
            REPR(obj)->name, MVM_6model_get_debug_name(tc, obj));
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
                    case MVM_CARRAY_ELEM_KIND_CPPSTRUCT:
                        objptr = ((MVMCPPStructBody *)OBJECT_BODY(body->child_objs[i]))->cppstruct;
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
            MVMint32  kind = repr_data->attribute_locations[i] & MVM_CSTRUCT_ATTR_MASK;
            MVMint32  slot = repr_data->attribute_locations[i] >> MVM_CSTRUCT_ATTR_SHIFT;
            void *cptr   = NULL; /* Address of the struct member holding the pointer in the C storage. */
            void *objptr = NULL; /* The pointer in the object representing the C object. */

            if (kind == MVM_CSTRUCT_ATTR_IN_STRUCT || !body->child_objs[slot])
                continue;

            cptr = (void *)(storage + (uintptr_t)repr_data->struct_offsets[i]);
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
                    case MVM_CSTRUCT_ATTR_CPPSTRUCT:
                        objptr = (MVMCPPStructBody *)OBJECT_BODY(body->child_objs[slot]);
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
    else if (REPR(cthingy)->ID == MVM_REPR_ID_MVMCPPStruct) {
        MVMCPPStructBody     *body      = (MVMCPPStructBody *)OBJECT_BODY(cthingy);
        MVMCPPStructREPRData *repr_data = (MVMCPPStructREPRData *)STABLE(cthingy)->REPR_data;
        char                 *storage   = (char *) body->cppstruct;
        MVMint64              i;

        for (i = 0; i < repr_data->num_attributes; i++) {
            MVMint32 kind = repr_data->attribute_locations[i] & MVM_CPPSTRUCT_ATTR_MASK;
            MVMint32 slot = repr_data->attribute_locations[i] >> MVM_CPPSTRUCT_ATTR_SHIFT;
            void *cptr = NULL;   /* The pointer in the C storage. */
            void *objptr = NULL; /* The pointer in the object representing the C object. */

            if (kind == MVM_CPPSTRUCT_ATTR_IN_STRUCT || !body->child_objs[slot])
                continue;

            cptr = (void *)(storage + (uintptr_t)repr_data->struct_offsets[i]);
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
                    case MVM_CPPSTRUCT_ATTR_CPPSTRUCT:
                        objptr = (MVMCPPStructBody *)OBJECT_BODY(body->child_objs[slot]);
                        break;
                    case MVM_CPPSTRUCT_ATTR_CUNION:
                        objptr = (MVMCUnionBody *)OBJECT_BODY(body->child_objs[slot]);
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

typedef struct ResolverData {
    MVMObject *site;
    MVMRegister args[1];
} ResolverData;
static void callback_invoke(MVMThreadContext *tc, void *data) {
    /* Invoke the coderef, to set up the nested interpreter. */
    ResolverData *r = (ResolverData*)data;
    MVMNativeCallBody *body = MVM_nativecall_get_nc_body(tc, r->site);
    MVMObject *code = body->resolve_lib_name;
    MVMCallsite *callsite = MVM_callsite_get_common(tc, MVM_CALLSITE_ID_OBJ);
    r->args[0].o = body->resolve_lib_name_arg;
    STABLE(code)->invoke(tc, code, callsite, r->args);

    /* Ensure we exit interp after callback. */
    tc->thread_entry_frame = tc->cur_frame;
}
void MVM_nativecall_restore_library(MVMThreadContext *tc, MVMNativeCallBody *body, MVMObject *root) {
    if (!MVM_is_null(tc, body->resolve_lib_name) && !MVM_is_null(tc, body->resolve_lib_name_arg)) {
        MVMRegister res = {NULL};
        ResolverData data = {root, {NULL}};

        MVM_interp_run_nested(tc, callback_invoke, &data, &res);

        /* Handle return value. */
        if (res.o) {
            MVMContainerSpec const *contspec = STABLE(res.o)->container_spec;
            if (contspec && contspec->fetch_never_invokes)
                contspec->fetch(tc, res.o, &res);
        }

        body->lib_name = MVM_string_utf8_encode_C_string(tc, MVM_repr_get_str(tc, res.o));
    }
    if (body->lib_name && body->sym_name && !body->lib_handle) {
        MVM_nativecall_setup(tc, body, 0);
    }
}

void MVM_nativecall_invoke_jit(MVMThreadContext *tc, MVMObject *site) {
    MVMNativeCallBody *body = MVM_nativecall_get_nc_body(tc, site);

    if (MVM_UNLIKELY(!body->lib_handle)) {
        MVMROOT(tc, site, {
            MVM_nativecall_restore_library(tc, body, site);
        });
        body = MVM_nativecall_get_nc_body(tc, site);
    }

    MVMJitCode * const jitcode = body->jitcode;
    assert(jitcode);
    assert(jitcode->func_ptr);
    jitcode->func_ptr(tc, *tc->interp_cu, jitcode->labels[0]);
}
