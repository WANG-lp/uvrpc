/**
 * Copyright 2018 Lipeng WANG (wang.lp@outlook.com)
 */
#include "blockQueue.h"
#include <stdlib.h>

blockQueue *init_blockQueue(int size) {
    blockQueue *bq = (blockQueue *) malloc(sizeof(blockQueue));
    bq->mutex = malloc(sizeof(uv_mutex_t));
    bq->cond = malloc(sizeof(uv_cond_t));
    uv_mutex_init(bq->mutex);
    uv_cond_init(bq->cond);
    bq->item_array = (void **) malloc(sizeof(void *) * size);
    bq->head = bq->tail = 0;
    bq->max_size = (size_t) size;
    bq->tail_ahead = 0;
    return bq;
}

void free_blockQueue(blockQueue *bq) {
    if (bq != NULL) {
        uv_cond_destroy(bq->cond);
        uv_mutex_destroy(bq->mutex);
        free(bq->cond);
        free(bq->mutex);
        free(bq->item_array);
        free(bq);
    }
}

int test_full(blockQueue *bq) {
    if (bq->head == bq->tail && bq->tail_ahead == 1) {
        return 1;
    }
    return 0;
}

int test_empty(blockQueue *bq) {
    if (bq->head == bq->tail && bq->tail_ahead == 0) {
        return 1;
    }
    return 0;
}

//return 1 if full, 0 if success
int bq_push(blockQueue *bq, void *item) {
    uv_mutex_lock(bq->mutex);
    if (test_full(bq)) {
        uv_mutex_unlock(bq->mutex);
        return 1;// drop if full
    }
    bq->item_array[bq->tail] = item;
    bq->tail++;
    if (bq->tail == bq->max_size) {
        bq->tail = 0;
        bq->tail_ahead = 1;
    }
    uv_mutex_unlock(bq->mutex);
    uv_cond_signal(bq->cond);//signal we have one new element
    return 0;
}

void *bq_pull(blockQueue *bq) {
    void *item = NULL;
    uv_mutex_lock(bq->mutex);
    while (test_empty(bq)) {
        uv_cond_wait(bq->cond, bq->mutex);
    }

    item = bq->item_array[bq->head];
    bq->head++;
    if (bq->head == bq->max_size) {
        bq->head = 0;
        bq->tail_ahead = 0;
    }
    uv_mutex_unlock(bq->mutex);
    return item;
}