#include <stdio.h>
#include <moarvm.h>

int main(int argc, const char *argv[]) {
    MVMInstance *instance;

    apr_status_t rv;
    apr_pool_t *mp;
    static const apr_getopt_option_t opt_option[] = {
        /* values must be greater than 255 so it doesn't have a single-char form
           Otherwise, use a character such as 'h' */
        { "dump", 256, 0, "dump bytecode" },
        { "help", 257, 0, "show help" },
        { NULL, 0, 0, NULL }
    };
    apr_getopt_t *opt;
    int optch;
    const char *optarg;
    int dump = 0;
    char *input_file;
    int exitcode = 0;
    const char *helptext = "\
    MoarVM usage: moarvm [options] bytecode.moarvm [program args]           \n\
      --help, display this message                                          \n\
      --dump, dump the bytecode to stdout instead of executing              \n";
    int processed_args = 0;

    instance = MVM_vm_create_instance();

    apr_pool_create(&mp, NULL);
    apr_getopt_init(&opt, mp, argc, argv);
    while ((rv = apr_getopt_long(opt, opt_option, &optch, &optarg)) == APR_SUCCESS) {
        switch (optch) {
        case 256:
            dump = 1;
            break;
        case 257:
            printf("%s", helptext);
            goto terminate;
        }
    }
    processed_args = opt->ind;
    if (processed_args == argc) {
        fprintf(stderr, "Error: You must supply an input file.\n\n%s", helptext);
        exitcode = 1;
        goto terminate;
    }
    input_file = (char *)opt->argv[processed_args++];

    /* stash the rest of the raw command line args in the instance */
    instance->num_clargs = argc - processed_args;
    instance->raw_clargs = (char **)(opt->argv + processed_args);

    if (dump) {
        MVM_vm_dump_file(instance, input_file);
    }
    else {
        MVM_vm_run_file(instance, input_file);
    }

  terminate:
    apr_pool_destroy(mp);
    MVM_vm_destroy_instance(instance);

    return exitcode;
}
