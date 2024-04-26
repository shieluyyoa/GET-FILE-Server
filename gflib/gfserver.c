#include <stdlib.h>
#include "gfserver-student.h"

#define BUFSIZE 2048
// Modify this file to implement the interface specified in
// gfserver.h.
struct gfserver_t
{
    unsigned short port;
    gfh_error_t (*gfs_handler)(gfcontext_t **ctx, const char *path, void *arg);
    void *handlerarg;
    int max_pending;
    int sock_fd;
    gfcontext_t **ctx;
};

struct gfcontext_t
{
    int sock_fd;
    gfstatus_t status;
    char header[BUFSIZE];
    char path[BUFSIZE];
};

int sendall(int s, const void *buf, size_t len)
{
    size_t total = 0;       // how many bytes we've sent
    size_t bytesleft = len; // how many we have left to send
    size_t n;

    while (total < len)
    {
        n = send(s, buf + total, bytesleft, 0);
        if (n == -1)
        {
            break; // an error occurred
        }
        printf("length %zu\n", len);
        printf("sent %ld bytes\n", n);
        total += n;
        bytesleft -= n;
    }

    return n == -1 ? -1 : total; // return -1 on failure, 0 on success
}

void gfs_abort(gfcontext_t **ctx)
{
    if ((*ctx) == NULL)
    {
        printf("ctx is NULL\n");
        return;
    }

    if ((*ctx)->sock_fd > 0)
    {
        gfs_sendheader(ctx, GF_ERROR, 0);
        close((*ctx)->sock_fd);
    }
}

ssize_t gfs_send(gfcontext_t **ctx, const void *data, size_t len)
{
    ssize_t bytes_sent;
    if ((bytes_sent = sendall((*ctx)->sock_fd, data, len)) == -1)
    {
        printf("send failed\n");
    }

    return bytes_sent;
}

ssize_t gfs_sendheader(gfcontext_t **ctx, gfstatus_t status, size_t file_len)
{
    char *eof = "\r\n\r\n";
    char *scheme = "GETFILE";
    memset((*ctx)->header, 0, BUFSIZE);

    if (status == GF_OK)
    {
        sprintf((*ctx)->header, "%s OK %zu%s", scheme, file_len, eof);
    }
    else if (status == GF_FILE_NOT_FOUND)
    {
        sprintf((*ctx)->header, "%s FILE_NOT_FOUND%s", scheme, eof);
    }
    else if (status == GF_ERROR)
    {
        sprintf((*ctx)->header, "%s ERROR%s", scheme, eof);
    }
    else if (status == GF_INVALID)
    {
        sprintf((*ctx)->header, "%s INVALID%s", scheme, eof);
    }

    if ((sendall((*ctx)->sock_fd, (*ctx)->header, strlen((*ctx)->header))) == -1)
    {
        perror("send");
        return -1;
    }

    return strlen((*ctx)->header);
}

int set_gfserver(gfserver_t *gfs)
{
    struct addrinfo config, *serverinfo, *p;
    int yes = 1;
    int res;
    int port_len = snprintf(NULL, 0, "%d", gfs->port);
    char port[port_len + 1];
    sprintf(port, "%d", gfs->port);

    memset(&config, 0, sizeof config);
    config.ai_family = AF_UNSPEC;
    config.ai_socktype = SOCK_STREAM;
    config.ai_flags = AI_PASSIVE; // use my IP

    if ((res = getaddrinfo(NULL, port, &config, &serverinfo)) != 0)
    {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(res));
        return -1;
    }

    for (p = serverinfo; p != NULL; p = p->ai_next)
    {
        if ((gfs->sock_fd = socket(p->ai_family, p->ai_socktype,
                                   p->ai_protocol)) == -1)
        {
            perror("sereser: socket");
            continue;
        }

        if (setsockopt(gfs->sock_fd, SOL_SOCKET, SO_REUSEADDR, &yes,
                       sizeof(int)) == -1)
        {
            perror("setsockopt");
            exit(1);
        }

        if (bind(gfs->sock_fd, p->ai_addr, p->ai_addrlen) == -1)
        {
            close(gfs->sock_fd);
            perror("sereser: bind");
            continue;
        }

        break;
    }

    freeaddrinfo(serverinfo);

    if (p == NULL)
    {
        fprintf(stderr, "sereser: failed to bind\n");
        return -1;
    }

    if (listen(gfs->sock_fd, gfs->max_pending) == -1)
    {
        perror("listen");
        return -1;
    }

    return 0;
}

// Parse request header
static int parse_req_header(gfserver_t *gfs)
{
    char scheme[BUFSIZE] = {0};
    char method[BUFSIZE] = {0};
    char header_buffer[BUFSIZE] = {0};
    char path[BUFSIZE] = {0};
    gfcontext_t **ctx = gfs->ctx;

    memset((*ctx)->path, 0, BUFSIZE);

    ssize_t header_res = recv((*ctx)->sock_fd, header_buffer, BUFSIZE, 0);
    if (header_res == 0)
    {
        (*ctx)->status = GF_INVALID;
        return -1;
    }
    else if (header_res == -1)
    {
        (*ctx)->status = GF_ERROR;
        return -1;
    }
    printf("header buffer : %s\n", header_buffer);
    printf("header_res : %ld\n", header_res);

    while (strstr(header_buffer, "\r\n\r\n") == NULL)
    {
        ssize_t new_header_res = recv((*ctx)->sock_fd, header_buffer + header_res, BUFSIZE, 0);
        if (new_header_res == 0)
        {
            printf("Error: Client closed connection\n");
            return -1;
        }
        else if (new_header_res == -1)
        {
            printf("Error: Failed to receive header\n");
            return -1;
        }
        printf("new_header_res : %ld\n", new_header_res);
        header_res += new_header_res;
    }

    printf("header_buffer: %s\n", header_buffer);

    if (sscanf(header_buffer, "%s %s %s\r\n\r\n", scheme, method, path) == EOF)
    {
        printf("Error: Failed to parse client header\n");
        (*ctx)->status = GF_INVALID;
        return -1;
    }

    if (strcmp(scheme, "GETFILE") != 0)
    {
        printf(scheme);
        printf("Error: Invalid response scheme\n");
        (*ctx)->status = GF_INVALID;
        return -1;
    }

    if (strcmp(method, "GET") != 0)
    {
        printf("Error: Invalid response method\n");
        (*ctx)->status = GF_INVALID;
        return -1;
    }

    if (strncmp(path, "/", 1) != 0)
    {
        printf("Error: Invalid response path\n");
        (*ctx)->status = GF_INVALID;
        return -1;
    }

    strcpy((*ctx)->path, path);
    printf("path : %s\n", (*ctx)->path);

    return 0;
}

gfserver_t *gfserver_create()
{
    gfserver_t *gfs = malloc(sizeof(gfserver_t));
    (gfs)->ctx = malloc(sizeof(gfcontext_t **));
    *((gfs)->ctx) = malloc(sizeof(gfcontext_t));
    return gfs;
}

void gfserver_set_port(gfserver_t **gfs, unsigned short port)
{
    (*gfs)->port = port;
}

void gfserver_serve(gfserver_t **gfs)
{
    set_gfserver(*gfs);
    socklen_t sin_size;
    struct sockaddr_storage gfclient_addr;
    gfcontext_t **ctx = (*gfs)->ctx;

    while (1)
    {
        sin_size = sizeof gfclient_addr;
        (*ctx)->sock_fd = accept((*gfs)->sock_fd, (struct sockaddr *)&gfclient_addr, &sin_size);

        if ((*ctx)->sock_fd == -1)
        {
            perror("accept");
            continue;
        }

        if (parse_req_header(*gfs) == -1)
        {
            printf("Error: Failed to parse header\n");
            gfs_sendheader(ctx, (*ctx)->status, 0);
            printf("Closing socket\n");
            close((*ctx)->sock_fd);
            continue;
        }

        (*gfs)->gfs_handler(ctx, (*ctx)->path, (*gfs)->handlerarg);

        if ((*ctx) == NULL)
        {
            printf("ctx is NULL\n");
            continue;
        }

        if ((*ctx)->sock_fd > 0)
        {
            printf("Closing socket\n");
            close((*ctx)->sock_fd);
        }
    }
}

void gfserver_set_handlerarg(gfserver_t **gfs, void *arg)
{
    (*gfs)->handlerarg = arg;
}

void gfserver_set_handler(gfserver_t **gfs, gfh_error_t (*handler)(gfcontext_t **, const char *, void *))
{
    (*gfs)->gfs_handler = handler;
}

void gfserver_set_maxpending(gfserver_t **gfs, int max_pending)
{
    (*gfs)->max_pending = max_pending;
}
