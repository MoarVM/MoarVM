#include <moarvm.h>

/* Takes serialized data, an empty SerializationContext to deserialize it into,
 * a strings heap and the set of static code refs for the compilation unit.
 * Deserializes the data into the required objects and STables. */
void MVM_serialization_deserialize(MVMThreadContext *tc, MVMObject *sc,
        MVMObject *string_heap, MVMObject *codes_static, 
        MVMObject *repo_conflicts, MVMString *data) {
    printf("WARNING: Deserialization NYI\n");
}
