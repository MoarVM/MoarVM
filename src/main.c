#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <moarvm.h>

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
    FLAG_HELP,
    FLAG_TRACING,
};

static const char *const FLAGS[] = {
    "--crash",
    "--dump",
    "--help",
    "--tracing",
};

static const char USAGE[] = "\
USAGE: moarvm [--dump] [--crash] " TRACING_OPT "input.moarvm [program args]\n\
       moarvm [--help]\n\
\n\
    --help     display this message\n\
    --dump     dump the bytecode to stdout instead of executing\n\
    --crash    abort instead of exiting on unhandled exception"
    TRACING_USAGE;

static int cmp_flag(const void *key, const void *value)
{
    return strcmp(key, *(char **)value);
}

static int parse_flag(const char *arg)
{
    const char *const *found;

    if(!arg || arg[0] != '-')
        return NOT_A_FLAG;

    found = bsearch(arg, FLAGS, sizeof FLAGS / sizeof *FLAGS, sizeof *FLAGS, cmp_flag);
    return found ? (int)(found - FLAGS) : UNKNOWN_FLAG;
}

int main(int argc, char *argv[])
{
    MVMInstance *instance;
    const char  *input_file;

    int dump = 0;
    int argi = 1;
    int flag;

    for (; (flag = parse_flag(argv[argi])) != NOT_A_FLAG; ++argi) {
        switch (flag) {
            case FLAG_CRASH:
            MVM_crash_on_error();
            continue;

            case FLAG_DUMP:
            dump = 1;
            continue;

            case FLAG_HELP:
            puts(USAGE);
            return EXIT_SUCCESS;

#if MVM_TRACING
            case FLAG_TRACING:
            MVM_interp_enable_tracing();
            continue;
#endif

            default:
            fprintf(stderr, "ERROR: Unknown flag %s.\n\n%s\n", argv[argi], USAGE);
            return EXIT_FAILURE;
        }
    }

    if (argi >= argc) {
        fprintf(stderr, "ERROR: Missing input file.\n\n%s\n", USAGE);
        return EXIT_FAILURE;
    }

    instance   = MVM_vm_create_instance();
    input_file = argv[argi++];

    /* stash the rest of the raw command line args in the instance */
    instance->num_clargs = argc - argi;
    instance->raw_clargs = argv + argi;
    instance->prog_name  = input_file;

    if (dump) MVM_vm_dump_file(instance, input_file);
    else MVM_vm_run_file(instance, input_file);

    MVM_vm_destroy_instance(instance);

    return EXIT_SUCCESS;
}
