#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "moar.h"
#include "platform/io.h"
#include <stdbool.h>

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

/* Persistent mode fuzzing harness taken from aflplusplus
 * */

/* this lets the source compile without afl-clang-fast/lto */
#ifndef __AFL_FUZZ_TESTCASE_LEN

ssize_t       fuzz_len;
unsigned char fuzz_buf[1024000];

  #define __AFL_FUZZ_TESTCASE_LEN fuzz_len
  #define __AFL_FUZZ_TESTCASE_BUF fuzz_buf
  #define __AFL_FUZZ_INIT() void sync(void);
  #define __AFL_LOOP(x) \
    ((fuzz_len = read(0, fuzz_buf, sizeof(fuzz_buf))) > 0 ? 1 : 0)
  #define __AFL_INIT() sync()

#endif

__AFL_FUZZ_INIT();

/* To ensure checks are not optimized out it is recommended to disable
   code optimization for the fuzzer harness main() */
/*#pragma clang optimize off
#pragma GCC optimize("O0")*/

/* End of aflplusplus example code. */


int times_jump_reached = 0;

int main(int argc, char **argv) {
    MVMInstance *instance;
    const char  *executable_name = NULL;
    char *fake_args[1] = {"fake"};

    int encoding = 0;

    bool do_encode = false;

    bool read_input_from_file = false;

    if (argc >= 4) {
        if (strncmp("enc", argv[3], 3) == 0) {
            // fprintf(stderr, "will encode output.\n");
            do_encode = true;
        } else {
            fprintf(stderr, "Usage: program @@ [<enc-number> [enc]]\n");
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
    if (argc >= 2) {
        if (strncmp("./", argv[1], 2) == 0) {
            read_input_from_file = true;
        }
    }

    /* Do not start a spesh thread in MVM_vm_create_instance. */
    setenv("MVM_SPESH_DISABLE", "1", 0);

    /* "warm up" the globally initialised things ... */
    instance   = MVM_vm_create_instance();
    MVM_vm_set_clargs(instance, 1, fake_args);
    MVM_vm_set_prog_name(instance, "afl-fuzzing");
    MVM_vm_set_exec_name(instance, executable_name);
    instance->full_cleanup = 1;

    MVM_vm_destroy_instance(instance);
    instance = NULL;

    unsigned char *Data;                       /* test case buffer pointer    */

    /* The number passed to __AFL_LOOP() controls the maximum number of
        iterations before the loop exits and the program is allowed to
        terminate normally. This limits the impact of accidental memory leaks
        and similar hiccups. */

    __AFL_INIT();

    Data = __AFL_FUZZ_TESTCASE_BUF;  // this must be assigned before __AFL_LOOP!

    while (__AFL_LOOP(100000)) {  // increase if you have good stability
        instance = NULL;
        times_jump_reached = 0;

        ssize_t Size = 0;

        if (!read_input_from_file) {
            Size = __AFL_FUZZ_TESTCASE_LEN;  // do not use the macro directly in a call!
        }

        ssize_t nommed = 0;

        size_t seen_total = 0;

        /* If we didn't have AFL give us the test cases directly in memory, we would do this */
        if (read_input_from_file) {
            FILE *input = fopen(argv[1], "rb");

            if (!input) {
                fprintf(stderr, "Could not open input file %s: %s\n", argv[1], strerror(errno));
                exit( 1);
            }

            fseek(input, 0, SEEK_END);

            Size = ftell(input);
            Data = malloc(Size);
            fseek(input, 0, SEEK_SET);

            while (nommed < Size) {
                size_t read = fread(Data + nommed, 1, Size - nommed, input);
                if (read == 0) {
                    fprintf(stderr, "failed to read from input file! (fread failed with %s)\n", strerror(errno));
                }
                nommed += read;
            }

            fprintf(stderr, "read %ld from input file\n", Size);
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
                    goto continue_afl_main_loop;
                }
                num_blocksizes++;
                break;
            }
            if ((list_of_numbers_parse_state == '\0' || list_of_numbers_parse_state == ',') && Data[nommed] >= '1' && Data[nommed] <= '9') {
                list_of_numbers_parse_state = '1';
            }
            else if (list_of_numbers_parse_state == '1' && Data[nommed] >= '0' && Data[nommed] <= '9') {
                list_of_numbers_parse_state = '1';
            }
            else if (list_of_numbers_parse_state == '1' && Data[nommed] == ',') {
                list_of_numbers_parse_state = ',';
                num_blocksizes++;
            }
            else {
                fprintf(stderr, "At character %zu, failed to parse list of ascii numbers! Saw a %c (%d)\n", nommed, Data[nommed], Data[nommed]);
                goto continue_afl_main_loop;
            }
            nommed++;
        }
        if (nommed == Size) {
            fprintf(stderr, "expected to parse a line of ascii numbers as the first line, but reached the end of the file?\n");
            goto continue_afl_main_loop;
        }
        if (list_of_numbers_parse_state != '1') {
            fprintf(stderr, "At position %zu, expected at least one byte before the first newline of the input file... (at least one read-x-at-a-time value)\n", nommed);
            goto continue_afl_main_loop;
        }

        // fprintf(stderr, "Saw %ld blocksizes in the first line.\n", num_blocksizes);

        MVMint64 *read_at_a_time = malloc(num_blocksizes * sizeof(MVMint64));

        size_t blocksize_idx = 0;
        size_t last_start_pos = 0;
        nommed = 0;

        for (; blocksize_idx < num_blocksizes; nommed++) {
            if (Data[nommed] == ',' || Data[nommed] == '\n') {
                /* Reject test cases with huge read-at-a-time values */
                if (nommed - last_start_pos > 8) {
                    fprintf(stderr, "i don't want a read-n-at-a-time longer than 8 chars (found value from %ld to %ld)\n", last_start_pos, nommed);
                    goto continue_afl_main_loop;
                }
                read_at_a_time[blocksize_idx] = strtol((char *)Data + last_start_pos, NULL, 10);
                // fprintf(stderr, "read at a time[%ld] = %ld\n", blocksize_idx, read_at_a_time[blocksize_idx]);
                last_start_pos = nommed + 1;
                blocksize_idx++;
            }
            if (Data[nommed] == '\n') {
                break;
            }
        }

        nommed = 0;

        blocksize_idx = 0;

        instance   = MVM_vm_create_instance();

        /* stash the rest of the raw command line args in the instance */
        MVM_vm_set_clargs(instance, 1, fake_args);
        MVM_vm_set_prog_name(instance, "afl-fuzzing");
        MVM_vm_set_exec_name(instance, executable_name);
        instance->full_cleanup = 1;
        // MVM_vm_set_lib_path(instance, lib_path_i, lib_path);

        MVMThreadContext *tc = instance->main_thread;

        MVM_setjmp(tc->interp_jump);

        times_jump_reached++;

        /* TODO: without a frame that has exception handlers defined, this
         *       does nothing. */
        if (times_jump_reached > 1) {
            goto continue_afl_main_loop;
        }

        MVMDecodeStream *ds = MVM_string_decodestream_create(tc, encoding, 0, 0);
        MVMDecodeStreamSeparators dss = {0};
        MVM_string_decode_stream_sep_default(tc, &dss);


        while (nommed < Size) {
            ssize_t to_nom = read_at_a_time[blocksize_idx];

            blocksize_idx++;
            blocksize_idx %= num_blocksizes;

            if (nommed + to_nom > Size) {
                to_nom = Size - nommed;
            }

            // fprintf(stderr, "making buffer to hold %ld bytes\n", to_nom);
            MVMuint8 *buffer = malloc(to_nom);
            memcpy(buffer, Data + nommed, to_nom);

            // fprintf(stderr, "nommed %10ld out of %10ld bytes. Last read %ld\n", nommed, Size, to_nom);

            MVM_string_decodestream_add_bytes(tc, ds, buffer, to_nom);
            for (;;) {
                if (!MVM_string_decodestream_is_empty(tc, ds)) {
                    MVMString *res_str = MVM_string_decodestream_get_until_sep(tc, ds, &dss, 1);
                    if (!res_str) break;
                    /* Super, super cheeky optimization: any string that had
                     * some memory allocated for it, we free and switch over
                     * to claiming to have in situ storage instead so if
                     * we ever enter GC, we don't attempt to free its pointer.
                     */
                    if (res_str->body.storage_type != MVM_STRING_IN_SITU_8 && res_str->body.storage_type != MVM_STRING_IN_SITU_32) {
                        MVM_free(res_str->body.storage.any_ptr);
                        res_str->body.storage_type = MVM_STRING_IN_SITU_8;
                        res_str->body.num_graphs = 0;
                    }
                    /* We could go one step further and manually turn the
                     * allocation pointer back to what it was before we got
                     * the MVMString, but that feels a lot more evil ... */
                    if (do_encode) {
                        // fprintf(stderr, "  res str not null\n");
                        char *res_bytes = MVM_string_utf8_encode_C_string(tc, res_str);
                        // fprintf(stderr, "\"%s\"\n", res_bytes);
                        seen_total += strlen(res_bytes);
                        MVM_free(res_bytes);
                    }
                }
                else {
                    break;
                }

            }
            nommed += to_nom;
        }

/*        if (do_encode)
            fprintf(stderr, "total bytes out seen: %ld\n", seen_total);*/

continue_afl_main_loop:
        if (instance) {
            MVM_vm_destroy_instance(instance);
        }

        if (read_input_from_file) {
            fprintf(stderr, "read a single file from input, we are not in AFL mode.\n");
            break;
        }
    }
}
