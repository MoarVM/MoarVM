#include "moar.h"

int MVM_ext_load(MVMThreadContext *tc, MVMString *lib, MVMString *ext) {
    MVMString *colon, *prefix, *name;
    MVMExtRegistry *entry;
    MVMDLLSym *sym;
    void (*init)(MVMThreadContext *);

    MVMROOT(tc, lib, {
    MVMROOT(tc, ext, {
        colon = MVM_string_ascii_decode_nt(
            tc, tc->instance->VMString, ":");
        prefix = MVM_string_concatenate(tc, lib, colon);
        name = MVM_string_concatenate(tc, prefix, ext);
    });
    });

    uv_mutex_lock(&tc->instance->mutex_ext_registry);

    MVM_HASH_GET(tc, tc->instance->ext_registry, name, entry);

    /* Extension already loaded. */
    if (entry) {
        uv_mutex_unlock(&tc->instance->mutex_ext_registry);
        return 0;
    }

    MVMROOT(tc, name, {
        sym = (MVMDLLSym *)MVM_dll_find_symbol(tc, lib, ext);
    });
    if (!sym) {
        uv_mutex_unlock(&tc->instance->mutex_ext_registry);
        MVM_exception_throw_adhoc(tc, "extension symbol not found");
    }

    entry = MVM_malloc(sizeof *entry);
    entry->sym = sym;
    entry->name = name;

    MVM_gc_root_add_permanent_desc(tc, (MVMCollectable **)&entry->name,
        "Extension name");
    MVM_HASH_BIND(tc, tc->instance->ext_registry, name, entry);

    uv_mutex_unlock(&tc->instance->mutex_ext_registry);

    /* Call extension's initializer */
    init = (void (*)(MVMThreadContext *))sym->body.address;
    init(tc);

    return 1;
}

int MVM_ext_register_extop(MVMThreadContext *tc, const char *cname,
        MVMExtOpFunc func, MVMuint8 num_operands, MVMuint8 operands[],
        MVMExtOpSpesh *spesh, MVMExtOpFactDiscover *discover, MVMuint32 flags) {
    MVMExtOpRegistry *entry;
    MVMString *name = MVM_string_ascii_decode_nt(
            tc, tc->instance->VMString, cname);

    uv_mutex_lock(&tc->instance->mutex_extop_registry);

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

    entry                    = MVM_malloc(sizeof *entry);
    entry->name              = name;
    entry->func              = func;
    entry->info.name         = cname;
    entry->info.opcode       = (MVMuint16)-1;
    entry->info.mark[0]      = '.';
    entry->info.mark[1]      = 'x';
    entry->info.num_operands = num_operands;
    entry->info.pure         = flags & MVM_EXTOP_PURE;
    entry->info.deopt_point  = 0;
    entry->info.no_inline    = flags & MVM_EXTOP_NOINLINE;
    entry->info.jittivity    = (flags & MVM_EXTOP_INVOKISH) ? MVM_JIT_INFO_INVOKISH : 0;
    memcpy(entry->info.operands, operands, num_operands);
    memset(entry->info.operands + num_operands, 0,
            MVM_MAX_OPERANDS - num_operands);
    entry->spesh      = spesh;
    entry->discover   = discover;
    entry->no_jit     = flags & MVM_EXTOP_NO_JIT;
    entry->allocating = flags & MVM_EXTOP_ALLOCATING;

    MVM_gc_root_add_permanent_desc(tc, (MVMCollectable **)&entry->name,
        "Extension op name");
    MVM_HASH_BIND(tc, tc->instance->extop_registry, name, entry);

    uv_mutex_unlock(&tc->instance->mutex_extop_registry);

    return 1;
}

const MVMOpInfo * MVM_ext_resolve_extop_record(MVMThreadContext *tc,
        MVMExtOpRecord *record) {
    MVMExtOpRegistry *entry;

    /* Already resolved. */
    if (record->info)
        return record->info;

    uv_mutex_lock(&tc->instance->mutex_extop_registry);

    MVM_HASH_GET(tc, tc->instance->extop_registry, record->name, entry);

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
