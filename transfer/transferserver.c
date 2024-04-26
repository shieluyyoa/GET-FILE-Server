#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <errno.h>
#include <stdio.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <getopt.h>
#include <sys/socket.h>

#define BUFSIZE 512

#define USAGE                                              \
    "usage:\n"                                             \
    "  transferserver [options]\n"                         \
    "options:\n"                                           \
    "  -f                  Filename (Default: 6200.txt)\n" \
    "  -p                  Port (Default: 17485)\n"        \
    "  -h                  Show this help message\n"

/* OPTIONS DESCRIPTOR ====================================================== */
static struct option gLongOptions[] = {
    {"filename", required_argument, NULL, 'f'},
    {"help", no_argument, NULL, 'h'},
    {"port", required_argument, NULL, 'p'},
    {NULL, 0, NULL, 0}};

int sendall(int s, char *buf, size_t *len)
{
    size_t total = 0;        // how many bytes we've sent
    size_t bytesleft = *len; // how many we have left to send
    size_t n;

    while (total < *len)
    {
        n = send(s, buf + total, bytesleft, 0);
        if (n == -1)
        {
            break; // an error occurred
        }

        printf("sent %ld bytes\n", n);
        total += n;
        bytesleft -= n;
    }

    *len = total; // return the number actually sent here

    return n == -1 ? -1 : 0; // return -1 on failure, 0 on success
}

int main(int argc, char **argv)
{
    int option_char;
    int portno = 17485;          /* port to listen on */
    char *filename = "6200.txt"; /* file to transfer */

    setbuf(stdout, NULL); // disable buffering

    // Parse and set command line arguments
    while ((option_char = getopt_long(argc, argv, "p:hf:x", gLongOptions, NULL)) != -1)
    {
        switch (option_char)
        {
        case 'p': // listen-port
            portno = atoi(optarg);
            break;
        case 'f': // file to transfer
            filename = optarg;
            break;
        case 'h': // help
            fprintf(stdout, "%s", USAGE);
            exit(0);
            break;
        default:
            fprintf(stderr, "%s", USAGE);
            exit(1);
        }
    }

    if ((portno < 1025) || (portno > 65535))
    {
        fprintf(stderr, "%s @ %d: invalid port number (%d)\n", __FILE__, __LINE__, portno);
        exit(1);
    }

    if (NULL == filename)
    {
        fprintf(stderr, "%s @ %d: invalid filename\n", __FILE__, __LINE__);
        exit(1);
    }

    int socket_fd, new_fd;
    size_t messagebytes; // listen on sock_fd, new connection on new_fd
    char buffer[BUFSIZE];
    struct addrinfo config, *serverinfo, *p;
    struct sockaddr_storage their_addr; // connector's address information
    socklen_t sin_size;
    int yes = 1;
    int res;
    int port_len = snprintf(NULL, 0, "%d", portno);
    char port[port_len + 1];
    sprintf(port, "%d", portno);

    memset(&config, 0, sizeof config);
    config.ai_family = AF_UNSPEC;
    config.ai_socktype = SOCK_STREAM;
    config.ai_flags = AI_PASSIVE; // use my IP

    if ((res = getaddrinfo(NULL, port, &config, &serverinfo)) != 0)
    {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(res));
        return 1;
    }

    for (p = serverinfo; p != NULL; p = p->ai_next)
    {
        if ((socket_fd = socket(p->ai_family, p->ai_socktype,
                                p->ai_protocol)) == -1)
        {
            perror("sereser: socket");
            continue;
        }

        if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &yes,
                       sizeof(int)) == -1)
        {
            perror("setsockopt");
            exit(1);
        }

        if (bind(socket_fd, p->ai_addr, p->ai_addrlen) == -1)
        {
            close(socket_fd);
            perror("sereser: bind");
            continue;
        }

        break;
    }

    freeaddrinfo(serverinfo);

    if (p == NULL)
    {
        fprintf(stderr, "sereser: failed to bind\n");
        exit(1);
    }

    if (listen(socket_fd, 10) == -1)
    {
        perror("listen");
        exit(1);
    }

    // printf("sereser: waiting for connections...\n");

    while (1)
    {
        sin_size = sizeof their_addr;
        new_fd = accept(socket_fd, (struct sockaddr *)&their_addr, &sin_size);
        if (new_fd == -1)
        {
            perror("accept");
            continue;
        }

        FILE *fp = fopen(filename, "r");
        if (NULL == fp)
        {
            perror("fopen");
            exit(1);
        }

        while (!feof(fp))
        {

            if ((messagebytes = fread(buffer, 1, BUFSIZE, fp)) == 0)
            {
                break;
            }

            printf("%ld\n", messagebytes);

            if ((sendall(new_fd, buffer, &messagebytes)) == -1)
            {
                perror("send");
                exit(1);
            }
        }

        close(new_fd);
        fclose(fp);
    }

    return 0;
}
