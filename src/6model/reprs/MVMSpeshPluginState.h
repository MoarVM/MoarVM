/* Representation used for holding spesh plugin data. */
/* Representation used for a data structure that hangs off a static frame's
 * spesh state, holding the guard tree at each applicable bytecode position.
 * Updated versions are installed using atomic operations. */

struct MVMSpeshPluginStateBody {
    /* Array of position states. Held in bytecode index order, which
     * allows for binary searching. */
    MVMSpeshPluginPosition *positions;

    /* Number of bytecode positions we have state at. */
    MVMuint32 num_positions;
};
struct MVMSpeshPluginState {
    MVMObject common;
    MVMSpeshPluginStateBody body;
};

/* Function for REPR setup. */
const MVMREPROps * MVMSpeshPluginState_initialize(MVMThreadContext *tc);
