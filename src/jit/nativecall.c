#include "moar.h"

#define APPLY(...) MVM_jit_expr_apply_template_adhoc(__VA_ARGS__)

static MVMint32 function_const_ptr(MVMThreadContext *tc, MVMJitExprTree *tree, const void * function_ptr) {
    MVMint32 function_constant = MVM_jit_expr_add_const_ptr(tc, tree, function_ptr);
    return APPLY(tc, tree, "ns.", MVM_JIT_CONST_PTR, 0, function_constant);
}

static MVMint32 nullary_void_function(MVMThreadContext *tc, MVMJitExprTree *tree, const void *function_ptr) {
    MVMint32 function = function_const_ptr(tc, tree, function_ptr);
    return APPLY(tc, tree, "nsnsl.nslns.l",
                 /* 0: */ MVM_JIT_TC, 0,
                 /* 2: */ MVM_JIT_CARG, 1, /* tc */ 0, MVM_JIT_PTR_SZ,
                 /* 6: */ MVM_JIT_ARGLIST, 1, /* carg */ 2,
                 /* 10: */ MVM_JIT_CALLV, 2, function, /* arglist */ 6);
}

static MVMint32 box_value(MVMThreadContext *tc, MVMJitExprTree *tree, const void *function_ptr, MVMint32 boxee, MVMint32 type) {
    MVMint32 function = function_const_ptr(tc, tree, function_ptr);
    return APPLY(tc, tree, "nsnsl.ns..ns..nslllns.l.",
                 /* 0: */ MVM_JIT_TC, 0,
                 /* 2: */ MVM_JIT_CARG, 1, /* tc */ 0, MVM_JIT_PTR_SZ,
                 /* 6: */ MVM_JIT_CARG, 1, type, MVM_JIT_PTR_SZ,
                 /* 10: */ MVM_JIT_CARG, 1, boxee, MVM_JIT_PTR_SZ,
                 /* 14: */ MVM_JIT_ARGLIST, 3, /* tc */ 2, /* type */ 6, /* boxee */ 10,
                 /* 19 */ MVM_JIT_CALL, 2, function, /* arglist */ 14, MVM_JIT_PTR_SZ);
}

static MVMint32 unbox_string(MVMThreadContext *tc, MVMJitExprTree *tree, MVMint32 str_node, const void *function_ptr) {
    MVMint32 function = function_const_ptr(tc, tree, function_ptr);
    return APPLY(tc, tree, "nsnsl.ns..nsllns.l.",
                 /* 0: */ MVM_JIT_TC, 0,
                 /* 2: */ MVM_JIT_CARG, 1, 0, MVM_JIT_PTR_SZ,
                 /* 6: */ MVM_JIT_CARG, 1, str_node, MVM_JIT_PTR_SZ,
                 /* 10: */ MVM_JIT_ARGLIST, 2, /* tc */ 2, /* str_node */ 6,
                 /* 14: */ MVM_JIT_CALL, 2, function, /* arglist */ 10, MVM_JIT_PTR_SZ);
}


MVMint32 MVM_jit_nativecall_expr_tree(MVMThreadContext *tc, MVMJitExprTree *tree, MVMObject *call, MVMint32 dst, MVMint32 type) {
    /* Nodes for arguments */
    MVMNativeCallBody *body = MVM_nativecall_get_nc_body(tc, call);
    MVMint32 *args    = alloca(sizeof(MVMint32) * body->num_args);
    /* nodes that we have allocated and need to be freed */
    MVMint32 *to_free = alloca(sizeof(MVMint32) * body->num_args), num_free = 0;
    /*
     * - 1 for mark_blocked
     * - 1 for the CALL
     * - 1 for mark unblocked
     * - 1 for cleanup
     * - 1 for the COPY or box operation
     */
    MVMint32 code[5];
    MVMint32 result = 0;
    MVMint32 i;

    MVMint32 arg_base = APPLY(tc, tree, "nsnsl.nsl.nsl.nsl.",
                              /* 0: */ MVM_JIT_TC, 0,
                              /* 2: */ MVM_JIT_ADDR, 1, /* tc */ 0, offsetof(MVMThreadContext, cur_frame),
                              /* 6: */ MVM_JIT_LOAD, 1, /* addr */ 2, sizeof(MVMFrame*),
                              /* 10: */ MVM_JIT_ADDR, 1, /* load */ 6, offsetof(MVMFrame, args),
                              /* 14: */ MVM_JIT_LOAD, 1, /* addr */ 10, sizeof(MVMRegister*));
    fprintf(stderr, "building expr tree for nativeinvoke\n");
    for (i = 0; i < body->num_args; i++) {
        MVMint16 arg_type = body->arg_types[i];
        MVMint32 size = MVM_JIT_PTR_SZ;
        MVMint32 is_rw = (arg_type & MVM_NATIVECALL_ARG_RW_MASK) == MVM_NATIVECALL_ARG_RW;

        void    *unbox_function;
        MVMint32 arg  = APPLY(tc, tree, "ns..", MVM_JIT_ADDR, 1, arg_base,
                              sizeof(MVMRegister*) * i);

        if (!is_rw) {
            arg = APPLY(tc, tree, "ns..", MVM_JIT_LOAD, 1, arg, size);
        }
        switch (arg_type & MVM_NATIVECALL_ARG_TYPE_MASK) {
        case MVM_NATIVECALL_ARG_VMARRAY:
            if (is_rw) return 0;
            arg = APPLY(tc, tree, "ns..nsl.",
                        /* 0: */ MVM_JIT_ADDR, 1, arg, offsetof(MVMArray, body.slots),
                        /* 4: */ MVM_JIT_LOAD, 1, /* addr */ 0, sizeof(void*));
            unbox_function = NULL;
            break;
        case MVM_NATIVECALL_ARG_ASCIISTR:
            unbox_function = MVM_string_ascii_encode_any;
            break;
        case MVM_NATIVECALL_ARG_UTF8STR:
            unbox_function = MVM_string_utf8_maybe_encode_C_string;
            break;
        case MVM_NATIVECALL_ARG_UTF16STR:
            unbox_function = MVM_string_utf16_encode;
            break;
        default:
            unbox_function = NULL;
            break;
        }
        if (unbox_function != NULL) {
            if (is_rw) return 0; /* nonsense */
            arg = unbox_string(tc, tree, arg, unbox_function);
            if ((arg_type & MVM_NATIVECALL_ARG_FREE_STR_MASK) == MVM_NATIVECALL_ARG_FREE_STR)
                to_free[num_free++] = arg;
        }
        args[i] = APPLY(tc, tree, "ns..", MVM_JIT_CARG, 1 , arg, size);
    }

    code[0] = nullary_void_function(tc, tree, MVM_gc_mark_thread_blocked);
    {
        MVMint32 native_function = function_const_ptr(tc, tree, body->entry_point);
        MVMint32 arglist = MVM_jit_expr_add_variadic(tc, tree, MVM_JIT_ARGLIST, body->num_args, args);
        if ((body->ret_type & MVM_NATIVECALL_ARG_TYPE_MASK) == MVM_NATIVECALL_ARG_VOID) {
            code[1] = APPLY(tc, tree, "ns..", MVM_JIT_CALLV, 2, native_function, arglist);
        } else {
            MVMint32 size = MVM_JIT_PTR_SZ; /* TODO calculate this */
            result = APPLY(tc, tree, "ns...", MVM_JIT_CALL, 2, native_function, arglist, size);
            code[1] = APPLY(tc, tree, "ns.", MVM_JIT_DISCARD, 1, result);
        }
    }
    code[2] = nullary_void_function(tc, tree, MVM_gc_mark_thread_unblocked);

    if (num_free > 0) {
        MVMint32 free_function = function_const_ptr(tc, tree, MVM_free);
        MVMint32 *cleanup = alloca(sizeof(MVMint32) * num_free);
        for (i = 0; i < num_free; i++) {
            cleanup[i] = APPLY(tc, tree, "ns..nslns.l",
                               MVM_JIT_CARG, 1, to_free[i], MVM_JIT_PTR_SZ,
                               MVM_JIT_ARGLIST, 1, 0,
                               MVM_JIT_CALLV, 2, free_function, 4);
        }
        code[3] = MVM_jit_expr_add_variadic(tc, tree, MVM_JIT_DOV, num_free, cleanup);
    } else {
        /* a noop */
        code[3] = APPLY(tc, tree, "nsnsl", MVM_JIT_LOCAL, 0, MVM_JIT_DISCARD, 1, 0);
    }

    if (result == 0) {
        /* void function, no more work here */
        return MVM_jit_expr_add_variadic(tc, tree, MVM_JIT_DOV, 4, code);
    }

    if (dst <= 0) {
        /* -1 means resolve from current position in interpreter.
         * char *interp_cur_op = *tc->interp_cur_op */
        MVMint32 cur_op = APPLY(tc, tree, "nsnsl.nsl.",
                                /* 0: */ MVM_JIT_TC, 0,
                                /* 2: */ MVM_JIT_ADDR, 1, /* tc */ 0, offsetof(MVMThreadContext, interp_cur_op),
                                /* 6: */ MVM_JIT_LOAD, 1, /* addr */ 2, sizeof(char*));
        /* dst = &GET_REG(cur_op, 0) = &reg_base[*(MVMint16)(*pc+0)] */
        dst = APPLY(tc, tree, "ns..nsnsll.",
                    /* 0: */ MVM_JIT_LOAD, 1, cur_op, sizeof(MVMint16),
                    /* 4: */ MVM_JIT_LOCAL, 0,
                    /* 6: */ MVM_JIT_IDX, 2, /* local */ 4, /* load */ 0, sizeof(MVMRegister));
        /* type = GET_REG(cur_op, 2) */
        type = APPLY(tc, tree, "ns..nsl.nsnsll.nsl.",
                     /* 0: */ MVM_JIT_ADDR, 1, cur_op, 2, /* cur_op is defined as char* */
                     /* 4: */ MVM_JIT_LOAD, 1, /* addr */ 0, sizeof(MVMint16),
                     /* 8: */ MVM_JIT_LOCAL, 0,
                     /* 10: */ MVM_JIT_IDX, 2, /* local */ 8, /* load */ 4, sizeof(MVMRegister),
                     /* 15: */ MVM_JIT_LOAD, 1, /* idx */ 10, MVM_JIT_PTR_SZ);
    }

    switch (body->ret_type) {
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
        result = box_value(tc, tree, MVM_nativecall_make_int, result, type);
        break;
    case MVM_NATIVECALL_ARG_CPOINTER:
        result = box_value(tc, tree, MVM_nativecall_make_cpointer, result, type);
        break;
    case MVM_NATIVECALL_ARG_UTF8STR:
        result = APPLY(tc, tree, "nsnsl.ns..ns..nsl.ns..nsllllns.l.",
                       /* 0:  */ MVM_JIT_TC, 0,
                       /* 2:  */ MVM_JIT_CARG, 1, /* tc */ 0, MVM_JIT_PTR,
                       /* 6:  */ MVM_JIT_CARG, 1, type, MVM_JIT_PTR,
                       /* 10: */ MVM_JIT_CONST, 0, body->ret_type, sizeof(body->ret_type),
                       /* 14: */ MVM_JIT_CARG, 1, /* const */ 10, MVM_JIT_INT,
                       /* 18: */ MVM_JIT_CARG, 1,  result, MVM_JIT_PTR,
                       /* 22: */ MVM_JIT_ARGLIST, 4, 2, 6, 14, 18,
                       /* 28: */ MVM_JIT_CALL, 2, function_const_ptr(tc, tree, MVM_nativecall_make_str), 22, MVM_JIT_PTR_SZ);
        break;
    default:
        return 0;
    }

    /* Apply the store */
    code[4] = APPLY(tc, tree, "ns...", MVM_JIT_STORE, 2, dst, result, MVM_JIT_REG_SZ);

    return MVM_jit_expr_add_variadic(tc, tree, MVM_JIT_DOV, 5, code);
}
