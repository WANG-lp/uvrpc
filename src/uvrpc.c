/**
 * Copyright 2018 Lipeng WANG (wang.lp@outlook.com)
 */
#include "../include/uvrpc.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "./utils/int2bytes.h"
#include "./utils/blockQueue.h"

#define DEFAULT_BACKLOG 4096
#define MAX_TCP_BUFFER_SIZE (4096)
#define REQ_HEADER_LENGTH (19)
#define REP_HEADER_LENGTH (23)

struct _uvrpc_server_thread_s {
    uvrpcs_t *uvrpcs;
    int thread_id;
    uv_loop_t *work_loop;
    uv_tcp_t *tcp_server;

    uv_async_t *async_stop_t;
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

    uv_async_t *async_stop_t;
    uv_async_t *async_t;
    char *send_buf;
    size_t send_length;

    char *result_buf;
    size_t result_length;
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

struct _uvrpc_req_object_s {
    uv_stream_t *stream;
    struct _uvrpc_server_msg_s *msg;
    char *result_buf;
    size_t result_length;
    int32_t ret_code;
};

typedef struct _uvrpc_server_thread_s _uvrpc_server_thread_t;
typedef struct _uvrpc_client_thread_s _uvrpc_client_thread_t;
typedef struct _uvrpc_server_msg_s _uvrpc_server_msg_t;
typedef struct _uv_rpc_server_connection_s _uv_rpc_server_connection_t;
typedef struct _uvrpc_req_object_s _uvrpc_req_object_t;

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

void _free_msg(_uvrpc_server_msg_t *msg) {
    if (msg->buf != NULL)
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

void reuse_client_thread_buffer(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf) {
    _uvrpc_client_thread_t *client_thread_data = handle->data;
    if (client_thread_data->buf == NULL) {
        client_thread_data->buf = malloc(sizeof(char) * MAX_TCP_BUFFER_SIZE);
        client_thread_data->max_length = MAX_TCP_BUFFER_SIZE;
        client_thread_data->current_length = 0;
    }
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
    if (status != 0) {
        printf("write back to client error: %s\n", uv_strerror(status));
    }
    free(write_req->data);
    free(write_req);
}

void _server_run_func(_uvrpc_req_object_t *req_object, _uv_rpc_server_connection_t *client_connection,
                      _uvrpc_server_msg_t *msg) {

    char *out_buf = NULL;
    size_t out_length = 0;

    int32_t ret = (*(client_connection->uvrpc_server_thread_s->uvrpcs->register_func_table[msg->func_id]))(
            msg->buf + REQ_HEADER_LENGTH,
            msg->current_length - REQ_HEADER_LENGTH, &out_buf, &out_length);

    char *result = malloc(sizeof(char) * (REP_HEADER_LENGTH + out_length));
    uint16_to_bytes(UVRPC_MAGIC, (unsigned char *) result);
    result[2] = msg->func_id;
    uint64_to_bytes(msg->req_id, (unsigned char *) (result + 3));
    uint32_to_bytes((uint32_t) ret, (unsigned char *) (result + 11));
    uint64_to_bytes(out_length, (unsigned char *) (result + 15));

    if (out_buf != NULL && out_length > 0) {
        memcpy(result + REP_HEADER_LENGTH, out_buf, out_length);
    }

    if (out_buf != NULL)
        free(out_buf);

    req_object->result_buf = result;
    req_object->ret_code = ret;
    req_object->result_length = REP_HEADER_LENGTH + out_length;
}

void _after_worker_finish(uv_work_t *req, int status) {
    _uvrpc_req_object_t *req_object = req->data;
    uv_stream_t *stream = req_object->stream;
    _uv_rpc_server_connection_t *client_connection = stream->data;

    uv_write_t *write_req = malloc(sizeof(uv_write_t));
    write_req->data = req_object->result_buf;
    uv_buf_t buf1 = uv_buf_init(req_object->result_buf, req_object->result_length);
    uv_write(write_req, stream, &buf1, 1, _server_after_write_response);

    free(req_object);
    free(req);
}

void _worker_thread_job(uv_work_t *req) {
    _uvrpc_req_object_t *req_object = req->data;
    uv_stream_t *stream = req_object->stream;
    _uv_rpc_server_connection_t *client_connection = stream->data;

    _server_run_func(req_object, client_connection, req_object->msg);
    _free_msg(req_object->msg);
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
        uv_work_t *work_req = malloc(sizeof(uv_work_t));
        _uvrpc_req_object_t *req_object = malloc(sizeof(_uvrpc_req_object_t));
        req_object->stream = stream;
        req_object->msg = msg;
        work_req->data = req_object;

        uv_queue_work(client_connection->uvrpc_server_thread_s->work_loop, work_req, _worker_thread_job,
                      _after_worker_finish);
        client_connection->msg = NULL;
    } else if (nread < 0) {
        if (nread != UV_EOF) {
            printf("Read error %s\n", uv_strerror(nread));
        }
        if (!uv_is_closing((const uv_handle_t *) stream))
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

    uv_tcp_init_ex(uvrpc_thread_data->work_loop, uvrpc_thread_data->tcp_server, AF_INET);
    uv_os_fd_t fd = uvrpc_thread_data->tcp_server->io_watcher.fd;
    int optval = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(optval));
    uv_tcp_nodelay(uvrpc_thread_data->tcp_server, 1);
    int r = uv_tcp_bind(uvrpc_thread_data->tcp_server, (const struct sockaddr *) uvrpc_thread_data->uvrpcs->base.addr,
                        0);
    if (r) {
        printf("ERROR: %s\n", uv_strerror(r));
        exit(1);
    }
    uvrpc_thread_data->tcp_server->data = uvrpc_thread_data;
    r = uv_listen((uv_stream_t *) uvrpc_thread_data->tcp_server, DEFAULT_BACKLOG, _server_on_new_connection);

    if (r) {
        printf("ERROR: %s\n", uv_strerror(r));
        exit(1);
    }
    uv_run(uvrpc_thread_data->work_loop, UV_RUN_DEFAULT);
}

void async_send_stop_loop(uv_async_t *handle) {
    uv_loop_t *work_loop = handle->data;
    uv_stop(work_loop);
}

int32_t __return_error(const char *buf, size_t length, char **out_buf, size_t *out_length) {
    *out_length = 0;
    return 255;
}

uvrpcs_t *start_server(char *ip, int port, int eventloop_num, int thread_num_per_eventloop) {
    char num_str[128];
    sprintf(num_str, "%d", thread_num_per_eventloop);
    uv_os_setenv("UV_THREADPOOL_SIZE", num_str);
    uvrpcs_t *server = malloc(sizeof(uvrpcs_t));
    memset(server->register_func_table, 0, sizeof(int32_t (*[256])(char *, size_t)));
    server->base.tids = malloc(sizeof(uv_thread_t) * eventloop_num);;
    server->base.thread_count = eventloop_num;
    server->base.thread_data = malloc(sizeof(void *) * eventloop_num);
    server->base.addr = malloc(sizeof(struct sockaddr_storage));
    server->status = 0;

    uv_ip4_addr(ip, port, (struct sockaddr_in *) server->base.addr);

    for (int i = 0; i < eventloop_num; i++) {
        _uvrpc_server_thread_t *uvrpc_server_data = malloc(sizeof(_uvrpc_server_thread_t));
        uvrpc_server_data->thread_id = i;
        uvrpc_server_data->uvrpcs = server;
        uvrpc_server_data->tcp_server = malloc(sizeof(uv_tcp_t));
        uvrpc_server_data->work_loop = malloc(sizeof(uv_loop_t));
        uv_loop_init(uvrpc_server_data->work_loop);
        server->base.thread_data[i] = uvrpc_server_data;
        uv_thread_create(&(server->base.tids[i]), server_cb, uvrpc_server_data);
        uvrpc_server_data->async_stop_t = malloc(sizeof(uv_async_t));
        uvrpc_server_data->async_stop_t->data = uvrpc_server_data->work_loop;
        uv_async_init(uvrpc_server_data->work_loop, uvrpc_server_data->async_stop_t, async_send_stop_loop);
    }
    server->register_func_table[255] = __return_error;
    return server;
}

int wait_server_forever(uvrpcs_t *server) {
    for (int i = 0; i < server->base.thread_count; i++) {
        uv_thread_join(&(server->base.tids[i]));
    }
    server->status = 1;
    return 0;
}

int register_function(uvrpcs_t *uvrpc_server, unsigned char magic,
                      int32_t (*func)(const char *, size_t, char **, size_t *)) {
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

void _uv_walk_close_all(uv_handle_t *handle, void *args) {
    if (!uv_is_closing(handle))
        uv_close(handle, _free_handle);
}

int stop_server(uvrpcs_t *uvrpc_server) {
    for (int i = 0; i < uvrpc_server->base.thread_count; i++) {
        _uvrpc_server_thread_t *uvrpc_server_thread_data = uvrpc_server->base.thread_data[i];

        uv_async_send(uvrpc_server_thread_data->async_stop_t);

        uv_walk(uvrpc_server_thread_data->work_loop, _uv_walk_close_all, NULL);
        while (uvrpc_server->status == 0) {};

        uv_run(uvrpc_server_thread_data->work_loop,
               UV_RUN_DEFAULT);// run this work loop again. If no more events, it will exit automatically.
        int ret = uv_loop_close(uvrpc_server_thread_data->work_loop);
        if (ret) {
            printf("%s\n", uv_strerror(ret));
        }

        free(uvrpc_server_thread_data->work_loop);
        free(uvrpc_server_thread_data);
    }

    free(uvrpc_server->base.tids);
    free(uvrpc_server->base.addr);
    free(uvrpc_server->base.thread_data);
    free(uvrpc_server);

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

        uint64_t out_length = bytes_to_uint64((unsigned char *) (client_thread_data->buf + 15));
        if (client_thread_data->max_length < out_length + REP_HEADER_LENGTH) {
            client_thread_data->buf = realloc(client_thread_data->buf, sizeof(char) * (REP_HEADER_LENGTH + out_length));
            client_thread_data->max_length = REP_HEADER_LENGTH + out_length;
        }
        if (client_thread_data->current_length < REP_HEADER_LENGTH + out_length) {
            return;
        }


        unsigned char func_id = (unsigned char) client_thread_data->buf[2];
        uint64_t req_id = bytes_to_uint64((unsigned char *) (client_thread_data->buf + 3));
        int32_t result = (int32_t) bytes_to_uint32((unsigned char *) (client_thread_data->buf + 11));

        //printf("func_if: %d, req_id: %ld, ret_code: %d\n", func_id, req_id, result);

        client_thread_data->result_buf = malloc(sizeof(char) * out_length);
        memcpy(client_thread_data->result_buf, client_thread_data->buf + REP_HEADER_LENGTH, out_length);
        client_thread_data->result_length = out_length;

        free(client_thread_data->buf);
        client_thread_data->buf = NULL;
        client_thread_data->max_length = client_thread_data->current_length = 0;

        uv_mutex_lock(client_thread_data->result_mutex);
        client_thread_data->result_req_id = req_id;
        client_thread_data->ret_result = result;
        uv_mutex_unlock(client_thread_data->result_mutex);
        uv_cond_signal(client_thread_data->result_cond);

    } else if (nread < 0) {
        if (nread != UV_EOF) {
            printf("Read error %s\n", uv_strerror(nread));
        }
        if (!uv_is_closing((const uv_handle_t *) stream))
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
        if (client_thread_data->result_req_id < INT64_MAX) {
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
    _uvrpc_client_thread_t *client_thread_data = write1->data;
    if (status != 0) {
        printf("write to server failed. %s\n", uv_strerror(status));

        client_thread_data->ret_result = 255;
        uv_cond_broadcast(client_thread_data->result_cond);

    }
    free(write1);
}

void async_send_to_server(uv_async_t *handle) {
    _uvrpc_client_thread_t *client_thread_data = handle->data;
    uv_write_t *write_req = malloc(sizeof(uv_write_t));

    uv_buf_t uvbuf = uv_buf_init(client_thread_data->send_buf, client_thread_data->send_length);

    write_req->data = client_thread_data;
    uv_write(write_req, (uv_stream_t *) client_thread_data->tcp_server, &uvbuf, 1, _client_after_send);
}

uvrpcc_t *start_client(char *server_URL, int port, int thread_num) {
    uv_mutex_init(&global_mutex);
    global_count = 0;

    uvrpcc_t *uvrpc_client = malloc(sizeof(uvrpcc_t));
    uvrpc_client->base.thread_count = thread_num;
    uvrpc_client->base.thread_data = malloc(sizeof(void *) * thread_num);
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
        client_thread_data->buf = NULL;
        client_thread_data->result_req_id = UINT64_MAX;
        client_thread_data->result_cond = malloc(sizeof(uv_cond_t));
        client_thread_data->result_mutex = malloc(sizeof(uv_mutex_t));
        uv_mutex_init(client_thread_data->result_mutex);
        uv_cond_init(client_thread_data->result_cond);
        uv_thread_create(&(uvrpc_client->base.tids[i]), client_cb, client_thread_data);
        uvrpc_client->base.thread_data[i] = client_thread_data;
        bq_push(uvrpc_client->bq, client_thread_data);
        client_thread_data->async_t = malloc(sizeof(uv_async_t));
        client_thread_data->async_t->data = client_thread_data;
        uv_async_init(client_thread_data->work_loop, client_thread_data->async_t, async_send_to_server);

        client_thread_data->async_stop_t = malloc(sizeof(uv_async_t));
        client_thread_data->async_stop_t->data = client_thread_data->work_loop;
        uv_async_init(client_thread_data->work_loop, client_thread_data->async_stop_t, async_send_stop_loop);
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

int uvrpc_send(uvrpcc_t *client, char *buf, size_t length, unsigned char func_id, char **out_buf, size_t *out_length) {
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
    if (out_buf != NULL && out_length != NULL) {
        *out_buf = client_thread_data->result_buf;
        *out_length = client_thread_data->result_length;
    } else {
        free(client_thread_data->result_buf);
    }

    client_thread_data->result_buf = NULL;
    result = (int) client_thread_data->ret_result;
    bq_push(client->bq, client_thread_data);
    return result;
}

int stop_client(uvrpcc_t *client) {
    for (int i = 0; i < client->base.thread_count; i++) {
        _uvrpc_client_thread_t *client_thread_data = client->base.thread_data[i];
        uv_async_send(client_thread_data->async_stop_t);
        uv_thread_join(&(client->base.tids[i]));

        uv_walk(client_thread_data->work_loop, _uv_walk_close_all, NULL);
        uv_run(client_thread_data->work_loop,
               UV_RUN_DEFAULT);// run this work loop again. If no more events, it will exit automatically.
        int ret = uv_loop_close(client_thread_data->work_loop);
        if (ret) {
            printf("%s\n", uv_strerror(ret));
        }

        uv_cond_destroy(client_thread_data->result_cond);
        free(client_thread_data->result_cond);
        uv_mutex_destroy(client_thread_data->result_mutex);
        free(client_thread_data->result_mutex);

        free(client_thread_data->server_conn);
        free(client_thread_data->work_loop);
        free(client_thread_data);

    }

    free(client->base.tids);
    free(client->base.addr);
    free(client->base.thread_data);
    free_blockQueue(client->bq);
    free(client);

    return 0;
}

char *uvrpc_errstr(int uvrpc_errno) {
    switch (uvrpc_errno) {
        case 0:
            return "no error was found";
        case 255:
            return "requested function was not registered";
        case 0xee00:
            return "register function failed: magic code out of range";
        case 0xee01:
            return "register function failed: magic code already registered";
        default:
            return "unknown error";
    }
}