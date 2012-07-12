#include "moarvm.h"

/* Loads a compilation unit from a bytecode file, mapping it into
 * memory. */
MVMCompUnit * MVM_cu_map_from_file(MVMThreadContext *tc, char *filename) {
    MVMCompUnit *cu          = NULL;
    apr_pool_t  *pool        = NULL;
    apr_file_t  *file_handle = NULL;
    apr_mmap_t  *mmap_handle = NULL;
    apr_finfo_t  stat_info;
    int          apr_return_status;

    /* Ensure the file exists, and get its size. */
    if ((apr_return_status = apr_pool_create(&pool, NULL)) != APR_SUCCESS) {
        MVM_panic(MVM_exitcode_compunit, "Could not allocate APR memory pool: errorcode %d", apr_return_status);
    }
    if ((apr_return_status = apr_stat(&stat_info, filename, APR_FINFO_SIZE, pool)) != APR_SUCCESS) {
        apr_pool_destroy(pool);
        MVM_exception_throw_apr_error(tc, apr_return_status, "While looking for '%s': ", filename);
    }

    /* Map the bytecdoe file into memory. */
    if ((apr_return_status = apr_file_open(&file_handle, filename,
            APR_READ | APR_BINARY, APR_OS_DEFAULT, pool)) != APR_SUCCESS) {
        apr_pool_destroy(pool);
        MVM_exception_throw_apr_error(tc, apr_return_status, "While trying to open '%s': ", filename);
    } 	
    if ((apr_return_status = apr_mmap_create(&mmap_handle, file_handle, 0,
            stat_info.size, APR_MMAP_READ, pool)) != APR_SUCCESS) {
        apr_pool_destroy(pool);
        MVM_exception_throw_apr_error(tc, apr_return_status, "Could not map file into memory '%s': ", filename);
    }
    
    /* close the filehandle. */
    apr_file_close(file_handle);

    /* Create compilation unit data structure. */
    cu = malloc(sizeof(MVMCompUnit));
    memset(cu, 0, sizeof(MVMCompUnit));
    cu->pool       = pool;
    cu->data_start = (MVMuint8 *)mmap_handle->mm;
    cu->data_size  = (MVMuint32)mmap_handle->size;
    
    /* Process the input. */
    MVM_bytecode_unpack(tc, cu);
    
    return cu;
}

static void append_string(char **out, MVMuint32 *size,
        MVMuint32 *length, char *str, ...) {
    char *string = calloc(1024, 1);
    MVMuint32 len;
    va_list args;
    va_start(args, str);
    
    vsprintf(string, str, args);
    va_end(args);
    
    len = strlen(string);
    if (*length + len > *size) {
        *size = *size * 2;
        *out = realloc(*out, *size);
    }
    
    memcpy(*out + *length, string, len);
    *length = *length + len;
    free(string);
}

static char * build_string(char *str, ...) {
    char *string = calloc(100, 1);
    va_list args;
    va_start(args, str);
    vsprintf(string, str, args);
    va_end(args);
    return string;
}

static const char * get_typename(MVMuint16 type) {
    switch(type) {
        case MVM_reg_int8 : return "int8";
        case MVM_reg_int16: return "int16";
        case MVM_reg_int32: return "int32";
        case MVM_reg_int64: return "int";
        case MVM_reg_num32: return "num32";
        case MVM_reg_num64: return "num";
        case MVM_reg_str  : return "str";
        case MVM_reg_obj  : return "obj";
        default           : return "UNKNOWN";
    }
}

#define a(...) append_string(&o,&s,&l, __VA_ARGS__)
/* Macros for getting things from the bytecode stream. */
/* GET_REG is defined differently here from interp.c */
#define GET_I8(pc, idx)     *((MVMint8 *)(pc + idx))
#define GET_REG(pc, idx)    *((MVMuint16 *)(pc + idx))
#define GET_I16(pc, idx)    *((MVMint16 *)(pc + idx))
#define GET_UI16(pc, idx)   *((MVMuint16 *)(pc + idx))
#define GET_I32(pc, idx)    *((MVMint32 *)(pc + idx))
#define GET_UI32(pc, idx)   *((MVMuint32 *)(pc + idx))
#define GET_I64(pc, idx)    *((MVMint64 *)(pc + idx))
#define GET_UI64(pc, idx)   *((MVMuint64 *)(pc + idx))
#define GET_N32(pc, idx)    *((MVMnum32 *)(pc + idx))
#define GET_N64(pc, idx)    *((MVMnum64 *)(pc + idx))

enum {
    MVM_val_branch_target = 1,
    MVM_val_op_boundary   = 2
};

char * MVM_cu_dump(MVMThreadContext *tc, MVMCompUnit *cu) {
    MVMuint32 s = 1024;
    MVMuint32 l = 0;
    MVMuint32 i, j, k, q;
    char *o = calloc(sizeof(char) * s, 1);
    char ***frame_lexicals = malloc(sizeof(char **) * cu->num_frames);
    
    a("\nMoarVM dump of binary compilation unit:\n\n");
    
    for (k = 0; k < cu->num_callsites; k++) {
        MVMCallsite *callsite = cu->callsites[k];
        a("  Callsite_%u :\n", k);
        a("    num_pos: %d\n", callsite->num_pos);
        a("    arg_count: %u\n", callsite->arg_count);
        for (j = 0; j < callsite->arg_count; j++) {
            MVMCallsiteEntry csitee = callsite->arg_flags[j];
            a("    Arg %u : ", j);
            if (csitee & MVM_CALLSITE_ARG_NAMED) a("named ");
            else a("positional ");
            if (csitee & MVM_CALLSITE_ARG_OBJ) a("obj ");
            else if (csitee & MVM_CALLSITE_ARG_INT) a("int ");
            else if (csitee & MVM_CALLSITE_ARG_UINT) a("uint ");
            else if (csitee & MVM_CALLSITE_ARG_NUM) a("num ");
            else if (csitee & MVM_CALLSITE_ARG_STR) a("str ");
            if (csitee & MVM_CALLSITE_ARG_FLAT) a("flat");
            a("\n");
        }
    }
    
    for (k = 0; k < cu->num_frames; k++) {
        MVMStaticFrame *frame = cu->frames[k];
        apr_hash_index_t *entry;
        char **lexicals = malloc(sizeof(char *) * frame->num_lexicals);
        frame_lexicals[k] = lexicals;
        for (j = 0; j < frame->num_lexicals; j++) {
            const void *key;
            MVMuint16 val;
            apr_ssize_t ssize;
            MVMString *name;
            
            entry = j ? apr_hash_next(entry)
                : apr_hash_first(NULL, frame->lexical_names);
            apr_hash_this(entry, &key, &ssize, (void **)&val);
            name = MVM_string_utf8_decode(tc, tc->instance->boot_types->BOOTStr, "", 0);
            name->body.data = (void *)key;
            name->body.graphs = ssize / sizeof(MVMint32);
            lexicals[j] = MVM_string_utf8_encode_C_string(tc, name);
        }
    }
    for (k = 0; k < cu->num_frames; k++) {
        MVMStaticFrame *frame = cu->frames[k];
        char *cuuid;
        char *fname;
        cuuid = MVM_string_utf8_encode_C_string(tc, frame->cuuid);
        fname = MVM_string_utf8_encode_C_string(tc, frame->name);
        a("  Frame_%u :\n", k);
        a("    cuuid : %s\n", cuuid);
        free(cuuid);
        a("    name : %s\n", fname);
        free(fname);
        for (j = 0; j < cu->num_frames; j++) {
            if (cu->frames[j] == frame->outer)
                a("    outer : Frame_%u\n", j);
        }
        
        for (j = 0; j < frame->num_locals; j++) {
            if (!j)
            a("    Locals :\n");
            a("  %6u: loc_%u_%s\n", j, j, get_typename(frame->local_types[j]));
        }
        
        for (j = 0; j < frame->num_lexicals; j++) {
            if (!j)
            a("    Lexicals :\n");
            a("  %6u: lex_Frame_%u_%s_%s\n", j, k, frame_lexicals[k][j], get_typename(frame->lexical_types[j]));
        }
        a("    Bytecode :\n");
        {
    
    /* mostly stolen from validation.c */
    MVMStaticFrame *static_frame = frame;
    MVMuint32 bytecode_size = static_frame->bytecode_size;
    MVMuint8 *bytecode_start = static_frame->bytecode;
    MVMuint8 *bytecode_end = bytecode_start + bytecode_size;
    /* current position in the bytestream */
    MVMuint8 *cur_op = bytecode_start;
    /* positions in the bytestream that are starts of ops and goto targets */
    MVMuint8 *labels = calloc(bytecode_size, 1);
    MVMuint32 *jumps = calloc(sizeof(MVMuint32) * bytecode_size, 1);
    char **lines = malloc(sizeof(char *) * bytecode_size / 2);
    MVMuint32 *linelocs = malloc(bytecode_size / 2);
    MVMuint32 lineno = 0;
    MVMuint32 lineloc;
    MVMuint8 bank_num;
    MVMuint8 op_num;
    MVMOpInfo *op_info;
    MVMuint32 operand_size;
    unsigned char op_rw;
    unsigned char op_type;
    unsigned char op_flags;
    /* stash the outer output buffer */
    MVMuint32 sP = s;
    MVMuint32 lP = l;
    char *oP = o;
    char *tmpstr;
    while (cur_op < bytecode_end - 1) {
        
        /* allocate a line buffer */
        s = 50;
        l = 0;
        o = calloc(sizeof(char) * s, 1);
        
        lineloc = cur_op - bytecode_start;
        /* mark that this line starts at this point in the bytestream */
        linelocs[lineno] = lineloc;
        /* mark that this point in the bytestream is an op boundary */
        labels[lineloc] |= MVM_val_op_boundary;
        
        bank_num = *(cur_op++);
        op_num = *(cur_op++);
        op_info = MVM_op_get_op((unsigned char)bank_num, (unsigned char)op_num);
        a("%-12s ", op_info->name);
        
        for (i = 0; i < op_info->num_operands; i++) {
            if (i) a(", ");
            op_flags = op_info->operands[i];
            op_rw   = op_flags & MVM_operand_rw_mask;
            op_type = op_flags & MVM_operand_type_mask;
            
            if (op_rw == MVM_operand_literal) {
                switch (op_type) {
                    case MVM_operand_int8:
                        operand_size = 1;
                        a("%d", GET_I8(cur_op, 0));
                        break;
                    case MVM_operand_int16:
                        operand_size = 2;
                        a("%d", GET_I16(cur_op, 0));
                        break;
                    case MVM_operand_int32:
                        operand_size = 4;
                        a("%d", GET_I32(cur_op, 0));
                        break;
                    case MVM_operand_int64:
                        operand_size = 8;
                        a("%d", GET_I64(cur_op, 0));
                        break;
                    case MVM_operand_num32:
                        operand_size = 4;
                        a("%f", GET_N32(cur_op, 0));
                        break;
                    case MVM_operand_num64:
                        operand_size = 8;
                        a("%f", GET_N64(cur_op, 0));
                        break;
                    case MVM_operand_callsite:
                        operand_size = 2;
                        a("Callsite_%u", GET_UI16(cur_op, 0));
                        break;
                    case MVM_operand_coderef:
                        operand_size = 2;
                        a("Frame_%u", GET_UI16(cur_op, 0));
                        break;
                    case MVM_operand_str:
                        operand_size = 2;
                        tmpstr = MVM_string_utf8_encode_C_string(
                            tc, cu->strings[GET_UI16(cur_op, 0)]);
                        /* XXX escape the \ and ' someday */
                        a("'%s'", tmpstr);
                        free(tmpstr);
                        break;
                    case MVM_operand_ins:
                        operand_size = 4;
                        /* luckily all the ins operands are at the end
                        of op operands, so I can wait to resolve the label 
                        to the end. */
                        labels[GET_UI32(cur_op, 0)] |= MVM_val_branch_target;
                        jumps[lineno] = GET_UI32(cur_op, 0);
                        break;
                    case MVM_operand_obj:
                        /* not sure what a literal object is */
                        operand_size = 4;
                }
            }
            else if (op_rw == MVM_operand_read_reg || op_rw == MVM_operand_write_reg) {
                /* register operand */
                operand_size = 2;
                a("loc_%u_%s", GET_REG(cur_op, 0),
                    get_typename(frame->local_types[GET_REG(cur_op, 0)]));
            }
            else if (op_rw == MVM_operand_read_lex || op_rw == MVM_operand_write_lex) {
                /* lexical operand */
                MVMuint16 idx, frames, m;
                MVMStaticFrame *applicable_frame = static_frame;
                
                operand_size = 4;
                idx = GET_UI16(cur_op, 0);
                frames = GET_UI16(cur_op, 2);
                
                m = frames;
                while (m > 0) {
                    applicable_frame = applicable_frame->outer;
                    m--;
                }
                /* inefficient, I know. should use a hash. */
                for (m = 0; m < cu->num_frames; m++) {
                    if (cu->frames[m] == applicable_frame) {
                        a("lex_Frame_%u_%s_%s", m, frame_lexicals[m][idx],
                            get_typename(applicable_frame->lexical_types[idx]));
                    }
                }
            }
            cur_op += operand_size;
        }
        lines[lineno++] = o;
    }
    {
        MVMuint32 *linelabels = calloc(sizeof(MVMuint32) * lineno, 1);
        MVMuint32 byte_offset = 0;
        MVMuint32 line_number = 0;
        MVMuint32 label_number = 1;
        for (; byte_offset < bytecode_size; byte_offset++) {
            if (labels[byte_offset] & MVM_val_branch_target) {
                /* found a byte_offset where a label should be.
                 now crawl up through the lines to find which line starts there */
                while (linelocs[line_number] != byte_offset) line_number++;
                linelabels[line_number] = label_number++;
            }
        }
        o = oP;
        l = lP;
        s = sP;
        for (j = 0; j < lineno; j++) {
            if (linelabels[j])
                a("     label_%u:\n", linelabels[j]);
            a("        %s", lines[j]);
            if (jumps[j]) {
                /* hoirrbly inefficient for large frames.  again, should use a hash */
                line_number = 0;
                while (linelocs[line_number] != jumps[j]) line_number++;
                a("label_%u", linelabels[line_number]);
            }
            a("\n");
            /*free(lines[j]);*/
        }
    }
    
        }
        /*
        for (j = 0; j < frame->num_lexicals; j++) {
            free(lexicals[j]);
        }*/
    }
    return o;
}


