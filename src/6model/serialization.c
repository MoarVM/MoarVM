#include <moarvm.h>

#define MAX(x, y) ((y) > (x) ? (y) : (x))

/* Version of the serialization format that we are currently at and lowest
 * version we support. */
#define CURRENT_VERSION 4
#define MIN_VERSION     4

/* Various sizes (in bytes). */
#define HEADER_SIZE                 4 * 16
#define DEP_TABLE_ENTRY_SIZE        8
#define STABLES_TABLE_ENTRY_SIZE    12
#define OBJECTS_TABLE_ENTRY_SIZE    16
#define CLOSURES_TABLE_ENTRY_SIZE   24
#define CONTEXTS_TABLE_ENTRY_SIZE   16
#define REPOS_TABLE_ENTRY_SIZE      16

/* Some guesses. */
#define DEFAULT_STABLE_DATA_SIZE     4096
#define STABLES_TABLE_ENTRIES_GUESS  16
#define OBJECT_SIZE_GUESS            8
#define CLOSURES_TABLE_ENTRIES_GUESS 16
#define CONTEXTS_TABLE_ENTRIES_GUESS 4
#define DEFAULT_CONTEXTS_DATA_SIZE   1024

/* Possible reference types we can serialize. */
#define REFVAR_NULL                 1
#define REFVAR_OBJECT               2
#define REFVAR_VM_NULL              3
#define REFVAR_VM_INT               4
#define REFVAR_VM_NUM               5
#define REFVAR_VM_STR               6
#define REFVAR_VM_ARR_VAR           7
#define REFVAR_VM_ARR_STR           8
#define REFVAR_VM_ARR_INT           9
#define REFVAR_VM_HASH_STR_VAR      10
#define REFVAR_STATIC_CODEREF       11
#define REFVAR_CLONED_CODEREF       12

/* Endian translation (file format is little endian, so on big endian we need
 * to twiddle. */
#if MVM_BIGENDIAN
static void switch_endian(char *bytes, size_t size)
{
    size_t low  = 0;
    size_t high = size - 1;
    while (high > low) {
        char tmp    = bytes[low];
        bytes[low]  = bytes[high];
        bytes[high] = tmp;
        low++;
        high--;
    }
}
#endif

/* Base64 encoding */
static char * base64_encode(const void *buf, size_t size)
{
	static const char base64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

	char* str = (char*) malloc((size+3)*4/3 + 1);

	char* p = str;
	const unsigned char* q = (const unsigned char*) buf;
	size_t i = 0;

	while (i < size) {
		int c = q[i++];
		c *= 256;
		if (i < size)
            c += q[i];
		i++;

		c *= 256;
		if (i < size)
            c += q[i];
		i++;

		*p++ = base64[(c & 0x00fc0000) >> 18];
		*p++ = base64[(c & 0x0003f000) >> 12];

		if (i > size + 1)
			*p++ = '=';
		else
			*p++ = base64[(c & 0x00000fc0) >> 6];

		if (i > size)
			*p++ = '=';
		else
			*p++ = base64[c & 0x0000003f];
	}

	*p = 0;

	return str;
}

/* Base64 decoding */
static int POS(char c)
{
	if (c>='A' && c<='Z') return c - 'A';
	if (c>='a' && c<='z') return c - 'a' + 26;
	if (c>='0' && c<='9') return c - '0' + 52;
	if (c == '+') return 62;
	if (c == '/') return 63;
	if (c == '=') return -1;
    return -2;
}
static void * base64_decode(const char *s, size_t *data_len)
{
    const char *p;
    unsigned char *q, *data;
    int n[4];

	size_t len = strlen(s);
	if (len % 4)
		return NULL;
	data = (unsigned char*) malloc(len/4*3);
	q = (unsigned char*) data;

	for (p = s; *p; ) {
	    n[0] = POS(*p++);
	    n[1] = POS(*p++);
	    n[2] = POS(*p++);
	    n[3] = POS(*p++);

            if (n[0] == -2 || n[1] == -2 || n[2] == -2 || n[3] == -2)
                return NULL;

	    if (n[0] == -1 || n[1] == -1)
		return NULL;

	    if (n[2] == -1 && n[3] != -1)
		return NULL;

            q[0] = (n[0] << 2) + (n[1] >> 4);
	    if (n[2] != -1)
                q[1] = ((n[1] & 15) << 4) + (n[2] >> 2);
	    if (n[3] != -1)
                q[2] = ((n[2] & 3) << 6) + n[3];
	    q += 3;
	}

	*data_len = q-data - (n[2]==-1) - (n[3]==-1);

	return data;
}


/* ***************************************************************************
 * Deserialization (reading related)
 * ***************************************************************************/

/* Reads an int64 from a buffer. */
static MVMint64 read_int64(char *buffer, size_t offset) {
    MVMint64 value;
#if MVM_BIGENDIAN
    switch_endian(buffer + offset, 8);
#endif
    memcpy(&value, buffer + offset, 8);
    return value;
}

/* Reads an int32 from a buffer. */
static MVMint32 read_int32(char *buffer, size_t offset) {
    MVMint32 value;
#if MVM_BIGENDIAN
    switch_endian(buffer + offset, 4);
#endif
    memcpy(&value, buffer + offset, 4);
    return value;
}

/* Reads an int16 from a buffer. */
static MVMint16 read_int16(char *buffer, size_t offset) {
    MVMint16 value;
#if MVM_BIGENDIAN
    switch_endian(buffer + offset, 2);
#endif
    memcpy(&value, buffer + offset, 2);
    return value;
}

/* Reads double from a buffer. */
static MVMnum64 read_double(char *buffer, size_t offset) {
    MVMnum64 value;
#if MVM_BIGENDIAN
    switch_endian(buffer + offset, 8);
#endif
    memcpy(&value, buffer + offset, 8);
    return value;
}

/* If deserialization should fail, cleans up before throwing an exception. */
MVM_NO_RETURN
static void fail_deserialize(MVMThreadContext *tc, MVMSerializationReader *reader,
                             const char *messageFormat, ...) MVM_NO_RETURN_GCC;
MVM_NO_RETURN
static void fail_deserialize(MVMThreadContext *tc, MVMSerializationReader *reader, 
        const char *messageFormat, ...) {
    va_list args;
    if (reader->data)
        free(reader->data);
    free(reader);
    MVM_gc_allocate_gen2_default_clear(tc);
    va_start(args, messageFormat);
    MVM_exception_throw_adhoc(tc, messageFormat, args);
    va_end(args);
}

/* Reads the item from the string heap at the specified index. */
static MVMString * read_string_from_heap(MVMThreadContext *tc, MVMSerializationReader *reader, MVMint32 idx) {
    if (idx < MVM_repr_elems(tc, reader->root.string_heap))
        return MVM_repr_at_pos_s(tc, reader->root.string_heap, idx);
    else
        fail_deserialize(tc, reader,
            "Attempt to read past end of string heap (index %d)", idx);
}

/* Locates a serialization context; 0 is the current one, otherwise see the
 * dependencies table. */
static MVMSerializationContext * locate_sc(MVMThreadContext *tc, MVMSerializationReader *reader, MVMint32 sc_id) {
    MVMSerializationContext *sc;
    if (sc_id == 0)
        sc = reader->root.sc;
    else if (sc_id > 0 && sc_id - 1 < reader->root.num_dependencies)
        sc = reader->root.dependent_scs[sc_id - 1];
    else
        fail_deserialize(tc, reader,
            "Invalid dependencies table index encountered (index %d)", sc_id);
    return sc;
}

/* Looks up an STable. */
static MVMSTable * lookup_stable(MVMThreadContext *tc, MVMSerializationReader *reader, MVMint32 sc_id, MVMint32 idx) {
    return MVM_sc_get_stable(tc, locate_sc(tc, reader, sc_id), idx);
}

/* Ensure that we aren't going to read off the end of the buffer. */
static void assert_can_read(MVMThreadContext *tc, MVMSerializationReader *reader, MVMint32 amount) {
    char *read_end = *(reader->cur_read_buffer) + *(reader->cur_read_offset) + amount;
    if (read_end > *(reader->cur_read_end))
        fail_deserialize(tc, reader,
            "Read past end of serialization data buffer");
}

/* Reading function for native integers. */
static MVMint64 read_int_func(MVMThreadContext *tc, MVMSerializationReader *reader) {
    MVMint64 result;
    assert_can_read(tc, reader, 8);
    result = read_int64(*(reader->cur_read_buffer), *(reader->cur_read_offset));
    *(reader->cur_read_offset) += 8;
    return result;
}

/* Reading function for native numbers. */
static MVMnum64 read_num_func(MVMThreadContext *tc, MVMSerializationReader *reader) {
    MVMnum64 result;
    assert_can_read(tc, reader, 8);
    result = read_double(*(reader->cur_read_buffer), *(reader->cur_read_offset));
    *(reader->cur_read_offset) += 8;
    return result;
}

/* Reading function for native strings. */
static MVMString * read_str_func(MVMThreadContext *tc, MVMSerializationReader *reader) {
    MVMString *result;
    assert_can_read(tc, reader, 4);
    result = read_string_from_heap(tc, reader,
        read_int32(*(reader->cur_read_buffer), *(reader->cur_read_offset)));
    *(reader->cur_read_offset) += 4;
    return result;
}

/* Reads in and resolves an object references. */
static MVMObject * read_obj_ref(MVMThreadContext *tc, MVMSerializationReader *reader) {
    MVMint32 sc_id, idx;

    assert_can_read(tc, reader, 8);
    sc_id = read_int32(*(reader->cur_read_buffer), *(reader->cur_read_offset));
    *(reader->cur_read_offset) += 4;
    idx = read_int32(*(reader->cur_read_buffer), *(reader->cur_read_offset));
    *(reader->cur_read_offset) += 4;
    
    return MVM_sc_get_object(tc, locate_sc(tc, reader, sc_id), idx);
}

/* Forward-declare read_ref_func. */
MVMObject * read_ref_func(MVMThreadContext *tc, MVMSerializationReader *reader);

/* Reads in an hash with string keys and variant references. */
static MVMObject * read_hash_str_var(MVMThreadContext *tc, MVMSerializationReader *reader) {
    MVMObject *result = MVM_repr_alloc_init(tc, tc->instance->boot_types->BOOTHash);
    MVMint32 elems, i;

    /* Read the element count. */
    assert_can_read(tc, reader, 4);
    elems = read_int32(*(reader->cur_read_buffer), *(reader->cur_read_offset));
    *(reader->cur_read_offset) += 4;

    /* Read in the elements. */
    for (i = 0; i < elems; i++) {
        MVMString *key = read_str_func(tc, reader);
        MVM_repr_bind_key_boxed(tc, result, key, read_ref_func(tc, reader));
    }
    
    /* Set the SC. */
    MVM_sc_set_obj_sc(tc, result, reader->root.sc);

    return result;
}

/* Reads in a code reference. */
static MVMObject * read_code_ref(MVMThreadContext *tc, MVMSerializationReader *reader) {
    MVMint32 sc_id, idx;

    assert_can_read(tc, reader, 8);
    sc_id = read_int32(*(reader->cur_read_buffer), *(reader->cur_read_offset));
    *(reader->cur_read_offset) += 4;
    idx = read_int32(*(reader->cur_read_buffer), *(reader->cur_read_offset));
    *(reader->cur_read_offset) += 4;

    return MVM_sc_get_code(tc, locate_sc(tc, reader, sc_id), idx);
}

/* Reading function for references. */
MVMObject * read_ref_func(MVMThreadContext *tc, MVMSerializationReader *reader) {
    MVMObject *result;
    
    /* Read the discriminator. */
    short discrim;
    assert_can_read(tc, reader, 2);
    discrim = read_int16(*(reader->cur_read_buffer), *(reader->cur_read_offset));
    *(reader->cur_read_offset) += 2;
    
    /* Decide what to do based on it. */
    switch (discrim) {
        case REFVAR_NULL:
            return NULL;
        case REFVAR_OBJECT:
            return read_obj_ref(tc, reader);
        case REFVAR_VM_NULL:
            return NULL;
        case REFVAR_VM_HASH_STR_VAR:
            return read_hash_str_var(tc, reader);
        case REFVAR_STATIC_CODEREF:
        /*case REFVAR_CLONED_CODEREF:*/
            return read_code_ref(tc, reader);
        default:
            fail_deserialize(tc, reader,
                "Serialization Error: Unimplemented case of read_ref");
    }
}

/* Reading function for STable references. */
static MVMSTable * read_stable_ref_func(MVMThreadContext *tc, MVMSerializationReader *reader) {
    MVMint32 sc_id, idx;
    
    assert_can_read(tc, reader, 8);
    sc_id = read_int32(*(reader->cur_read_buffer), *(reader->cur_read_offset));
    *(reader->cur_read_offset) += 4;
    idx = read_int32(*(reader->cur_read_buffer), *(reader->cur_read_offset));
    *(reader->cur_read_offset) += 4;
    
    return lookup_stable(tc, reader, sc_id, idx);
}

/* Checks the header looks sane and all of the places it points to make sense.
 * Also disects the input string into the tables and data segments and populates
 * the reader data structure more fully. */
static void check_and_disect_input(MVMThreadContext *tc,
        MVMSerializationReader *reader, MVMString *data_str) {
    /* Grab data from string. */
    size_t  data_len;
    char   *data_b64 = (char *)MVM_string_ascii_encode(tc, data_str, NULL);
    char   *data     = (char *)base64_decode(data_b64, &data_len);
    char   *prov_pos = data;
    char   *data_end = data + data_len;
    free(data_b64);
    
    /* Ensure we got the data. */
    if (data == NULL)
        fail_deserialize(tc, reader,
            "Failed to decode base64-encoded serialization data");
    reader->data = data;

    /* Ensure that we have enough space to read a version number and check it. */
    if (data_len < 4)
        fail_deserialize(tc, reader,
            "Serialized data too short to read a version number (< 4 bytes)");
    reader->root.version = read_int32(data, 0);
    if (reader->root.version < MIN_VERSION || reader->root.version > CURRENT_VERSION)
        fail_deserialize(tc, reader,
            "Unsupported serialization format version %d (current version is %d)",
            reader->root.version, CURRENT_VERSION);

    /* Ensure that the data is at least as long as the header is expected to be. */
    if (data_len < HEADER_SIZE)
        fail_deserialize(tc, reader,
            "Serialized data shorter than header (< %d bytes)", HEADER_SIZE);
    prov_pos += HEADER_SIZE;

    /* Get size and location of dependencies table. */
    reader->root.dependencies_table = data + read_int32(data, 4);
    reader->root.num_dependencies   = read_int32(data, 8);
    if (reader->root.dependencies_table < prov_pos)
        fail_deserialize(tc, reader,
            "Corruption detected (dependencies table starts before header ends)");
    prov_pos = reader->root.dependencies_table + reader->root.num_dependencies * DEP_TABLE_ENTRY_SIZE;
    if (prov_pos > data_end)
        fail_deserialize(tc, reader,
            "Corruption detected (dependencies table overruns end of data)");
 
    /* Get size and location of STables table. */
    reader->root.stables_table = data + read_int32(data, 12);
    reader->root.num_stables   = read_int32(data, 16);
    if (reader->root.stables_table < prov_pos)
        fail_deserialize(tc, reader,
            "Corruption detected (STables table starts before dependencies table ends)");
    prov_pos = reader->root.stables_table + reader->root.num_stables * STABLES_TABLE_ENTRY_SIZE;
    if (prov_pos > data_end)
        fail_deserialize(tc, reader,
            "Corruption detected (STables table overruns end of data)");

    /* Get location of STables data. */
    reader->root.stables_data = data + read_int32(data, 20);
    if (reader->root.stables_data < prov_pos)
        fail_deserialize(tc, reader,
            "Corruption detected (STables data starts before STables table ends)");
    prov_pos = reader->root.stables_data;
    if (prov_pos > data_end)
        fail_deserialize(tc, reader,
            "Corruption detected (STables data starts after end of data)");
            
    /* Get size and location of objects table. */
    reader->root.objects_table = data + read_int32(data, 24);
    reader->root.num_objects   = read_int32(data, 28);
    if (reader->root.objects_table < prov_pos)
        fail_deserialize(tc, reader,
            "Corruption detected (objects table starts before STables data ends)");
    prov_pos = reader->root.objects_table + reader->root.num_objects * OBJECTS_TABLE_ENTRY_SIZE;
    if (prov_pos > data_end)
        fail_deserialize(tc, reader,
            "Corruption detected (objects table overruns end of data)");
            
    /* Get location of objects data. */
    reader->root.objects_data = data + read_int32(data, 32);
    if (reader->root.objects_data < prov_pos)
        fail_deserialize(tc, reader,
            "Corruption detected (objects data starts before objects table ends)");
    prov_pos = reader->root.objects_data;
    if (prov_pos > data_end)
        fail_deserialize(tc, reader,
            "Corruption detected (objects data starts after end of data)");
    
    /* Get size and location of closures table. */
    reader->root.closures_table = data + read_int32(data, 36);
    reader->root.num_closures   = read_int32(data, 40);
    if (reader->root.closures_table < prov_pos)
        fail_deserialize(tc, reader,
            "Corruption detected (Closures table starts before objects data ends)");
    prov_pos = reader->root.closures_table + reader->root.num_closures * CLOSURES_TABLE_ENTRY_SIZE;
    if (prov_pos > data_end)
        fail_deserialize(tc, reader,
            "Corruption detected (Closures table overruns end of data)");
    
    /* Get size and location of contexts table. */
    reader->root.contexts_table = data + read_int32(data, 44);
    reader->root.num_contexts   = read_int32(data, 48);
    if (reader->root.contexts_table < prov_pos)
        fail_deserialize(tc, reader,
            "Corruption detected (contexts table starts before closures table ends)");
    prov_pos = reader->root.contexts_table + reader->root.num_contexts * CONTEXTS_TABLE_ENTRY_SIZE;
    if (prov_pos > data_end)
        fail_deserialize(tc, reader,
            "Corruption detected (contexts table overruns end of data)");
    
    /* Get location of contexts data. */
    reader->root.contexts_data = data + read_int32(data, 52);
    if (reader->root.contexts_data < prov_pos)
        fail_deserialize(tc, reader,
            "Corruption detected (contexts data starts before contexts table ends)");
    prov_pos = reader->root.contexts_data;
    if (prov_pos > data_end)
        fail_deserialize(tc, reader,
            "Corruption detected (contexts data starts after end of data)");

    /* Get size and location of repossessions table. */
    reader->root.repos_table = data + read_int32(data, 56);
    reader->root.num_repos   = read_int32(data, 60);
    if (reader->root.repos_table < prov_pos)
        fail_deserialize(tc, reader,
            "Corruption detected (repossessions table starts before contexts data ends)");
    prov_pos = reader->root.repos_table + reader->root.num_repos * REPOS_TABLE_ENTRY_SIZE;
    if (prov_pos > data_end)
        fail_deserialize(tc, reader,
            "Corruption detected (repossessions table overruns end of data)");
    
    /* Set reading limits for data chunks. */
    reader->stables_data_end = reader->root.objects_table;
    reader->objects_data_end = reader->root.closures_table;
    reader->contexts_data_end = reader->root.repos_table;
}

/* Goes through the dependencies table and resolves the dependencies that it
 * contains to SerializationContexts. */
static void resolve_dependencies(MVMThreadContext *tc, MVMSerializationReader *reader) {
    char      *table_pos = reader->root.dependencies_table;
    MVMuint32  num_deps  = reader->root.num_dependencies;
    MVMuint32  i;
    reader->root.dependent_scs = malloc(MAX(num_deps, 1) * sizeof(MVMSerializationContext *));
    for (i = 0; i < num_deps; i++) {
        MVMString *handle = read_string_from_heap(tc, reader, read_int32(table_pos, 0));
        MVMSerializationContext *sc;
        sc = MVM_sc_find_by_handle(tc, handle);
        if (sc == NULL) {
            MVMString *desc = read_string_from_heap(tc, reader, read_int32(table_pos, 4));
            fail_deserialize(tc, reader,
                "Missing or wrong version of dependency '%s'",
                MVM_string_ascii_encode(tc, desc, NULL));
        }
        reader->root.dependent_scs[i] = sc;
        table_pos += 8;
    }
}

/* Allocates STables that we need to deserialize, associating each one
 * with its REPR. */
static void stub_stables(MVMThreadContext *tc, MVMSerializationReader *reader) {
    MVMuint32  num_sts  = reader->root.num_stables;
    MVMuint32  i;
    for (i = 0; i < num_sts; i++) {
        /* Calculate location of STable's table row. */
        char *st_table_row = reader->root.stables_table + i * STABLES_TABLE_ENTRY_SIZE;
        
        /* Read in and look up representation. */
        MVMREPROps *repr = MVM_repr_get_by_name(tc,
            read_string_from_heap(tc, reader, read_int32(st_table_row, 0)));
        
        /* Allocate and store stub STable. */
        MVMSTable *st = MVM_gc_allocate_stable(tc, repr, NULL);
        MVM_sc_set_stable(tc, reader->root.sc, i, st);
        
        /* Set the STable's SC. */
        MVM_sc_set_stable_sc(tc, st, reader->root.sc);
    }
}

/* Goes through all STables and gets them to calculate their sizes, setting
 * the STable size field. */
static void set_stable_sizes(MVMThreadContext *tc, MVMSerializationReader *reader) {
    MVMuint32  num_sts  = reader->root.num_stables;
    MVMuint32  i;
    for (i = 0; i < num_sts; i++) {
        /* Grab STable. */
        MVMSTable *st = MVM_sc_get_stable(tc, reader->root.sc, i);
        
        /* Set STable read position, and set current read buffer to the
         * location of the REPR data. */
        char *st_table_row = reader->root.stables_table + i * STABLES_TABLE_ENTRY_SIZE;
        reader->stables_data_offset = read_int32(st_table_row, 8);
        
        reader->cur_read_buffer     = &(reader->root.stables_data);
        reader->cur_read_offset     = &(reader->stables_data_offset);
        reader->cur_read_end        = &(reader->stables_data_end);
        
        if (st->REPR->deserialize_stable_size)
            st->REPR->deserialize_stable_size(tc, st, reader);
        else
            fail_deserialize(tc, reader, "Missing deserialize_stable_size");
        if (st->size == 0)
            fail_deserialize(tc, reader, "STable with size zero after deserialization");
    }
}

/* Stubs all the objects we need to deserialize, setting their REPR and type
 * object flag. */
static void stub_objects(MVMThreadContext *tc, MVMSerializationReader *reader) {
    MVMuint32  num_objs = reader->root.num_objects;
    MVMuint32  i;
    for (i = 0; i < num_objs; i++) {
        /* Calculate location of object's table row. */
        char *obj_table_row = reader->root.objects_table + i * OBJECTS_TABLE_ENTRY_SIZE;
        
        /* Resolve the STable. */
        MVMSTable *st = lookup_stable(tc, reader,
            read_int32(obj_table_row, 0),   /* The SC in the dependencies table, + 1 */
            read_int32(obj_table_row, 4));  /* The index in that SC */

        /* Allocate and store stub object. */
        MVMObject *obj;
        if ((read_int32(obj_table_row, 12) & 1))
            obj = st->REPR->allocate(tc, st);
        else
            obj = MVM_gc_allocate_type_object(tc, st);
        MVM_sc_set_object(tc, reader->root.sc, i, obj);
        
        /* Set the object's SC. */
        MVM_sc_set_obj_sc(tc, obj, reader->root.sc);
    }
}

/* Deserializes a single STable, along with its REPR data. */
static void deserialize_stable(MVMThreadContext *tc, MVMSerializationReader *reader, MVMint32 i, MVMSTable *st) {
    /* Calculate location of STable's table row. */
    char *st_table_row = reader->root.stables_table + i * STABLES_TABLE_ENTRY_SIZE;
    
    /* Set STable read position, and set current read buffer to the correct thing. */
    reader->stables_data_offset = read_int32(st_table_row, 4);
    reader->cur_read_buffer     = &(reader->root.stables_data);
    reader->cur_read_offset     = &(reader->stables_data_offset);
    reader->cur_read_end        = &(reader->stables_data_end);
    
    /* Read the HOW, WHAT and WHO. */
    MVM_ASSIGN_REF(tc, st, st->HOW, read_obj_ref(tc, reader));
    MVM_ASSIGN_REF(tc, st, st->WHAT, read_obj_ref(tc, reader));
    MVM_ASSIGN_REF(tc, st, st->WHO, read_ref_func(tc, reader));
    
    /* Method cache and v-table. */
    MVM_ASSIGN_REF(tc, st, st->method_cache, read_ref_func(tc, reader));
    st->vtable_length = read_int_func(tc, reader);
    if (st->vtable_length > 0)
        st->vtable = (MVMObject **)malloc(st->vtable_length * sizeof(MVMObject *));
    for (i = 0; i < st->vtable_length; i++)
        MVM_ASSIGN_REF(tc, st, st->vtable[i], read_ref_func(tc, reader));
    
    /* Type check cache. */
    st->type_check_cache_length = read_int_func(tc, reader);
    if (st->type_check_cache_length > 0) {
        st->type_check_cache = (MVMObject **)malloc(st->type_check_cache_length * sizeof(MVMObject *));
        for (i = 0; i < st->type_check_cache_length; i++)
            MVM_ASSIGN_REF(tc, st, st->type_check_cache[i], read_ref_func(tc, reader));
    }
    
    /* Mode flags. */
    st->mode_flags = read_int_func(tc, reader);
    
    /* Boolification spec. */
    if (read_int_func(tc, reader)) {
        st->boolification_spec = (MVMBoolificationSpec *)malloc(sizeof(MVMBoolificationSpec));
        st->boolification_spec->mode = read_int_func(tc, reader);
        MVM_ASSIGN_REF(tc, st, st->boolification_spec->method, read_ref_func(tc, reader));
    }

    /* Container spec. */
    if (read_int_func(tc, reader)) {
        fail_deserialize(tc, reader, "Container spec deserialization NYI");
    }

    /* If the REPR has a function to deserialize representation data, call it. */
    if (st->REPR->deserialize_repr_data)
        st->REPR->deserialize_repr_data(tc, st, reader);
}

/* Takes serialized data, an empty SerializationContext to deserialize it into,
 * a strings heap and the set of static code refs for the compilation unit.
 * Deserializes the data into the required objects and STables. */
void MVM_serialization_deserialize(MVMThreadContext *tc, MVMSerializationContext *sc,
        MVMObject *string_heap, MVMObject *codes_static, 
        MVMObject *repo_conflicts, MVMString *data) {
    MVMint32 i;
    
    /* Allocate and set up reader. */
    MVMSerializationReader *reader = malloc(sizeof(MVMSerializationReader));
    memset(reader, 0, sizeof(MVMSerializationReader));
    reader->root.sc          = sc;
    reader->root.string_heap = string_heap;
    
    /* Put reader functions in place. */
    reader->read_int        = read_int_func;
    reader->read_num        = read_num_func;
    reader->read_str        = read_str_func;
    reader->read_ref        = read_ref_func;
    reader->read_stable_ref = read_stable_ref_func;
    
    /* Put code root list into SC. We'll end up mutating it, but that's
     * probably fine. */
    MVM_sc_set_code_list(tc, sc, codes_static);
    
    /* During deserialization, we allocate directly in generation 2. This
     * is because these objects are almost certainly going to be long lived,
     * but also because if we know that we won't end up moving the objects
     * we are working on during a deserialization run, it's a bunch easier
     * to have those partially constructed objects floating around. */
    MVM_gc_allocate_gen2_default_set(tc);
    
    /* Read header and disect the data into its parts. */
    check_and_disect_input(tc, reader, data);
    
    /* Resolve the SCs in the dependencies table. */
    resolve_dependencies(tc, reader);
    
    /* Stub allocate all STables, and then have the appropriate REPRs do
     * the required size calculations. */
    stub_stables(tc, reader);
    set_stable_sizes(tc, reader);
    
    /* Stub allocate all objects. */
    stub_objects(tc, reader);
    
    /* Fully deserialize STables, along with their representation data. */
    for (i = 0; i < reader->root.num_stables; i++)
        deserialize_stable(tc, reader, i, MVM_sc_get_stable(tc, sc, i));
    
    /* TODO: The rest... */
    
    /* Clear up afterwards. */
    if (reader->data)
        free(reader->data);
    free(reader);
    
    /* Restore normal GC allocation. */
    MVM_gc_allocate_gen2_default_clear(tc);
}
