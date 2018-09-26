/**
 * Copyright 2018 Lipeng WANG (wang.lp@outlook.com)
 */
#include "../../include/uvrpc.h"
#include <unistd.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

void *pthread_stop_server(void *args) {
    uvrpcs_t *server = args;

    sleep(3600);// stop server after 1 hour
    stop_server(server);
    printf("after stop_server\n");

}

int32_t print_buf(const char *buf, size_t length, char **out_buf, size_t *out_length) {

    char internal_buf[1024];

    memcpy(internal_buf, buf, length < 1024 ? length : 1024);

    printf("recv length: %ld, content: %.13s\n", length, internal_buf);

    return 0;
}

int32_t return1(const char *buf, size_t length, char **out_buf, size_t *out_length) {
    return 1;
}

int32_t not_register(const char *buf, size_t length, char **out_buf, size_t *out_length) {
    return 2;
}

int main(int argc, char **argv) {
    uvrpcs_t *uvrpcs = start_server("localhost", 8080, 1, 4);

    //here we register 3 rpc functions
    int ret = register_function(uvrpcs, 0, print_buf);
    if (ret) {
        printf("error: %s\n", uvrpc_errstr(ret));
    }
    ret = register_function(uvrpcs, 1, return1);
    if (ret) {
        printf("error: %s\n", uvrpc_errstr(ret));
    }
    //here is a register error example: register function failed: magic code out of range, because the function code 255 is reserved
    ret = register_function(uvrpcs, 255, not_register);
    if (ret) {
        printf("error: %s\n", uvrpc_errstr(ret));
    }

    pthread_t tid;
    //create a thread to stop the server after 1 hour
    pthread_create(&tid, NULL, pthread_stop_server, uvrpcs);

    //start the server forever!
    wait_server_forever(uvrpcs);

    pthread_join(tid, NULL);

    return 0;
}