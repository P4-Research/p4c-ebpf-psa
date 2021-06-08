#include <stdio.h>
#include <stdlib.h>
#include <gmp.h>  // GNU LGPL v3 or GNU GPL v2, used only by function translate_data_to_bytes()

#include <bpf/bpf.h>

#include "../include/psabpf.h"
#include "table.h"

#ifdef NEXT_ARG
    #undef NEXT_ARG
#endif
#define NEXT_ARG()	({ argc--; argv++; if (argc < 1) { fprintf(stderr, "too few parameters\n"); exit(1); }})

enum destination_ctx_type_t {
    CTX_MATCH_KEY,
    CTX_ACTION_DATA
};

int translate_data_to_bytes(const char *data, void *ctx, enum destination_ctx_type_t ctx_type)
{
    // converts any precision number to stream of bytes
    mpz_t number;
    size_t len, forced_len = 0;
    char * buffer;
    int error_code = -1;

    // try find width specification
    if (strstr(data, "w") != NULL) {
        char * end_ptr = NULL;
        forced_len = strtoul(data, &end_ptr, 0);
        if (forced_len == 0) {
            fprintf(stderr, "%s: failed to parse width\n", data);
            return -1;
        }
        if (end_ptr == NULL) {
            fprintf(stderr, "%s: failed to parse width\n", data);
            return -1;
        }
        if (strlen(end_ptr) <= 1) {
            fprintf(stderr, "%s: failed to parse width (no data after width)\n", data);
            return -1;
        }
        if (end_ptr[0] != 'w') {
            fprintf(stderr, "%s: failed to parse width (wrong format)\n", data);
            return -1;
        }
        data = end_ptr + 1;
        size_t part_byte = forced_len % 8;
        forced_len = forced_len / 8;
        if (part_byte != 0)
            forced_len += 1;
    }

//    printf("data: %s\n", data);
    mpz_init(number);
    if (mpz_set_str(number, data, 0) != 0) {
        fprintf(stderr, "%s: failed to parse number\n", data);
        goto free_gmp;
    }

    len = mpz_sizeinbase(number, 16);
    if (len % 2 != 0)
        len += 1;
    len /= 2;  // two digits per byte
//    printf("len: %zu\n", len);

    if (forced_len != 0) {
        if (len > forced_len) {
            fprintf(stderr, "%s: do not fits into %zu bytes\n", data, forced_len);
            goto free_gmp;
        }
        len = forced_len;
    }

    buffer = malloc(len);
    if (buffer == NULL) {
        fprintf(stderr, "not enough memory\n");
        goto free_gmp;
    }
    // when data is "0", gmp may not write any value
    memset(buffer, 0, len);
    mpz_export(buffer, 0, -1, 1, 0, 0, number);

//    for (int i = 0; i < len; i++)
//        printf("byte %d: 0x%x\n", i, buffer[i] & 0xff);
    if (ctx_type == CTX_MATCH_KEY)
        error_code = psabpf_matchkey_data(ctx, buffer, len);
    else if (ctx_type == CTX_ACTION_DATA)
        error_code = psabpf_action_param_create(ctx, buffer, len);
    else
        error_code = -1;

    free(buffer);
free_gmp:
    mpz_clear(number);

    return error_code;
}

int do_table_add(int argc, char **argv)
{
    psabpf_table_entry_t entry;
    psabpf_table_entry_ctx_t ctx;
    psabpf_action_t action;
    int error_code = -1;

    // no NEXT_ARG before, so this check must be preserved
    if (argc < 1) {
        fprintf(stderr, "too few parameters\n");
        return -1;
    }

    psabpf_table_entry_ctx_init(&ctx);
    psabpf_table_entry_init(&entry);
    psabpf_action_init(&action);

    // 1. Get table

    if (is_keyword(*argv, "id")) {
        NEXT_ARG();
        fprintf(stderr, "id: table access not supported\n");
        goto clean_up;
    } else if (is_keyword(*argv, "name")) {
        NEXT_ARG();
        fprintf(stderr, "name: table access not supported yet\n");
        goto clean_up;
    } else {
        error_code = psabpf_table_entry_ctx_tblname(&ctx, *argv);
        if (error_code != 0)
            goto clean_up;
    }

    NEXT_ARG();

    // 2. Get action

    error_code = -1;
    if (is_keyword(*argv, "id")) {
        NEXT_ARG();
        char *ptr;
        psabpf_action_set_id(&action, strtoul(*argv, &ptr, 0));
        if (*ptr) {
            fprintf(stderr, "%s: unable to parse as an action id\n", *argv);
            goto clean_up;
        }
    } else {
        fprintf(stderr, "specify an action by name is not supported yet\n");
        goto clean_up;
    }

    NEXT_ARG();

    // 3. Get key

    if (is_keyword(*argv, "key")) {
        bool has_any_key = false;
        do {
            NEXT_ARG();
            error_code = -1;
            if (is_keyword(*argv, "data") || is_keyword(*argv, "priority"))
                break;

            if (is_keyword(*argv, "none")) {
                if (!has_any_key) {
                    printf("Support for table with empty key not implemented yet\n");
                    goto clean_up;
                } else {
                    printf("Unexpected none key\n");
                    goto clean_up;
                }
            }

            psabpf_match_key_t mk;
            psabpf_matchkey_init(&mk);
            if (strstr(*argv, "/") != NULL) {
                fprintf(stderr, "lpm match key not supported yet\n");
                goto clean_up;
            } else if (strstr(*argv, "..") != NULL) {
                fprintf(stderr, "range match key not supported yet\n");
                goto clean_up;
            } else if (strstr(*argv, "%") != NULL) {
                fprintf(stderr, "ternary match key not supported yet\n");
                goto clean_up;
            } else {
                psabpf_matchkey_type(&mk, PSABPF_EXACT);
                error_code = translate_data_to_bytes(*argv, &mk, CTX_MATCH_KEY);
                if (error_code != 0)
                    goto clean_up;
                error_code = psabpf_table_entry_matchkey(&entry, &mk);
            }
            psabpf_matchkey_free(&mk);
            if (error_code != 0)
                goto clean_up;

            has_any_key = true;
        } while (argc > 1);
    }

    // 4. Get action parameters

    if (is_keyword(*argv, "data")) {
        do {
            NEXT_ARG();
            if (is_keyword(*argv, "priority"))
                break;

            psabpf_action_param_t param;
            error_code = translate_data_to_bytes(*argv, &param, CTX_ACTION_DATA);
            if (error_code != 0) {
                psabpf_action_param_free(&param);
                goto clean_up;
            }
            error_code = psabpf_action_param(&action, &param);
            if (error_code != 0)
                goto clean_up;
        } while (argc > 1);
    }
    psabpf_table_entry_action(&entry, &action);

    // 5. Get entry priority

    error_code = -1;
    if (is_keyword(*argv, "priority")) {
        NEXT_ARG();
        fprintf(stderr, "Priority not supported\n");
        printf("priority: %s\n", *argv);
        goto clean_up;
    }

    error_code = psabpf_table_entry_add(&ctx, &entry);

clean_up:
    psabpf_action_free(&action);
    psabpf_table_entry_free(&entry);
    psabpf_table_entry_ctx_free(&ctx);

    return error_code;
}

int do_table_help(int argc, char **argv)
{
    (void) argc; (void) argv;

    fprintf(stderr,
            "Usage: %s table add TABLE ACTION key MATCH_KEY [data ACTION_PARAMS] [priority PRIORITY]\n"
            "       %s table update TABLE ACTION key MATCH_KEY [data ACTION_PARAMS] [priority PRIORITY]\n"
            "       %s table del TABLE [key MATCH_KEY]\n"
            "       %s table get TABLE [key MATCH_KEY]\n"
            "       %s table default TABLE set ACTION [data ACTION_PARAMS]\n"
            "       %s table default TABLE\n"
            // for far future
            "       %s table timeout TABLE set { on TTL | off }\n"
            "       %s table timeout TABLE\n"
            "\n"
            "       TABLE := { id TABLE_ID | name FILE | TABLE_FILE }\n"
            "       ACTION := { id ACTION_ID | ACTION_NAME }\n"
            "       MATCH_KEY := { EXACT_KEY | LPM_KEY | RANGE_KEY | TERNARY_KEY | none }\n"
            "       EXACT_KEY := { DATA }\n"
            "       LPM_KEY := { DATA/PREFIX_LEN }\n"
            // note: simple_switch_CLI uses '->' for range match, but this is
            // harder to write in a CLI (needs an escape sequence)
            "       RANGE_KEY := { DATA_MIN..DATA_MAX }\n"
            // note: by default '&&&' is used but it also will requires
            // an escape sequence in a CLI, so lets use '%' instead
            "       TERNARY_KEY := { DATA%%MASK }\n"
            "       ACTION_PARAMS := { DATA }\n"
            "",
            program_name, program_name, program_name, program_name, program_name, program_name,
            program_name, program_name);
    return 0;
}
