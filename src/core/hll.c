#include "moar.h"

MVMHLLConfig *MVM_hll_get_config_for(MVMThreadContext *tc, MVMString *name) {
    MVMHLLConfig *entry;

    if (!MVM_str_hash_key_is_valid(tc, name)) {
        MVM_str_hash_key_throw_invalid(tc, name);
    }

    uv_mutex_lock(&tc->instance->mutex_hllconfigs);

    if (tc->instance->hll_compilee_depth) {
        entry = MVM_fixkey_hash_lvalue_fetch_nocheck(tc, &tc->instance->compilee_hll_configs, name);
    }
    else {
        entry = MVM_fixkey_hash_lvalue_fetch_nocheck(tc, &tc->instance->compiler_hll_configs, name);
    }

    if (!entry->name) {
        memset(entry, 0, sizeof(*entry));
        entry->name = name;
        entry->int_box_type = tc->instance->boot_types.BOOTInt;
        entry->num_box_type = tc->instance->boot_types.BOOTNum;
        entry->str_box_type = tc->instance->boot_types.BOOTStr;
        entry->slurpy_array_type = tc->instance->boot_types.BOOTArray;
        entry->slurpy_hash_type = tc->instance->boot_types.BOOTHash;
        entry->array_iterator_type = tc->instance->boot_types.BOOTIter;
        entry->hash_iterator_type = tc->instance->boot_types.BOOTIter;
        entry->foreign_type_int = tc->instance->boot_types.BOOTInt;
        entry->foreign_type_num = tc->instance->boot_types.BOOTNum;
        entry->foreign_type_str = tc->instance->boot_types.BOOTStr;
        entry->max_inline_size = MVM_SPESH_DEFAULT_MAX_INLINE_SIZE;
        MVM_gc_root_add_permanent_desc(tc, (MVMCollectable **)&entry->int_box_type, "HLL int_box_type");
        MVM_gc_root_add_permanent_desc(tc, (MVMCollectable **)&entry->num_box_type, "HLL num_box_type");
        MVM_gc_root_add_permanent_desc(tc, (MVMCollectable **)&entry->str_box_type, "HLL str_box_type");
        MVM_gc_root_add_permanent_desc(tc, (MVMCollectable **)&entry->slurpy_array_type, "HLL slurpy_array_type");
        MVM_gc_root_add_permanent_desc(tc, (MVMCollectable **)&entry->slurpy_hash_type, "HLL slurpy_hash_type");
        MVM_gc_root_add_permanent_desc(tc, (MVMCollectable **)&entry->array_iterator_type, "HLL array_iterator_type");
        MVM_gc_root_add_permanent_desc(tc, (MVMCollectable **)&entry->hash_iterator_type, "HLL hash_iterator_type");
        MVM_gc_root_add_permanent_desc(tc, (MVMCollectable **)&entry->foreign_type_int, "HLL foreign_type_int");
        MVM_gc_root_add_permanent_desc(tc, (MVMCollectable **)&entry->foreign_type_num, "HLL foreign_type_num");
        MVM_gc_root_add_permanent_desc(tc, (MVMCollectable **)&entry->foreign_type_str, "HLL foreign_type_str");
        MVM_gc_root_add_permanent_desc(tc, (MVMCollectable **)&entry->foreign_transform_array, "HLL foreign_transform_array");
        MVM_gc_root_add_permanent_desc(tc, (MVMCollectable **)&entry->foreign_transform_hash, "HLL foreign_transform_hash");
        MVM_gc_root_add_permanent_desc(tc, (MVMCollectable **)&entry->foreign_transform_code, "HLL foreign_transform_code");
        MVM_gc_root_add_permanent_desc(tc, (MVMCollectable **)&entry->null_value, "HLL null_value");
        MVM_gc_root_add_permanent_desc(tc, (MVMCollectable **)&entry->exit_handler, "HLL exit_handler");
        MVM_gc_root_add_permanent_desc(tc, (MVMCollectable **)&entry->finalize_handler, "HLL finalize_handler");
        MVM_gc_root_add_permanent_desc(tc, (MVMCollectable **)&entry->bind_error, "HLL bind_error");
        MVM_gc_root_add_permanent_desc(tc, (MVMCollectable **)&entry->method_not_found_error, "HLL method_not_found_error");
        MVM_gc_root_add_permanent_desc(tc, (MVMCollectable **)&entry->lexical_handler_not_found_error, "HLL lexical_handler_not_found_error");
        MVM_gc_root_add_permanent_desc(tc, (MVMCollectable **)&entry->int_lex_ref, "HLL int_lex_ref");
        MVM_gc_root_add_permanent_desc(tc, (MVMCollectable **)&entry->num_lex_ref, "HLL num_lex_ref");
        MVM_gc_root_add_permanent_desc(tc, (MVMCollectable **)&entry->str_lex_ref, "HLL str_lex_ref");
        MVM_gc_root_add_permanent_desc(tc, (MVMCollectable **)&entry->int_attr_ref, "HLL int_attr_ref");
        MVM_gc_root_add_permanent_desc(tc, (MVMCollectable **)&entry->num_attr_ref, "HLL num_attr_ref");
        MVM_gc_root_add_permanent_desc(tc, (MVMCollectable **)&entry->str_attr_ref, "HLL str_attr_ref");
        MVM_gc_root_add_permanent_desc(tc, (MVMCollectable **)&entry->int_pos_ref, "HLL int_pos_ref");
        MVM_gc_root_add_permanent_desc(tc, (MVMCollectable **)&entry->num_pos_ref, "HLL num_pos_ref");
        MVM_gc_root_add_permanent_desc(tc, (MVMCollectable **)&entry->str_pos_ref, "HLL str_pos_ref");
        MVM_gc_root_add_permanent_desc(tc, (MVMCollectable **)&entry->int_multidim_ref, "HLL int_multidim_ref");
        MVM_gc_root_add_permanent_desc(tc, (MVMCollectable **)&entry->num_multidim_ref, "HLL num_multidim_ref");
        MVM_gc_root_add_permanent_desc(tc, (MVMCollectable **)&entry->str_multidim_ref, "HLL str_multidim_ref");
        MVM_gc_root_add_permanent_desc(tc, (MVMCollectable **)&entry->name, "HLL name");
    }

    uv_mutex_unlock(&tc->instance->mutex_hllconfigs);

    return entry;
}

#define check_config_key(tc, hash, name, member, config) do { \
    MVMString *key = MVM_string_utf8_decode((tc), (tc)->instance->VMString, (name), strlen((name))); \
    MVMObject *val = MVM_repr_at_key_o((tc), (hash), key); \
    if (!MVM_is_null(tc, val)) (config)->member = val; \
} while (0)
#define check_config_key_reftype(tc, hash, name, member, config, wantprim, wantkind) do { \
    MVMString *key = MVM_string_utf8_decode((tc), (tc)->instance->VMString, (name), strlen((name))); \
    MVMObject *val = MVM_repr_at_key_o((tc), (hash), key); \
    if (!MVM_is_null(tc, val)) { \
        MVM_nativeref_ensure(tc, val, wantprim, wantkind, name); \
        (config)->member = val; \
    }\
} while (0)
void set_max_inline_size(MVMThreadContext *tc, MVMObject *config_hash, MVMHLLConfig *config) {
    MVMROOT(tc, config_hash, {
        MVMString *key = MVM_string_ascii_decode_nt(tc, tc->instance->VMString, "max_inline_size");
        MVMObject *size = MVM_repr_at_key_o(tc, config_hash, key);
        if (!MVM_is_null(tc, size))
            config->max_inline_size = MVM_repr_get_int(tc, size);
    });
}
MVMObject * MVM_hll_set_config(MVMThreadContext *tc, MVMString *name, MVMObject *config_hash) {
    MVMHLLConfig *config;

    config = MVM_hll_get_config_for(tc, name);

    if (!config_hash || REPR(config_hash)->ID != MVM_REPR_ID_MVMHash
            || !IS_CONCRETE(config_hash)) {
        MVM_exception_throw_adhoc(tc, "set hll config needs concrete hash");
    }

    /* MVM_string_utf8_decode() can potentially allocate, and hence gc. */
    MVMROOT(tc, config_hash, {
            check_config_key(tc, config_hash, "int_box", int_box_type, config);
            check_config_key(tc, config_hash, "num_box", num_box_type, config);
            check_config_key(tc, config_hash, "str_box", str_box_type, config);
            check_config_key(tc, config_hash, "slurpy_array", slurpy_array_type, config);
            check_config_key(tc, config_hash, "slurpy_hash", slurpy_hash_type, config);
            check_config_key(tc, config_hash, "array_iter", array_iterator_type, config);
            check_config_key(tc, config_hash, "hash_iter", hash_iterator_type, config);
            check_config_key(tc, config_hash, "foreign_type_int", foreign_type_int, config);
            check_config_key(tc, config_hash, "foreign_type_num", foreign_type_num, config);
            check_config_key(tc, config_hash, "foreign_type_str", foreign_type_str, config);
            check_config_key(tc, config_hash, "foreign_transform_array", foreign_transform_array, config);
            check_config_key(tc, config_hash, "foreign_transform_hash", foreign_transform_hash, config);
            check_config_key(tc, config_hash, "foreign_transform_code", foreign_transform_code, config);
            check_config_key(tc, config_hash, "null_value", null_value, config);
            check_config_key(tc, config_hash, "exit_handler", exit_handler, config);
            check_config_key(tc, config_hash, "finalize_handler", finalize_handler, config);
            check_config_key(tc, config_hash, "bind_error", bind_error, config);
            check_config_key(tc, config_hash, "method_not_found_error", method_not_found_error, config);
            check_config_key(tc, config_hash, "lexical_handler_not_found_error", lexical_handler_not_found_error, config);
            check_config_key(tc, config_hash, "true_value", true_value, config);
            check_config_key(tc, config_hash, "false_value", false_value, config);
            check_config_key_reftype(tc, config_hash, "int_lex_ref", int_lex_ref,
                config, MVM_STORAGE_SPEC_BP_INT, MVM_NATIVEREF_LEX);
            check_config_key_reftype(tc, config_hash, "num_lex_ref", num_lex_ref,
                config, MVM_STORAGE_SPEC_BP_NUM, MVM_NATIVEREF_LEX);
            check_config_key_reftype(tc, config_hash, "str_lex_ref", str_lex_ref,
                config, MVM_STORAGE_SPEC_BP_STR, MVM_NATIVEREF_LEX);
            check_config_key_reftype(tc, config_hash, "int_attr_ref", int_attr_ref,
                config, MVM_STORAGE_SPEC_BP_INT, MVM_NATIVEREF_ATTRIBUTE);
            check_config_key_reftype(tc, config_hash, "num_attr_ref", num_attr_ref,
                config, MVM_STORAGE_SPEC_BP_NUM, MVM_NATIVEREF_ATTRIBUTE);
            check_config_key_reftype(tc, config_hash, "str_attr_ref", str_attr_ref,
                config, MVM_STORAGE_SPEC_BP_STR, MVM_NATIVEREF_ATTRIBUTE);
            check_config_key_reftype(tc, config_hash, "int_pos_ref", int_pos_ref,
                config, MVM_STORAGE_SPEC_BP_INT, MVM_NATIVEREF_POSITIONAL);
            check_config_key_reftype(tc, config_hash, "num_pos_ref", num_pos_ref,
                config, MVM_STORAGE_SPEC_BP_NUM, MVM_NATIVEREF_POSITIONAL);
            check_config_key_reftype(tc, config_hash, "str_pos_ref", str_pos_ref,
                config, MVM_STORAGE_SPEC_BP_STR, MVM_NATIVEREF_POSITIONAL);
            check_config_key_reftype(tc, config_hash, "int_multidim_ref", int_multidim_ref,
                config, MVM_STORAGE_SPEC_BP_INT, MVM_NATIVEREF_MULTIDIM);
            check_config_key_reftype(tc, config_hash, "num_multidim_ref", num_multidim_ref,
                config, MVM_STORAGE_SPEC_BP_NUM, MVM_NATIVEREF_MULTIDIM);
            check_config_key_reftype(tc, config_hash, "str_multidim_ref", str_multidim_ref,
                config, MVM_STORAGE_SPEC_BP_STR, MVM_NATIVEREF_MULTIDIM);
            set_max_inline_size(tc, config_hash, config);

            /* Without this the integer objects are allocated in the nursery,
             * which just makes work for the GC moving them around twice.
             * The other call to MVM_intcache_for is from MVM_6model_bootstrap,
             * which runs with MVM_gc_allocate_gen2_default_set. */
            MVM_gc_allocate_gen2_default_set(tc);
            MVM_intcache_for(tc, config->int_box_type);
            MVM_gc_allocate_gen2_default_clear(tc);
        });

    return config_hash;
}

/* Gets the current HLL configuration. */
MVMHLLConfig *MVM_hll_current(MVMThreadContext *tc) {
    return (*tc->interp_cu)->body.hll_config;
}

/* Enter a level of compilee HLL configuration mode. */
void MVM_hll_enter_compilee_mode(MVMThreadContext *tc) {
    uv_mutex_lock(&tc->instance->mutex_hllconfigs);
    tc->instance->hll_compilee_depth++;
    uv_mutex_unlock(&tc->instance->mutex_hllconfigs);
}

/* Leave a level of compilee HLL configuration mode. */
void MVM_hll_leave_compilee_mode(MVMThreadContext *tc) {
    uv_mutex_lock(&tc->instance->mutex_hllconfigs);
    tc->instance->hll_compilee_depth--;
    uv_mutex_unlock(&tc->instance->mutex_hllconfigs);
}

/* Checks if an object belongs to the correct HLL, and does a type mapping
 * of it if not. */
void MVM_hll_map(MVMThreadContext *tc, MVMObject *obj, MVMHLLConfig *hll, MVMRegister *res_reg) {
    /* Null objects get mapped to null_value. */
    if (MVM_is_null(tc, obj)) {
        res_reg->o = hll->null_value;
    }

    /* If the object belongs to the current HLL, we're done. */
    else if (STABLE(obj)->hll_owner == hll) {
        res_reg->o = obj;
    }

    /* Otherwise, need to try a mapping. */
    else {
        switch (STABLE(obj)->hll_role) {
            case MVM_HLL_ROLE_INT:
                if (hll->foreign_type_int)
                    res_reg->o = IS_CONCRETE(obj)
                        ? MVM_repr_box_int(tc, hll->foreign_type_int, MVM_repr_get_int(tc, obj))
                        : hll->foreign_type_int;
                else
                    res_reg->o = obj;
                break;
            case MVM_HLL_ROLE_NUM:
                if (hll->foreign_type_num)
                    res_reg->o = IS_CONCRETE(obj)
                        ? MVM_repr_box_num(tc, hll->foreign_type_num, MVM_repr_get_num(tc, obj))
                        : hll->foreign_type_num;
                else
                    res_reg->o = obj;
                break;
            case MVM_HLL_ROLE_STR:
                if (hll->foreign_type_str)
                    res_reg->o = IS_CONCRETE(obj)
                        ? MVM_repr_box_str(tc, hll->foreign_type_str, MVM_repr_get_str(tc, obj))
                        : hll->foreign_type_str;
                else
                    res_reg->o = obj;
                break;
            case MVM_HLL_ROLE_ARRAY:
                if (hll->foreign_transform_array) {
                    /* Invoke and set result register as return location. */
                    MVMObject *code = MVM_frame_find_invokee(tc, hll->foreign_transform_array, NULL);
                    MVMCallsite *inv_arg_callsite = MVM_callsite_get_common(tc, MVM_CALLSITE_ID_OBJ);
                    MVM_args_setup_thunk(tc, res_reg, MVM_RETURN_OBJ, inv_arg_callsite);
                    tc->cur_frame->args[0].o = obj;
                    STABLE(code)->invoke(tc, code, inv_arg_callsite, tc->cur_frame->args);
                }
                else {
                    res_reg->o = obj;
                }
                break;
            case MVM_HLL_ROLE_HASH:
                if (hll->foreign_transform_hash) {
                    /* Invoke and set result register as return location. */
                    MVMObject *code = MVM_frame_find_invokee(tc, hll->foreign_transform_hash, NULL);
                    MVMCallsite *inv_arg_callsite = MVM_callsite_get_common(tc, MVM_CALLSITE_ID_OBJ);
                    MVM_args_setup_thunk(tc, res_reg, MVM_RETURN_OBJ, inv_arg_callsite);
                    tc->cur_frame->args[0].o = obj;
                    STABLE(code)->invoke(tc, code, inv_arg_callsite, tc->cur_frame->args);
                }
                else {
                    res_reg->o = obj;
                }
                break;
            case MVM_HLL_ROLE_CODE:
                if (hll->foreign_transform_code) {
                    /* Invoke and set result register as return location. */
                    MVMObject *code = MVM_frame_find_invokee(tc, hll->foreign_transform_code, NULL);
                    MVMCallsite *inv_arg_callsite = MVM_callsite_get_common(tc, MVM_CALLSITE_ID_OBJ);

                    MVM_args_setup_thunk(tc, res_reg, MVM_RETURN_OBJ, inv_arg_callsite);
                    tc->cur_frame->args[0].o = obj;
                    STABLE(code)->invoke(tc, code, inv_arg_callsite, tc->cur_frame->args);
                }
                else {
                    res_reg->o = obj;
                }
                break;
            default:
                res_reg->o = obj;
        }
    }
}

/* Looks up an object in the HLL symbols stash. */
MVMObject * MVM_hll_sym_get(MVMThreadContext *tc, MVMString *hll, MVMString *sym) {
    MVMObject *syms = tc->instance->hll_syms, *hash, *result;
    uv_mutex_lock(&tc->instance->mutex_hll_syms);
    hash = MVM_repr_at_key_o(tc, syms, hll);
    if (MVM_is_null(tc, hash)) {
        MVMROOT2(tc, hll, syms, {
            hash = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTHash);
        });
        MVM_repr_bind_key_o(tc, syms, hll, hash);
        result = tc->instance->VMNull;
    }
    else {
        result = MVM_repr_at_key_o(tc, hash, sym);
    }
    uv_mutex_unlock(&tc->instance->mutex_hll_syms);
    return result;
}
