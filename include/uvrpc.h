/**
 * Copyright 2018 Lipeng WANG (wang.lp@outlook.com)
 */
#ifndef UVRPC_H
#define UVRPC_H

#include <stdlib.h>
#include <uv.h>

struct blockQueue_s;

#define UVRPC_MAGIC (0xcffe)

//common things
struct uvrpc_s {
    int thread_count;
    uv_thread_t *tids;
    void **thread_data;
    struct sockaddr_storage *addr;
};

//server object
struct uvrpcs_s {
    struct uvrpc_s base;
    volatile int status;

    int32_t (*register_func_table[256])(const char *, size_t);

};

//client object
struct uvrpcc_s {
    struct uvrpc_s base;
    struct blockQueue_s *bq;
};

typedef struct uvrpcs_s uvrpcs_t; // the server handle
typedef struct uvrpcc_s uvrpcc_t; // the client handle

// start a new server with a custom ip, port, eventloop number and thread number per eventloop
uvrpcs_t *start_server(char *ip, int port, int eventloop_num, int thread_num_per_eventloop);

// run the server forever (this will block the caller thread until other thread calls the stop_server function)!
int wait_server_forever(uvrpcs_t *server);

// register a RPC-procedure to this server, associate it to a function magic code (0-254)
int register_function(uvrpcs_t *uvrpc_server, unsigned char magic, int32_t (*func)(const char *, size_t));

// stop the server
int stop_server(uvrpcs_t *uvrpc_server);

// create a client with custom ip, port and thread number
uvrpcc_t *start_client(char *server_ip, int port, int thread_num);

// call a RPC-procedure (via magic code) with a provided buffer
// This function is thread-safe (you can invoke it concurrently).
int uvrpc_send(uvrpcc_t *client, char *buf, size_t length, unsigned char func_id);

// stop the client
int stop_client(uvrpcc_t *client);

// get built-in error message
char *uvrpc_errstr(int uvrpc_errno);

#endif