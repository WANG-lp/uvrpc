/**
 * Copyright 2018 Lipeng WANG (wang.lp@outlook.com)
 */
#include "../../include/uvrpc.h"
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

int32_t echo(const char *buf, size_t length, char** out_buf, size_t *out_length) {
    *out_buf = malloc(sizeof(char) * length);
    memcpy(*out_buf, buf, length * sizeof(char));
    *out_length = length;
    return 0;
}

int main(int argc, char **argv) {
    uvrpcs_t *uvrpcs = start_server("localhost", 8080, 1, 4);

    int ret = register_function(uvrpcs, 3, echo);
    if (ret) {
        printf("error: %s\n", uvrpc_errstr(ret));
    }

    //start the server forever!
    wait_server_forever(uvrpcs);
    return 0;
}