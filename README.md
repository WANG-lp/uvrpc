# UVRPC - A very fast RPC library based on [libuv](https://github.com/libuv/libuv)

## Why UVRPC (Motivation) ?

libuv is my favorite network library, RPC is an essential building block in my project.

UVRPC aims to provide a very fast and simple RPC abstraction.

## How to use it?

UVRPC only provide few functions, but can fulfilled most of your requirements.

Function list:


```c
typedef struct uvrpcs_s uvrpcs_t; // the server handle
typedef struct uvrpcc_s uvrpcc_t; // the client handle

// start a new server with a custom ip, port, eventloop number and thread number per eventloop
uvrpcs_t *start_server(char *ip, int port, int eventloop_num, int thread_num_per_eventloop);

// run the server forever (this will block the caller thread until other thread calls the stop_server function)!
int wait_server_forever(uvrpcs_t *server);

// register a RPC-procedure to this server, associate it to a function magic code (0-254)
int register_function(uvrpcs_t *uvrpc_server, unsigned char magic, int32_t (*func)(const char *, size_t, char**, size_t*));

// stop the server
int stop_server(uvrpcs_t *uvrpc_server);

// create a client with custom ip, port and thread number
uvrpcc_t *start_client(char *server_ip, int port, int thread_num);

// call a RPC-procedure (via magic code) with a provided buffer
// This function is thread-safe (you can invoke it concurrently).
int uvrpc_send(uvrpcc_t *client, char *buf, size_t length, unsigned char func_id, char** out_buf, size_t *out_length);

// stop the client
int stop_client(uvrpcc_t *client);

// get built-in error message
char *uvrpc_errstr(int uvrpc_errno);
```

## Examples

Following is a server example, it registers 3 functions with magic code 0, 1 and 255, respectively. 
The last one (255) is failed to register because the allowed magic code is [0, 254], magic code 255 has been reserved
for a built-in error indicate function.

server.c

```c
/**
 * Copyright 2018 Lipeng WANG (wang.lp@outlook.com)
 */
#include "../uvrpc.h"
#include <unistd.h>
#include <pthread.h>

#include <stdlib.h>
#include <string.h>

int32_t print_buf(const char *buf, size_t length) {

    char internal_buf[1024];

    memcpy(internal_buf, buf, length);

    printf("recv length: %ld, content: %.13s\n", length, internal_buf);

    return 0;
}

int32_t return1(const char *buf, size_t length) {
    return 1;
}

int32_t not_register(const char *buf, size_t length) {
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

    //start the server forever!
    wait_server_forever(uvrpcs);

    return 0;
}
```


Following is a client example, it calls 3 RPC-procedures(magic code 0-2) randomly . Magic code 2 is not registered. Calling an unregistered function gives a return code 255. 

client.c 

```c
/**
 * Copyright 2018 Lipeng WANG (wang.lp@outlook.com)
 */
#include "../uvrpc.h"
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
```

## Valgrind tests
valgrind is a memory leak test tool, we use it to test any memory leak in our program.
If you want to use valgrind, please compile this project with `-g -O1` instead of `-O2`,
otherwise, valgrind could not give you the correct line number of the source code. 

Following is the test results of the server side:
```bash
➜  cmake-build-debug git:(dev) ✗ valgrind --leak-check=yes ./uvrpc_server_echo
==23257== Memcheck, a memory error detector
==23257== Copyright (C) 2002-2017, and GNU GPL'd, by Julian Seward et al.
==23257== Using Valgrind-3.14.0.GIT and LibVEX; rerun with -h for copyright info
==23257== Command: ./uvrpc_server_echo
==23257== 
server thread 0 started
close server connection
close server connection
close server connection
close server connection
close server connection
close server connection
close server connection
close server connection
close server connection
close server connection
close server connection
close server connection
after stop_server
==23257== 
==23257== HEAP SUMMARY:
==23257==     in use at exit: 0 bytes in 0 blocks
==23257==   total heap usage: 278 allocs, 278 frees, 196,291 bytes allocated
==23257== 
==23257== All heap blocks were freed -- no leaks are possible
==23257== 
==23257== For counts of detected and suppressed errors, rerun with: -v
==23257== ERROR SUMMARY: 0 errors from 0 contexts (suppressed: 0 from 0)

```
 
Following is the test results of the client side:
```bash
➜  cmake-build-debug git:(dev) ✗ valgrind --leak-check=yes ./uvrpc_client_echo
==23518== Memcheck, a memory error detector
==23518== Copyright (C) 2002-2017, and GNU GPL'd, by Julian Seward et al.
==23518== Using Valgrind-3.14.0.GIT and LibVEX; rerun with -h for copyright info
==23518== Command: ./uvrpc_client_echo
==23518== 
wait for server ready...
wait for server ready...
wait for server ready...
wait for server ready...
connected to server
connected to server
connected to server
connected to server
ret: 0, buf: hello, world!
ret: 0, buf: hello, world!
ret: 0, buf: hello, world!
ret: 0, buf: hello, world!
ret: 0, buf: hello, world!
ret: 0, buf: hello, world!
ret: 0, buf: hello, world!
ret: 0, buf: hello, world!
ret: 0, buf: hello, world!
ret: 0, buf: hello, world!
==23518== 
==23518== HEAP SUMMARY:
==23518==     in use at exit: 0 bytes in 0 blocks
==23518==   total heap usage: 91 allocs, 91 frees, 53,914 bytes allocated
==23518== 
==23518== All heap blocks were freed -- no leaks are possible
==23518== 
==23518== For counts of detected and suppressed errors, rerun with: -v
==23518== ERROR SUMMARY: 0 errors from 0 contexts (suppressed: 0 from 0)
```

## License
MIT
