#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "moar.h"
#include "platform/io.h"

#if MVM_TRACING
#  define TRACING_OPT "[--tracing] "
#  define TRACING_USAGE "\n    --tracing         output a line to stderr on every interpreter instr"
#else
#  define TRACING_OPT ""
#  define TRACING_USAGE ""
#endif

#ifdef HAVE_TELEMEH
#  define TELEMEH_USAGE "    MVM_TELEMETRY_LOG           Log internal events at high precision to this file\n"
#else
#  define TELEMEH_USAGE ""
#endif


#ifndef _WIN32
#  include <unistd.h>
#else
#  include <process.h>
#endif

#ifdef _WIN32
#  define snprintf _snprintf
#endif

#if defined(_MSC_VER)
#define strtoll _strtoi64
#endif

enum {
    NOT_A_FLAG = -2,
    UNKNOWN_FLAG = -1,

    FLAG_CRASH,
    FLAG_SUSPEND,
    FLAG_DUMP,
    FLAG_FULL_CLEANUP,
    FLAG_HELP,
    FLAG_TRACING,
    FLAG_VERSION,

    OPT_EXECNAME,
    OPT_LIBPATH,
    OPT_DEBUGPORT,
#ifdef MVM_DO_PTY_OURSELF
    OPT_PTY_SPAWN_HELPER,
#endif
};

/* FLAGS needs to be sorted alphabetically. */
static const char *const FLAGS[] = {
    "--crash",
    "--debug-suspend",
    "--dump",
    "--full-cleanup",
    "--help",
    "--tracing",
    "--version",
};

static const char USAGE[] = "\
USAGE: moar [--crash] [--libpath=...] " TRACING_OPT "input.moarvm [program args]\n\
       moar --dump input.moarvm\n\
       moar --help\n\
\n\
    --help            display this message\n\
    --dump            dump the bytecode to stdout instead of executing\n\
    --full-cleanup    try to free all memory and exit cleanly\n\
    --crash           abort instead of exiting on unhandled exception\n\
    --libpath         specify path loadbytecode should search in\n\
    --version         show version information\n\
    --debug-port=1234 listen for incoming debugger connections\n\
    --debug-suspend   pause execution at the entry point"
    TRACING_USAGE
    "\n\
\n\
The following environment variables are respected:\n\
\n\
    MVM_SPESH_DISABLE           Disables all dynamic optimization\n\
    MVM_SPESH_INLINE_DISABLE    Disables inlining\n\
    MVM_SPESH_OSR_DISABLE       Disables on-stack replacement\n\
    MVM_SPESH_PEA_DISABLE       Disables partial escape analysis and related optimizations\n\
    MVM_SPESH_BLOCKING          Blocks log-sending thread while specializer runs\n\
    MVM_SPESH_LOG               Specifies a dynamic optimizer log file\n\
    MVM_SPESH_NODELAY           Run dynamic optimization even for cold frames\n\
    MVM_SPESH_LIMIT             Limit the maximum number of specializations\n\
    MVM_JIT_DISABLE             Disables JITting to machine code\n\
    MVM_JIT_EXPR_ENABLE         Enable advanced 'expression' JIT\n\
    MVM_JIT_DEBUG               Add JIT debugging information to spesh log\n\
    MVM_JIT_PERF_MAP            Create a map file for the 'perf' profiler (linux only)\n\
    MVM_JIT_DUMP_BYTECODE       Dump bytecode in temporary directory\n\
    MVM_SPESH_INLINE_LOG        Dump details of inlining attempts to stderr\n\
    MVM_CROSS_THREAD_WRITE_LOG  Log unprotected cross-thread object writes to stderr\n\
    MVM_COVERAGE_LOG            Append (de-duped by default) line-by-line coverage messages to this file\n\
    MVM_COVERAGE_CONTROL        If set to 1, non-de-duping coverage started with nqp::coveragecontrol(1),\n\
                                  if set to 2, non-de-duping coverage started right away\n"
    TELEMEH_USAGE;

static int cmp_flag(const void *key, const void *value)
{
    return strcmp(key, *(char **)value);
}

static int starts_with(const char *str, const char *want) {
    size_t str_len  = strlen(str);
    size_t want_len = strlen(want);
    return str_len < want_len
        ? 0
        : strncmp(str, want, want_len) == 0;
}

static int parse_flag(const char *arg)
{
    const char *const *found;

    if (!arg || arg[0] != '-')
        return NOT_A_FLAG;

    found = bsearch(arg, FLAGS, sizeof FLAGS / sizeof *FLAGS, sizeof *FLAGS, cmp_flag);

    if (found)
        return (int)(found - FLAGS);
    else if (starts_with(arg, "--libpath="))
        return OPT_LIBPATH;
    else if (starts_with(arg, "--execname="))
        return OPT_EXECNAME;
    else if (starts_with(arg, "--debug-port="))
        return OPT_DEBUGPORT;
#ifdef MVM_DO_PTY_OURSELF
    else if (starts_with(arg, "--pty-spawn-helper="))
        return OPT_PTY_SPAWN_HELPER;
#endif
    else
        return UNKNOWN_FLAG;
}

int times_jump_reached = 0;

int main(int argc, char **argv) {
    MVMInstance *instance;
    const char  *input_file;
    const char  *executable_name = NULL;
    const char  *lib_path[8];
    char *fake_args[1] = {"fake"};

#ifdef _WIN32
    char **argv = MVM_UnicodeToUTF8_argv(argc, wargv);
#endif

    int dump         = 0;
    int full_cleanup = 0;
    int argi         = 1;
    int lib_path_i   = 0;
    int flag;

#ifdef HAVE_TELEMEH
    unsigned int interval_id = 0;
    char telemeh_inited = 0;
#endif

    int encoding = 0;

    bool do_encode = false;

    if (argc >= 4) {
        if (strcmp("enc", argv[3]) == 0) {
            fprintf(stderr, "will encode output.\n");
            do_encode = true;
        } else {
            fprintf(stderr, "Usage: program @@ [enc number [enc]]\n");
            exit(1);
        }
    }
    if (argc >= 3) {
        /* Choose the encoding based on last parameter. */
        encoding = atoi(argv[2]);
        if (encoding < MVM_encoding_type_MIN || encoding > MVM_encoding_type_MAX) {
            fprintf(stderr, "encoding number %d not valid.", encoding);
            exit(1);
        }
    }
    else {
        /* Choose default encoding: utf8-c8. */
        encoding = MVM_encoding_type_utf8_c8;
    }

    lib_path[lib_path_i] = NULL;

    /* Do not start a spesh thread in MVM_vm_create_instance. */
    setenv("MVM_SPESH_DISABLE", "1", 0);

    instance   = MVM_vm_create_instance();

    /* stash the rest of the raw command line args in the instance */
    MVM_vm_set_clargs(instance, 1, fake_args);
    MVM_vm_set_prog_name(instance, "afl-fuzzing");
    MVM_vm_set_exec_name(instance, executable_name);
    // MVM_vm_set_lib_path(instance, lib_path_i, lib_path);

    instance->full_cleanup = full_cleanup;

    MVMThreadContext *tc = instance->main_thread;

    MVM_crash_on_error();

    MVM_setjmp(tc->interp_jump);

    times_jump_reached++;

    if (times_jump_reached > 1) {
        exit(1);
    }

    MVMDecodeStream *ds = MVM_string_decodestream_create(tc, MVM_encoding_type_utf8_c8, 0, 0);
    MVMDecodeStreamSeparators dss = {0};
    MVM_string_decode_stream_sep_default(tc, &dss);

    size_t nommed = 0;

    size_t seen_total = 0;

    /* AFL Initialisation can go here. */

    FILE *input = fopen(argv[1], "rb");

    if (!input) { exit( 1); }

    fseek(input, 0, SEEK_END);

    size_t Size = ftell(input);
    char *Data = MVM_malloc(Size);
    fseek(input, 0, SEEK_SET);

    while (nommed < Size) {
        size_t read = fread(Data + nommed, 1, Size - nommed, input);
        if (read == 0) {
            fprintf(stderr, "failed to read from input file! (fread failed with %s)", strerror(errno));
        }
        nommed += read;
    }

    nommed = 0;

    size_t num_blocksizes = 0;

    int list_of_numbers_parse_state = '\0';

    /* Allow passing a bunch of "how many bytes to read" in the
     * first "line" of the input: */
    while (nommed < Size) {
        if (Data[nommed] == '\n') {
            if (list_of_numbers_parse_state == ',' || list_of_numbers_parse_state == '\0') {
                fprintf(stderr, "Expected an ascii number before first newline\n");
                exit(2);
            }
            break;
        }
        if ((list_of_numbers_parse_state == '\0' || list_of_numbers_parse_state == ',') && Data[nommed] >= '1' && Data[nommed] <= '9') {
            list_of_numbers_parse_state = '1';
            num_blocksizes++;
        }
        else if (list_of_numbers_parse_state == '1' && Data[nommed] >= '0' && Data[nommed] <= '9') {
            list_of_numbers_parse_state = '1';
        }
        else if (list_of_numbers_parse_state == '1' && Data[nommed] == ',') {
            list_of_numbers_parse_state = ',';
        }
        else {
            fprintf(stderr, "At character %zu, failed to parse list of ascii numbers!\n", nommed);
        }
        nommed++;
    }
    if (nommed == Size) {
        fprintf(stderr, "expected to parse a line of ascii numbers as the first line, but reached the end of the file?\n");
        exit(2);
    }
    if (list_of_numbers_parse_state != '1') {
        fprintf(stderr, "At position %zu, expected at least one byte before the first newline of the input file... (at least one read-x-at-a-time value)\n", nommed);
        exit(3);
    }

    MVMuint64 *read_at_a_time = MVM_malloc(num_blocksizes);

    size_t blocksize_idx;
    size_t last_start_pos = 0;
    nommed = 0;

    for (blocksize_idx = 0; blocksize_idx < num_blocksizes; blocksize_idx++) {
        if (Data[nommed] == ',' || Data[nommed] == '\n') {
            read_at_a_time[blocksize_idx] = strtol(Data + last_start_pos, NULL, 10);
            last_start_pos = nommed + 1;
        }
    }

    nommed = 0;

    blocksize_idx = 0;

    while (nommed < Size) {
        size_t to_nom = read_at_a_time[blocksize_idx];

        blocksize_idx++;
        blocksize_idx %= num_blocksizes;

        if (nommed + to_nom > Size) {
            to_nom = Size - nommed;
        }

        MVMuint8 *buffer = MVM_malloc(to_nom);
        memcpy(buffer, Data + nommed, to_nom);

        fprintf(stderr, "nommed %10ld out of %10ld bytes. Last read %ld", nommed, Size, to_nom);

        MVM_string_decodestream_add_bytes(tc, ds, buffer, 16);
        for (;;) {
            if (!MVM_string_decodestream_is_empty(tc, ds)) {
                MVMString *res_str = MVM_string_decodestream_get_until_sep(tc, ds, &dss, 1);
                if (!res_str) break;
                /*if (do_encode) {
                    fprintf(stderr, "  res str not null\n");
                    char *res_bytes = MVM_string_utf8_encode_C_string(tc, res_str);
                    fprintf(stderr, "\"%s\"\n", res_bytes);
                    seen_total += strlen(res_bytes);
                    MVM_free(res_bytes);
                }*/
            }

        }
        nommed += to_nom;
    }

    if (do_encode)
        fprintf(stderr, "total bytes out seen: %ld\n", seen_total);

    MVM_vm_exit(instance);
}
