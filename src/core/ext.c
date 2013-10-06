#include "moar.h"

int MVM_ext_load(MVMThreadContext *tc, MVMString *lib, MVMString *ext) {
    MVMString *colon = MVM_string_ascii_decode_nt(
            tc, tc->instance->VMString, ":");
    MVMString *prefix = MVM_string_concatenate(tc, lib, colon);
    MVMString *name = MVM_string_concatenate(tc, prefix, ext);
    MVMExtRegistry *entry;
    MVMDLLSym *sym;
    void (*init)(MVMThreadContext *);

    uv_mutex_lock(&tc->instance->mutex_ext_registry);

    MVM_string_flatten(tc, name);
    MVM_HASH_GET(tc, tc->instance->ext_registry, name, entry);

    /* Extension already loaded. */
    if (entry) {
        uv_mutex_unlock(&tc->instance->mutex_ext_registry);
        return 0;
    }

    sym = (MVMDLLSym *)MVM_dll_find_symbol(tc, lib, ext);
    if (!sym) {
        uv_mutex_unlock(&tc->instance->mutex_ext_registry);
        MVM_exception_throw_adhoc(tc, "extension symbol not found");
    }

    entry = malloc(sizeof *entry);
    entry->sym = sym;
    entry->name = name;

    MVM_gc_root_add_permanent(tc, (MVMCollectable **)&entry->name);
    MVM_HASH_BIND(tc, tc->instance->ext_registry, name, entry);

    uv_mutex_unlock(&tc->instance->mutex_ext_registry);

    /* Call extension's initializer */
    init = (void (*)(MVMThreadContext *))sym->body.address;
    init(tc);

    return 1;
}

int MVM_ext_register_extop(MVMThreadContext *tc, const char *cname,
        MVMExtOpFunc func, MVMuint8 num_operands, MVMuint8 operands[]) {
    MVMExtOpRegistry *entry;
    MVMString *name = MVM_string_ascii_decode_nt(
            tc, tc->instance->VMString, cname);

    uv_mutex_lock(&tc->instance->mutex_extop_registry);

    MVM_string_flatten(tc, name);
    MVM_HASH_GET(tc, tc->instance->extop_registry, name, entry);

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
                case MVM_operand_obj:
                case MVM_operand_ins:
                case MVM_operand_type_var:
                case MVM_operand_coderef:
                case MVM_operand_callsite:
                    continue;

                default:
                    goto fail;
            }

        check_reg:
            switch (flags >> 3) {
                case MVM_reg_int8:
                case MVM_reg_int16:
                case MVM_reg_int32:
                case MVM_reg_int64:
                case MVM_reg_num32:
                case MVM_reg_num64:
                case MVM_reg_str:
                case MVM_reg_obj:
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

    entry = malloc(sizeof *entry);
    entry->func = func;
    entry->name = name;
    entry->info.name = cname;
    entry->info.opcode = (MVMuint16)-1;
    entry->info.mark[0] = '.';
    entry->info.mark[1] = 'x';
    entry->info.num_operands = num_operands;
    memcpy(entry->info.operands, operands, num_operands);

    MVM_gc_root_add_permanent(tc, (MVMCollectable **)&entry->name);
    MVM_HASH_BIND(tc, tc->instance->extop_registry, name, entry);

    uv_mutex_unlock(&tc->instance->mutex_extop_registry);

    return 1;
}
