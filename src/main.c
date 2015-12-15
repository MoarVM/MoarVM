#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <moar.h>

#if MVM_TRACING
#  define TRACING_OPT "[--tracing] "
#  define TRACING_USAGE "\n    --tracing  output a line to stderr on every interpreter instr"
#else
#  define TRACING_OPT ""
#  define TRACING_USAGE ""
#endif

/* flags need to be sorted alphabetically */

enum {
    NOT_A_FLAG = -2,
    UNKNOWN_FLAG = -1,

    FLAG_CRASH,
    FLAG_DUMP,
    FLAG_FULL_CLEANUP,
    FLAG_HELP,
    FLAG_TRACING,
    FLAG_VERSION,

    OPT_EXECNAME,
    OPT_LIBPATH
};

static const char *const FLAGS[] = {
    "--crash",
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
    --version         show version information"
    TRACING_USAGE
    "\n\
\n\
The following environment variables are respected:\n\
\n\
    MVM_SPESH_DISABLE           Disables all dynamic optimization\n\
    MVM_SPESH_NODELAY           Run dynamic optimization even for cold frames\n\
    MVM_SPESH_INLINE_DISABLE    Disables inlining\n\
    MVM_SPESH_OSR_DISABLE       Disables on-stack replacement\n\
    MVM_JIT_DISABLE             Disables JITting to machine code\n\
    MVM_SPESH_LOG               Specifies a dynamic optimizer log file\n\
    MVM_JIT_LOG                 Specifies a JIT-compiler log file\n\
    MVM_JIT_BYTECODE_DIR        Specifies a directory for JIT bytecode dumps\n\
    MVM_CROSS_THREAD_WRITE_LOG  Log unprotected cross-thread object writes to stderr\n\
    MVM_COVERAGE_LOG            Append line-by-line coverage messages to this file\n\
";

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
    else
        return UNKNOWN_FLAG;
}

int main(int argc, char *argv[])
{
    MVMInstance *instance;
    const char  *input_file;
    const char  *executable_name = NULL;
    const char  *lib_path[8];

    int dump         = 0;
    int full_cleanup = 0;
    int argi         = 1;
    int lib_path_i   = 0;
    int flag;
    for (; (flag = parse_flag(argv[argi])) != NOT_A_FLAG; ++argi) {
        switch (flag) {
            case FLAG_CRASH:
            MVM_crash_on_error();
            continue;

            case FLAG_DUMP:
            dump = 1;
            continue;

            case FLAG_FULL_CLEANUP:
            full_cleanup = 1;
            continue;

            case FLAG_HELP:
            puts(USAGE);
            return EXIT_SUCCESS;

#if MVM_TRACING
            case FLAG_TRACING:
            MVM_interp_enable_tracing();
            continue;
#endif

            case OPT_EXECNAME:
            executable_name = argv[argi] + strlen("--execname=");
            continue;

            case OPT_LIBPATH:
            if (lib_path_i == 7) { /* 0..7 == 8 */
                fprintf(stderr, "ERROR: Only up to eight --libpath options are allowed.\n");
                return EXIT_FAILURE;
            }

            lib_path[lib_path_i++] = argv[argi] + strlen("--libpath=");
            continue;

            case FLAG_VERSION: {
            char *spesh_disable;
            char *jit_disable;

            printf("This is MoarVM version %s", MVM_VERSION);
            if (MVM_jit_support()) {
                printf(" built with JIT support");

                spesh_disable = getenv("MVM_SPESH_DISABLE");
                jit_disable = getenv("MVM_JIT_DISABLE");
                if (spesh_disable && strlen(spesh_disable) != 0) {
                    printf(" (disabled via MVM_SPESH_DISABLE)");
                } else if (jit_disable && strlen(jit_disable) != 0) {
                    printf(" (disabled via MVM_JIT_DISABLE)");
                }
            }
            printf("\n");
            return EXIT_SUCCESS;
            }

            default:
            fprintf(stderr, "ERROR: Unknown flag %s.\n\n%s\n", argv[argi], USAGE);
            return EXIT_FAILURE;
        }
    }

    lib_path[lib_path_i] = NULL;

    if (argi >= argc) {
        fprintf(stderr, "ERROR: Missing input file.\n\n%s\n", USAGE);
        return EXIT_FAILURE;
    }

    instance   = MVM_vm_create_instance();
    input_file = argv[argi++];

    /* stash the rest of the raw command line args in the instance */
    MVM_vm_set_clargs(instance, argc - argi, argv + argi);
    MVM_vm_set_prog_name(instance, input_file);
    MVM_vm_set_exec_name(instance, executable_name);
    MVM_vm_set_lib_path(instance, lib_path_i, lib_path);

    if (dump) MVM_vm_dump_file(instance, input_file);
    else MVM_vm_run_file(instance, input_file);

    if (full_cleanup) {
        MVM_vm_destroy_instance(instance);
        return EXIT_SUCCESS;
    }
    else {
        MVM_vm_exit(instance);
    }
}
