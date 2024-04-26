#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <netdb.h>
#include <string.h>
#include <errno.h>
#include <netinet/in.h>
#include <getopt.h>
#include <sys/socket.h>

#define BUFSIZE 512

#define USAGE                                             \
    "usage:\n"                                            \
    "  transferclient [options]\n"                        \
    "options:\n"                                          \
    "  -p                  Port (Default: 17485)\n"       \
    "  -s                  Server (Default: localhost)\n" \
    "  -h                  Show this help message\n"      \
    "  -o                  Output file (Default cs6200.txt)\n"

/* OPTIONS DESCRIPTOR ====================================================== */
static struct option gLongOptions[] = {
    {"server", required_argument, NULL, 's'},
    {"output", required_argument, NULL, 'o'},
    {"help", no_argument, NULL, 'h'},
    {"port", required_argument, NULL, 'p'},
    {NULL, 0, NULL, 0}};

/* Main ========================================================= */
int main(int argc, char **argv)
{
    int option_char = 0;
    char *hostname = "localhost";
    unsigned short portno = 17485;
    char *filename = "cs6200.txt";

    setbuf(stdout, NULL);

    // Parse and set command line arguments
    while ((option_char = getopt_long(argc, argv, "s:p:o:hx", gLongOptions, NULL)) != -1)
    {
        switch (option_char)
        {
        case 's': // server
            hostname = optarg;
            break;
        case 'p': // listen-port
            portno = atoi(optarg);
            break;
        default:
            fprintf(stderr, "%s", USAGE);
            exit(1);
        case 'o': // filename
            filename = optarg;
            break;
        case 'h': // help
            fprintf(stdout, "%s", USAGE);
            exit(0);
            break;
        }
    }

    if (NULL == hostname)
    {
        fprintf(stderr, "%s @ %d: invalid host name\n", __FILE__, __LINE__);
        exit(1);
    }

    if (NULL == filename)
    {
        fprintf(stderr, "%s @ %d: invalid filename\n", __FILE__, __LINE__);
        exit(1);
    }

    if ((portno < 1025) || (portno > 65535))
    {
        fprintf(stderr, "%s @ %d: invalid port number (%d)\n", __FILE__, __LINE__, portno);
        exit(1);
    }

    int socket_fd;
    size_t messagebytes;
    char buffer[BUFSIZE];
    struct addrinfo config, *serverinfo, *p;
    int res;

    int port_len = snprintf(NULL, 0, "%d", portno);
    char port[port_len + 1];
    sprintf(port, "%d", portno);

    memset(&config, 0, sizeof config);
    config.ai_family = AF_UNSPEC;
    config.ai_socktype = SOCK_STREAM;

    if ((res = getaddrinfo(hostname, port, &config, &serverinfo)) != 0)
    {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(res));
        return 1;
    }

    for (p = serverinfo; p != NULL; p = p->ai_next)
    {
        if ((socket_fd = socket(p->ai_family, p->ai_socktype,
                                p->ai_protocol)) == -1)
        {
            perror("socket");
            continue;
        }

        if (connect(socket_fd, p->ai_addr, p->ai_addrlen) == -1)
        {
            close(socket_fd);
            perror("connect");
            continue;
        }

        break;
    }

    if (p == NULL)
    {
        fprintf(stderr, "failed to connect\n");
        return 2;
    }

    freeaddrinfo(serverinfo);
    FILE *fp = fopen(filename, "w");

    while (1)
    {

        if ((messagebytes = recv(socket_fd, buffer, BUFSIZE, 0)) == -1)
        {
            perror("recv");
            exit(1);
        }

        printf("%ld\n", messagebytes);

        if (0 == messagebytes)
        {
            break;
        }

        fwrite(buffer, messagebytes, 1, fp);
    }

    close(socket_fd);
    fclose(fp);

    return 0;
}
