/* Information about an allocation we are tracking in partial escape analysis. */
struct MVMSpeshPEAAllocation {
    /* The allocating instruction. */
    MVMSpeshIns *allocator;

    /* The allocated type. */
   MVMObject *type; 

    /* The set of indexes for registers we will hypothetically allocate for
     * the attributes of this type. */
    MVMuint16 *hypothetical_attr_reg_idxs;
};

/* Information held per SSA value. */
struct MVMSpeshPEAInfo {
    MVMSpeshPEAAllocation *allocation;
};

void MVM_spesh_pea(MVMThreadContext *tc, MVMSpeshGraph *g);
