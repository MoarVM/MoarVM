#include "moar.h"

/* Maps outside-world normalization form codes to our internal set, validating
 * that we got something valid. */
MVMNormalization MVN_unicode_normalizer_form(MVMThreadContext *tc, MVMint64 form_in) {
    switch (form_in) {
    case 1: return MVM_NORMALIZE_NFC;
    case 2: return MVM_NORMALIZE_NFD;
    case 3: return MVM_NORMALIZE_NFKC;
    case 4: return MVM_NORMALIZE_NFKD;
    default: MVM_exception_throw_adhoc(tc, "Invalid normalization form %d", (int)form_in);
    }
}

/* Takes two objects, which must be of VMArray representation and holding
 * 32-bit integers. Performs normalization to the specified form. */
static void assert_codepoint_array(MVMThreadContext *tc, MVMObject *arr, char *error) {
    if (IS_CONCRETE(arr) && REPR(arr)->ID == MVM_REPR_ID_MVMArray) {
        MVMuint8 slot_type = ((MVMArrayREPRData *)STABLE(arr)->REPR_data)->slot_type;
        if (slot_type == MVM_ARRAY_I32 || slot_type == MVM_ARRAY_U32)
            return;
    }
    MVM_exception_throw_adhoc(tc, error);
}
MVM_STATIC_INLINE void maybe_grow_result(MVMCodepoint **result, MVMint64 *result_alloc, MVMint64 needed) {
    if (needed >= *result_alloc) {
        *result_alloc += 32;
        *result = MVM_realloc(*result, *result_alloc * sizeof(MVMCodepoint));
    }
}
void MVM_unicode_normalize_codepoints(MVMThreadContext *tc, MVMObject *in, MVMObject *out, MVMNormalization form) {
    MVMNormalizer  norm;
    MVMCodepoint  *input;
    MVMCodepoint  *result;
    MVMint64       input_pos, input_codes, result_pos, result_alloc;
    MVMint32       ready;

    /* Validate input/output array. */
    assert_codepoint_array(tc, in, "Normalization input must be native array of 32-bit integers");
    assert_codepoint_array(tc, out, "Normalization output must be native array of 32-bit integers");

    /* Get input array; if it's empty, we're done already. */
    input       = (MVMCodepoint *)((MVMArray *)in)->body.slots.u32 + ((MVMArray *)in)->body.start;
    input_codes = ((MVMArray *)in)->body.elems;
    if (input_codes == 0)
        return;

    /* Guess output size based on input size. */
    result_alloc = input_codes;
    result       = MVM_malloc(result_alloc * sizeof(MVMCodepoint));

    /* Perform normalization. */
    MVM_unicode_normalizer_init(tc, &norm, form);
    input_pos  = 0;
    result_pos = 0;
    while (input_pos < input_codes) {
        MVMCodepoint cp;
        ready = MVM_unicode_normalizer_process_codepoint(tc, &norm, input[input_pos], &cp);
        if (ready) {
            maybe_grow_result(&result, &result_alloc, result_pos + ready);
            result[result_pos++] = cp;
            while (--ready > 0)
                result[result_pos++] = MVM_unicode_normalizer_get_codepoint(tc, &norm);
        }
        input_pos++;
    }
    MVM_unicode_normalizer_eof(tc, &norm);
    ready = MVM_unicode_normalizer_available(tc, &norm);
    maybe_grow_result(&result, &result_alloc, result_pos + ready);
    while (ready--)
        result[result_pos++] = MVM_unicode_normalizer_get_codepoint(tc, &norm);
    MVM_unicode_normalizer_cleanup(tc, &norm);

    /* Put result into array body. */
    ((MVMArray *)out)->body.slots.u32 = result;
    ((MVMArray *)out)->body.start     = 0;
    ((MVMArray *)out)->body.elems     = result_pos;
}

/* Initialize the MVMNormalizer pointed to to perform the specified kind of
 * normalization. */
void MVM_unicode_normalizer_init(MVMThreadContext *tc, MVMNormalizer *n, MVMNormalization form) {
    n->form = form;
}

/* Cleanup an MVMNormalization once we're done normalizing. */
void MVM_unicode_normalizer_cleanup(MVMThreadContext *tc, MVMNormalizer *n) {
}

/* Called when we are expecting no more codepoints. */
void MVM_unicode_normalizer_eof(MVMThreadContext *tc, MVMNormalizer *n) {
}
