/*
 * GCC Jit Reader Support
 * ======================
 *
 * This code compiled to a shared object is loadable directly into
 * GDB and allows frames jitted by moar to show up properly in a
 * backtrace, allows you to see the frames below it (like the
 * MVM_interp_run function that usually calls into the jitted frames)
 *
 * Also, with the "disassemble" command in GDB you will get the
 * disassembly of the frame from its start, which is very helpful.
 *
 * The jit reader relies upon a symbol called __jit_debug_descriptor and
 * a function __jit_debug_register_code that is called whenever the debug
 * descriptor data structure gets changed. Find it in src/jit/compile.c
 *
 * Compile with
 *
 *     gcc -fPIC -shared -o ../jit_reader.so moar-gdb-jitreader.c
 *
 * Make sure to have built moar with DEBUG_HELPERS defined.
 *
 * Then in gdb load the compiled .so file like so:
 *
 *     jit-reader-load ~/raku/moarvm/jit_reader.so
 *
 * It's useful to put the jit-reader-load command in your gdb invocation
 * with the -iex="bla" flag. It can also go into your ~/.gdbinit
 *
 */

#include "gdb/jit-reader.h"

#include "../src/platform/inttypes.h"
#include <setjmp.h>
#include <stddef.h>
#include <stdio.h>

/* stolen from moar.h */
/* Sized types. */
typedef int8_t   MVMint8;
typedef uint8_t  MVMuint8;
typedef int16_t  MVMint16;
typedef uint16_t MVMuint16;
typedef int32_t  MVMint32;
typedef uint32_t MVMuint32;
typedef int64_t  MVMint64;
typedef uint64_t MVMuint64;
typedef float    MVMnum32;
typedef double   MVMnum64;

// ...
typedef MVMuint32 MVMHashNumItems;
typedef MVMuint64 MVMHashv;

// ...
#ifdef MVM_USE_C11_ATOMICS
#include <stdatomic.h>
typedef atomic_uintptr_t AO_t;
#else
#include "../3rdparty/libatomicops/src/atomic_ops.h"
#endif

#define MVM_PUBLIC
#define MVM_PRIVATE
/* end stolen from moar.h */

#define MVM_STATIC_INLINE
#define MVM_LIKELY
#define MVM_UNLIKELY
#define MVM_NO_RETURN
#define MVM_NO_RETURN_ATTRIBUTE
#define MVM_FORMAT(X, Y, Z)

#include <stdarg.h>

#include "../src/types.h"
#include "../src/core/exceptions.h"
#include "../src/core/vector.h"

#include "../src/6model/6model.h"
#include "../src/disp/inline_cache.h"
#include "../src/spesh/pea.h"
#include "../src/core/index_hash_table.h"
#include "../src/core/str_hash_table.h"
#include "../src/6model/reprs/MVMStaticFrame.h"
#include "../src/6model/reprs/MVMString.h"
#include "../src/6model/reprs/MVMSpeshCandidate.h"
#include "../src/jit/compile.h"

/* partially stolen, partially simplified MVMThreadContext */
struct MVMThreadContext {
    /************************************************************************
     * Information about this thread
     ************************************************************************/

    /* Internal ID of the thread. */
    MVMuint32 thread_id;

    /* Thread object representing the thread. */
    MVMThread *thread_obj;

    /* The VM instance that this thread belongs to. */
    MVMInstance *instance;

    /* The number of locks the thread is holding. */
    MVMint64 num_locks;

    /************************************************************************
     * Garbage collection and memory management
     ************************************************************************/

    /* Start of fromspace, the place we're copying objects from during a
     * copying collection or processing dead objects that need to do extra
     * resource release afterwards. */
    void *nursery_fromspace;

    /* Where we evacuate objects to when collecting this thread's nursery, or
     * allocate new ones. */
    void *nursery_tospace;

    /* The current allocation pointer, where the next object to be allocated
     * should be placed. */
    void *nursery_alloc;

    /* The end of the space we're allowed to allocate to. */
    void *nursery_alloc_limit;

    /* This thread's GC status. */
    AO_t gc_status;

    /* The second GC generation allocator. */
    MVMGen2Allocator *gen2;

    /* The current sizes of the nursery fromspace/tospace for this thread, in
     * bytes. Used to handle growing it over time depending on usage. */
    MVMuint32 nursery_fromspace_size;
    MVMuint32 nursery_tospace_size;

    /* Non-zero is we should allocate in gen2; incremented/decremented as we
     * enter/leave a region wanting gen2 allocation. */
    MVMuint32 allocate_in_gen2;

    /* Number of bytes promoted to gen2 in current GC run. */
    MVMuint32 gc_promoted_bytes;

    /* Temporarily rooted objects. This is generally used by code written in
     * C that wants to keep references to objects. Since those may change
     * if the code in question also allocates, there is a need to register
     * them; this ensures the GC will not swallow them but also that they
     * will get updated if a GC run happens. Note that this is used as a
     * stack and is also thread-local, so it's cheap to push/pop. */
    MVMuint32             num_temproots;
    MVMuint32             mark_temproots;
    MVMuint32             alloc_temproots;
    MVMCollectable     ***temproots;

    /* Nursery collectables (maybe STables) rooted because something in
     * generation 2 is pointing at them. */
    MVMuint32             num_gen2roots;
    MVMuint32             alloc_gen2roots;
    MVMCollectable      **gen2roots;

    /* Finalize queue objects, which need to have a finalizer invoked once
     * they are no longer referenced from anywhere except this queue. */
    MVMuint32             num_finalize;
    MVMuint32             alloc_finalize;
    MVMObject           **finalize;

    /* List of objects we're in the process of finalizing. */
    MVMuint32             num_finalizing;
    MVMuint32             alloc_finalizing;
    MVMObject           **finalizing;

    /* The GC's cross-thread in-tray of processing work. */
    MVMGCPassedWork *gc_in_tray;

    /* Threads we will do GC work for this run (ourself plus any that we stole
     * work from because they were blocked). */
    MVMWorkThread   *gc_work;
    MVMuint32        gc_work_size;
    MVMuint32        gc_work_count;

    /************************************************************************
     * Interpreter state
     ************************************************************************/

    /* Pointer to where the interpreter's current opcode is stored. */
    MVMuint8 **interp_cur_op;

    /* Pointer to where the interpreter's bytecode start pointer is stored. */
    MVMuint8 **interp_bytecode_start;

    /* Pointer to where the interpreter's base of the current register
     * set is stored. */
    MVMRegister **interp_reg_base;

    /* Pointer to where the interpreter's current compilation unit pointer
     * is stored. */
    MVMCompUnit **interp_cu;

    /* Jump buffer, used when an exception is thrown from C-land and we need
     * to fall back into the interpreter. These things are huge, so put it
     * near the end to keep the hotter stuff on the same cacheline. */
    jmp_buf interp_jump;

    /************************************************************************
     * Frames, call stack, and exception state
     ************************************************************************/

    /* The bytecode frame we're currently executing. */
    MVMFrame *cur_frame;

    /* The frame lying at the base of the current thread. */
    MVMFrame *thread_entry_frame;

    /* First call stack memory region, so we can traverse them for cleanup. */
    MVMCallStackRegion *stack_first_region;

    /* Current call stack region, which the next record will be allocated
     * so long as there is space. */
    MVMCallStackRegion *stack_current_region;

    /* The current call stack record on the top of the stack. */
    MVMCallStackRecord *stack_top;

    /* Linked list of exception handlers that we're currently executing, topmost
     * one first in the list. */
    MVMActiveHandler *active_handlers;

    /* Result object of the last-run exception handler. */
    MVMObject *last_handler_result;

    /* Last payload made available in a payload-goto exception handler. */
    MVMObject *last_payload;

    /************************************************************************
     * Specialization and JIT compilation
     ************************************************************************/

    /* JIT return address pointer, so we can figure out the current position in
     * the code */
    void **jit_return_address;
};

#include <stdlib.h>
#include <string.h>

GDB_DECLARE_GPL_COMPATIBLE_READER;

MVM_PUBLIC MVM_NO_RETURN void MVM_oops(MVMThreadContext *tc, const char *messageFormat, ...) MVM_NO_RETURN_ATTRIBUTE MVM_FORMAT(printf, 2, 3) {}

static void gdb_reg_free_handler(struct gdb_reg_value *reg) {
    free(reg);
}

struct valid_RIP_ranges {
    GDB_CORE_ADDR begin;
    GDB_CORE_ADDR end;
    char *name;
};

struct priv_data {
    MVMuint32 ranges_alloc;
    MVMuint32 ranges_items;

    struct valid_RIP_ranges *ranges;
} priv_data;


#define REGNO_RBX 3
#define REGNO_RBP 6
#define REGNO_RSP 7
#define REGNO_RIP 16

enum gdb_status get_string_from_remote(struct gdb_symbol_callbacks *cb, GDB_CORE_ADDR string_addr, char **out) {
    MVMString string_obj = {0};
    MVMuint8 free_storage_array = 0;
    MVMuint8 *in = 0;

    if (!out) {
        return GDB_FAIL;
    }

    if (cb->target_read(string_addr, &string_obj, sizeof(MVMString)) != GDB_SUCCESS) {
        return GDB_FAIL;
    }

    if (string_obj.body.num_graphs == 0) {
        *out = 0;
        return GDB_SUCCESS;
    }

    if (string_obj.body.storage_type == MVM_STRING_GRAPHEME_32 || string_obj.body.storage_type == MVM_STRING_STRAND || string_obj.body.storage_type == MVM_STRING_IN_SITU_32) {
        char *res_msg = malloc(200);
        snprintf(res_msg, 200, "%lx_st_%d_%ug", string_addr, string_obj.body.storage_type, string_obj.body.num_graphs);
        *out = res_msg;
        return GDB_SUCCESS;
    }
    if (string_obj.body.storage_type == MVM_STRING_GRAPHEME_8 || string_obj.body.storage_type == MVM_STRING_GRAPHEME_ASCII || string_obj.body.storage_type == MVM_STRING_IN_SITU_8) {
        char *storage;
        if (string_obj.body.storage_type == MVM_STRING_GRAPHEME_8 || string_obj.body.storage_type == MVM_STRING_GRAPHEME_ASCII) {
            in = malloc(sizeof(MVMGrapheme8) * string_obj.body.num_graphs);
            if (cb->target_read((GDB_CORE_ADDR)string_obj.body.storage.any, in, sizeof(MVMGrapheme8) * string_obj.body.num_graphs) != GDB_SUCCESS) {
                free(in);
                return GDB_FAIL;
            }
            storage = in;
            free_storage_array = 1;
        }
        else {
            storage = string_obj.body.storage.in_situ_8;
        }

        *out = malloc(string_obj.body.num_graphs + 1);
        for (uint32_t pos = 0; pos < string_obj.body.num_graphs; pos++) {
            if (storage[pos] < 0) {
                (*out)[pos] = '@';
            }
            else {
                (*out)[pos] = storage[pos];
            }
        }
        (*out)[string_obj.body.num_graphs] = 0;

        if (free_storage_array) { free(in); }
        return GDB_SUCCESS;
    }

    return GDB_FAIL;
}

enum gdb_status read_debug_info(struct gdb_reader_funcs *self,
                     struct gdb_symbol_callbacks *cb,
                     void *memory, long memory_sz)
{
    //fprintf(stderr, "read symbol file %p (%ld bytes)\n", memory, memory_sz);
#define MOAR_JIT_OBJECT_SIZE (sizeof(MVMJitCode) + 256 + sizeof(MVMint32))
    if (memory_sz != MOAR_JIT_OBJECT_SIZE) {
        fprintf(stderr, "expected symbol file size to be %ld but got %ld\n", MOAR_JIT_OBJECT_SIZE, memory_sz);
        return GDB_FAIL;
    }

    MVMJitCode *code = (MVMJitCode*)memory;
    char *name_input = (char *)(code + 1);
    /*MVMint32 line_nr = *(MVMint32*)(((char *)memory) + sizeof(MVMJitCode) + 256);*/

    // Just make extra sure there's a null terminator in the filename field
    name_input[255] = '\0';
    char *last_open_paren = strrchr(name_input, '(');
    MVMuint16 len_before_paren = last_open_paren == NULL ? strlen(name_input) : last_open_paren - name_input - 1;

    char *name = calloc(len_before_paren + 1, sizeof(char));
    strncpy(name, name_input, len_before_paren);

    MVMStaticFrame sf;
    if (cb->target_read((GDB_CORE_ADDR)code->sf, &sf, sizeof(MVMStaticFrame)) != GDB_SUCCESS) {
        return GDB_FAIL;
    }

    char *func_name;
    if (get_string_from_remote(cb, (GDB_CORE_ADDR)sf.body.name, &func_name) != GDB_SUCCESS) {
        fprintf(stderr, "oh no! couldn't get string for the func name!\n");
        return GDB_FAIL;
    }

    GDB_CORE_ADDR *labels = malloc(code->num_labels * sizeof(GDB_CORE_ADDR));
    cb->target_read((GDB_CORE_ADDR)code->labels, labels, code->num_labels * sizeof(GDB_CORE_ADDR));

    GDB_CORE_ADDR from = (GDB_CORE_ADDR)code->func_ptr;
    GDB_CORE_ADDR to   = from + code->size;

    struct gdb_object *func_obj = cb->object_open(cb);
    // struct gdb_symtab *symtab_obj = cb->symtab_open(cb, func_obj, name);
    struct gdb_symtab *symtab_obj = cb->symtab_open(cb, func_obj, name);
    cb->block_open(cb, symtab_obj, 0, from, (GDB_CORE_ADDR)to, func_name ? func_name : "<unknown>");

    struct gdb_line_mapping *mapping = malloc(sizeof(struct gdb_line_mapping) * code->num_labels);

    // fprintf(stderr, "reading debug info for function '%s' in file '%s'\n", func_name ? func_name : "<unknown>", name);
    MVMint32 mapping_i = 0;

    mapping[mapping_i].line = 1;
    mapping[mapping_i].pc = from;

    mapping_i++;
    for (MVMuint32 label_i = 0; label_i < code->num_labels; label_i++) {
        if (labels[label_i] == (GDB_CORE_ADDR)code->exit_label) {
            //fprintf(stderr, "  -> exit label reached\n");
            break;
        }
        if (label_i > 0 && mapping_i > 0 && labels[label_i] <= mapping[mapping_i - 1].pc) {
            //fprintf(stderr, "  -> skipped over a backwards label\n");
            continue;
        }
        if (labels[label_i] < from || labels[label_i] >= to) {
            //fprintf(stderr, "  -> skipped over an outside-of-range label\n");
            continue;
        }
        mapping[mapping_i].line = label_i + 1;
        mapping[mapping_i].pc = labels[label_i];
        //fprintf(stderr, "    mapping[%3u] pc is labels[%3u] %p   (offset is %p)\n", mapping_i, label_i, (GDB_CORE_ADDR)mapping[mapping_i].pc, (GDB_CORE_ADDR)mapping[mapping_i].pc - from);
        mapping_i++;
    }
    cb->line_mapping_add(cb, symtab_obj, mapping_i, mapping);

    free(mapping);

    cb->symtab_close(cb, symtab_obj);
    cb->object_close(cb, func_obj);

    struct priv_data *pd = (struct priv_data *)self->priv_data;
    if (++pd->ranges_items > pd->ranges_alloc) {
        pd->ranges_alloc = (MVMuint16)(pd->ranges_alloc * 1.5f);
        pd->ranges = realloc(pd->ranges, sizeof(struct valid_RIP_ranges) * pd->ranges_alloc);
    }
    pd->ranges[pd->ranges_items - 1].begin = from;
    pd->ranges[pd->ranges_items - 1].end = to;
    pd->ranges[pd->ranges_items - 1].name = func_name;

    return GDB_SUCCESS;
}

void destroy (struct gdb_reader_funcs *self) {
    fprintf(stderr, "MoarVM jit reader destroyed :(\n");
}

struct gdb_frame_id get_frame_id (struct gdb_reader_funcs *self,
                                  struct gdb_unwind_callbacks *cb) {
    struct priv_data *pd = (struct priv_data *)self->priv_data;
    struct gdb_frame_id result;

    struct gdb_reg_value *rbp = cb->reg_get(cb, REGNO_RBP);
    GDB_CORE_ADDR rbp_addr = *((GDB_CORE_ADDR*)(&rbp->value));
    if (rbp->free) { rbp->free(rbp); }

    struct gdb_reg_value *rip = cb->reg_get(cb, REGNO_RIP);
    GDB_CORE_ADDR rip_addr = *((GDB_CORE_ADDR*)(&rip->value));
    if (rip->free) { rip->free(rip); }

    MVMuint8 range_matched = 0;
    MVMuint32 range_idx;
    for (range_idx = 0; range_idx < pd->ranges_items; range_idx++) {
        if (pd->ranges[range_idx].begin <= rip_addr && pd->ranges[range_idx].end > rip_addr) {
            range_matched = 1;
            break;
        }
    }

    if (range_matched) {
        result.code_address = pd->ranges[range_idx].begin;
        result.stack_address = (GDB_CORE_ADDR)rbp_addr;
    }
    else {
        result.code_address = 0;
        result.stack_address = 0;
    }
    return result;
}

enum gdb_status unwind (struct gdb_reader_funcs *self,
                        struct gdb_unwind_callbacks *cb) {
    struct priv_data *pd = (struct priv_data *)self->priv_data;

    struct gdb_reg_value *rip = cb->reg_get(cb, REGNO_RIP);

    GDB_CORE_ADDR rip_addr = *((GDB_CORE_ADDR*)(&rip->value));

    MVMuint8 range_matched = 0;

    for (MVMuint32 range_idx = 0; range_idx < pd->ranges_items; range_idx++) {
        if (pd->ranges[range_idx].begin <= rip_addr && pd->ranges[range_idx].end > rip_addr) {
            range_matched = 1;
            break;
        }
    }

    if (rip->free) { rip->free(rip); }

    if (!range_matched) {
        return GDB_FAIL;
    }

    struct gdb_reg_value *rbp = cb->reg_get(cb, REGNO_RBP);
    MVMuint64 rbp_addr = *((MVMuint64*)(&rbp->value));
    if (rbp->free) { rbp->free(rbp); }

    GDB_CORE_ADDR return_address;
    if (cb->target_read((GDB_CORE_ADDR)rbp_addr + 0x8, &return_address, sizeof(GDB_CORE_ADDR)) != GDB_SUCCESS) {
        fprintf(stderr, "failed to read return address from the stack\n");
        return GDB_FAIL;
    }

    GDB_CORE_ADDR prev_rsp_value_pos = rbp_addr;
    GDB_CORE_ADDR prev_rsp_value = 0;
    if (cb->target_read(prev_rsp_value_pos, &prev_rsp_value, sizeof(GDB_CORE_ADDR)) != GDB_SUCCESS) {
        fprintf(stderr, "couldn't grab previous rsp from rbp\n");
        return GDB_FAIL;
    }

    struct gdb_reg_value *prev_rbp = malloc(sizeof(struct gdb_reg_value) + 8 - 1);
    prev_rbp->defined = 1;
    prev_rbp->size = sizeof(char *);
    prev_rbp->free = gdb_reg_free_handler;
    *(GDB_CORE_ADDR*)prev_rbp->value = prev_rsp_value;

    struct gdb_reg_value *prev_rsp = malloc(sizeof(struct gdb_reg_value) + 8 - 1);
    prev_rsp->defined = 1;
    prev_rsp->size = sizeof(char *);
    prev_rsp->free = gdb_reg_free_handler;
    *(GDB_CORE_ADDR*)prev_rsp->value = rbp_addr + 0x10;

    struct gdb_reg_value *prev_rip = malloc(sizeof(struct gdb_reg_value) + 8 - 1);
    prev_rip->defined = 1;
    prev_rip->size = sizeof(char *);
    prev_rip->free = gdb_reg_free_handler;
    *(GDB_CORE_ADDR*)prev_rip->value = return_address;

    struct gdb_reg_value *prev_r14 = malloc(sizeof(struct gdb_reg_value) + 8 - 1);
    prev_r14->defined = 1;
    prev_r14->size = sizeof(char *);
    prev_r14->free = gdb_reg_free_handler;
    cb->target_read((GDB_CORE_ADDR)rbp_addr - 0x8, (GDB_CORE_ADDR*)prev_r14->value, sizeof(MVMuint64));

    struct gdb_reg_value *prev_r13 = malloc(sizeof(struct gdb_reg_value) + 8 - 1);
    prev_r13->defined = 1;
    prev_r13->size = sizeof(char *);
    prev_r13->free = gdb_reg_free_handler;
    cb->target_read((GDB_CORE_ADDR)rbp_addr - 0x10, (GDB_CORE_ADDR*)prev_r13->value, sizeof(MVMuint64));

    struct gdb_reg_value *prev_rbx = malloc(sizeof(struct gdb_reg_value) + 8 - 1);
    prev_rbx->defined = 1;
    prev_rbx->size = sizeof(char *);
    prev_rbx->free = gdb_reg_free_handler;
    cb->target_read((GDB_CORE_ADDR)rbp_addr - 0x18, (GDB_CORE_ADDR*)prev_rbx->value, sizeof(MVMuint64));

    cb->reg_set(cb, REGNO_RBP, prev_rbp);
    cb->reg_set(cb, REGNO_RSP, prev_rsp);
    cb->reg_set(cb, REGNO_RIP, prev_rip);

    cb->reg_set(cb, 14, prev_r14);
    cb->reg_set(cb, 13, prev_r13);
    cb->reg_set(cb, REGNO_RBX, prev_rbx);

    return GDB_SUCCESS;
}

struct gdb_reader_funcs moar_jit_reader_funcs = {
    GDB_READER_INTERFACE_VERSION,
    0, read_debug_info,
    unwind, get_frame_id,
    destroy,
};

extern struct gdb_reader_funcs *gdb_init_reader (void) {
    fprintf(stderr, "initiating MoarVM jit reader :)\n");
    moar_jit_reader_funcs.priv_data = (void *)malloc(sizeof(struct priv_data));
    ((struct priv_data*)moar_jit_reader_funcs.priv_data)->ranges_alloc = 16;
    ((struct priv_data*)moar_jit_reader_funcs.priv_data)->ranges_items = 0;
    ((struct priv_data*)moar_jit_reader_funcs.priv_data)->ranges = (struct valid_RIP_ranges *)malloc(16 * sizeof(struct valid_RIP_ranges));
    return &moar_jit_reader_funcs;
}
