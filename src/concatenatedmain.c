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

MVMCompUnit *MVM_load_bytecode_memregion(MVMThreadContext *tc, char* filename_cstr, MVMuint8 *data, MVMuint32 data_size) {
    MVMCompUnit *cu = NULL;
    MVMLoadedCompUnitName *loaded_name;
    MVMuint8 had_loaded_name = 0;

    MVMString *const filename = MVM_string_utf8_c8_decode(tc, tc->instance->VMString, filename_cstr, strlen(filename_cstr));

    uv_mutex_lock(&tc->instance->mutex_loaded_compunits);
    MVM_tc_set_ex_release_mutex(tc, &tc->instance->mutex_loaded_compunits);
    MVM_HASH_GET(tc, tc->instance->loaded_compunits, filename, loaded_name);
    if (loaded_name) {
        /* already loaded */
        had_loaded_name = 1;
        MVM_free(filename);
        goto LEAVE;
    }

    cu = MVM_cu_from_bytes(tc, data, data_size);
    cu->body.filename = filename;

    loaded_name = MVM_calloc(1, sizeof(MVMLoadedCompUnitName));
    loaded_name->filename = filename;
    loaded_name->still_to_set_up = cu;
    MVM_HASH_BIND(tc, tc->instance->loaded_compunits, filename, loaded_name);

 LEAVE:
    MVM_tc_clear_ex_release_mutex(tc);
    uv_mutex_unlock(&tc->instance->mutex_loaded_compunits);

    return had_loaded_name ? NULL : cu;
}
/* This callback is passed to the interpreter code. It takes care of making
 * the initial invocation. */
static void toplevel_initial_invoke(MVMThreadContext *tc, void *data) {
    /* Create initial frame, which sets up all of the interpreter state also. */
    MVM_frame_invoke(tc, (MVMStaticFrame *)data, MVM_callsite_get_common(tc, MVM_CALLSITE_ID_NULL_ARGS), NULL, NULL, NULL, -1);
}

void MVM_vm_run_directly(MVMInstance *instance, MVMCompUnit *cu) {
    /* Map the compilation unit into memory and dissect it. */
    MVMThreadContext *tc = instance->main_thread;

    MVMROOT(tc, cu, {
        /* Run deserialization frame, if there is one. */
        if (cu->body.deserialize_frame) {
            MVM_interp_run(tc, toplevel_initial_invoke, cu->body.deserialize_frame);
        }
    });

    /* Run the entry-point frame. */
    MVM_interp_run(tc, toplevel_initial_invoke, cu->body.main_frame);
}

MVMCompUnit *find_concat_content(MVMInstance *instance) {
    MVMThreadContext *tc = instance->main_thread;
    FILE *myself = fopen("/proc/self/exe", "r");
    MVMuint32 allocated_blob_offsets = 16;
    MVMuint32 current_blob_offset = 0;
    MVMuint64 *blob_offsets = MVM_malloc(sizeof(MVMuint64) * allocated_blob_offsets);
    MVMint32 i;
    MVMuint64 read_limit;
    MVMCompUnit *main_cu = NULL;
    fseek(myself, 0, SEEK_END);
    if (ftell(myself) < 64) {
        fprintf(stderr, "this binary is smaller than the header i'm expecting at the end. how did this happen?\n");
        exit(1);
    }

    fseek(myself, -64, SEEK_CUR);

    read_limit = ftell(myself);

    while (1)
    {
        MVMuint64 size = 0;
        char header[65];
        if (!fread(header, 64, 1, myself)) {
            fprintf(stderr, "couldn't read 64 bits here\n");
            break;
        }
        if (strncmp(header, "This file contains extra stuff. Size: ", 38) != 0) {
            break;
        }

        memcpy(&size, header + 38, 8);

        /*fprintf(stderr, "got a data block of %" PRIu64 " bytes size\n", size);*/

        if (current_blob_offset == allocated_blob_offsets) {
            allocated_blob_offsets *= 2;
            blob_offsets = MVM_realloc(blob_offsets, sizeof(MVMuint64) * allocated_blob_offsets);
        }

        fseek(myself, -size - 64 - 64, SEEK_CUR);

        blob_offsets[current_blob_offset++] = ftell(myself) + 64;
        /*fprintf(stderr, "storing blob offset %lx\n", ftell(myself) + 64);*/
        /*fprintf(stderr, "current blob offset: %d\n", current_blob_offset);*/
    }

    /* Second pass: go through each blob and nom all its individual entries */
    for (i = current_blob_offset - 1; i >= 0; i--) {
        MVMuint64 namelen = 0;
        MVMuint64 readpos = 0;
        char read_value = 1;
        char *filename;
        char kind[4] = {0};
        MVMuint64 subfilesize;
        MVMuint8 *databuffer;

        /*fprintf(stderr, "setting readpos to blob %d\n", i);*/

        readpos = blob_offsets[i];
        fseek(myself, readpos, SEEK_SET);
        /*fprintf(stderr, "looking for a subfile header at offset %lx\n", ftell(myself));*/

        {
            char cookie[8] = {0};
            fread(cookie, 7, 1, myself);
            if (strncmp(cookie, "SUBFILE:", 7) != 0) {
                fprintf(stderr, "while looking for subfiles: invalid cookie found :(\n");
                exit(2);
            }
        }

        do {
            read_value = fgetc(myself);
            namelen++;
            readpos++;
        } while (read_value != 0 && readpos < read_limit && !feof(myself) && !ferror(myself));

        if (feof(myself) || ferror(myself)) {
            fprintf(stderr, "While reading through a blob at the end of our binary, eof or error occured.\n");
            exit(2);
        }

        filename = MVM_malloc(namelen + 1);
        fseek(myself, -namelen + 1, SEEK_CUR);
        fread(filename, namelen - 1, 1, myself);
        /*fprintf(stderr, "this subfile is %s\n", filename);*/

        /*fprintf(stderr, "reading filesize at offset %lx\n", ftell(myself));*/

        fread(&subfilesize, 8, 1, myself);
        /*fprintf(stderr, "this subfile is %ld bytes big\n", subfilesize);*/

        fread(&kind, 3, 1, myself);
        /*fprintf(stderr, "it is a %s\n", kind);*/

        databuffer = MVM_malloc(subfilesize);
        fread(databuffer, 1, subfilesize, myself);

        if (strncmp(kind, "mbc", 3) == 0) {
            if (strncmp(filename, "packaged_main", 13) == 0) {
                main_cu = MVM_load_bytecode_memregion(tc, "packaged_main", databuffer, subfilesize);
            } else {
                if (!MVM_load_bytecode_memregion(tc, filename, databuffer, subfilesize)) {
                    MVM_free(filename);
                }
            }
        } else if (strncmp(kind, "dll", 3) == 0) {
            fprintf(stderr, "ignoring dll kind in concatenated blob\n");
        }
    }

    MVM_free(blob_offsets);
    return main_cu;
}

/* flags need to be sorted alphabetically */

enum {
    NOT_A_FLAG = -2,
    UNKNOWN_FLAG = -1,

    FLAG_CRASH,
    FLAG_FULL_CLEANUP,
    FLAG_HELP,
    FLAG_TRACING,
    FLAG_VERSION,

    OPT_EXECNAME,
    OPT_LIBPATH
};

static const char *const FLAGS[] = {
    "--crash",
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
    MVM_SPESH_LIMIT             Limit the maximum number of specializations\n\
    MVM_JIT_DISABLE             Disables JITting to machine code\n\
    MVM_SPESH_LOG               Specifies a dynamic optimizer log file\n\
    MVM_JIT_LOG                 Specifies a JIT-compiler log file\n\
    MVM_JIT_BYTECODE_DIR        Specifies a directory for JIT bytecode dumps\n\
    MVM_CROSS_THREAD_WRITE_LOG  Log unprotected cross-thread object writes to stderr\n\
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
    MVMCompUnit *main_compunit;
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

            /*case FLAG_DUMP:*/
            /*dump = 1;*/
            /*continue;*/

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
        fprintf(stderr, "ERROR: please be so kind and provide an arbitrary single commandline argument.\n");
        return EXIT_FAILURE;
    }

    instance   = MVM_vm_create_instance();
    input_file = argv[argi++];

    /* stash the rest of the raw command line args in the instance */
    MVM_vm_set_clargs(instance, argc - argi, argv + argi);
    MVM_vm_set_prog_name(instance, input_file);
    MVM_vm_set_exec_name(instance, executable_name);
    MVM_vm_set_lib_path(instance, lib_path_i, lib_path);

    main_compunit = find_concat_content(instance);

    if (!main_compunit) {
        fprintf(stderr, "ERROR: didn't find a mbc named 'packaged_main' in the concatenated blob.\n");
        exit(1);
    }

    /*if (dump) MVM_vm_dump_file(instance, input_file);*/
    else MVM_vm_run_directly(instance, main_compunit);

    if (full_cleanup) {
        MVM_vm_destroy_instance(instance);
        return EXIT_SUCCESS;
    }
    else {
        MVM_vm_exit(instance);
    }
}
