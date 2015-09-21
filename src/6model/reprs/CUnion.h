/* Attribute location flags. */
#define MVM_CUNION_ATTR_IN_STRUCT 0
#define MVM_CUNION_ATTR_CSTRUCT   1
#define MVM_CUNION_ATTR_CARRAY    2
#define MVM_CUNION_ATTR_CPTR      3
#define MVM_CUNION_ATTR_STRING    4
#define MVM_CUNION_ATTR_CUNION    5
#define MVM_CUNION_ATTR_CPPSTRUCT 6
#define MVM_CUNION_ATTR_MASK      7

#define MVM_CUNION_ATTR_INLINED   8

/* Bits to shift a slot position to make room for MVM_CUNION_ATTR_*. */
#define MVM_CUNION_ATTR_SHIFT     4

/* The CUnion representation maintains a chunk of memory that it can
 * always pass off to C land. If we in turn embed any strings, pointers
 * to other CUnion REPR objects and so forth, we need to both keep the
 * C-friendly bit of memory and a copy to the GC-able, 6model objects in
 * sync. */
struct MVMCUnionBody {
    /* GC-marked objects that our C structure points into. */
    MVMObject **child_objs;

    /* Pointer to the actual C structure memory; we don't inline it
     * directly in the body, since it doesn't work so well if we get
     * something returned and are wrapping it. */
    void *cunion;
};

struct MVMCUnion {
    MVMObject common;
    MVMCUnionBody body;
};

/* This is used in the name to class mapping. */
struct MVMCUnionNameMap {
    MVMObject *class_key;
    MVMObject *name_map;
};

/* The CUnion REPR data contains info we need to do allocations, look up
 * attributes and so forth. */
struct MVMCUnionREPRData {
    /* The size and alignment of the structure in bytes. */
    MVMint32 struct_size;
    MVMint32 struct_align;

    /* The number of attributes we have allocated slots for. Note that
     * slots can vary in size. */
    MVMint32 num_attributes;

    /* Number of child objects we store. */
    MVMint32 num_child_objs;

    /* Lower bits are flags indicating what kind of attribute we have;
     * whether it's one that is just a simple value that we can always
     * access directly in the C struct body, or a more complex one that
     * we need to maintain in the C struct and in the GC-able list. Upper
     * bits say where to find it. */
    MVMint32 *attribute_locations;

    /* Maps attribute position numbers to their location in the C struct.
     * Note that this will not be the only place we need to update for
     * any reference type. */
    MVMint32 *struct_offsets;

    /* If the attribute was actually flattened in to this object from another
     * representation, this is the s-table of the type of that attribute. NULL
     * for attributes that are reference types. */
    MVMSTable **flattened_stables;

    /* For reference type members, we cache the relevant type objects.
     * Flattened types have NULL here. */
    MVMObject **member_types;

    /* A table mapping attribute names to indexes (which can then be looked
     * up in the offset table). Uses a final null entry as a sentinel. */
    MVMCUnionNameMap *name_to_index_mapping;

    /* Slots holding flattened objects that need another REPR to initialize
     * them; terminated with -1. */
    MVMint32 *initialize_slots;
};

/* Initializes the CUnion REPR. */
const MVMREPROps * MVMCUnion_initialize(MVMThreadContext *tc);
