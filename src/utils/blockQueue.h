/**
 * Copyright 2018 Lipeng WANG (wang.lp@outlook.com)
 */

#pragma once

#include <uv.h>

typedef struct {
    uv_mutex_t *mutex;
    uv_cond_t *cond;
    size_t head, tail, max_size;
    int tail_ahead;
    void **item_array;
} blockQueue;

blockQueue *init_blockQueue(int size);

void free_blockQueue(blockQueue *bq);

int test_full(blockQueue *bq);

int test_empty(blockQueue *bq);

int bq_push(blockQueue *bq, void *http_req);

void *bq_pull(blockQueue *bq);