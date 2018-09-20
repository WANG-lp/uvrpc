/**
 * Copyright 2018 Lipeng WANG (wang.lp@outlook.com)
 */
#include "uvrpc.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "./utils/int2bytes.h"

#define DEFAULT_BACKLOG 4096
#define MAX_TCP_BUFFER_SIZE (4096)
#define REQ_HEADER_LENGTH (19)
#define REP_HEADER_LENGTH (15)

struct _uvrpc_server_thread_s {
    uvrpcs_t *uvrpcs;
    int thread_id;
    uv_loop_t *work_loop;
    uv_tcp_t *tcp_server;
};

struct _uvrpc_client_thread_s {
    uvrpcc_t *uvrpcc;
    int thread_id;
    uv_loop_t *work_loop;
    uv_connect_t *server_conn;
    uv_tcp_t *tcp_server;
    char *buf;
    size_t max_length;
    size_t current_length;

    uv_async_t *async_t;
    char *send_buf;
    size_t send_length;

    uint64_t result_req_id;
    int32_t ret_result;
    uv_mutex_t *result_mutex;
    uv_cond_t *result_cond;
};

struct _uvrpc_server_msg_s {
    char *buf;
    uint64_t req_id;
    size_t buf_max_length;
    size_t current_length;
    unsigned char func_id;
};

struct _uv_rpc_server_connection_s {
    struct _uvrpc_server_msg_s *msg;
    struct _uvrpc_server_thread_s *uvrpc_server_thread_s;
};

typedef struct _uvrpc_server_thread_s _uvrpc_server_thread_t;
typedef struct _uvrpc_client_thread_s _uvrpc_client_thread_t;
typedef struct _uvrpc_server_msg_s _uvrpc_server_msg_t;
typedef struct _uv_rpc_server_connection_s _uv_rpc_server_connection_t;

static size_t global_count = 0;
uv_mutex_t global_mutex;

_uvrpc_server_msg_t *_make_new_msg(uint64_t req_id, size_t size, unsigned char func_id) {
    _uvrpc_server_msg_t *msg = malloc(sizeof(_uvrpc_server_msg_t));
    if (msg == NULL) {
        goto __UVRPC_A_ERROR;
    }

    msg->buf = malloc(sizeof(char) * size);
    if (msg->buf == NULL) {
        free(msg);
        goto __UVRPC_A_ERROR;
    }
    msg->req_id = req_id;
    msg->buf_max_length = size;
    msg->current_length = 0;
    msg->func_id = func_id;
    return msg;

    __UVRPC_A_ERROR:
    printf("cannot alloc new buffer\n");
    return NULL;
}

_uvrpc_server_msg_t *_warp_msg(uint64_t req_id, char *buf, size_t length) {
    _uvrpc_server_msg_t *msg = malloc(sizeof(_uvrpc_server_msg_t));
    if (msg == NULL) {
        goto __UVRPC_A_ERROR;
    }
    msg->buf = buf;
    msg->req_id = req_id;
    msg->buf_max_length = length;
    msg->current_length = length;
    msg->func_id = 0;
    return msg;

    __UVRPC_A_ERROR:
    printf("cannot alloc new buffer\n");
    return NULL;
}

void _free_msg(_uvrpc_server_msg_t *msg) {
    free(msg->buf);
    free(msg);
}

void _free_handle(uv_handle_t *handle) {
    free(handle);
}

void _close_server_connection(uv_handle_t *handle) {
    printf("close server connection\n");
    _uv_rpc_server_connection_t *client_connection = handle->data;
    _uvrpc_server_msg_t *msg = client_connection->msg;
    if (msg != NULL) {
        _free_msg(msg);
    }
    free(client_connection);
    free(handle);
}

void alloc_new_buffer(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf) {
    buf->base = malloc(sizeof(char) * suggested_size);
    buf->len = suggested_size;
}

void reuse_client_thread_buffer(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf) {
    _uvrpc_client_thread_t *client_thread_data = handle->data;

    buf->base = client_thread_data->buf + client_thread_data->current_length;
    buf->len = client_thread_data->max_length - client_thread_data->current_length;
}

void reuse_server_thread_buffer(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf) {
    _uv_rpc_server_connection_t *connection_data = handle->data;
    if (connection_data->msg == NULL) {
        connection_data->msg = _make_new_msg(0, MAX_TCP_BUFFER_SIZE, 0);
    }
    buf->base = connection_data->msg->buf + connection_data->msg->current_length;
    buf->len = connection_data->msg->buf_max_length - connection_data->msg->current_length;
}

void _server_after_write_response(uv_write_t *write_req, int status) {
    if(status!=0){
        printf("write back to client error: %s\n", uv_strerror(status));
    }
    free(write_req->data);
    free(write_req);
}

void _server_run_func(uv_stream_t *stream, _uv_rpc_server_connection_t *client_connection, _uvrpc_server_msg_t *msg) {
    int32_t ret = (*(client_connection->uvrpc_server_thread_s->uvrpcs->register_func_table[msg->func_id]))(
            msg->buf + REQ_HEADER_LENGTH,
            msg->current_length - REQ_HEADER_LENGTH);

    char *result = malloc(sizeof(char) * 15);
    uint16_to_bytes(UVRPC_MAGIC, (unsigned char *) result);
    result[2] = msg->func_id;
    uint64_to_bytes(msg->req_id, (unsigned char *) (result + 3));
    uint32_to_bytes((uint32_t) ret, (unsigned char *) (result + 11));

    uv_write_t *write_req = malloc(sizeof(uv_write_t));
    write_req->data = result;
    uv_buf_t buf1 = uv_buf_init(result, 15);

    uv_write(write_req, stream, &buf1, 1, _server_after_write_response);
    _free_msg(msg);
    client_connection->msg = NULL;
}

void _server_read_msg_data(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf) {
    _uv_rpc_server_connection_t *client_connection = stream->data;
    if (nread > 0 && buf->base != NULL && buf->len > 0) {
        client_connection->msg->current_length += nread;
        _uvrpc_server_msg_t *msg = client_connection->msg;

        if (client_connection->msg->current_length < 2)
            return;
        uint16_t magic_code = bytes_to_uint16((unsigned char *) msg->buf);

        if (magic_code != UVRPC_MAGIC) {
            printf("Error magic code!\n");
            uv_close((uv_handle_t *) stream, _close_server_connection);
            return;
        }

        if (client_connection->msg->current_length < REQ_HEADER_LENGTH)
            return;

        uint64_t data_length = bytes_to_uint64((unsigned char *) (msg->buf + 11));

        if (data_length + REQ_HEADER_LENGTH > msg->buf_max_length) {
            msg->buf = realloc(msg->buf, sizeof(char) * (data_length + REQ_HEADER_LENGTH));
            msg->buf_max_length = data_length + REQ_HEADER_LENGTH;
        }

        if (msg->current_length < data_length + REQ_HEADER_LENGTH)
            return;

        unsigned char func_id = (unsigned char) msg->buf[2];
        uint64_t req_id = bytes_to_uint64((unsigned char *) (msg->buf + 3));
        msg->req_id = req_id;
        msg->func_id = func_id;

        if (client_connection->uvrpc_server_thread_s->uvrpcs->register_func_table[msg->func_id] == NULL) {
            msg->func_id = 255;
        }
        _server_run_func(stream, client_connection, msg);
    } else if (nread < 0) {
        if (nread != UV_EOF) {
            printf("Read error %s\n", uv_strerror(nread));
        }
        uv_close((uv_handle_t *) stream, _close_server_connection);
    }
}

void _server_on_new_connection(uv_stream_t *server, int status) {
    if (status != 0) {
        printf("New connection error %s\n", uv_strerror(status));
        return;
    }
    _uvrpc_server_thread_t *uvrpc_server = server->data;

    uv_tcp_t *client = (uv_tcp_t *) malloc(sizeof(uv_tcp_t));
    uv_tcp_init(uvrpc_server->work_loop, client);
    uv_tcp_keepalive(client, 1, 60);
    uv_tcp_nodelay(client, 1);
    _uv_rpc_server_connection_t *client_connection = malloc(sizeof(_uv_rpc_server_connection_t));
    client_connection->uvrpc_server_thread_s = uvrpc_server;
    client_connection->msg = NULL;
    client->data = client_connection;
    if (uv_accept(server, (uv_stream_t *) client) == 0) {
        uv_read_start((uv_stream_t *) client, reuse_server_thread_buffer, _server_read_msg_data);
    } else {
        printf("failed to accept connection");
        uv_close((uv_handle_t *) client, _free_handle);
    }
}

void server_cb(void *data) {
    _uvrpc_server_thread_t *uvrpc_thread_data = data;
    printf("server thread %d started\n", uvrpc_thread_data->thread_id);
    uvrpc_thread_data->tcp_server = malloc(sizeof(uv_tcp_t));
    uvrpc_thread_data->work_loop = malloc(sizeof(uv_loop_t));
    uv_loop_init(uvrpc_thread_data->work_loop);
    uvrpc_thread_data->uvrpcs->base.work_loops[uvrpc_thread_data->thread_id] = uvrpc_thread_data->work_loop;

    uv_tcp_init_ex(uvrpc_thread_data->work_loop, uvrpc_thread_data->tcp_server, AF_INET);
    uv_os_fd_t fd = uvrpc_thread_data->tcp_server->io_watcher.fd;
    int optval = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(optval));
    uv_tcp_nodelay(uvrpc_thread_data->tcp_server, 1);
    int r = uv_tcp_bind(uvrpc_thread_data->tcp_server, (const struct sockaddr *) uvrpc_thread_data->uvrpcs->base.addr, 0);
    if (r) {
        goto __UVRPC_L_ERROR;
    }
    uvrpc_thread_data->tcp_server->data = uvrpc_thread_data;
    r = uv_listen((uv_stream_t *) uvrpc_thread_data->tcp_server, DEFAULT_BACKLOG, _server_on_new_connection);

    __UVRPC_L_ERROR:
    if (r) {
        printf("ERROR: %s\n", uv_strerror(r));
        exit(1);
    }
    uv_run(uvrpc_thread_data->work_loop, UV_RUN_DEFAULT);
}

int32_t __return_error(const char *buf, size_t length) {
    return 255;
}

uvrpcs_t *init_server(char *ip, int port, int thread_num) {
    uvrpcs_t *server = malloc(sizeof(uvrpcs_t));
    memset(server->register_func_table, 0, sizeof(int32_t (*[256])(char *, size_t)));
    server->base.tids = malloc(sizeof(uv_thread_t) * thread_num);;
    server->base.thread_count = thread_num;
    server->base.work_loops = malloc(sizeof(uv_loop_t *) * thread_num);
    server->base.addr = malloc(sizeof(struct sockaddr_storage));

    uv_ip4_addr(ip, port, (struct sockaddr_in *) server->base.addr);

    for (int i = 0; i < thread_num; i++) {
        _uvrpc_server_thread_t *uvrpc_server = malloc(sizeof(_uvrpc_server_thread_t));
        uvrpc_server->thread_id = i;
        uvrpc_server->uvrpcs = server;
        uv_thread_create(&(server->base.tids[i]), server_cb, uvrpc_server);
    }
    server->register_func_table[255] = __return_error;
    return server;
}

int run_server_forever(uvrpcs_t *server) {
    for (int i = 0; i < server->base.thread_count; i++) {
        uv_thread_join(&(server->base.tids[i]));
    }
    return 0;
}

int register_function(uvrpcs_t *uvrpc_server, unsigned char magic, int32_t (*func)(const char *, size_t)) {
    if (magic < 255 && magic >= 0) {
        if (uvrpc_server->register_func_table[magic] != NULL) {
            return 0xee01;
        }
        uvrpc_server->register_func_table[magic] = func;
        return 0;
    } else {
        return 0xee00;
    }
}

int stop_server(uvrpcs_t *uvrpc_server) {
    return 0;
}
void _uvrpc_client_test_server_connection(_uvrpc_client_thread_t *client_thread_data);

void _client_after_read_result(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf) {
    _uvrpc_client_thread_t *client_thread_data = stream->data;
    if (nread > 0) {
        client_thread_data->current_length += nread;
        if (client_thread_data->current_length < 2)
            return;
        uint16_t magic_code = bytes_to_uint16((unsigned char *) client_thread_data->buf);
        if (magic_code != UVRPC_MAGIC) {
            printf("Error magic code!\n");
            uv_close((uv_handle_t *) stream, _free_handle);
            client_thread_data->current_length = 0;
            return;
        }
        if (client_thread_data->current_length < REP_HEADER_LENGTH)
            return;
        unsigned char func_id = (unsigned char) client_thread_data->buf[2];
        uint64_t req_id = bytes_to_uint64((unsigned char *) (client_thread_data->buf + 3));
        int32_t result = (int32_t) bytes_to_uint32((unsigned char *) (client_thread_data->buf + 11));

        //printf("func_if: %d, req_id: %ld, ret_code: %d\n", func_id, req_id, result);

        uv_mutex_lock(client_thread_data->result_mutex);
        client_thread_data->result_req_id = req_id;
        client_thread_data->ret_result = result;
        uv_mutex_unlock(client_thread_data->result_mutex);
        uv_cond_signal(client_thread_data->result_cond);

        client_thread_data->current_length = 0;

    } else if (nread < 0) {
        if (nread != UV_EOF) {
            printf("Read error %s\n", uv_strerror(nread));
        }
        uv_close((uv_handle_t *) stream, _free_handle);
        client_thread_data->current_length = 0;
        printf("lost connection, retry...\n");
//        sleep(1);
        _uvrpc_client_test_server_connection(client_thread_data);
    }
}

void _uvrpc_client_on_connection(uv_connect_t *connection, int status) {
    _uvrpc_client_thread_t *client_thread_data = connection->data;
    if (status == 0) {
        printf("connected to server\n");
        connection->handle->data = client_thread_data;
        uv_read_start(connection->handle, reuse_client_thread_buffer, _client_after_read_result);
        if(client_thread_data->result_req_id < INT64_MAX) {
            client_thread_data->ret_result = 255;
            uv_cond_broadcast(client_thread_data->result_cond);
        }
    } else {
        printf("server not ready, retry in 1s...\n");
        sleep(1);
        _uvrpc_client_test_server_connection(client_thread_data);
    }
}

void _uvrpc_client_test_server_connection(_uvrpc_client_thread_t *client_thread_data) {

    printf("wait for server ready...\n");
    client_thread_data->tcp_server = malloc(sizeof(uv_tcp_t));
    client_thread_data->server_conn->data = client_thread_data;
    uv_tcp_init(client_thread_data->work_loop, client_thread_data->tcp_server);
    uv_tcp_nodelay(client_thread_data->tcp_server, 1);
    uv_tcp_connect(client_thread_data->server_conn, client_thread_data->tcp_server,
                   (const struct sockaddr *) client_thread_data->uvrpcc->base.addr, _uvrpc_client_on_connection);
}

void client_cb(void *args) {
    _uvrpc_client_thread_t *client_thread_data = args;
    client_thread_data->server_conn = malloc(sizeof(uv_connect_t));

    _uvrpc_client_test_server_connection(client_thread_data);

    uv_run(client_thread_data->work_loop, UV_RUN_DEFAULT);
}

void _client_after_send(uv_write_t *write1, int status) {
    if (status != 0) {
        printf("write to server failed. %s\n", uv_strerror(status));
    }
    free(write1);
}

void async_send_to_server(uv_async_t *handle) {
    _uvrpc_client_thread_t *client_thread_data = handle->data;
    uv_write_t *write_req = malloc(sizeof(uv_write_t));

    uv_buf_t uvbuf = uv_buf_init(client_thread_data->send_buf, client_thread_data->send_length);

    write_req->data = client_thread_data->send_buf;
    uv_write(write_req, (uv_stream_t *) client_thread_data->tcp_server, &uvbuf, 1, _client_after_send);
}

uvrpcc_t *start_client(char *server_URL, int port, int thread_num) {
    uv_mutex_init(&global_mutex);
    global_count = 0;

    uvrpcc_t *uvrpc_client = malloc(sizeof(uvrpcc_t));
    uvrpc_client->base.thread_count = thread_num;
    uvrpc_client->base.tids = malloc(sizeof(uv_thread_t) * thread_num);
    uvrpc_client->base.addr = malloc(sizeof(struct sockaddr_storage));
    uvrpc_client->bq = init_blockQueue(thread_num);
    uv_ip4_addr(server_URL, port, (struct sockaddr_in *) uvrpc_client->base.addr);

    for (int i = 0; i < thread_num; i++) {
        _uvrpc_client_thread_t *client_thread_data = malloc(sizeof(_uvrpc_client_thread_t));
        client_thread_data->work_loop = malloc(sizeof(uv_loop_t));
        uv_loop_init(client_thread_data->work_loop);
        client_thread_data->thread_id = i;
        client_thread_data->uvrpcc = uvrpc_client;
        client_thread_data->ret_result = -1;
        client_thread_data->result_req_id = UINT64_MAX;
        client_thread_data->result_cond = malloc(sizeof(uv_cond_t));
        client_thread_data->result_mutex = malloc(sizeof(uv_mutex_t));
        uv_mutex_init(client_thread_data->result_mutex);
        uv_cond_init(client_thread_data->result_cond);
        client_thread_data->buf = malloc(sizeof(char) * MAX_TCP_BUFFER_SIZE);
        client_thread_data->max_length = MAX_TCP_BUFFER_SIZE;
        client_thread_data->current_length = 0;
        uv_thread_create(&(uvrpc_client->base.tids[i]), client_cb, client_thread_data);
        bq_push(uvrpc_client->bq, client_thread_data);
        client_thread_data->async_t = malloc(sizeof(uv_async_t));
        client_thread_data->async_t->data = client_thread_data;
        uv_async_init(client_thread_data->work_loop, client_thread_data->async_t, async_send_to_server);
    }
    return uvrpc_client;
}

char *_client_make_request(char *buf, size_t length, unsigned char func_id, size_t *new_length, uint64_t *req_id) {
    *new_length = length + REQ_HEADER_LENGTH;
    char *internal_buf = malloc(sizeof(char) * (*new_length));
    memcpy(internal_buf + REQ_HEADER_LENGTH, buf, length);

    uint16_to_bytes(UVRPC_MAGIC, (unsigned char *) internal_buf);
    internal_buf[2] = func_id;

    uv_mutex_lock(&global_mutex);
    global_count++;
    *req_id = global_count;
    uv_mutex_unlock(&global_mutex);

    uint64_to_bytes(*req_id, (unsigned char *) (internal_buf + 3));
    uint64_to_bytes(length, (unsigned char *) (internal_buf + 11));

    return internal_buf;
}

int uvrpc_send(uvrpcc_t *client, char *buf, size_t length, unsigned char func_id) {
    _uvrpc_client_thread_t *client_thread_data = bq_pull(client->bq);

    size_t new_length;
    uint64_t req_id;
    char *internal_buf = _client_make_request(buf, length, func_id, &new_length, &req_id);

    client_thread_data->send_buf = internal_buf;
    client_thread_data->send_length = new_length;

    uv_async_send(client_thread_data->async_t);

    int result;
    if (client_thread_data->result_req_id != req_id) {
        uv_mutex_lock(client_thread_data->result_mutex);
        if (client_thread_data->result_req_id != req_id) {
            uv_cond_wait(client_thread_data->result_cond, client_thread_data->result_mutex);
        }
        uv_mutex_unlock(client_thread_data->result_mutex);
    }
    free(internal_buf);
    result = (int) client_thread_data->ret_result;
    bq_push(client->bq, client_thread_data);
    return result;
}

int stop_client(uvrpcc_t *client) {
    return 0;
}

char *uvrpc_errstr(int uvrpc_errno) {
    switch (uvrpc_errno) {
        case 0:
            return "no error was found";
        case 255:
            return "requested function was not register";
        case 0xee00:
            return "register function failed: magic code out of range";
        case 0xee01:
            return "register function failed: magic code already registered";
        default:
            return "unknown error";
    }
}