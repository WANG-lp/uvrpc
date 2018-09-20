/**
 * Copyright 2018 Lipeng WANG (wang.lp@outlook.com)
 */
#include "../uvrpc.h"
#include <unistd.h>
#include <pthread.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    if(argc < 3){
        printf("Usage: %s server_ip server_port\n", argv[0]);
        exit(1);
    }
    uvrpcc_t *uvrpcc = start_client(argv[1], atoi(argv[2]), 1);

    char buf[] = "hello, world!";

    int ret = uvrpc_send(uvrpcc, buf, 13, 1);
    printf("ret: %d\n", ret);

    //sleep(3);
    return 0;
}