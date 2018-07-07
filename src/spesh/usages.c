#include "moar.h"

/* Adds a usage of an SSA value. */
void MVM_spesh_usages_add(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshFacts *facts, MVMSpeshIns *by) {
    MVMSpeshUseChainEntry *entry = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshUseChainEntry));
    entry->user = by;
    entry->next = facts->usage.users;
    facts->usage.users = entry;
}
void MVM_spesh_usages_add_by_reg(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshOperand used, MVMSpeshIns *by) {
    MVM_spesh_usages_add(tc, g, MVM_spesh_get_facts(tc, g, used), by);
}

/* Removes a usage of an SSA value. */
void MVM_spesh_usages_delete(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshFacts *facts, MVMSpeshIns *by) {
    MVMSpeshUseChainEntry *cur = facts->usage.users;
    MVMSpeshUseChainEntry *prev = NULL;
    while (cur) {
        if (cur->user == by) {
            if (prev)
                prev->next = cur->next;
            else
                facts->usage.users = cur->next;
            return;
        }
        prev = cur;
        cur = cur->next;
    }
    MVM_oops(tc, "Spesh: instruction missing from define-use chain");
}
void MVM_spesh_usages_delete_by_reg(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshOperand used, MVMSpeshIns *by) {
    MVM_spesh_usages_delete(tc, g, MVM_spesh_get_facts(tc, g, used), by);
}

/* Marks that an SSA value is required for deopt purposes. */
void MVM_spesh_usages_add_for_deopt(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshFacts *facts) {
    facts->usage.deopt_required = 1;
}

/* Checks if the value is used, either by another instruction in the graph or
 * by being needed for deopt. */
MVMuint32 MVM_spesh_usages_is_used(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshOperand check) {
    MVMSpeshFacts *facts = MVM_spesh_get_facts(tc, g, check);
    return facts->usage.deopt_required || facts->usage.users;
}

/* Checks if the value is used due to being required for deopt. */
MVMuint32 MVM_spesh_usages_is_used_by_deopt(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshOperand check) {
    MVMSpeshFacts *facts = MVM_spesh_get_facts(tc, g, check);
    return facts->usage.deopt_required;
}

/* Checks if there is precisely one known non-deopt user of the value. */
MVMuint32 MVM_spesh_usages_used_once(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshOperand check) {
    MVMSpeshFacts *facts = MVM_spesh_get_facts(tc, g, check);
    return !facts->usage.deopt_required && facts->usage.users && !facts->usage.users->next;
}

/* Gets the count of usages, excluding use for deopt purposes. */
MVMuint32 MVM_spesh_usages_count(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshOperand check) {
    MVMuint32 count = 0;
    MVMSpeshUseChainEntry *cur = MVM_spesh_get_facts(tc, g, check)->usage.users;
    while (cur) {
        count++;
        cur = cur->next;
    }
    return count;
}
