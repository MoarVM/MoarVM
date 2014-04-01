#include "moar.h"

/* The code in this file walks the spesh graph, recording facts we discover
 * about each version of each local variable, and propagating the info as it
 * can. */

/* Copies facts from one var to another. */
static void copy_facts(MVMThreadContext *tc, MVMSpeshGraph *g, MVMuint16 to_orig,
                       MVMuint16 to_i, MVMuint16 from_orig, MVMuint16 from_i) {
    g->facts[to_orig][to_i] = g->facts[from_orig][from_i];
}

/* Handles object-creating instructions. */
static void create_facts(MVMThreadContext *tc, MVMSpeshGraph *g, MVMuint16 obj_orig,
                         MVMuint16 obj_i, MVMuint16 type_orig, MVMuint16 type_i) {
    /* The type is carried. */
    if (g->facts[type_orig][type_i].flags & MVM_SPESH_FACT_KNOWN_TYPE) {
        g->facts[obj_orig][obj_i].type   = g->facts[type_orig][type_i].type;
        g->facts[obj_orig][obj_i].flags |= MVM_SPESH_FACT_KNOWN_TYPE;
    }

    /* We know it's a concrete object. */
    g->facts[obj_orig][obj_i].flags |= MVM_SPESH_FACT_CONCRETE;

    /* If we know the original value, then we can check the type to see if
     * it's a container type. */
    if (g->facts[type_orig][type_i].flags & MVM_SPESH_FACT_KNOWN_VALUE) {
        MVMObject *value = g->facts[type_orig][type_i].value.o;
        if (value && !STABLE(value)->container_spec)
            g->facts[obj_orig][obj_i].flags |= MVM_SPESH_FACT_DECONTED;
    }
}

/* Adds facts from knowing the exact value being put into an object local. */
static void object_facts(MVMThreadContext *tc, MVMSpeshGraph *g, MVMuint16 tgt_orig,
                         MVMuint16 tgt_i, MVMObject *obj) {
    /* Ensure it's non-null. */
    if (!obj)
        return;

    /* Set the value itself. */
    g->facts[tgt_orig][tgt_i].value.o  = obj;
    g->facts[tgt_orig][tgt_i].flags   |= MVM_SPESH_FACT_KNOWN_VALUE;

    /* We also know the type. */
    g->facts[tgt_orig][tgt_i].type   = STABLE(obj)->WHAT;
    g->facts[tgt_orig][tgt_i].flags |= MVM_SPESH_FACT_KNOWN_TYPE;

    /* Set concreteness and decontainerized flags. */
    if (IS_CONCRETE(obj)) {
        g->facts[tgt_orig][tgt_i].flags |= MVM_SPESH_FACT_CONCRETE;
        if (!STABLE(obj)->container_spec)
            g->facts[tgt_orig][tgt_i].flags |= MVM_SPESH_FACT_DECONTED;
    }
    else {
        g->facts[tgt_orig][tgt_i].flags |= MVM_SPESH_FACT_TYPEOBJ | MVM_SPESH_FACT_DECONTED;
    }
}

/* Propagates information relating to decontainerization. */
static void decont_facts(MVMThreadContext *tc, MVMSpeshGraph *g, MVMuint16 out_orig,
                         MVMuint16 out_i, MVMuint16 in_orig, MVMuint16 in_i) {
    /* If we know the original is decontainerized already, just copy its
     * info. */
    if (g->facts[in_orig][in_i].flags & MVM_SPESH_FACT_DECONTED)
        copy_facts(tc, g, out_orig, out_i, in_orig, in_i);

    /* We know the result is decontainerized. */
    g->facts[out_orig][out_i].flags |= MVM_SPESH_FACT_DECONTED;
}

/* Looks up a wval and adds information based on it. */
static void wval_facts(MVMThreadContext *tc, MVMSpeshGraph *g, MVMuint16 tgt_orig,
                       MVMuint16 tgt_i, MVMuint16 dep, MVMint64 idx) {
    MVMCompUnit *cu = g->sf->body.cu;
    if (dep >= 0 && dep < cu->body.num_scs) {
        MVMSerializationContext *sc = MVM_sc_get_sc(tc, cu, dep);
        if (sc)
            object_facts(tc, g, tgt_orig, tgt_i, MVM_sc_get_object(tc, sc, idx));
    }
}

/* Visits the blocks in dominator tree order, recursively. */
static void add_bb_facts(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB *bb) {
    MVMint32 i;

    /* Look for instructions that provide or propagate facts. */
    MVMSpeshIns *ins = bb->first_ins;
    while (ins) {
        switch (ins->info->opcode) {
        case MVM_OP_set:
            copy_facts(tc, g,
                ins->operands[0].reg.orig, ins->operands[0].reg.i,
                ins->operands[1].reg.orig, ins->operands[1].reg.i);
            break;
        case MVM_OP_create:
            create_facts(tc, g,
                ins->operands[0].reg.orig, ins->operands[0].reg.i,
                ins->operands[1].reg.orig, ins->operands[1].reg.i);
            break;
        case MVM_OP_bootint:
            object_facts(tc, g,
                ins->operands[0].reg.orig, ins->operands[0].reg.i,
                tc->instance->boot_types.BOOTInt);
            break;
        case MVM_OP_bootnum:
            object_facts(tc, g,
                ins->operands[0].reg.orig, ins->operands[0].reg.i,
                tc->instance->boot_types.BOOTNum);
            break;
        case MVM_OP_bootstr:
            object_facts(tc, g,
                ins->operands[0].reg.orig, ins->operands[0].reg.i,
                tc->instance->boot_types.BOOTStr);
            break;
        case MVM_OP_bootarray:
            object_facts(tc, g,
                ins->operands[0].reg.orig, ins->operands[0].reg.i,
                tc->instance->boot_types.BOOTArray);
            break;
        case MVM_OP_bootintarray:
            object_facts(tc, g,
                ins->operands[0].reg.orig, ins->operands[0].reg.i,
                tc->instance->boot_types.BOOTIntArray);
            break;
        case MVM_OP_bootnumarray:
            object_facts(tc, g,
                ins->operands[0].reg.orig, ins->operands[0].reg.i,
                tc->instance->boot_types.BOOTNumArray);
            break;
        case MVM_OP_bootstrarray:
            object_facts(tc, g,
                ins->operands[0].reg.orig, ins->operands[0].reg.i,
                tc->instance->boot_types.BOOTStrArray);
            break;
        case MVM_OP_boothash:
            object_facts(tc, g,
                ins->operands[0].reg.orig, ins->operands[0].reg.i,
                tc->instance->boot_types.BOOTHash);
            break;
        case MVM_OP_hllboxtype_i:
            object_facts(tc, g,
                ins->operands[0].reg.orig, ins->operands[0].reg.i,
                g->sf->body.cu->body.hll_config->int_box_type);
            break;
        case MVM_OP_hllboxtype_n:
            object_facts(tc, g,
                ins->operands[0].reg.orig, ins->operands[0].reg.i,
                g->sf->body.cu->body.hll_config->num_box_type);
            break;
        case MVM_OP_hllboxtype_s:
            object_facts(tc, g,
                ins->operands[0].reg.orig, ins->operands[0].reg.i,
                g->sf->body.cu->body.hll_config->str_box_type);
            break;
        case MVM_OP_hlllist:
            object_facts(tc, g,
                ins->operands[0].reg.orig, ins->operands[0].reg.i,
                g->sf->body.cu->body.hll_config->slurpy_array_type);
            break;
        case MVM_OP_hllhash:
            object_facts(tc, g,
                ins->operands[0].reg.orig, ins->operands[0].reg.i,
                g->sf->body.cu->body.hll_config->slurpy_hash_type);
            break;
        case MVM_OP_decont:
            decont_facts(tc, g,
                ins->operands[0].reg.orig, ins->operands[0].reg.i,
                ins->operands[1].reg.orig, ins->operands[1].reg.i);
            break;
        case MVM_OP_wval:
            wval_facts(tc, g,
                ins->operands[0].reg.orig, ins->operands[0].reg.i,
                ins->operands[1].lit_i16, ins->operands[2].lit_i16);
            break;
        case MVM_OP_wval_wide:
            wval_facts(tc, g,
                ins->operands[0].reg.orig, ins->operands[0].reg.i,
                ins->operands[1].lit_i16, ins->operands[2].lit_i64);
            break;
        }
        ins = ins->next;
    }

    /* Visit children. */
    for (i = 0; i < bb->num_children; i++)
        add_bb_facts(tc, g, bb->children[i]);
}

/* Kicks off fact discovery from the top of the (dominator) tree. */
void MVM_spesh_facts_discover(MVMThreadContext *tc, MVMSpeshGraph *g) {
    add_bb_facts(tc, g, g->entry);
}
