/* Representation used for a VM-provided decoder. */
struct MVMDecoderBody {
    MVMDecodeStream *ds;
};
struct MVMDecoder {
    MVMObject common;
    MVMDecoderBody body;
};

/* Function for REPR setup. */
const MVMREPROps * MVMDecoder_initialize(MVMThreadContext *tc);

/* Operations on a Decoder object. */
