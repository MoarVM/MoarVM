#include "moar.h"

int MVM_ext_load(MVMThreadContext *tc, MVMString *lib, MVMString *ext) {
    MVMString *colon, *prefix, *name;
    MVMDLLSym *sym;
    void (*init)(MVMThreadContext *);

    MVMROOT2(tc, lib, ext, {
        colon = MVM_string_ascii_decode_nt(
            tc, tc->instance->VMString, ":");
        prefix = MVM_string_concatenate(tc, lib, colon);
        name = MVM_string_concatenate(tc, prefix, ext);
    });

    uv_mutex_lock(&tc->instance->mutex_ext_registry);

    /* MVM_string_concatenate returns a concrete MVMString, will always pass the
     * MVM_str_hash_key_is_valid check. */
    MVMExtRegistry *entry = MVM_fixkey_hash_fetch_nocheck(tc, &tc->instance->ext_registry, name);

    /* Extension already loaded. */
    if (entry) {
        uv_mutex_unlock(&tc->instance->mutex_ext_registry);
        return 0;
    }

    MVMROOT(tc, name, {
        sym = (MVMDLLSym *)MVM_dll_find_symbol(tc, lib, ext);
    });
    if (!sym) {
        char *c_name = MVM_string_utf8_encode_C_string(tc, name);
        char *waste[] = { c_name, NULL };
        uv_mutex_unlock(&tc->instance->mutex_ext_registry);
        MVM_exception_throw_adhoc_free(tc, waste, "extension symbol (%s) not found", c_name);
    }

    entry = MVM_fixkey_hash_insert_nocheck(tc, &tc->instance->ext_registry, name);
    entry->sym = sym;

    MVM_gc_root_add_permanent_desc(tc, (MVMCollectable **)&entry->hash_key,
        "Extension name hash key");

    uv_mutex_unlock(&tc->instance->mutex_ext_registry);

    /* Call extension's initializer */
    init = (void (*)(MVMThreadContext *))sym->body.address;
    init(tc);

    return 1;
}

int MVM_ext_register_extop(MVMThreadContext *tc, const char *cname,
        MVMExtOpFunc func, MVMuint8 num_operands, MVMuint8 operands[],
        MVMExtOpSpesh *spesh, MVMExtOpFactDiscover *discover, MVMuint32 flags) {
    /* This MVMString ends up being permarooted, so if we create it in gen2 we
     * save some GC work */
    MVM_gc_allocate_gen2_default_set(tc);
    MVMString *name = MVM_string_ascii_decode_nt(
            tc, tc->instance->VMString, cname);
    MVM_gc_allocate_gen2_default_clear(tc);

    uv_mutex_lock(&tc->instance->mutex_extop_registry);

    struct MVMExtOpRegistry *entry = MVM_fixkey_hash_fetch_nocheck(tc, &tc->instance->extop_registry, name);

    /* Op already registered, so just verify its signature. */
    if (entry) {
        uv_mutex_unlock(&tc->instance->mutex_extop_registry);

        if (num_operands != entry->info.num_operands
                || memcmp(operands, entry->info.operands, num_operands) != 0)
            MVM_exception_throw_adhoc(tc,
                    "signature mismatch when re-registering extension op %s",
                    cname);
        return 0;
    }

    /* Sanity-check signature. */
    if (num_operands > MVM_MAX_OPERANDS) {
        uv_mutex_unlock(&tc->instance->mutex_extop_registry);
        MVM_exception_throw_adhoc(tc,
                "cannot register extension op with more than %u operands",
                MVM_MAX_OPERANDS);
    }
    {
        MVMuint8 i = 0;

        for(; i < num_operands; i++) {
            MVMuint8 flags = operands[i];

            switch (flags & MVM_operand_rw_mask) {
                case MVM_operand_literal:
                    goto check_literal;

                case MVM_operand_read_reg:
                case MVM_operand_write_reg:
                case MVM_operand_read_lex:
                case MVM_operand_write_lex:
                    goto check_reg;

                default:
                    goto fail;
            }

        check_literal:
            switch (flags & MVM_operand_type_mask) {
                case MVM_operand_int8:
                case MVM_operand_int16:
                case MVM_operand_int32:
                case MVM_operand_int64:
                case MVM_operand_num32:
                case MVM_operand_num64:
                case MVM_operand_str:
                case MVM_operand_coderef:
                    continue;

                case MVM_operand_ins:
                case MVM_operand_callsite:
                default:
                    goto fail;
            }

        check_reg:
            switch (flags & MVM_operand_type_mask) {
                case MVM_operand_int8:
                case MVM_operand_int16:
                case MVM_operand_int32:
                case MVM_operand_int64:
                case MVM_operand_num32:
                case MVM_operand_num64:
                case MVM_operand_str:
                case MVM_operand_obj:
                case MVM_operand_type_var:
                case MVM_operand_uint8:
                case MVM_operand_uint16:
                case MVM_operand_uint32:
                case MVM_operand_uint64:
                    continue;

                default:
                    goto fail;
            }

        fail:
            uv_mutex_unlock(&tc->instance->mutex_extop_registry);
            MVM_exception_throw_adhoc(tc,
                    "extension op %s has illegal signature", cname);
        }
    }

    entry = MVM_fixkey_hash_insert_nocheck(tc, &tc->instance->extop_registry, name);
    entry->func              = func;
    entry->info.name         = cname;
    entry->info.opcode       = (MVMuint16)-1;
    entry->info.num_operands = num_operands;
    entry->info.pure         = flags & MVM_EXTOP_PURE;
    entry->info.deopt_point  = 0;
    entry->info.logged       = 0;
    entry->info.no_inline    = flags & MVM_EXTOP_NOINLINE;
    entry->info.jittivity    = (flags & MVM_EXTOP_INVOKISH) ? MVM_JIT_INFO_INVOKISH : 0;
    entry->info.uses_hll     = 0;
    entry->info.uses_cache   = 0;
    entry->info.may_cause_deopt = 0;
    entry->info.specializable = spesh ? 1 : 0;
    memcpy(entry->info.operands, operands, num_operands);
    memset(entry->info.operands + num_operands, 0,
            MVM_MAX_OPERANDS - num_operands);
    entry->spesh      = spesh;
    entry->discover   = discover;
    entry->no_jit     = flags & MVM_EXTOP_NO_JIT;
    entry->allocating = flags & MVM_EXTOP_ALLOCATING;

    MVM_gc_root_add_permanent_desc(tc, (MVMCollectable **)&entry->hash_key,
        "Extension op name hash key");

    uv_mutex_unlock(&tc->instance->mutex_extop_registry);

    return 1;
}

const MVMOpInfo * MVM_ext_resolve_extop_record(MVMThreadContext *tc,
        MVMExtOpRecord *record) {

    /* Already resolved. */
    if (record->info)
        return record->info;

    if (!MVM_str_hash_key_is_valid(tc, record->name)) {
        MVM_str_hash_key_throw_invalid(tc, record->name);
    }

    uv_mutex_lock(&tc->instance->mutex_extop_registry);

    MVMExtOpRegistry *entry = MVM_fixkey_hash_fetch_nocheck(tc, &tc->instance->extop_registry, record->name);

    if (!entry) {
        uv_mutex_unlock(&tc->instance->mutex_extop_registry);
        return NULL;
    }

    /* Resolve record. */
    record->info       = &entry->info;
    record->func       = entry->func;
    record->spesh      = entry->spesh;
    record->discover   = entry->discover;
    record->no_jit     = entry->no_jit;
    record->allocating = entry->allocating;

    uv_mutex_unlock(&tc->instance->mutex_extop_registry);

    return record->info;
}
