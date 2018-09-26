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

    char *out_buf = NULL;
    size_t out_length = 0;

    for (int i = 0; i < 10; i++) {
        int ret = uvrpc_send(uvrpcc, buf, 13, 3, &out_buf, &out_length);
        printf("ret: %d, buf: %.*s\n", ret, (int) out_length, out_buf);

        if(out_buf)
            free(out_buf); //remember to free memory here!
    }

    stop_client(uvrpcc);

    return 0;
}