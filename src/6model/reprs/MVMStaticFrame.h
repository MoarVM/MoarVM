/* Representation for static code in the VM. */
struct MVMStaticFrameBody {
    /* The start of the stream of bytecode for this routine. */
    MVMuint8 *bytecode;

    /* The compilation unit this frame belongs to. */
    MVMCompUnit *cu;

    /* The list of local types. */
    MVMuint16 *local_types;

    /* The list of lexical types. */
    MVMuint16 *lexical_types;

    /* Lexicals name map. */
    MVMLexicalRegistry *lexical_names;
    MVMLexicalRegistry **lexical_names_list;

    /* The environment for this frame, which lives beyond its execution. */

    /* Defaults for lexicals upon new frame creation. */
    MVMRegister *static_env;

    /* Flag for if this frame has been invoked ever. */
    MVMuint32 invoked;

    /* The size in bytes to allocate for the lexical environment. */
    MVMuint32 env_size;

    /* The size in bytes to allocate for the work and arguments area. */
    MVMuint32 work_size;

    /* The size of the bytecode. */
    MVMuint32 bytecode_size;

    /* Count of locals. */
    MVMuint32 num_locals;

    /* Count of lexicals. */
    MVMuint32 num_lexicals;

    /* The number of exception handlers this frame has. */
    MVMuint32 num_handlers;

    /* Frame exception handlers information. */
    MVMFrameHandler *handlers;

    /* The compilation unit unique ID of this frame. */
    MVMString *cuuid;

    /* The name of this frame. */
    MVMString *name;

    /* This frame's static outer frame. */
    MVMStaticFrame *outer;

    /* the static coderef */
    MVMCode *static_code;

    /* Index into each threadcontext's table of frame pools. */
    MVMuint32 pool_index;

    /* Annotation details */
    MVMuint32              num_annotations;
    MVMuint8              *annotations_data;

    /* Cached instruction offsets */
    MVMuint8 *instr_offsets;
};
struct MVMStaticFrame {
    MVMObject common;
    MVMStaticFrameBody body;
};

/* Function for REPR setup. */
const MVMREPROps * MVMStaticFrame_initialize(MVMThreadContext *tc);
