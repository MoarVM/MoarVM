#include <moarvm.h>

#define MAX(x, y) ((y) > (x) ? (y) : (x))

/* Version of the serialization format that we are currently at and lowest
 * version we support. */
#define CURRENT_VERSION 3
#define MIN_VERSION     3

/* Various sizes (in bytes). */
#define HEADER_SIZE                 4 * 16
#define DEP_TABLE_ENTRY_SIZE        8
#define STABLES_TABLE_ENTRY_SIZE    8
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
        const char *messageFormat, ...) MVM_NO_RETURN_GCC {
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
    if (idx < MVM_repr_pos_elems(tc, reader->root.string_heap))
        return MVM_repr_at_pos_s(tc, reader->root.string_heap, idx);
    else
        fail_deserialize(tc, reader,
            "Attempt to read past end of string heap (index %d)", idx);
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

/* Takes serialized data, an empty SerializationContext to deserialize it into,
 * a strings heap and the set of static code refs for the compilation unit.
 * Deserializes the data into the required objects and STables. */
void MVM_serialization_deserialize(MVMThreadContext *tc, MVMSerializationContext *sc,
        MVMObject *string_heap, MVMObject *codes_static, 
        MVMObject *repo_conflicts, MVMString *data) {
    
    /* Allocate and set up reader. */
    MVMSerializationReader *reader = malloc(sizeof(MVMSerializationReader));
    memset(reader, 0, sizeof(MVMSerializationReader));
    reader->root.sc          = sc;
    reader->root.string_heap = string_heap;
    
    /* During deserialization, we allocate directly in generation 2. This
     * is because these objects are almost certainly going to be long lived,
     * but also because if we know that we won't end up moving the objects
     * we are working on during a deserialization run, it's a bunch easier
     * to have those partially constructed objects floating around. */
    MVM_gc_allocate_gen2_default_set(tc);
    
    /* Read header and disect the data into its parts. */
    check_and_disect_input(tc, reader, data);
    
    /* TODO: The rest... */
    printf("WARNING: Deserialization NYI, but disection done\n");
    
    /* Clear up afterwards. */
    if (reader->data)
        free(reader->data);
    free(reader);
    
    /* Restore normal GC allocation. */
    MVM_gc_allocate_gen2_default_clear(tc);
}
