#include "moar.h"

#include "rapidhash/rapidhash.h"

#include <stdbool.h>

#include "cmp.h"

typedef struct buffered_cmp_writer {
    MVMThreadContext *tc;
    char *buf;
    size_t write_offs;
    size_t size;
} buffered_cmp_writer;

static size_t cmp_buf_writer(cmp_ctx_t *ctx, const void *data, size_t limit) {
    struct buffered_cmp_writer *buf = (struct buffered_cmp_writer*)ctx->buf;

    if (buf->write_offs + limit > buf->size) {
        fprintf(stderr, "write offs %ld + limit %ld > size %ld\n", buf->write_offs, limit, buf->size);
        memset(buf->buf + buf->write_offs, 'X', buf->size - buf->write_offs);
        return 0;
    }

    memcpy(buf->buf + buf->write_offs, data, limit);
    buf->write_offs += limit;

    return 1;
}

static void flush_buf(cmp_ctx_t *ctx) {
    struct buffered_cmp_writer *buf = (struct buffered_cmp_writer*)ctx->buf;
    fwrite(buf->buf, 1, buf->write_offs, buf->tc->instance->edge_coverage_fh);
    buf->write_offs = 0;
}

#ifdef _WIN32
#define snprintf _snprintf
#endif

typedef struct filename_id_entry {
    struct MVMStrHashHandle hash_handle;
    MVMuint64 id_for_filename;
} filename_id_entry;

#define CMP_WRITE_TYPE(typ) do { char *t = (typ); cmp_write_str(&cmp_writer, "T", 1); cmp_write_str(&cmp_writer, t, strlen(t)); } while(0)

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

    cmp_ctx_t cmp_writer;
    struct buffered_cmp_writer buffered_writer = {0};

    MVMuint8 is_running_cmplog = !!getenv("__AFL_CMPLOG_SHM_ID");

    MVMuint8 has_written_frame_name = 0;

    MVMuint64 frame_base_hash;

    MVMint64 last_filename = -1;
    MVMint64 last_line_number = -1;
    MVMuint64 last_filename_cmp_strid = 0;

    MVMuint64 cmp_strid_for_cu_filename = 0;

    if (g->sf->body.cu->body.filename) {
        char *c_filename = MVM_string_utf8_encode_C_string(tc, g->sf->body.cu->body.filename);
        frame_base_hash = rapidhash(c_filename, strlen(c_filename));

        if (should_dump) {
            buffered_writer.buf = MVM_malloc(4096);
            buffered_writer.size = 4096;
            buffered_writer.write_offs = 0;
            buffered_writer.tc = tc;
            cmp_init(&cmp_writer, &buffered_writer, NULL, NULL, cmp_buf_writer);

            MVMStrHashTable *sht = tc->instance->afl_edge_coverage_filenames_reported;
            if (!sht) {
                tc->instance->afl_edge_coverage_filenames_reported = sht = MVM_calloc(1, sizeof(MVMStrHashTable));
                MVM_str_hash_build(tc, sht, sizeof(struct filename_id_entry), 4096);
            }

            struct filename_id_entry *lv = MVM_str_hash_lvalue_fetch(tc, sht, g->sf->body.cu->body.filename);
            if (lv->hash_handle.key == NULL) {
                lv->hash_handle.key = g->sf->body.cu->body.filename;
                cmp_strid_for_cu_filename = lv->id_for_filename = MVM_str_hash_count(tc, sht) + 0xfa000000;
                cmp_write_map(&cmp_writer, 3);
                CMP_WRITE_TYPE("STR");
                cmp_write_str(&cmp_writer, "id", 2);
                cmp_write_integer(&cmp_writer, cmp_strid_for_cu_filename);
                cmp_write_str(&cmp_writer, "str", 3);
                cmp_write_str(&cmp_writer, c_filename, strlen(c_filename));
                flush_buf(&cmp_writer);
            }
            else {
                cmp_strid_for_cu_filename = lv->id_for_filename;
            }
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

        log_ins              = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshIns));
        log_ins->info        = MVM_op_get_op(MVM_OP_bb_entered);
        log_ins->operands    = MVM_spesh_alloc(tc, g, 1 * sizeof(MVMSpeshOperand));

        MVMuint64 bb_id = rapidhash_withSeed(&bb->idx, sizeof(bb->idx), frame_base_hash);
        log_ins->operands[0].lit_i64 = bb_id;

        if (should_dump) {
            cmp_write_map(&cmp_writer, 5 + !!bb->num_succ + !!bb->num_handler_succ + !!bb->num_pred);

            CMP_WRITE_TYPE("BBIDX");
            cmp_write_str(&cmp_writer, "cu", 2);
            cmp_write_integer(&cmp_writer, cmp_strid_for_cu_filename);

            cmp_write_str(&cmp_writer, "cuid", 4);
            cmp_write_str(&cmp_writer, c_cuid, strlen(c_cuid));

            cmp_write_str(&cmp_writer, "idx", 3);
            cmp_write_integer(&cmp_writer, bb->idx);

            cmp_write_str(&cmp_writer, "bbid", 4);
            cmp_write_uinteger(&cmp_writer, bb_id);

            if (bb->num_succ) {
                cmp_write_str(&cmp_writer, "su", 2);
                cmp_write_array(&cmp_writer, bb->num_succ);
                for (MVMuint32 succid = 0; succid < bb->num_succ; succid++) {
                    cmp_write_uinteger(&cmp_writer, bb->succ[succid]->idx);
                }
            }

            if (bb->num_handler_succ) {
                cmp_write_str(&cmp_writer, "hsu", 3);
                cmp_write_array(&cmp_writer, bb->num_handler_succ);
                for (MVMuint32 succid = 0; succid < bb->num_handler_succ; succid++) {
                    cmp_write_uinteger(&cmp_writer, bb->handler_succ[succid]->idx);
                }
            }

            if (bb->num_pred) {
                cmp_write_str(&cmp_writer, "pr", 2);
                cmp_write_array(&cmp_writer, bb->num_pred);
                for (MVMuint32 predid = 0; predid < bb->num_pred; predid++) {
                    cmp_write_uinteger(&cmp_writer, bb->pred[predid]->idx);
                }
            }

            flush_buf(&cmp_writer);

            if (!has_written_frame_name && g->sf->body.name && MVM_string_graphs(tc, g->sf->body.name)) {
                has_written_frame_name = 1;
                char *encoded = MVM_string_utf8_encode_C_string(tc, g->sf->body.name);

                cmp_write_map(&cmp_writer, 3);

                CMP_WRITE_TYPE("FNAME");

                cmp_write_str(&cmp_writer, "id", 2);
                cmp_write_uinteger(&cmp_writer, bb_id);
                cmp_write_str(&cmp_writer, "str", 3);
                cmp_write_str(&cmp_writer, encoded, strlen(encoded));

                MVM_free(encoded);

                flush_buf(&cmp_writer);
            }

        }

        MVM_spesh_manipulate_insert_ins(tc, bb, ins, log_ins);

        MVMSpeshIns *look_for_cmp = ins->next;

        MVMuint8 found_annotation = 0;
        MVMuint32 fnshi = 0;
        MVMuint32 linenum = 0;

        if (should_track_linenos) {
            if (bbba) {
                while (bbba->bytecode_offset < bb->initial_pc && bbba->bytecode_offset != (MVMuint32)-1) {
                    MVM_bytecode_advance_annotation(tc, &g->sf->body, bbba);
                }
            }
            else {
                bbba = MVM_bytecode_resolve_annotation(tc, &g->sf->body, bb->initial_pc);
            }

            if (bbba
                    && bbba->filename_string_heap_index != 0
                    && (bbba->line_number != last_line_number
                        || bbba->filename_string_heap_index != last_filename)) {
                found_annotation = 1;
                fnshi = bbba->filename_string_heap_index;
                linenum = bbba->line_number;
                goto got_a_lineno_for_this_bb;
            }

            /* Now go through instructions to see if any are annotated with a
            * precise filename/lineno as well. */
            while (ins) {
                MVMSpeshAnn *ann = ins->annotations;

                while (ann) {
                    if (ann->type == MVM_SPESH_ANN_LINENO
                            && ann->data.lineno.filename_string_index != last_filename
                            && ann->data.lineno.line_number != last_line_number) {
                        found_annotation = 1;
                        fnshi = ann->data.lineno.filename_string_index;
                        linenum = ann->data.lineno.line_number;
                        goto got_a_lineno_for_this_bb;
                    }

                    ann = ann->next;
                }

                ins = ins->next;
            }
        }

got_a_lineno_for_this_bb:
        if (found_annotation) {
            /* If the filename hasn't changed, no need to look in the hash. */
            if (last_filename != fnshi) {
                MVMString *fn = MVM_cu_string(tc, g->sf->body.cu, fnshi);

                MVMStrHashTable *sht = tc->instance->afl_edge_coverage_filenames_reported;
                struct filename_id_entry *lv = MVM_str_hash_lvalue_fetch(tc, sht, fn);
                if (lv->hash_handle.key == NULL) {
                    char *encoded = MVM_string_utf8_encode_C_string(tc, fn);

                    lv->hash_handle.key = fn;
                    last_filename_cmp_strid = lv->id_for_filename = MVM_str_hash_count(tc, sht) + 0xcd000000;
                    cmp_write_map(&cmp_writer, 3);
                    CMP_WRITE_TYPE("STR");
                    cmp_write_str(&cmp_writer, "id", 2);
                    cmp_write_integer(&cmp_writer, last_filename_cmp_strid);
                    cmp_write_str(&cmp_writer, "str", 3);
                    cmp_write_str(&cmp_writer, encoded, strlen(encoded));

                    MVM_free(encoded);
                    flush_buf(&cmp_writer);
                }
                else {
                    last_filename_cmp_strid = lv->id_for_filename;
                }
            }

            cmp_write_map(&cmp_writer, 4);
            CMP_WRITE_TYPE("LINE");
            cmp_write_str(&cmp_writer, "bbid", 4);
            cmp_write_uinteger(&cmp_writer, bb_id);
            cmp_write_str(&cmp_writer, "fnm", 3);
            cmp_write_uinteger(&cmp_writer, last_filename_cmp_strid);
            cmp_write_str(&cmp_writer, "lnum", 4);
            cmp_write_uinteger(&cmp_writer, linenum);
            flush_buf(&cmp_writer);

            last_filename = fnshi;
            last_line_number = linenum;
        }

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

    if (buffered_writer.size)
        MVM_free(buffered_writer.buf);

    MVM_free(c_cuid);
}

/* Adds instrumented version of the unspecialized bytecode. */
static void add_instrumentation(MVMThreadContext *tc, MVMStaticFrame *sf) {
    MVMSpeshCode  *sc;
    MVMStaticFrameInstrumentation *ins;
    MVMSpeshGraph *sg = MVM_spesh_graph_create(tc, sf, 1, 0);
    /* If we don't dump the preds of BBs, we don't need to compute them here */
    if (tc->instance->afl_edge_coverage & MVM_BB_COVERAGE_DUMP_BB_IDS) {
        MVM_spesh_graph_recompute_dominance(tc, sg);
    }
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

            cmp_ctx_t cmp_writer;
            struct buffered_cmp_writer buffered_writer = {0};
            char cmpbuffer[2048];
            buffered_writer.buf = cmpbuffer;
            buffered_writer.size = 2048;
            buffered_writer.tc = tc;
            cmp_init(&cmp_writer, &buffered_writer, NULL, NULL, cmp_buf_writer);

            if ((tc->instance->afl_edge_coverage & MVM_BB_COVERAGE_BACKTRACE_ON_SELECTED_EDGES) && __mvm_afl_trace_edges[wrapped_pos]) {
                if (tc->instance->afl_edge_coverage & MVM_BB_COVERAGE_DUMP_FIRST_EDGE_HIT) {
                    cmp_write_map(&cmp_writer, 4);
                    CMP_WRITE_TYPE("C");
                    cmp_write_str(&cmp_writer, "p", 1);
                    cmp_write_uinteger(&cmp_writer, tc->previous_bb_id);
                    cmp_write_str(&cmp_writer, "i", 1);
                    cmp_write_uinteger(&cmp_writer, bb_id);
                    cmp_write_str(&cmp_writer, "w", 1);
                    cmp_write_uinteger(&cmp_writer, wrapped_pos);
                    flush_buf(&cmp_writer);
                    //fprintf(stderr, "CHIT:%lx:%lx:%lx\n", tc->previous_bb_id, bb_id, wrapped_pos);
                }
                MVM_dump_backtrace(tc);
                dumped = 1;
                __mvm_afl_trace_edges[wrapped_pos] = 0;
            }
            if (!dumped && (tc->instance->afl_edge_coverage & MVM_BB_COVERAGE_DUMP_FIRST_EDGE_HIT)) {
                cmp_write_map(&cmp_writer, 4);
                CMP_WRITE_TYPE("H");
                cmp_write_str(&cmp_writer, "p", 1);
                cmp_write_uinteger(&cmp_writer, tc->previous_bb_id);
                cmp_write_str(&cmp_writer, "i", 1);
                cmp_write_uinteger(&cmp_writer, bb_id);
                cmp_write_str(&cmp_writer, "w", 1);
                cmp_write_uinteger(&cmp_writer, wrapped_pos);
                flush_buf(&cmp_writer);

                //fprintf(stderr, "EHIT:%lx:%lx:%lx\n", tc->previous_bb_id, bb_id, wrapped_pos);
            }
        }
        tc->previous_bb_id = bb_id;
    }
#endif
}
