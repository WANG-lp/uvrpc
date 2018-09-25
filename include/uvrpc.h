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
    uv_loop_t **work_loops;
    struct sockaddr_storage *addr;
};

//server object
struct uvrpcs_s {
    struct uvrpc_s base;

    int32_t (*register_func_table[256])(const char *, size_t);

};

//client object
struct uvrpcc_s {
    struct uvrpc_s base;
    struct blockQueue_s *bq;
};

typedef struct uvrpcs_s uvrpcs_t; // the server handle
typedef struct uvrpcc_s uvrpcc_t; // the client handle

// create a new server with a custom ip, port and thread number
uvrpcs_t *init_server(char *ip, int port, int thread_num);

// run the server forever!
int run_server_forever(uvrpcs_t *server);

// register a RPC-procedure to this server, associate it to a function magic code (0-254)
int register_function(uvrpcs_t *uvrpc_server, unsigned char magic, int32_t (*func)(const char *, size_t));

// stop this server (not implemented yet)
int stop_server(uvrpcs_t *uvrpc_server);

// create a client with custom ip, port and thread number
uvrpcc_t *start_client(char *server_ip, int port, int thread_num);

// call a RPC-procedure (via magic code) with a provided buffer
// This function is thread-safe (you can invoke it concurrently).
int uvrpc_send(uvrpcc_t *client, char *buf, size_t length, unsigned char func_id);

// stop this client
int stop_client(uvrpcc_t *client);

// get built-in error message
char *uvrpc_errstr(int uvrpc_errno);


#endif