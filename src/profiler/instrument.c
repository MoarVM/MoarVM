#include "moar.h"

typedef struct {
    MVMuint32 items;
    MVMuint32 alloc;

    MVMProfileCallNode **list;
} NodeWorklist;

/* Adds an instruction to log an allocation. */
static void add_allocation_logging_at_location(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB *bb, MVMSpeshIns *ins, MVMSpeshIns *location) {
    MVMSpeshIns *alloc_ins = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshIns));
    alloc_ins->info        = MVM_op_get_op(MVM_OP_prof_allocated);
    alloc_ins->operands    = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshOperand));
    alloc_ins->operands[0] = ins->operands[0];
    MVM_spesh_manipulate_insert_ins(tc, bb, location, alloc_ins);
}

static void add_allocation_logging(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB *bb, MVMSpeshIns *ins) {
    add_allocation_logging_at_location(tc, g, bb, ins, ins);
}

static void add_nativecall_logging(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB *bb, MVMSpeshIns *ins) {
    MVMSpeshIns *enter_ins = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshIns));
    MVMSpeshIns *exit_ins  = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshIns));

    enter_ins->info        = MVM_op_get_op(MVM_OP_prof_enternative);
    enter_ins->operands    = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshOperand));
    enter_ins->operands[0] = ins->operands[2];

    MVM_spesh_manipulate_insert_ins(tc, bb, ins->prev, enter_ins);

    exit_ins->info         = MVM_op_get_op(MVM_OP_prof_exit);

    MVM_spesh_manipulate_insert_ins(tc, bb, ins, exit_ins);
}

static void instrument_graph(MVMThreadContext *tc, MVMSpeshGraph *g) {
    /* Insert entry instruction. */
    MVMSpeshBB *bb         = g->entry->linear_next;
    MVMSpeshIns *enter_ins = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshIns));
    enter_ins->info        = MVM_op_get_op(MVM_OP_prof_enter);
    MVM_spesh_manipulate_insert_ins(tc, bb, NULL, enter_ins);

    /* Walk the code and insert profile logging instructions as needed. */
    while (bb) {
        MVMSpeshIns *ins = bb->first_ins;
        while (ins) {
            switch (ins->info->opcode) {
            case MVM_OP_return_i:
            case MVM_OP_return_u:
            case MVM_OP_return_n:
            case MVM_OP_return_s:
            case MVM_OP_return_o:
            case MVM_OP_return: {
                /* Log a normal exit prior to returning. */
                MVMSpeshIns *exit_ins = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshIns));
                exit_ins->info        = MVM_op_get_op(MVM_OP_prof_exit);
                MVM_spesh_manipulate_insert_ins(tc, bb, ins->prev, exit_ins);

                /* If the return instruction is a goto target, move to the
                 * instrumentation instruction. */
                if (ins->annotations) {
                    MVMSpeshAnn *ann      = ins->annotations;
                    MVMSpeshAnn *prev_ann = NULL;
                    while (ann) {
                        if (ann->type == MVM_SPESH_ANN_FH_GOTO) {
                            if (prev_ann)
                                prev_ann->next = ann->next;
                            else
                                ins->annotations = ann->next;
                            exit_ins->annotations = ann;
                            ann->next = NULL;
                            break;
                        }
                        prev_ann = ann;
                        ann = ann->next;
                    }
                }

                break;
            }
            case MVM_OP_dispatch_o:
            case MVM_OP_param_rp_o:
            case MVM_OP_param_rn_o:
            case MVM_OP_param_rn2_o:
            case MVM_OP_param_rn2_s:
            case MVM_OP_param_sp:
            case MVM_OP_param_sn:
            case MVM_OP_newexception:
            case MVM_OP_usecapture:
            case MVM_OP_savecapture:
            case MVM_OP_takeclosure:
            case MVM_OP_getattr_o:
            case MVM_OP_getattrs_o:
            case MVM_OP_sp_p6ogetvc_o:
            case MVM_OP_create:
            case MVM_OP_sp_fastcreate:
            case MVM_OP_clone:
            case MVM_OP_box_i:
            case MVM_OP_box_u:
            case MVM_OP_box_n:
            case MVM_OP_box_s:
            case MVM_OP_iter:
            case MVM_OP_add_I:
            case MVM_OP_sub_I:
            case MVM_OP_mul_I:
            case MVM_OP_div_I:
            case MVM_OP_mod_I:
            case MVM_OP_neg_I:
            case MVM_OP_abs_I:
            case MVM_OP_bor_I:
            case MVM_OP_bxor_I:
            case MVM_OP_band_I:
            case MVM_OP_bnot_I:
            case MVM_OP_blshift_I:
            case MVM_OP_brshift_I:
            case MVM_OP_pow_I:
            case MVM_OP_gcd_I:
            case MVM_OP_lcm_I:
            case MVM_OP_expmod_I:
            case MVM_OP_rand_I:
            case MVM_OP_base_I:
            case MVM_OP_coerce_nI:
            case MVM_OP_coerce_sI:
            case MVM_OP_coerce_II:
            case MVM_OP_coerce_is:
            case MVM_OP_coerce_us:
            case MVM_OP_coerce_ns:
            case MVM_OP_buffertocu:
            case MVM_OP_serializetobuf:
            case MVM_OP_getsignals:
            case MVM_OP_decodersetlineseps:
            case MVM_OP_decoderconfigure:
            case MVM_OP_decoderaddbytes:
            case MVM_OP_decodertakeallchars:
            case MVM_OP_decodertakeavailablechars:
            case MVM_OP_decodertakeline:
            case MVM_OP_decodertakebytes:
            case MVM_OP_decodertakechars:
            case MVM_OP_decodertakecharseof:
            case MVM_OP_decodeconf:
            case MVM_OP_decoderepconf:
            case MVM_OP_strfromname:
            case MVM_OP_slice:
            case MVM_OP_loadbytecode:
            case MVM_OP_loadbytecodefh:
            case MVM_OP_multidimref_i:
            case MVM_OP_multidimref_u:
            case MVM_OP_multidimref_n:
            case MVM_OP_multidimref_s:
            case MVM_OP_asyncudp:
            case MVM_OP_asyncwritebytesto:
            case MVM_OP_asyncwritebytes:
            case MVM_OP_asyncreadbytes:
            case MVM_OP_encoderep:
            case MVM_OP_lc:
            case MVM_OP_uc:
            case MVM_OP_fc:
            case MVM_OP_getcodelocation:
            case MVM_OP_normalizecodes:
            case MVM_OP_strtocodes:
            case MVM_OP_strfromcodes:
            case MVM_OP_readlink:
            case MVM_OP_parameterizetype:
            case MVM_OP_spawnprocasync:
            case MVM_OP_execname:
            case MVM_OP_getuniprop_str:
            case MVM_OP_getuniname:
            case MVM_OP_sp_add_I:
            case MVM_OP_sp_sub_I:
            case MVM_OP_sp_mul_I:
            case MVM_OP_uname:
            case MVM_OP_decodelocaltime:
            case MVM_OP_indexingoptimized:
            case MVM_OP_setdimensions:
            case MVM_OP_dimensions:
            case MVM_OP_watchfile:
            case MVM_OP_timer:
            case MVM_OP_ctx:
            case MVM_OP_ctxouter:
            case MVM_OP_ctxcaller:
            case MVM_OP_ctxouterskipthunks:
            case MVM_OP_ctxcallerskipthunks:
            case MVM_OP_getlockcondvar:
            case MVM_OP_gethostname:
            case MVM_OP_backtrace:
            case MVM_OP_backtracestrings:
            case MVM_OP_replace:
            case MVM_OP_capturenamedshash:
            case MVM_OP_bitand_s:
            case MVM_OP_bitor_s:
            case MVM_OP_bitxor_s:
            case MVM_OP_backendconfig:
            case MVM_OP_findsym:
            case MVM_OP_getenvhash:
            case MVM_OP_clargs:
            case MVM_OP_cwd:
            case MVM_OP_newthread:
            case MVM_OP_accept_sk:
            case MVM_OP_bind_sk:
            case MVM_OP_connect_sk:
            case MVM_OP_socket:
            case MVM_OP_open_fh:
            case MVM_OP_read_dir:
            case MVM_OP_open_dir:
            case MVM_OP_compunitcodes:
            case MVM_OP_newmixintype:
            case MVM_OP_scgetdesc:
            case MVM_OP_popcompsc:
            case MVM_OP_createsc:
            case MVM_OP_sha1:
            case MVM_OP_freshcoderef:
            case MVM_OP_gethllsym:
            case MVM_OP_getcurhllsym:
            case MVM_OP_settypecache:
            case MVM_OP_newtype:
            case MVM_OP_nfafromstatelist:
            case MVM_OP_nfarunproto:
            case MVM_OP_flip:
            case MVM_OP_escape:
            case MVM_OP_chr:
            case MVM_OP_join:
            case MVM_OP_split:
            case MVM_OP_concat_s:
            case MVM_OP_repeat_s:
            case MVM_OP_substr_s:
            case MVM_OP_radix:
            case MVM_OP_radix_I: {
                add_allocation_logging(tc, g, bb, ins);
                break;
            }
            case MVM_OP_param_op_o:
            case MVM_OP_param_on_o: {
                /* These ops jump to a label if they "succeed", so the
                 * allocation logging goes in another bb. */
                MVMSpeshBB  *target   = ins->operands[2].ins_bb;
                MVMSpeshIns *location = target->first_ins;
                while (location && location->info->opcode == MVM_SSA_PHI) {
                    location = location->next;
                }
                location = location->prev;
                add_allocation_logging_at_location(tc, g, target, ins, location);
                break;
            }
            case MVM_OP_getlex:
            case MVM_OP_getlex_no:
            case MVM_OP_getlexstatic_o:
            case MVM_OP_getlexperinvtype_o:
            case MVM_OP_getlexouter:
            case MVM_OP_getlexrel:
            case MVM_OP_getlexreldyn:
            case MVM_OP_getlexrelcaller:
            case MVM_OP_getlexcaller:
            {
                /* We have to check if the target register is actually
                 * an object register. */
                if ((g->local_types && g->local_types[ins->operands[0].reg.orig] == MVM_reg_obj)
                    || (!g->local_types && g->sf->body.local_types[ins->operands[0].reg.orig] == MVM_reg_obj))
                    add_allocation_logging(tc, g, bb, ins);
                break;
            }
            case MVM_OP_getlexref_i:
            case MVM_OP_getlexref_i8:
            case MVM_OP_getlexref_i16:
            case MVM_OP_getlexref_i32:
            case MVM_OP_getlexref_u:
            case MVM_OP_getlexref_u8:
            case MVM_OP_getlexref_u16:
            case MVM_OP_getlexref_u32:
            case MVM_OP_getlexref_n:
            case MVM_OP_getlexref_n32:
            case MVM_OP_getlexref_s:
            case MVM_OP_getlexref_ni:
            case MVM_OP_getlexref_nu:
            case MVM_OP_getlexref_nn:
            case MVM_OP_getlexref_ns:
            case MVM_OP_atposref_i:
            case MVM_OP_atposref_u:
            case MVM_OP_atposref_n:
            case MVM_OP_atposref_s:
            case MVM_OP_getattrref_i:
            case MVM_OP_getattrref_u:
            case MVM_OP_getattrref_n:
            case MVM_OP_getattrref_s:
            case MVM_OP_getattrsref_i:
            case MVM_OP_getattrsref_u:
            case MVM_OP_getattrsref_n:
            case MVM_OP_getattrsref_s:
            case MVM_OP_nativecallcast:
            case MVM_OP_nativecallglobal:
                add_allocation_logging(tc, g, bb, ins);
                break;
            case MVM_OP_nativecallinvoke:
                add_nativecall_logging(tc, g, bb, ins);
                break;
            default:
                /* See if it's an allocating extop. */
                if (ins->info->opcode == (MVMuint16)-1) {
                    MVMExtOpRecord *extops     = g->sf->body.cu->body.extops;
                    MVMuint16       num_extops = g->sf->body.cu->body.num_extops;
                    MVMuint16       i;
                    for (i = 0; i < num_extops; i++) {
                        if (extops[i].info == ins->info) {
                            if (extops[i].allocating && extops[i].info->num_operands >= 1)
                                add_allocation_logging(tc, g, bb, ins);
                            break;
                        }
                    }
                }
                break;
            }
            ins = ins->next;
        }
        bb = bb->linear_next;
    }
}

/* Adds instrumented versions of the unspecialized bytecode. */
static void add_instrumentation(MVMThreadContext *tc, MVMStaticFrame *sf) {
    MVMSpeshCode  *sc;
    MVMStaticFrameInstrumentation *ins;
    MVMSpeshGraph *sg = MVM_spesh_graph_create(tc, sf, 1, 0);
    instrument_graph(tc, sg);
    sc = MVM_spesh_codegen(tc, sg);
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

/* Instruments a static frame for profiling, or uses an existing
 * instrumentation if it exists. */
void MVM_profile_instrument(MVMThreadContext *tc, MVMStaticFrame *sf) {
    if (!sf->body.instrumentation || sf->body.bytecode != sf->body.instrumentation->instrumented_bytecode) {
        /* Handle main, non-specialized, bytecode. */
        if (!sf->body.instrumentation)
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

/* Ensures we're no longer in instrumented code. */
void MVM_profile_ensure_uninstrumented(MVMThreadContext *tc, MVMStaticFrame *sf) {
    /* XXX due to multithreading trouble, just turning instrumentation off by
     * switching bytecode back does not work. Profiling instrumentation is
     * safe to keep around with only a small performance penalty, and CTW
     * instrumentation is normally not turned off during run time,
     * so for now we'll just do nothing. */
}

/* Starts instrumented profiling. */
void MVM_profile_instrumented_start(MVMThreadContext *tc, MVMObject *config) {
    /* Wait for specialization thread to stop working, so it won't trip over
     * bytecode instrumentation, then enable profiling. */
    MVM_gc_mark_thread_blocked(tc);
    uv_mutex_lock(&(tc->instance->mutex_spesh_sync));
    while (tc->instance->spesh_working != 0)
        uv_cond_wait(&(tc->instance->cond_spesh_sync), &(tc->instance->mutex_spesh_sync));
    tc->instance->profiling = 1;
    tc->instance->instrumentation_level++;
    uv_mutex_unlock(&(tc->instance->mutex_spesh_sync));
    MVM_gc_mark_thread_unblocked(tc);
}

/* Simple allocation functions. */
static MVMObject * new_array(MVMThreadContext *tc) {
    return MVM_repr_alloc_init(tc, MVM_hll_current(tc)->slurpy_array_type);
}
static MVMObject * new_hash(MVMThreadContext *tc) {
    return MVM_repr_alloc_init(tc, MVM_hll_current(tc)->slurpy_hash_type);
}
static MVMObject * box_i(MVMThreadContext *tc, MVMint64 i) {
    return MVM_repr_box_int(tc, MVM_hll_current(tc)->int_box_type, i);
}
static MVMObject * box_s(MVMThreadContext *tc, MVMString *s) {
    return MVM_repr_box_str(tc, MVM_hll_current(tc)->str_box_type, s);
}
static MVMString * str(MVMThreadContext *tc, const char *buf) {
    return MVM_string_ascii_decode_nt(tc, tc->instance->VMString, buf);
}

/* String constants we'll reuse. */
typedef struct {
    MVMString *total_time;
    MVMString *call_graph;
    MVMString *name;
    MVMString *id;
    MVMString *parent;
    MVMString *file;
    MVMString *line;
    MVMString *entries;
    MVMString *spesh_entries;
    MVMString *jit_entries;
    MVMString *inlined_entries;
    MVMString *inclusive_time;
    MVMString *exclusive_time;
    MVMString *callees;
    MVMString *allocations;
    MVMString *spesh;
    MVMString *jit;
    MVMString *replaced;
    MVMString *type;
    MVMString *count;
    MVMString *gcs;
    MVMString *time;
    MVMString *full;
    MVMString *sequence;
    MVMString *responsible;
    MVMString *cleared_bytes;
    MVMString *retained_bytes;
    MVMString *promoted_bytes;
    MVMString *promoted_bytes_unmanaged;
    MVMString *gen2_roots;
    MVMString *stolen_gen2_roots;
    MVMString *start_time;
    MVMString *first_entry_time;
    MVMString *osr;
    MVMString *deopt_one;
    MVMString *deopt_all;
    MVMString *spesh_time;
    MVMString *thread;
    MVMString *native_lib;
    MVMString *managed_size;
    MVMString *has_unmanaged_data;
    MVMString *repr;
    MVMString *deallocs;
    MVMString *nursery_fresh;
    MVMString *nursery_seen;
    MVMString *gen2;
    MVMString *mono;
    MVMString *poly;
    MVMString *mega;
    MVMString *blow;
} ProfDumpStrs;

typedef struct {
    MVMThreadContext *tc;
    ProfDumpStrs *pds;
    MVMObject *types_array;
} ProfTcPdsStruct;

static MVMObject * insert_if_not_exists(MVMThreadContext *tc, ProfDumpStrs *pds, MVMObject *storage, MVMint64 key) {
    MVMuint64 index;
    MVMObject *result;
    MVMObject *type_info_hash;

    for (index = 0; index < MVM_repr_elems(tc, storage); index++) {
        MVMObject *array_at = MVM_repr_at_pos_o(tc, storage, index);
        MVMObject *key_of   = MVM_repr_at_pos_o(tc, array_at, 0);

        if (MVM_repr_get_int(tc, key_of) == key) {
            return NULL;
        }
    }

    result = new_array(tc);
    type_info_hash = new_hash(tc);

    MVM_repr_bind_pos_o(tc, result, 0, box_i(tc, key));
    MVM_repr_bind_pos_o(tc, result, 1, type_info_hash);
    MVM_repr_push_o(tc, storage, result);
    return result;
}

static void bind_extra_info(MVMThreadContext *tc, MVMObject *storage, MVMString *key, MVMObject *value) {
    MVMObject *hash = MVM_repr_at_pos_o(tc, storage, 1);
    MVM_repr_bind_key_o(tc, hash, key, value);
}

static MVMObject * dump_call_graph_node(MVMThreadContext *tc, ProfDumpStrs *pds, const MVMProfileCallNode *pcn, MVMObject *types_array);
static MVMObject * dump_call_graph_node_loop(ProfTcPdsStruct *tcpds, const MVMProfileCallNode *pcn) {
    MVMuint32 i;
    MVMuint64 exclusive_time = pcn->total_time;
    MVMuint64 overhead       = pcn->total_entries * tcpds->tc->instance->profiling_overhead;
    MVMObject *node_hash;

    /* Subtract profiling overhead, unless that would underflow, in which case just clamp to 0. */
    if (exclusive_time - overhead > exclusive_time)
        exclusive_time = 0;
    else
        exclusive_time -= overhead;

    node_hash = dump_call_graph_node(tcpds->tc, tcpds->pds, pcn, tcpds->types_array);

    /* Visit successors in the call graph, dumping them and working out the
     * exclusive time. */
    if (pcn->num_succ) {
        MVMObject *callees = new_array(tcpds->tc);
        for (i = 0; i < pcn->num_succ; i++) {
            MVMuint64 succ_exclusive_time = pcn->succ[i]->total_time;
            MVMuint64 succ_overhead       = pcn->succ[i]->total_entries * tcpds->tc->instance->profiling_overhead;

            MVM_repr_push_o(tcpds->tc, callees,
                dump_call_graph_node_loop(tcpds, pcn->succ[i]));

            /* Subtract profiling overhead, unless that would underflow, in which case just clamp to 0. */
            if (succ_exclusive_time - succ_overhead > succ_exclusive_time)
                succ_exclusive_time = 0;
            else
                succ_exclusive_time -= succ_overhead;

            /* Subtract profiling overhead, unless that would underflow, in which case just clamp to 0. */
            if (exclusive_time - succ_exclusive_time > exclusive_time)
                exclusive_time = 0;
            else
                exclusive_time -= succ_exclusive_time;
        }
        MVM_repr_bind_key_o(tcpds->tc, node_hash, tcpds->pds->exclusive_time,
            box_i(tcpds->tc, exclusive_time / 1000));
        MVM_repr_bind_key_o(tcpds->tc, node_hash, tcpds->pds->callees, callees);
    }
    else {
        MVM_repr_bind_key_o(tcpds->tc, node_hash, tcpds->pds->exclusive_time,
            box_i(tcpds->tc, exclusive_time / 1000));
    }

    return node_hash;
}

static void add_type_to_types_array(MVMThreadContext *tc, ProfDumpStrs *pds, MVMObject *type, MVMObject *types_array) {
    MVMObject *type_info  = insert_if_not_exists(tc, pds, types_array, (MVMint64)(uintptr_t)type);

    if (type_info) {
        bind_extra_info(tc, type_info, pds->managed_size, box_i(tc, STABLE(type)->size));
        if (REPR(type)->unmanaged_size)
            bind_extra_info(tc, type_info, pds->has_unmanaged_data, box_i(tc, 1));
        bind_extra_info(tc, type_info, pds->type, type);
        bind_extra_info(tc, type_info, pds->repr, box_s(tc, str(tc, REPR(type)->name)));
    }
}

/* Dumps a call graph node. */
static MVMObject * dump_call_graph_node(MVMThreadContext *tc, ProfDumpStrs *pds,
                                        const MVMProfileCallNode *pcn, MVMObject *types_array) {
    MVMObject *node_hash  = new_hash(tc);
    MVMuint32  i;
    MVMuint64 absolute_start_time;

    /* Let's see if we're dealing with a native call or a regular moar call */
    if (tc->prof_data->staticframe_array[pcn->sf_idx]) {
        MVMStaticFrame *sf = tc->prof_data->staticframe_array[pcn->sf_idx];
        /* Try to resolve the code filename and line number. */
        MVMBytecodeAnnotation *annot = MVM_bytecode_resolve_annotation(tc,
            &(sf->body), 0);
        MVMuint32 fshi = annot ? (MVMint32)annot->filename_string_heap_index : 0;

        /* Add name of code object. */
        MVM_repr_bind_key_o(tc, node_hash, pds->name,
            box_s(tc, sf->body.name));

        /* Add line number and file name. */
        if (annot && fshi < sf->body.cu->body.num_strings)
            MVM_repr_bind_key_o(tc, node_hash, pds->file,
                box_s(tc, MVM_cu_string(tc, sf->body.cu, fshi)));
        else if (sf->body.cu->body.filename)
            MVM_repr_bind_key_o(tc, node_hash, pds->file,
                box_s(tc, sf->body.cu->body.filename));
        else
            MVM_repr_bind_key_o(tc, node_hash, pds->file,
                box_s(tc, tc->instance->str_consts.empty));
        MVM_repr_bind_key_o(tc, node_hash, pds->line,
            box_i(tc, annot ? (MVMint32)annot->line_number : -1));
        MVM_free(annot);

        /* Use static frame memory address to get a unique ID. */
        MVM_repr_bind_key_o(tc, node_hash, pds->id,
            box_i(tc, (MVMint64)(uintptr_t)sf));

        /* Add data about cache entries. */
        MVMDispInlineCache *ic = &(sf->body.inline_cache);
        MVMuint32 monomorphic = 0;
        MVMuint32 polymorphic = 0;
        MVMuint32 megamorphic = 0;
        MVMuint32 megamorphic_blowout = 0;
        for (i = 0; i < ic->num_entries; i++) {
            /* Skip entries that don't correspond to a bytecode operation. */
            MVMDispInlineCacheEntry *entry = ic->entries[i];
            MVMuint32 count = 0;
            switch (MVM_disp_inline_cache_try_get_kind(tc, entry)) {
                case MVM_INLINE_CACHE_KIND_MONOMORPHIC_DISPATCH:
                case MVM_INLINE_CACHE_KIND_MONOMORPHIC_DISPATCH_FLATTENING:
                    count = 1;
                    break;
                case MVM_INLINE_CACHE_KIND_POLYMORPHIC_DISPATCH:
                    count = ((MVMDispInlineCacheEntryPolymorphicDispatch *)entry)->num_dps;
                    break;
                case MVM_INLINE_CACHE_KIND_POLYMORPHIC_DISPATCH_FLATTENING:
                    count = ((MVMDispInlineCacheEntryPolymorphicDispatchFlattening *)entry)->num_dps;
                    break;
            }
            if (count == 1)
                monomorphic++;
            else if (count > 1 && count <= 4)
                polymorphic++;
            else if (count > 4 && count < MVM_INLINE_CACHE_MAX_POLY)
                megamorphic++;
            else if (count == MVM_INLINE_CACHE_MAX_POLY)
                megamorphic_blowout = 1;
        }
        if (monomorphic)
            MVM_repr_bind_key_o(tc, node_hash, pds->mono, box_i(tc, monomorphic));
        if (polymorphic)
            MVM_repr_bind_key_o(tc, node_hash, pds->poly, box_i(tc, polymorphic));
        if (megamorphic)
            MVM_repr_bind_key_o(tc, node_hash, pds->mega, box_i(tc, megamorphic));
        if (megamorphic_blowout)
            MVM_repr_bind_key_o(tc, node_hash, pds->blow, box_i(tc, megamorphic_blowout));
    } else if (pcn->native_target_name) {
        MVMString *function_name_string =
            MVM_string_utf8_c8_decode(tc, tc->instance->VMString,
                                      pcn->native_target_name, strlen(pcn->native_target_name));

        MVM_repr_bind_key_o(tc, node_hash, pds->name,
            box_s(tc, function_name_string));
        MVM_repr_bind_key_o(tc, node_hash, pds->file,
            box_s(tc, pds->native_lib));

        MVM_repr_bind_key_o(tc, node_hash, pds->line,
            box_i(tc, -2));

        /* Use the address of the name string as unique ID. a hack, but oh well. */
        MVM_repr_bind_key_o(tc, node_hash, pds->id,
            box_i(tc, (MVMint64)(uintptr_t)pcn->native_target_name));
    }
    else {
        MVM_repr_bind_key_o(tc, node_hash, pds->name,
            box_s(tc, pds->call_graph));
        MVM_repr_bind_key_o(tc, node_hash, pds->file,
            box_s(tc, pds->call_graph));

        MVM_repr_bind_key_o(tc, node_hash, pds->line,
            box_i(tc, -10));

        MVM_repr_bind_key_o(tc, node_hash, pds->id,
            box_i(tc, (MVMint64)(uintptr_t)pcn));
    }

    /* Entry counts. */
    if (pcn->total_entries)
        MVM_repr_bind_key_o(tc, node_hash, pds->entries,
            box_i(tc, pcn->total_entries));
    if (pcn->specialized_entries)
        MVM_repr_bind_key_o(tc, node_hash, pds->spesh_entries,
            box_i(tc, pcn->specialized_entries));
    if (pcn->jit_entries)
        MVM_repr_bind_key_o(tc, node_hash, pds->jit_entries,
            box_i(tc, pcn->jit_entries));
    if (pcn->inlined_entries)
        MVM_repr_bind_key_o(tc, node_hash, pds->inlined_entries,
            box_i(tc, pcn->inlined_entries));

    /* Use the main thread's start time for absolute timings */
    absolute_start_time = tc->instance->main_thread->prof_data->start_time;

    /* Total (inclusive) time. */
    MVM_repr_bind_key_o(tc, node_hash, pds->inclusive_time,
        box_i(tc, pcn->total_time / 1000));

    /* Earliest entry time */
    MVM_repr_bind_key_o(tc, node_hash, pds->first_entry_time,
        box_i(tc, (pcn->first_entry_time - absolute_start_time) / 1000));

    /* OSR and deopt counts. */
    if (pcn->osr_count)
        MVM_repr_bind_key_o(tc, node_hash, pds->osr,
            box_i(tc, pcn->osr_count));
    if (pcn->deopt_one_count)
        MVM_repr_bind_key_o(tc, node_hash, pds->deopt_one,
            box_i(tc, pcn->deopt_one_count));
    if (pcn->deopt_all_count)
        MVM_repr_bind_key_o(tc, node_hash, pds->deopt_all,
            box_i(tc, pcn->deopt_all_count));

    if (pcn->num_alloc) {
        /* Emit allocations. */
        MVMObject *alloc_list = new_array(tc);
        MVM_repr_bind_key_o(tc, node_hash, pds->allocations, alloc_list);
        for (i = 0; i < pcn->num_alloc; i++) {
            MVMObject *alloc_info = new_hash(tc);
            MVMProfileAllocationCount *alloc = &pcn->alloc[i];

            MVMObject *type = tc->prof_data->type_array[pcn->alloc[i].type_idx];

            add_type_to_types_array(tc, pds, type, types_array);

            MVM_repr_bind_key_o(tc, alloc_info, pds->id, box_i(tc, (MVMint64)(uintptr_t)type));
            if (alloc->allocations_spesh)
                MVM_repr_bind_key_o(tc, alloc_info, pds->spesh,
                    box_i(tc, alloc->allocations_spesh));
            if (alloc->allocations_jit)
                MVM_repr_bind_key_o(tc, alloc_info, pds->jit,
                    box_i(tc, alloc->allocations_jit));
            MVM_repr_bind_key_o(tc, alloc_info, pds->count,
                box_i(tc, alloc->allocations_interp
                          + alloc->allocations_spesh
                          + alloc->allocations_jit));
            if (alloc->scalar_replaced)
                MVM_repr_bind_key_o(tc, alloc_info, pds->replaced,
                    box_i(tc, alloc->scalar_replaced));
            MVM_repr_push_o(tc, alloc_list, alloc_info);
        }
    }

    return node_hash;
}

/* Dumps data from a single thread. */
static MVMObject * dump_thread_data(MVMThreadContext *tc, ProfDumpStrs *pds,
                                    MVMThreadContext *othertc,
                                    const MVMProfileThreadData *ptd,
                                    MVMObject *types_data) {
    MVMObject *thread_hash = new_hash(tc);
    MVMObject *thread_gcs  = new_array(tc);
    MVMuint64 absolute_start_time;
    MVMuint32  i;

    ProfTcPdsStruct tcpds;

    tcpds.tc = othertc;
    tcpds.pds = pds;
    tcpds.types_array = types_data;

    /* Use the main thread's start time for absolute timings */
    absolute_start_time = tc->instance->main_thread->prof_data->start_time;

    /* Add time. */
    MVM_repr_bind_key_o(tc, thread_hash, pds->total_time,
        box_i(tc, (ptd->end_time - ptd->start_time) / 1000));

    /* Add start time */
    MVM_repr_bind_key_o(tc, thread_hash, pds->start_time,
        box_i(tc, (ptd->start_time - absolute_start_time) / 1000));

    /* Add call graph. */
    if (ptd->call_graph)
        MVM_repr_bind_key_o(tc, thread_hash, pds->call_graph,
            dump_call_graph_node_loop(&tcpds, ptd->call_graph));

    /* Add GCs. */
    for (i = 0; i < ptd->num_gcs; i++) {
        MVMuint32 tid;

        MVMObject *gc_hash = new_hash(tc);

        MVMProfileGC *gc = &(ptd->gcs[i]);

        MVM_repr_bind_key_o(tc, gc_hash, pds->time,
            box_i(tc, gc->time / 1000));
        MVM_repr_bind_key_o(tc, gc_hash, pds->full,
            box_i(tc, gc->full));
        MVM_repr_bind_key_o(tc, gc_hash, pds->sequence,
            box_i(tc, gc->gc_seq_num - 1));
        MVM_repr_bind_key_o(tc, gc_hash, pds->responsible,
            box_i(tc, gc->responsible));
        MVM_repr_bind_key_o(tc, gc_hash, pds->cleared_bytes,
            box_i(tc, gc->cleared_bytes));
        MVM_repr_bind_key_o(tc, gc_hash, pds->retained_bytes,
            box_i(tc, gc->retained_bytes));
        MVM_repr_bind_key_o(tc, gc_hash, pds->promoted_bytes,
            box_i(tc, gc->promoted_bytes));
        MVM_repr_bind_key_o(tc, gc_hash, pds->promoted_bytes_unmanaged,
            box_i(tc, gc->promoted_unmanaged_bytes));
        MVM_repr_bind_key_o(tc, gc_hash, pds->gen2_roots,
            box_i(tc, gc->num_gen2roots));
        MVM_repr_bind_key_o(tc, gc_hash, pds->stolen_gen2_roots,
            box_i(tc, gc->num_stolen_gen2roots));
        MVM_repr_bind_key_o(tc, gc_hash, pds->start_time,
            box_i(tc, (gc->abstime - absolute_start_time) / 1000));

        if (gc->num_dealloc) {
            MVMObject *deallocs_array = new_array(tc);
            MVM_repr_bind_key_o(tc, gc_hash, pds->deallocs,
                deallocs_array);

            for (tid = 0; tid < gc->num_dealloc; tid++) {
                MVMProfileDeallocationCount *dealloc = &(gc->deallocs[tid]);

                MVMObject *type_hash = new_hash(tc);

                if (dealloc->deallocs_nursery_fresh)
                    MVM_repr_bind_key_o(tc, type_hash, pds->nursery_fresh,
                            box_i(tc, dealloc->deallocs_nursery_fresh));
                if (dealloc->deallocs_nursery_seen)
                    MVM_repr_bind_key_o(tc, type_hash, pds->nursery_seen,
                            box_i(tc, dealloc->deallocs_nursery_seen));
                if (dealloc->deallocs_gen2)
                    MVM_repr_bind_key_o(tc, type_hash, pds->gen2,
                            box_i(tc, dealloc->deallocs_gen2));

                add_type_to_types_array(tc, pds, dealloc->type, types_data);

                MVM_repr_bind_key_o(tc, type_hash, pds->id, box_i(tc, (MVMint64)(uintptr_t)(dealloc->type)));

                MVM_repr_push_o(tc, deallocs_array, type_hash);
            }
        }

        MVM_repr_push_o(tc, thread_gcs, gc_hash);
    }
    MVM_repr_bind_key_o(tc, thread_hash, pds->gcs, thread_gcs);

    /* Add spesh time. */
    MVM_repr_bind_key_o(tc, thread_hash, pds->spesh_time,
        box_i(tc, ptd->spesh_time / 1000));

    /* Add thread id. */
    MVM_repr_bind_key_o(tc, thread_hash, pds->thread,
        box_i(tc, othertc->thread_id));

    /* Add parent thread id. */
    MVM_repr_bind_key_o(tc, thread_hash, pds->parent,
        box_i(tc, ptd->parent_thread_id));

    return thread_hash;
}

void MVM_profile_dump_instrumented_data(MVMThreadContext *tc) {
    if (tc->prof_data && tc->prof_data->collected_data) {
        ProfDumpStrs pds;
        MVMThread *thread;
        MVMObject *types_array;

        /* Record end time. */
        tc->prof_data->end_time = uv_hrtime();

        /* We'll allocate the data in gen2, but as we want to keep it, but to be
         * sure we don't trigger a GC run. */
        MVM_gc_allocate_gen2_default_set(tc);

        /* Some string constants to re-use. */
        pds.total_time      = str(tc, "total_time");
        pds.call_graph      = str(tc, "call_graph");
        pds.name            = str(tc, "name");
        pds.id              = str(tc, "id");
        pds.parent          = str(tc, "parent");
        pds.file            = str(tc, "file");
        pds.line            = str(tc, "line");
        pds.entries         = str(tc, "entries");
        pds.spesh_entries   = str(tc, "spesh_entries");
        pds.jit_entries     = str(tc, "jit_entries");
        pds.inlined_entries = str(tc, "inlined_entries");
        pds.inclusive_time  = str(tc, "inclusive_time");
        pds.exclusive_time  = str(tc, "exclusive_time");
        pds.callees         = str(tc, "callees");
        pds.allocations     = str(tc, "allocations");
        pds.type            = str(tc, "type");
        pds.count           = str(tc, "count");
        pds.spesh           = str(tc, "spesh");
        pds.jit             = str(tc, "jit");
        pds.replaced        = str(tc, "replaced");
        pds.gcs             = str(tc, "gcs");
        pds.time            = str(tc, "time");
        pds.full            = str(tc, "full");
        pds.sequence        = str(tc, "sequence");
        pds.responsible     = str(tc, "responsible");
        pds.cleared_bytes   = str(tc, "cleared_bytes");
        pds.retained_bytes  = str(tc, "retained_bytes");
        pds.promoted_bytes  = str(tc, "promoted_bytes");
        pds.gen2_roots      = str(tc, "gen2_roots");
        pds.start_time      = str(tc, "start_time");
        pds.first_entry_time= str(tc, "first_entry_time");
        pds.osr             = str(tc, "osr");
        pds.deopt_one       = str(tc, "deopt_one");
        pds.deopt_all       = str(tc, "deopt_all");
        pds.spesh_time      = str(tc, "spesh_time");
        pds.thread          = str(tc, "thread");
        pds.native_lib      = str(tc, "native library");
        pds.managed_size    = str(tc, "managed_size");
        pds.deallocs        = str(tc, "deallocs");
        pds.nursery_fresh   = str(tc, "nursery_fresh");
        pds.nursery_seen    = str(tc, "nursery_seen");
        pds.gen2            = str(tc, "gen2");
        pds.mono            = str(tc, "mono");
        pds.poly            = str(tc, "poly");
        pds.mega            = str(tc, "mega");
        pds.blow            = str(tc, "blow");

        pds.stolen_gen2_roots  = str(tc, "stolen_gen2_roots");
        pds.has_unmanaged_data = str(tc, "has_unmanaged_data");
        pds.repr               = str(tc, "repr");

        pds.promoted_bytes_unmanaged  = str(tc, "promoted_bytes_unmanaged");

        types_array = new_array(tc);

        MVM_repr_push_o(tc, tc->prof_data->collected_data, types_array);

        while (tc->prof_data->current_call)
            MVM_profile_log_exit(tc);

        MVM_repr_push_o(tc, tc->prof_data->collected_data, dump_thread_data(tc, &pds, tc, tc->prof_data, types_array));

        /* Get all thread's data */
        thread = tc->instance->threads;

        while (thread) {
            MVMThreadContext *othertc = thread->body.tc;
            /* Check for othertc to exist because joining threads nulls out
             * the tc entry in the thread object. */
            if (othertc && othertc->prof_data && othertc != tc) {
                /* If we have any call frames still on the profile stack, exit them. */
                while (othertc->prof_data->current_call)
                    MVM_profile_log_exit(othertc);

                /* Record end time. */
                othertc->prof_data->end_time = uv_hrtime();

                MVM_gc_allocate_gen2_default_set(othertc);
                MVM_repr_push_o(tc, tc->prof_data->collected_data, dump_thread_data(tc, &pds, othertc, othertc->prof_data, types_array));
                MVM_gc_allocate_gen2_default_clear(othertc);
            }
            thread = thread->body.next;
        }
        MVM_gc_allocate_gen2_default_clear(tc);
    }
}

/* Dumps data from all threads into an array of per-thread data. */
static MVMObject * dump_data(MVMThreadContext *tc) {
    MVMObject *collected_data;

    /* Build up threads array. */
    /* XXX Only main thread for now. */

    tc->prof_data->collected_data = new_array(tc);

    /* We rely on the GC orchestration to stop all threads and the
     * "main" gc thread to dump all thread data for us */
    MVM_gc_enter_from_allocator(tc);

    collected_data = tc->prof_data->collected_data;
    tc->prof_data->collected_data = NULL;

    return collected_data;
}

/* Ends profiling, builds the result data structure, and returns it. */
MVMObject * MVM_profile_instrumented_end(MVMThreadContext *tc) {

    /* Disable profiling. */
    MVM_gc_mark_thread_blocked(tc);
    uv_mutex_lock(&(tc->instance->mutex_spesh_sync));
    while (tc->instance->spesh_working != 0)
        uv_cond_wait(&(tc->instance->cond_spesh_sync), &(tc->instance->mutex_spesh_sync));
    tc->instance->profiling = 0;
    tc->instance->instrumentation_level++;
    uv_mutex_unlock(&(tc->instance->mutex_spesh_sync));
    MVM_gc_mark_thread_unblocked(tc);

    /* Build and return result data structure. */
    return dump_data(tc);
}


static void mark_gc_entries(MVMThreadContext *tc, MVMProfileThreadData *ptd, MVMGCWorklist *worklist) {
    MVMuint32 gci;
    for (gci = 0; gci < ptd->num_gcs; gci++) {
        MVMProfileGC *gc = &(ptd->gcs[gci]);
        MVMuint32 ti;
        for (ti = 0; ti < gc->num_dealloc; ti++) {
            MVM_gc_worklist_add(tc, worklist, &(gc->deallocs[ti].type));
        }
    }
}
void MVM_profile_instrumented_mark_data(MVMThreadContext *tc, MVMGCWorklist *worklist) {
    if (tc->prof_data) {
        MVMProfileThreadData *ptd = tc->prof_data;
        MVMuint32 index;

        for (index = 0; index < MVM_VECTOR_ELEMS(ptd->staticframe_array); index++)
            MVM_gc_worklist_add(tc, worklist, &(ptd->staticframe_array[index]));
        for (index = 0; index < MVM_VECTOR_ELEMS(ptd->type_array); index++)
            MVM_gc_worklist_add(tc, worklist, &(ptd->type_array[index]));

        MVM_gc_worklist_add(tc, worklist, &(tc->prof_data->collected_data));

        mark_gc_entries(tc, tc->prof_data, worklist);
    }
}

static void MVM_profile_free_nodes(MVMThreadContext *tc, MVMProfileCallNode *node, MVMProfileCallNode ***seen, size_t *seen_num, size_t *seen_alloc) {
    for (MVMuint32 i = 0; i < node->num_succ; i++) {
        int found = 0;
        for (size_t j = 0; j < *seen_num; j++)
            if (node->succ[i] == (*seen)[j]) {
                found = 1;
                break;
            }
        if (!found) {
            MVM_VECTOR_PUSH(*seen, node->succ[i]);
            MVM_profile_free_nodes(tc, node->succ[i], seen, seen_num, seen_alloc);
        }
    }
    MVM_free(node->succ);
    MVM_free(node);
}

void MVM_profile_free_node(MVMThreadContext *tc, MVMProfileCallNode *node) {
    MVM_VECTOR_DECL(MVMProfileCallNode*, nodes);
    MVM_VECTOR_INIT(nodes, 0);

    MVM_profile_free_nodes(tc, node, &nodes, &nodes_num, &nodes_alloc);

    MVM_VECTOR_DESTROY(nodes);
}

void MVM_profile_instrumented_free_data(MVMThreadContext *tc) {
    if (tc->prof_data) {
        MVMProfileThreadData *ptd = tc->prof_data;
        MVMProfileCallNode *node = ptd->call_graph;

        if (node)
            MVM_profile_free_node(tc, node);

        MVM_VECTOR_DESTROY(ptd->staticframe_array);
        MVM_VECTOR_DESTROY(ptd->type_array);
        for (MVMuint32 i = 0; i < ptd->num_gcs; i++)
            MVM_free(ptd->gcs[i].deallocs);
        MVM_free(ptd->gcs);
        MVM_free(ptd);
        tc->prof_data = NULL;
    }
}

static void dump_callgraph_node(MVMThreadContext *tc, MVMProfileCallNode *n, MVMuint16 depth) {
    MVMuint16 dc = depth;
    MVMuint32 idx;
    char *name = NULL;

    for (dc = depth; dc > 0; dc--) {
        fputc(' ', stderr);
    }

    if (tc->prof_data->staticframe_array[n->sf_idx])
        name = MVM_string_utf8_encode_C_string(tc, tc->prof_data->staticframe_array[n->sf_idx]->body.name);

    fprintf(stderr, "+ [%3d] %s\n", n->num_succ, name ? name : "(unknown)");
    MVM_free(name);

    for (idx = 0; idx < n->num_succ; idx++) {
        dump_callgraph_node(tc, n->succ[idx], depth + 1);
    }
}

void MVM_dump_callgraph(MVMThreadContext *tc) {
    MVMProfileThreadData *ptd = tc->prof_data;
    MVMProfileCallNode *pcn = ptd->call_graph;
    fprintf(stderr, "\n----------\nCall Graph of TC %p\n\n", tc);
    if (pcn && pcn->num_succ) {
        dump_callgraph_node(tc, pcn, 0);
    }
    fprintf(stderr, "\n<<<<<<\n");
}
