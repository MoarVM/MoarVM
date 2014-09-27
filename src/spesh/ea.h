#define MVM_EA_NOESCAPE     0
#define MVM_EA_ESCAPE       1

struct MVMSpeshAllocation {
    MVMSpeshIns *allocating_ins;
    MVMuint32    escape_state;

    MVMSpeshAllocation **contained;
    MVMuint32            num_contained;
    MVMuint32            alloc_contained;

    MVMSpeshAllocation *equi_parent;
};
