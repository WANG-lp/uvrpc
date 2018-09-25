/**
 * Copyright 2018 Lipeng WANG (wang.lp@outlook.com)
 */
#include "../../include/uvrpc.h"
#include <unistd.h>
#include <pthread.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    uvrpcc_t *uvrpcc = start_client("localhost", 8080, 4);

    char buf[] = "hello, world!";
    srand(time(NULL));

    for (int i = 0; i < 10; i++) {
        int rand_n = rand();
        unsigned char func_id;

        func_id = rand_n % 3;

        int ret = uvrpc_send(uvrpcc, buf, 13, func_id);
        printf("ret: %d\n", ret);
    }

    stop_client(uvrpcc);

    return 0;
}