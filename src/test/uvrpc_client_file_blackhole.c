/**
 * Copyright 2018 Lipeng WANG (wang.lp@outlook.com)
 */
#include "../../include/uvrpc.h"
#include <unistd.h>
#include <pthread.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include <string.h>

#define FILE_SIZE (4L*1024*1024)
#define ITER_NUM (1000L)

int64_t get_wall_time() {
    struct timeval time;
    if (gettimeofday(&time, NULL)) {
        //  Handle error
        return 0;
    }
    return time.tv_sec * 1000000 + time.tv_usec;
}


int main(int argc, char **argv) {
    if (argc < 3) {
        printf("Usage: %s server_ip server_port\n", argv[0]);
        exit(1);
    }
    uvrpcc_t *uvrpcc = start_client(argv[1], atoi(argv[2]), 1);

    char *buf = malloc(sizeof(char) * FILE_SIZE);
    memset(buf, 0, sizeof(char) * FILE_SIZE);

    int64_t start_time = get_wall_time();
    for (size_t i = 0; i < ITER_NUM; i++) {
        int64_t chunk_start_time = get_wall_time();
        int ret = uvrpc_send(uvrpcc, buf, FILE_SIZE, 1, NULL, NULL);
        int64_t chunk_end_time = get_wall_time();
        if(ret != 0){
            continue;
        }
        if(i % 100 == 0) {
            double chunk_time = (chunk_end_time - chunk_start_time) / 1000.0;
            printf("time: %lfms, send size %ld, speed: %.3lfGB/s, ret: %d\n", chunk_time, FILE_SIZE,
                   (((double) FILE_SIZE) / (1024 * 1024 * 1024)) / (chunk_time / 1000.0), ret);
        }
    }
    int64_t end_time = get_wall_time();

    double total_time_in_ms = (end_time - start_time) / 1000.0;

    printf("Total time: %.3lfms, send size: %ld, speed: %.3lfGB/s\n", total_time_in_ms, ITER_NUM * FILE_SIZE,
           ((double) ITER_NUM * FILE_SIZE / (1024 * 1024 * 1024)) / (total_time_in_ms / 1000.0));

    stop_client(uvrpcc);
    free(buf);
    return 0;
}