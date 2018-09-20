/**
 * Copyright 2018 Lipeng WANG (wang.lp@outlook.com)
 */
#include "../uvrpc.h"
#include <unistd.h>
#include <pthread.h>
#include <stdlib.h>

#define FILE_SIZE (1L * 1024 * 1024 * 1024 + 1)

int main(int argc, char **argv) {
    if (argc < 3) {
        printf("Usage: %s server_ip server_port\n", argv[0]);
        exit(1);
    }
    uvrpcc_t *uvrpcc = start_client(argv[1], atoi(argv[2]), 1);

    char *buf = malloc(sizeof(char) * FILE_SIZE);

    for (int i = 0; i < 100; i++) {
        int ret = uvrpc_send(uvrpcc, buf, FILE_SIZE, 1);
        printf("ret: %d\n", ret);
    }
    //sleep(3);
    return 0;
}