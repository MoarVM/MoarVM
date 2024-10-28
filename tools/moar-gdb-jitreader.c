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
#include "moar.h"

#include <stdlib.h>
#include <string.h>

GDB_DECLARE_GPL_COMPATIBLE_READER;

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
