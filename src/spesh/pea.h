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

/* Information that we retain about a replaced allocations in order that we
 * can materialize them upon deoptimization. */
struct MVMSpeshPEADeopt {
    /* Array of materialization info, specifying how to materialize a given
     * replaced object. */
    MVM_VECTOR_DECL(MVMSpeshPEAMaterializeInfo, materialize_info);

    /* Pairings of deoptimization points and objects to materialize at those
     * point. */
    MVM_VECTOR_DECL(MVMSpeshPEADeoptPoint, deopt_point);
};

/* The information needed to materialize a particular replaced allocation
 * (that is, to recreate it on the heap). */
struct MVMSpeshPEAMaterializeInfo {
    /* The spesh slot containing the STable of the object to materialize. */
    MVMuint16 stable_sslot;

    /* The register to materialize into. */
    MVMuint16 target_reg;

    /* A list of the registers holding the attributes to put into the
     * materialized object. */
    MVMuint16 *attr_regs;
};

/* Information about that needs to be materialized at a particular deopt
 * point. */
struct MVMSpeshPEADeoptPoint {
    /* The index of the deopt point. */
    MVMint32 deopt_point_idx;

    /* The index into the materialize_info specifying how to materialize
     * this object. */
    MVMuint32 materialize_info_idx;
};

void MVM_spesh_pea(MVMThreadContext *tc, MVMSpeshGraph *g);
void MVM_spesh_pea_destroy_deopt_info(MVMThreadContext *tc, MVMSpeshPEADeopt *deopt_pea);
