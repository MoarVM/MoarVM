/* Information about an allocation we are tracking in partial escape analysis. */
struct MVMSpeshPEAAllocation {
    /* The allocating instruction. */
    MVMSpeshIns *allocator;

    /* The allocated type. */
   MVMObject *type; 

    /* The set of indexes for registers we will hypothetically allocate for
     * the attributes of this type. */
    MVMuint16 *hypothetical_attr_reg_idxs;

    /* The last deopt instruction offset when we started to track this
     * allocation. */
    MVMuint32 initial_deopt_ins;

    /* Have we seen something that invalidates our ability to scalar replace
     * this? */
    MVMuint8 irreplaceable;
};

/* Information held per SSA value. */
struct MVMSpeshPEAInfo {
    /* If this value is an allocation that is potentially being scalar
     * replaced, it's stored in here. */
    MVMSpeshPEAAllocation *allocation;

    /* If the facts of this depend on the scalar replacement of a given
     * allocation, it is recorded here. */
    MVMSpeshPEAAllocation *depend_allocation;
};

void MVM_spesh_pea(MVMThreadContext *tc, MVMSpeshGraph *g);
