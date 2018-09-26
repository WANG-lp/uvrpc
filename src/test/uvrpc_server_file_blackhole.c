/**
 * Copyright 2018 Lipeng WANG (wang.lp@outlook.com)
 */
#include "../../include/uvrpc.h"
#include <unistd.h>
#include <pthread.h>

#include <stdlib.h>
#include <string.h>

static __thread size_t total_size = 0;

int32_t file_blackhole(const char *buf, size_t length, char** out_buf, size_t *out_length) {
    total_size += length;

//    printf("file size %ld\n", total_size);
    return 0;
}

int main(int argc, char **argv) {

    if (argc < 2) {
        printf("Usage: %s 8080\n", argv[0]);
        exit(1);
    }

    uvrpcs_t *uvrpcs = start_server("0.0.0.0", atoi(argv[1]), 1, 4);

    int ret = register_function(uvrpcs, 1, file_blackhole);
    if (ret) {
        printf("error: %s\n", uvrpc_errstr(ret));
    }

    //start the server forever!
    wait_server_forever(uvrpcs);

    return 0;
}