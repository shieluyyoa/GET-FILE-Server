#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <netdb.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <getopt.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define BUFSIZE 1024

#define USAGE                                                        \
    "usage:\n"                                                         \
    "  echoserver [options]\n"                                         \
    "options:\n"                                                       \
    "  -p                  Port (Default: 37482)\n"                    \
    "  -m                  Maximum pending connections (default: 5)\n" \
    "  -h                  Show this help message\n"

/* OPTIONS DESCRIPTOR ====================================================== */
static struct option gLongOptions[] = {
    {"port",          required_argument,      NULL,           'p'},
    {"help",          no_argument,            NULL,           'h'},
    {"maxnpending",   required_argument,      NULL,           'm'},
    {NULL,            0,                      NULL,             0}
};


int main(int argc, char **argv) {
    int option_char;
    int portno = 37482; /* port to listen on */
    int maxnpending = 5;
  
    // Parse and set command line arguments
    while ((option_char = getopt_long(argc, argv, "p:m:hx", gLongOptions, NULL)) != -1) {
        switch (option_char) {
        case 'm': // server
            maxnpending = atoi(optarg);
            break; 
        case 'h': // help
            fprintf(stdout, "%s ", USAGE);
            exit(0);
            break;
        case 'p': // listen-port
            portno = atoi(optarg);
            break;                                        
        default:
            fprintf(stderr, "%s ", USAGE);
            exit(1);
        }
    }

    setbuf(stdout, NULL); // disable buffering

    if ((portno < 1025) || (portno > 65535)) {
        fprintf(stderr, "%s @ %d: invalid port number (%d)\n", __FILE__, __LINE__, portno);
        exit(1);
    }
    if (maxnpending < 1) {
        fprintf(stderr, "%s @ %d: invalid pending count (%d)\n", __FILE__, __LINE__, maxnpending);
        exit(1);
    }


  /* Socket Code Here */

}
