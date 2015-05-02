/* This represents the root of the serialization data; everything hangs
 * off this. In read mode, we don't do much besides populate and then
 * read this. In write mode, however, the tables and data chunks will be
 * filled out and grown as needed. */
struct MVMSerializationRoot {
    /* The version of the serialization format. */
    MVMint32 version;

    /* The SC we're serializing/deserializing. */
    MVMSerializationContext *sc;

    /* List of the serialization context objects that we depend on. */
    MVMSerializationContext **dependent_scs;

    /* The number of dependencies, as well as a pointer to the
     * dependencies table. */
    char     *dependencies_table;
    MVMint32  num_dependencies;

    /* The number of STables, as well as pointers to the STables
     * table and data chunk. */
    MVMint32  num_stables;
    char     *stables_table;
    char     *stables_data;

    /* The number of objects, as well as pointers to the objects
     * table and data chunk. */
    char     *objects_table;
    char     *objects_data;
    MVMint32  num_objects;

    /* The number of closures, as we as a pointer to the closures
     * table. */
    MVMint32  num_closures;
    char     *closures_table;

    /* The number of contexts (e.g. frames), as well as pointers to
     * the contexts table and data chunk. */
    char     *contexts_table;
    char     *contexts_data;
    MVMint32  num_contexts;

    /* The number of repossessions and pointer to repossessions table. */
    MVMint32  num_repos;
    char     *repos_table;

    /* The number of parameterized type intern entries, and the data segment
     * containing them. */
    MVMint32  num_param_interns;
    char     *param_interns_data;

    /* Array of strings making up the string heap we are constructing. If we
     * are reading, this will either contain a string heap array or be NULL
     * and the next field will be set. */
    MVMObject *string_heap;

    /* The compilation unit whose string heap we will use to locate strings.
     * This must be set of string_heap about is set to NULL. */
    MVMCompUnit *string_comp_unit;
};

/* Indexes the deserializer still has to work on. */
struct MVMDeserializeWorklist {
    MVMuint32 *indexes;
    MVMuint32  num_indexes;
    MVMuint32  alloc_indexes;
};

/* Represents the serialization reader and the various functions available
 * on it. */
struct MVMSerializationReader {
    /* Serialization root data. */
    MVMSerializationRoot root;

    /* Current offsets for the data chunks (also correspond to the amount of
     * data written in to them). */
    MVMint32 stables_data_offset;
    MVMint32 objects_data_offset;
    MVMint32 contexts_data_offset;
    MVMint32 param_interns_data_offset;

    /* Limits up to where we can read stables, objects and contexts data. */
    char *stables_data_end;
    char *objects_data_end;
    char *contexts_data_end;
    char *param_interns_data_end;

    /* Where to find details related to the current buffer we're reading from:
     * the buffer pointer itself, the current offset and the amount that is
     * allocated. These are all pointers back into this data structure. */
    char     **cur_read_buffer;
    MVMint32  *cur_read_offset;
    char     **cur_read_end;

    /* List of code objects (static first, then all the closures). */
    MVMObject *codes_list;

    /* Number of static code objects. */
    MVMuint32 num_static_codes;

    /* Array of contexts (num_contexts in length). */
    MVMFrame **contexts;

    /* Set of current worklists, for things we need to fully desrialize. When
     * they are all empty, the current (usually lazy) deserialization work is
     * done, and we have the required object graph. */
    MVMDeserializeWorklist wl_objects;
    MVMDeserializeWorklist wl_stables;

    /* Whether we're already working on these worklists. */
    MVMuint32 working;

    /* The current object we're deserializing. */
    MVMObject *current_object;

    /* The data, which we may want to free when the SC goes away; a flag
     * indicates when it should be. */
    char      *data;
    MVMuint32  data_needs_free;
};

/* Represents the serialization writer and the various functions available
 * on it. */
struct MVMSerializationWriter {
    /* Serialization root data. */
    MVMSerializationRoot root;

    /* The code refs and contexts lists we're working through/adding to. */
    MVMObject  *codes_list;
    MVMObject  *contexts_list;

    /* Current position in the stables, objects and contexts lists. */
    MVMint64 stables_list_pos;
    MVMint64 objects_list_pos;
    MVMint64 contexts_list_pos;

    /* Hash of strings we've already seen while serializing to the index they
     * are placed at in the string heap. */
    MVMObject *seen_strings;

    /* Amount of memory allocated for various things. */
    MVMuint32 dependencies_table_alloc;
    MVMuint32 stables_table_alloc;
    MVMuint32 stables_data_alloc;
    MVMuint32 objects_table_alloc;
    MVMuint32 objects_data_alloc;
    MVMuint32 closures_table_alloc;
    MVMuint32 contexts_table_alloc;
    MVMuint32 contexts_data_alloc;
    MVMuint32 repos_table_alloc;
    MVMuint32 param_interns_data_alloc;

    /* Current offsets for the data chunks (also correspond to the amount of
     * data written in to them). */
    MVMuint32 stables_data_offset;
    MVMuint32 objects_data_offset;
    MVMuint32 contexts_data_offset;
    MVMuint32 param_interns_data_offset;

    /* Where to find details related to the current buffer we're writing in
     * to: the buffer pointer itself, the current offset and the amount that
     * is allocated. These are all pointers back into this data structure. */
    char      **cur_write_buffer;
    MVMuint32  *cur_write_offset;
    MVMuint32  *cur_write_limit;
};

/* Core serialize and deserialize functions. */
void MVM_serialization_deserialize(MVMThreadContext *tc, MVMSerializationContext *sc,
    MVMObject *string_heap, MVMObject *codes_static, MVMObject *repo_conflicts,
    MVMString *data);
MVMString * MVM_sha1(MVMThreadContext *tc, MVMString *str);
MVMString * MVM_serialization_serialize(MVMThreadContext *tc, MVMSerializationContext *sc,
    MVMObject *empty_string_heap);

/* Functions for demanding an object/STable/code be made available (that is,
 * by lazily deserializing it). */
MVMObject * MVM_serialization_demand_object(MVMThreadContext *tc, MVMSerializationContext *sc, MVMint64 idx);
MVMSTable * MVM_serialization_demand_stable(MVMThreadContext *tc, MVMSerializationContext *sc, MVMint64 idx);
MVMObject * MVM_serialization_demand_code(MVMThreadContext *tc, MVMSerializationContext *sc, MVMint64 idx);
void MVM_serialization_finish_deserialize_method_cache(MVMThreadContext *tc, MVMSTable *st);

/* Reader/writer functions. */
MVMint64 MVM_serialization_read_int(MVMThreadContext *tc, MVMSerializationReader *reader);
MVMint64 MVM_serialization_read_varint(MVMThreadContext *tc, MVMSerializationReader *reader);
MVMnum64 MVM_serialization_read_num(MVMThreadContext *tc, MVMSerializationReader *reader);
MVMString * MVM_serialization_read_str(MVMThreadContext *tc, MVMSerializationReader *reader);
MVMObject * MVM_serialization_read_ref(MVMThreadContext *tc, MVMSerializationReader *reader);
MVMSTable * MVM_serialization_read_stable_ref(MVMThreadContext *tc, MVMSerializationReader *reader);
void MVM_serialization_force_stable(MVMThreadContext *tc, MVMSerializationReader *reader, MVMSTable *st);
void MVM_serialization_write_int(MVMThreadContext *tc, MVMSerializationWriter *writer, MVMint64 value);
void MVM_serialization_write_varint(MVMThreadContext *tc, MVMSerializationWriter *writer, MVMint64 value);
void MVM_serialization_write_num(MVMThreadContext *tc, MVMSerializationWriter *writer, MVMnum64 value);
void MVM_serialization_write_str(MVMThreadContext *tc, MVMSerializationWriter *writer, MVMString *value);
void MVM_serialization_write_ref(MVMThreadContext *tc, MVMSerializationWriter *writer, MVMObject *ref);
void MVM_serialization_write_stable_ref(MVMThreadContext *tc, MVMSerializationWriter *writer, MVMSTable *st);
