#include "moar.h"

#include "rapidhash/rapidhash.h"

#ifdef _WIN32
#define snprintf _snprintf
#endif

/* Taken from Linux man-pages 6.13 man page `stpncpy(3)`.
 * MSVC chokes on stpncpy while linking, even though it seems to have the
 * function prototype in its headers? */
char *
stpncpy(char *restrict dst, const char *restrict src, size_t dsize)
{
    size_t  dlen;

    dlen = strnlen(src, dsize);
    return memset(mempcpy(dst, src, dlen), 0, dsize - dlen);
}

static void instrument_graph(MVMThreadContext *tc, MVMSpeshGraph *g) {
    MVMSpeshBB *bb = g->entry->linear_next;
    MVMuint8 should_dump = tc->instance->afl_edge_coverage & MVM_BB_COVERAGE_DUMP_BB_IDS;
    MVMuint8 should_track_linenos = tc->instance->afl_edge_coverage & MVM_BB_COVERAGE_DUMP_BB_LINENOS;

    MVMuint8 is_running_cmplog = !!getenv("__AFL_CMPLOG_SHM_ID");

    MVMuint8 has_written_frame_name = 0;

    MVMuint64 frame_base_hash;

    MVMint64 last_filename = -1;
    MVMint64 last_line_number = -1;

    char info_buf[4096] = {0};
    char *concat_pos = NULL;

    concat_pos = stpncpy(info_buf, "BBI:", 5);

    if (g->sf->body.cu->body.filename) {
        char *c_filename = MVM_string_utf8_encode_C_string(tc, g->sf->body.cu->body.filename);
        frame_base_hash = rapidhash(c_filename, strlen(c_filename));
        if (should_dump) {
            // Lower limit than the full buffer for the first part of the line
            concat_pos = stpncpy(concat_pos, c_filename, 2048);
            *concat_pos = ':';
            concat_pos++;
        }
        MVM_free(c_filename);
    }
    else {
        /* We don't have something yet for code compiled at run
         * time to get reliable / stable bb IDs generated for them */
        return;
    }

    char *c_cuid = MVM_string_utf8_encode_C_string(tc, g->sf->body.cuuid);
    frame_base_hash = rapidhash_withSeed(c_cuid, strlen(c_cuid), frame_base_hash);
    if (should_dump) {
        concat_pos = stpncpy(concat_pos, c_cuid, 2048 - (concat_pos - info_buf));
        *concat_pos = ':';
        concat_pos++;
    }
    MVM_free(c_cuid);

    MVMBytecodeAnnotation *bbba = NULL;

    if (should_track_linenos)
        bbba = MVM_bytecode_resolve_annotation(tc, &g->sf->body, bb->initial_pc);

    while (bb) {
        MVMSpeshIns *ins = bb->first_ins;
        MVMSpeshIns *log_ins;

        /* skip PHI instructions, to make sure PHI only occur uninterrupted after start-of-bb */
        while (ins && ins->info->opcode == MVM_SSA_PHI) {
            ins = ins->next;
        }
        if (!ins) ins = bb->last_ins;

        /* Jumplists require the target BB to start in the goto op.
         * We must not break this, or we cause the interpreter to derail */
        if (bb->last_ins->info->opcode == MVM_OP_jumplist) {
            MVMint16 to_skip = bb->num_succ;
            for (; to_skip > 0; to_skip--) {
                bb = bb->linear_next;
            }
            continue;
        }

        log_ins = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshIns));
        log_ins->info        = MVM_op_get_op(MVM_OP_bb_entered);
        log_ins->operands    = MVM_spesh_alloc(tc, g, 1 * sizeof(MVMSpeshOperand));

        MVMuint64 bb_id = rapidhash_withSeed(&bb->idx, sizeof(bb->idx), frame_base_hash);
        log_ins->operands[0].lit_i64 = bb_id;

        if (should_dump) {
            snprintf(concat_pos, 4096 - (concat_pos - info_buf), "%d:%lx\n", bb->idx, bb_id);
            fputs(info_buf, stderr);

            if (!has_written_frame_name && g->sf->body.name && MVM_string_graphs(tc, g->sf->body.name)) {
                has_written_frame_name = 1;
                char *encoded = MVM_string_utf8_encode_C_string(tc, g->sf->body.name);
                char buffer[4096];
                snprintf(buffer, 4096, "FN:%lx:%s\n", bb_id, encoded);
                fputs(buffer, stderr);
                MVM_free(encoded);
            }

        }

        MVM_spesh_manipulate_insert_ins(tc, bb, ins, log_ins);

        MVMSpeshIns *look_for_cmp = ins->next;

        if (should_track_linenos) {
            if (bbba) {
                while (bbba->bytecode_offset < bb->initial_pc && bbba->bytecode_offset != (MVMuint32)-1) {
                    MVM_bytecode_advance_annotation(tc, &g->sf->body, bbba);
                }
            }
            else {
                bbba = MVM_bytecode_resolve_annotation(tc, &g->sf->body, bb->initial_pc);
            }

            if (bbba && bbba->filename_string_heap_index != 0 && (bbba->line_number != last_line_number || bbba->filename_string_heap_index != last_filename)) {
                MVMString *fn = MVM_cu_string(tc, g->sf->body.cu, bbba->filename_string_heap_index);
                char *encoded = MVM_string_utf8_encode_C_string(tc, fn);
                char buffer[4096];
                snprintf(buffer, 4096, "FNL:%lx:%u:%s\n", bb_id, bbba->line_number, encoded);
                MVM_free(encoded);

                fputs(buffer, stderr);

                last_filename = bbba->filename_string_heap_index;
                last_line_number = bbba->line_number;

                goto got_a_lineno_for_this_bb;
            }

            /* Now go through instructions to see if any are annotated with a
            * precise filename/lineno as well. */
            while (ins) {
                MVMSpeshAnn *ann = ins->annotations;

                while (ann) {
                    if (ann->type == MVM_SPESH_ANN_LINENO && ann->data.lineno.filename_string_index != last_filename && ann->data.lineno.line_number != last_line_number) {
                        MVMString *fn = MVM_cu_string(tc, g->sf->body.cu, ann->data.lineno.filename_string_index);
                        char *encoded = MVM_string_utf8_encode_C_string(tc, fn);
                        char buffer[4096];
                        snprintf(buffer, 4096, "FNL:%lx:%u:%s\n", bb_id, ann->data.lineno.line_number, encoded);
                        MVM_free(encoded);

                        fputs(buffer, stderr);

                        last_filename    = ann->data.lineno.filename_string_index;
                        last_line_number = ann->data.lineno.line_number;

                        goto got_a_lineno_for_this_bb;
                    }

                    ann = ann->next;
                }

                ins = ins->next;
            }
        }

got_a_lineno_for_this_bb:

        if (is_running_cmplog && look_for_cmp) {
            MVMuint64 caller_id = bb_id;
            while (look_for_cmp) {
                const MVMuint16 opcode = look_for_cmp->info->opcode;
                MVMuint16 attr = 0;
                if (opcode == MVM_OP_eq_i ||
                    opcode == MVM_OP_eq_u ||
                    opcode == MVM_OP_ne_i ||
                    opcode == MVM_OP_ne_u) {
                    attr = 1;
                }
                else if (opcode == MVM_OP_gt_i ||
                    opcode == MVM_OP_gt_u) {
                    attr = 2;
                }
                else if (opcode == MVM_OP_ge_i ||
                    opcode == MVM_OP_ge_u) {
                    attr = 3;
                }
                else if (opcode == MVM_OP_lt_i ||
                    opcode == MVM_OP_lt_u) {
                    attr = 4;
                }
                else if (opcode == MVM_OP_le_i ||
                    opcode == MVM_OP_le_u) {
                    attr = 5;
                }

                if (attr) {
                    caller_id =
                        rapidhash_withSeed(&bb->idx, sizeof(bb->idx), caller_id);

                    MVMSpeshIns *cmplog_ins =
                        MVM_spesh_alloc(tc, g, sizeof(MVMSpeshIns));
                    cmplog_ins->info = MVM_op_get_op(MVM_OP_cmplog_i);
                    cmplog_ins->operands =
                        MVM_spesh_alloc(tc, g, 4 * sizeof(MVMSpeshOperand));

                    cmplog_ins->operands[0] = look_for_cmp->operands[1];
                    cmplog_ins->operands[1] = look_for_cmp->operands[2];
                    cmplog_ins->operands[2].lit_i64 = caller_id;
                    cmplog_ins->operands[3].lit_i16 = attr;

                    MVM_spesh_manipulate_insert_ins(tc, bb, look_for_cmp->prev,
                                                    cmplog_ins);
                }
                else if (opcode == MVM_OP_atkey_i || opcode == MVM_OP_atkey_n || opcode == MVM_OP_atkey_s || opcode == MVM_OP_atkey_o || opcode == MVM_OP_atkey_u || opcode == MVM_OP_existskey) {
                    caller_id =
                        rapidhash_withSeed(&bb->idx, sizeof(bb->idx), caller_id);

                    MVMSpeshIns *cmplog_ins =
                        MVM_spesh_alloc(tc, g, sizeof(MVMSpeshIns));
                    cmplog_ins->info = MVM_op_get_op(MVM_OP_cmplog_atkey);
                    cmplog_ins->operands =
                        MVM_spesh_alloc(tc, g, 3 * sizeof(MVMSpeshOperand));

                    cmplog_ins->operands[0] = look_for_cmp->operands[1];
                    cmplog_ins->operands[1] = look_for_cmp->operands[2];
                    cmplog_ins->operands[2].lit_i64 = caller_id;

                    MVM_spesh_manipulate_insert_ins(tc, bb, look_for_cmp->prev,
                                                    cmplog_ins);
                }
                look_for_cmp = look_for_cmp->next;
            }
        }

        bb = bb->linear_next;
    }
}

/* Adds instrumented version of the unspecialized bytecode. */
static void add_instrumentation(MVMThreadContext *tc, MVMStaticFrame *sf) {
    MVMSpeshCode  *sc;
    MVMStaticFrameInstrumentation *ins;
    MVMSpeshGraph *sg = MVM_spesh_graph_create(tc, sf, 1, 0);
    instrument_graph(tc, sg);
    sc = MVM_spesh_codegen(tc, sg);
    ins = sf->body.instrumentation;
    if (!ins)
        ins = MVM_calloc(1, sizeof(MVMStaticFrameInstrumentation));
    ins->instrumented_bytecode        = sc->bytecode;
    ins->instrumented_handlers        = sc->handlers;
    ins->instrumented_bytecode_size   = sc->bytecode_size;
    ins->uninstrumented_bytecode      = sf->body.bytecode;
    ins->uninstrumented_handlers      = sf->body.handlers;
    ins->uninstrumented_bytecode_size = sf->body.bytecode_size;
    sf->body.instrumentation = ins;
    MVM_spesh_graph_destroy(tc, sg);
    MVM_free(sc);
}


/* Instruments code with per-BB-edge counting for code coverage. */
static void bb_edge_instrument(MVMThreadContext *tc, MVMStaticFrame *sf) {
    if (!sf->body.instrumentation || sf->body.bytecode != sf->body.instrumentation->instrumented_bytecode) {
        /* Handle main, non-specialized, bytecode. */
        if (!sf->body.instrumentation || !sf->body.instrumentation->instrumented_bytecode)
            add_instrumentation(tc, sf);
        sf->body.bytecode      = sf->body.instrumentation->instrumented_bytecode;
        if (sf->body.handlers)
            MVM_free(sf->body.handlers);
        sf->body.handlers      = sf->body.instrumentation->instrumented_handlers;
        sf->body.bytecode_size = sf->body.instrumentation->instrumented_bytecode_size;

        /* Throw away any existing specializations. */
        MVM_spesh_candidate_discard_existing(tc, sf);
    }
}

/* Instruments code with per-line logging of code coverage */
void MVM_edge_coverage_instrument(MVMThreadContext *tc, MVMStaticFrame *sf) {
    bb_edge_instrument(tc, sf);
}

/* A byte is nonzero here if a hit of the edge should result in a print-out. */
MVMuint8 *__mvm_afl_trace_edges;
/* Restore trace edges array from the original values with this. */
MVMuint8 *__mvm_afl_trace_edges_pristine;
/* A little "cache" of different parts of Edge IDs to further differentiate
 * what actual edge was hit in a given slot. */
MVMuint16 *__mvm_last_edge_seen;

#if __AFL_COMPILER
extern unsigned char *__afl_area_ptr;
extern unsigned int   __afl_cov_map_size;

MVMuint64 __instrumentation_active;

typedef MVMuint8 u8;
typedef MVMuint32 u32;
typedef MVMuint64 u64;

/* Stolen from afl's include/cmplog.h */

#define CMPLOG_LVL_MAX 3

#define CMP_MAP_W 65536
#define CMP_MAP_H 32
#define CMP_MAP_RTN_H (CMP_MAP_H / 2)

#define SHAPE_BYTES(x) (x + 1)

#define CMP_TYPE_INS 0
#define CMP_TYPE_RTN 1

static u8 __afl_cmplog_max_len = 32;  // 16-32

#define ADDR_ATTR_COMBINE(v0attr, v1attr) ((v0attr & 3) + ((v1attr & 3) << 2))
#define ADDR_ATTR_V0(x) (x & 3)
#define ADDR_ATTR_V1(x) ((x >> 2) & 3)

struct cmp_header {  // 16 bit = 2 bytes

  unsigned hits : 6;       // up to 63 entries, we have CMP_MAP_H = 32
  unsigned shape : 5;      // 31+1 bytes max
  unsigned type : 1;       // 2: cmp, rtn
  unsigned attribute : 4;  // 16 for arithmetic comparison types

} __attribute__((packed));

struct cmp_operands {

  u64 v0;
  u64 v0_128;
  u64 v0_256_0;  // u256 is unsupported by any compiler for now, so future use
  u64 v0_256_1;
  u64 v1;
  u64 v1_128;
  u64 v1_256_0;
  u64 v1_256_1;
  u8  unused[8];  // 2 bits could be used for "is constant operand"

} __attribute__((packed));

struct cmpfn_operands {

  u8 v0[32];
  u8 v1[32];
  u8 v0_len;
  u8 v1_len;
  u8 addr_attr;
  u8 unused[5];  // 2 bits could be used for "is constant operand"

} __attribute__((packed));

typedef struct cmp_operands cmp_map_list[CMP_MAP_H];

struct cmp_map {

  struct cmp_header   headers[CMP_MAP_W];
  struct cmp_operands log[CMP_MAP_W][CMP_MAP_H];

};


/* Adapted from afl's afl-compiler-rt.o.c to work better with moar. */

extern struct cmp_map *__afl_cmp_map;
extern struct cmp_map *__afl_cmp_map_backup;

void MVM_fuzzing_cmplog_ins_hook8(uint64_t arg1, uint64_t arg2, uint64_t caller_id, uint8_t attr) {

  // fprintf(stderr, "hook8 arg0=%lx arg1=%lx attr=%u\n", arg1, arg2, attr);

  if (MVM_LIKELY(!__afl_cmp_map)) return;
  if (MVM_UNLIKELY(arg1 == arg2)) return;

  uintptr_t k = (uintptr_t)caller_id;
  k = (uintptr_t)(k & (CMP_MAP_W - 1));

  u32 hits;

  if (__afl_cmp_map->headers[k].type != CMP_TYPE_INS) {

    __afl_cmp_map->headers[k].type = CMP_TYPE_INS;
    hits = 0;
    __afl_cmp_map->headers[k].hits = 1;
    __afl_cmp_map->headers[k].shape = 7;

  } else {

    hits = __afl_cmp_map->headers[k].hits++;

    if (__afl_cmp_map->headers[k].shape < 7) {

      __afl_cmp_map->headers[k].shape = 7;

    }

  }

  __afl_cmp_map->headers[k].attribute = attr;

  hits &= CMP_MAP_H - 1;
  __afl_cmp_map->log[k][hits].v0 = arg1;
  __afl_cmp_map->log[k][hits].v1 = arg2;

/*  if (hits == 0) {
    fprintf(stderr, "cmplog: entry %lx latest entry: %lx == %lx\n", k, arg1, arg2);
  }*/
}

void MVM_fuzzing_cmplog_rtn_hook_atkey_hook(MVMThreadContext *tc, MVMObject *hash, MVMString *str, uint64_t caller_id) {

  // fprintf(stderr, "RTN1 %p %p %u\n", ptr1, ptr2, len);
  if (MVM_LIKELY(!__afl_cmp_map)) return;

  if (MVM_UNLIKELY(!hash)) return;

  MVMuint64 len = MVM_string_graphs(tc, str);
  if (MVM_UNLIKELY(len < 2 || len > __afl_cmplog_max_len)) return;

  MVMuint64 hash_elems = MVM_repr_elems(tc, hash);
  if (MVM_UNLIKELY(!hash_elems)) return;

  if (MVM_UNLIKELY(REPR(hash)->ID != MVM_REPR_ID_MVMHash)) return;

  u32 k = (uintptr_t)(caller_id) & (CMP_MAP_W - 1);

  u32 hits;

  u32 l = len > 32 ? 32 : len;

  if (__afl_cmp_map->headers[k].type != CMP_TYPE_RTN) {

    __afl_cmp_map->headers[k].type = CMP_TYPE_RTN;
    __afl_cmp_map->headers[k].hits = 1;
    __afl_cmp_map->headers[k].shape = l - 1;
    hits = 0;

  } else {

    hits = __afl_cmp_map->headers[k].hits++;

    if (__afl_cmp_map->headers[k].shape < l) {

      __afl_cmp_map->headers[k].shape = l - 1;

    }

  }

  hits &= CMP_MAP_RTN_H - 1;

  MVMStrHashTable *hashtable = &((MVMHash *)hash)->body.hashtable;
  MVMStrHashIterator iterator = MVM_str_hash_first(tc, hashtable);

  struct cmpfn_operands *cmpfn = (struct cmpfn_operands *)__afl_cmp_map->log[k];
  u32 init_hits = hits;
  char *encoded_key = MVM_string_utf8_encode_C_string(tc, str);
  memcpy(cmpfn[init_hits].v0, encoded_key, l);

  for (MVMuint64 written = 0; written < CMP_MAP_RTN_H && written < hash_elems && !MVM_str_hash_at_end(tc, hashtable, iterator); written++) {
    MVMHashEntry *current = MVM_str_hash_current_nocheck(tc, hashtable, iterator);
    char *other_key = MVM_string_utf8_encode_C_string(tc, current->hash_handle.key);

    cmpfn[hits].v0_len = 0x80 + l;
    cmpfn[hits].v1_len = 0x80 + l;
    if (hits != init_hits)
        memcpy(cmpfn[hits].v0, encoded_key, l);
    memcpy(cmpfn[hits].v1, other_key, l);

    hits++;
    hits &= CMP_MAP_RTN_H - 1;

    iterator = MVM_str_hash_next_nocheck(tc, hashtable, iterator);
    MVM_free(other_key);
  }

  /*
  fprintf(stderr, "cmplog hash: entry %lx from %lu to %lu length %ld compare_against %32s\n", k, init_hits, hits, l, encoded_key);
  */

  MVM_free(encoded_key);
}

#else
void MVM_fuzzing_cmplog_ins_hook8(uint64_t arg1, uint64_t arg2, uint64_t caller_id, uint8_t attr) {
}
void MVM_fuzzing_cmplog_rtn_hook_atkey_hook(MVMThreadContext *tc, MVMObject *hash, MVMString *str, uint64_t caller_id) {
}
#endif


void MVM_edge_coverage_report_bb_edge_hit(MVMThreadContext *tc, MVMuint64 bb_id) {
#if __AFL_COMPILER
    MVMuint64 combined_id = (tc->previous_bb_id >> 1) ^ bb_id;
    MVMuint64 wrapped_pos;

    if (!tc->suppress_coverage) {
        if (__afl_cov_map_size == 65536 || __afl_cov_map_size == 2 * 65536 || __afl_cov_map_size == 4 * 65536) {
            wrapped_pos = combined_id % __afl_cov_map_size;
        } else {
            wrapped_pos = combined_id % __afl_cov_map_size;
        }
        MVMuint8 *map_pos = &__afl_area_ptr[wrapped_pos];
        MVMuint8 is_new = 0;
        is_new = *map_pos == 0;
        *map_pos = *map_pos + 1 == 0 ? 1 : *map_pos + 1;
        if (!is_new && __mvm_last_edge_seen && __mvm_last_edge_seen[wrapped_pos] != (MVMuint32)(combined_id << 32)) {
            is_new = 2;
        }
        if (__mvm_last_edge_seen) __mvm_last_edge_seen[wrapped_pos] = (MVMuint32)(combined_id << 32);
        if (MVM_UNLIKELY(is_new)) {
            MVMuint8 dumped = 0;
            if ((tc->instance->afl_edge_coverage & MVM_BB_COVERAGE_BACKTRACE_ON_SELECTED_EDGES) && __mvm_afl_trace_edges[wrapped_pos]) {
                if (tc->instance->afl_edge_coverage & MVM_BB_COVERAGE_DUMP_FIRST_EDGE_HIT)
                    fprintf(stderr, "CHIT:%lx:%lx:%lx\n", tc->previous_bb_id, bb_id, wrapped_pos);
                MVM_dump_backtrace(tc);
                dumped = 1;
                __mvm_afl_trace_edges[wrapped_pos] = 0;
            }
            if (!dumped && (tc->instance->afl_edge_coverage & MVM_BB_COVERAGE_DUMP_FIRST_EDGE_HIT)) {
                fprintf(stderr, "EHIT:%lx:%lx:%lx\n", tc->previous_bb_id, bb_id, wrapped_pos);
            }
        }
        tc->previous_bb_id = bb_id;
    }
#endif
}
