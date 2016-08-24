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
void MVM_decoder_ensure_decoder(MVMThreadContext *tc, MVMObject *decoder, const char *op);
void MVM_decoder_configure(MVMThreadContext *tc, MVMDecoder *decoder,
                           MVMString *encoding, MVMObject *config);
MVMint64 MVM_decoder_empty(MVMThreadContext *tc, MVMDecoder *decoder);
void MVM_decoder_add_bytes(MVMThreadContext *tc, MVMDecoder *decoder, MVMObject *blob);
MVMString * MVM_decoder_take_all_chars(MVMThreadContext *tc, MVMDecoder *decoder);
MVMString * MVM_decoder_take_chars(MVMThreadContext *tc, MVMDecoder *decoder, MVMint64 chars);
