/* This represents the root of the serialization data; everything hangs
 * off this. In read mode, we don't do much besides populate and then
 * read this. In write mode, however, the tables and data chunks will be
 * filled out and grown as needed. */
typedef struct {
    /* The version of the serialization format. */
    MVMint32 version;

    /* The number of dependencies, as well as a pointer to the
     * dependencies table. */
    MVMint32  num_dependencies;
    char     *dependencies_table;

    /* The SC we're serializing/deserializing. */
    MVMSerializationContext *sc;

    /* List of the serialization context objects that we depend on. */
    MVMSerializationContext **dependent_scs;

    /* The number of STables, as well as pointers to the STables
     * table and data chunk. */
    MVMint32  num_stables;
    char     *stables_table;
    char     *stables_data;

    /* The number of objects, as well as pointers to the objects
     * table and data chunk. */
    MVMint32  num_objects;
    char     *objects_table;
    char     *objects_data;

    /* The number of closures, as we as a pointer to the closures
     * table. */
    MVMint32  num_closures;
    char     *closures_table;

    /* The number of contexts (e.g. frames), as well as pointers to
     * the contexts table and data chunk. */
    MVMint32  num_contexts;
    char     *contexts_table;
    char     *contexts_data;

    /* The number of repossessions and pointer to repossessions table. */
    MVMint32  num_repos;
    char     *repos_table;

    /* Array of STRINGs. */
    MVMObject *string_heap;
} MVMSerializationRoot;

/* Represents the serialization reader and the various functions available
 * on it. */
typedef struct _MVMSerializationReader {
    /* Serialization root data. */
    MVMSerializationRoot root;

    /* The object repossession conflicts list. */
    MVMObject *repo_conflicts_list;

    /* Current offsets for the data chunks (also correspond to the amount of
     * data written in to them). */
    MVMint32 stables_data_offset;
    MVMint32 objects_data_offset;
    MVMint32 contexts_data_offset;

    /* Limits up to where we can read stables, objects and contexts data. */
    char *stables_data_end;
    char *objects_data_end;
    char *contexts_data_end;

    /* Where to find details related to the current buffer we're reading from:
     * the buffer pointer itself, the current offset and the amount that is
     * allocated. These are all pointers back into this data structure. */
    char     **cur_read_buffer;
    MVMint32  *cur_read_offset;
    char     **cur_read_end;

    /* Various reading functions. */
    MVMint64    (*read_int)   (MVMThreadContext *tc, struct _MVMSerializationReader *reader);
    MVMint32    (*read_int32) (MVMThreadContext *tc, struct _MVMSerializationReader *reader);
    MVMint16    (*read_int16) (MVMThreadContext *tc, struct _MVMSerializationReader *reader);
    MVMnum64    (*read_num)   (MVMThreadContext *tc, struct _MVMSerializationReader *reader);
    MVMString * (*read_str)   (MVMThreadContext *tc, struct _MVMSerializationReader *reader);
    MVMObject * (*read_ref)   (MVMThreadContext *tc, struct _MVMSerializationReader *reader);
    MVMSTable * (*read_stable_ref) (MVMThreadContext *tc, struct _MVMSerializationReader *reader);

    /* List of code objects (static first, then all the closures). */
    MVMObject *codes_list;

    /* Array of contexts (num_contexts in length). */
    MVMFrame **contexts;

    /* The data, which we'll want to free after deserialization. */
    char *data;
} MVMSerializationReader;

/* Represents the serialization writer and the various functions available
 * on it. */
typedef struct _MVMSerializationWriter {
    /* Serialization root data. */
    MVMSerializationRoot root;

    /* Much more todo here... */

    /* Various writing functions. */
    void (*write_int) (MVMThreadContext *tc, struct _MVMSerializationWriter *writer, MVMint64 value);
    void (*write_num) (MVMThreadContext *tc, struct _MVMSerializationWriter *writer, MVMnum64 value);
    void (*write_str) (MVMThreadContext *tc, struct _MVMSerializationWriter *writer, MVMString *value);
    void (*write_ref) (MVMThreadContext *tc, struct _MVMSerializationWriter *writer, MVMObject *value);
    void (*write_stable_ref) (MVMThreadContext *tc, struct _MVMSerializationWriter *writer, MVMSTable *st);
} MVMSerializationWriter;

/* Core serialize and deserialize functions. */
void MVM_serialization_deserialize(MVMThreadContext *tc, MVMSerializationContext *sc,
    MVMObject *string_heap, MVMObject *codes_static, MVMObject *repo_conflicts,
    MVMString *data);
MVMString * MVM_sha1(MVMThreadContext *tc, MVMString *str);
