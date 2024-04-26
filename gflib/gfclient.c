
#include <stdlib.h>

#include "gfclient-student.h"

// Modify this file to implement the interface specified in
// gfclient.h.
#define BUFSIZE 2048
#define SCHEMESIZE 2048
#define FILELENSIZE 2048
#define STATUSSIZE 2048

const char *scheme = "GETFILE ";
const char *method = "GET ";
const char *endofreq = "\r\n\r\n";

// Define gfcrequest_t
struct gfcrequest_t
{
  unsigned short port;
  const char *req_path;
  const char *server;
  void (*writefunc)(void *data, size_t len, void *arg);
  void (*headerfunc)(void *header_buffer, size_t header_buffer_len, void *handlerarg);
  void *writearg, *headerarg;
  int sock_fd;
  char buffer[BUFSIZE];
  char header[BUFSIZE];
  size_t file_len;
  size_t bytes_received;
  gfstatus_t status;
};
int sendall(int s, char *buf, size_t len)
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

    // printf("sent %ld bytes\n", n);
    total += n;
    bytesleft -= n;
  }

  len = total; // return the number actually sent here

  return n == -1 ? -1 : 0; // return -1 on failure, 0 on success
}

// Get GET FILE header
static void get_request_header(gfcrequest_t *gfr)
{
  strcpy(gfr->header, scheme);
  strcat(gfr->header, method);
  strcat(gfr->header, gfr->req_path);
  strcat(gfr->header, endofreq);
}

// Parse response header
static int parse_res_header(gfcrequest_t *gfr)
{
  char scheme[SCHEMESIZE] = {0};
  char status_code[STATUSSIZE] = {0};
  char header_buffer[BUFSIZE] = {0};
  int file_len_str = 0;
  int n = 0;

  ssize_t header_res = recv(gfr->sock_fd, header_buffer, BUFSIZE, 0);
  if (header_res == 0)
  {
    gfr->status = GF_INVALID;
    return -1;
  }
  else if (header_res == -1)
  {
    gfr->status = GF_ERROR;
    return -1;
  }

  while (strstr(header_buffer, "\r\n\r\n") == NULL)
  {
    ssize_t new_header_res = recv(gfr->sock_fd, header_buffer + header_res, BUFSIZE, 0);
    if (new_header_res == 0)
    {
      gfr->status = GF_INVALID;
      return -1;
    }
    else if (new_header_res == -1)
    {
      gfr->status = GF_ERROR;
      return -1;
    }
    header_res += new_header_res;
  }

  if (sscanf(header_buffer, "%s %s %i\r\n\r\n%n", scheme, status_code, &file_len_str, &n) == EOF)
  {
    printf("Error: Failed to parse response header\n");
    gfr->status = GF_INVALID;
    return -1;
  }

  printf("scheme: %s, status_code: %s, file_len: %i\n, header_size: %i\n", scheme, status_code, file_len_str, n);

  if (strcmp(scheme, "GETFILE") != 0)
  {
    printf(scheme);
    printf("Error: Invalid response scheme\n");
    gfr->status = GF_INVALID;
    return -1;
  }

  if (strcmp(status_code, "OK") == 0)
  {
    printf("OK\n");
    gfr->status = GF_OK;
  }
  else if (strcmp(status_code, "ERROR") == 0)
  {
    printf("ERROR\n");
    gfr->status = GF_ERROR;
    return 0;
  }
  else if (strcmp(status_code, "FILE_NOT_FOUND") == 0)
  {
    printf("FILE_NOT_FOUND\n");
    gfr->status = GF_FILE_NOT_FOUND;
    return 0;
  }
  else
  {
    printf("Error: Invalid response status code\n");
    gfr->status = GF_INVALID;
    return -1;
  }

  printf("scheme: %s, status_code: %s, file_len: %i\n, header_size: %i\n", scheme, status_code, file_len_str, n);

  gfr->file_len = file_len_str;
  gfr->writefunc(header_buffer + n, strlen(header_buffer) - n, gfr->writearg);
  gfr->bytes_received += header_res - n;
  return 0;
}
// optional function for cleaup processing.
void gfc_cleanup(gfcrequest_t **gfr)
{
  free(*gfr);
  *gfr = NULL;
}

gfcrequest_t *gfc_create()
{
  gfcrequest_t *gfr = (gfcrequest_t *)malloc(sizeof(gfcrequest_t));
  return gfr;
}

size_t gfc_get_filelen(gfcrequest_t **gfr)
{
  return (*gfr)->file_len;
}

size_t gfc_get_bytesreceived(gfcrequest_t **gfr)
{
  return (*gfr)->bytes_received;
}

gfstatus_t gfc_get_status(gfcrequest_t **gfr)
{
  return (*gfr)->status;
}

void gfc_global_init() {}

void gfc_global_cleanup() {}

int gfc_perform(gfcrequest_t **gfr)
{
  struct addrinfo config, *serverinfo, *p;
  int res;
  size_t messagebytes;
  (*gfr)->bytes_received = 0;
  int port_len = snprintf(NULL, 0, "%d", (*gfr)->port);
  char port[port_len + 1];
  sprintf(port, "%d", (*gfr)->port);

  memset(&config, 0, sizeof config);
  config.ai_family = AF_UNSPEC;
  config.ai_socktype = SOCK_STREAM;

  // getaddrinfo() returns a list of address structures.
  if ((res = getaddrinfo((*gfr)->server, port, &config, &serverinfo)) != 0)
  {
    fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(res));
    return 1;
  }

  // loop through all the results and connect to the first we can
  for (p = serverinfo; p != NULL; p = p->ai_next)
  {
    if (((*gfr)->sock_fd = socket(p->ai_family, p->ai_socktype,
                                  p->ai_protocol)) == -1)
    {
      perror("socket");
      continue;
    }

    if (connect((*gfr)->sock_fd, p->ai_addr, p->ai_addrlen) == -1)
    {
      close((*gfr)->sock_fd);
      printf("failed to connect\n");
      (*gfr)->status = GF_ERROR;
      return -1;
    }

    break;
  }

  // if we couldn't connect to any of the addresses, just bail
  if (p == NULL)
  {
    fprintf(stderr, "failed to connect\n");
    return -1;
  }

  // free the linked list after we're done with it
  freeaddrinfo(serverinfo);

  get_request_header(*gfr);
  sendall((*gfr)->sock_fd, (*gfr)->header, strlen((*gfr)->header));

  if (parse_res_header(*gfr) == -1)
  {
    return -1;
  }
  shutdown((*gfr)->sock_fd, SHUT_WR);

  while ((*gfr)->status == GF_OK && (*gfr)->bytes_received < (*gfr)->file_len)
  {
    memset((*gfr)->buffer, 0, BUFSIZE);
    if ((messagebytes = recv((*gfr)->sock_fd, (*gfr)->buffer, BUFSIZE, 0)) == -1)
    {
      printf("error receiving data\n");
      return -1;
    }

    if (messagebytes == 0)
    {
      printf("file incomplete");
      return -1;
    }

    (*gfr)->writefunc(&((*gfr)->buffer), messagebytes, (*gfr)->writearg);
    (*gfr)->bytes_received += messagebytes;
    printf("bytes received: %ld\n", (*gfr)->bytes_received);
    ;
    printf("file_len: %ld\n", (*gfr)->file_len);
  }

  printf("closing socket\n");
  close((*gfr)->sock_fd);
  return 0;
}

void gfc_set_port(gfcrequest_t **gfr, unsigned short port)
{
  (*gfr)->port = port;
}
void gfc_set_headerarg(gfcrequest_t **gfr, void *headerarg)
{
  (*gfr)->headerarg = headerarg;
}

void gfc_set_server(gfcrequest_t **gfr, const char *server)
{
  (*gfr)->server = server;
}

void gfc_set_headerfunc(gfcrequest_t **gfr, void (*headerfunc)(void *, size_t, void *))
{
  (*gfr)->headerfunc = headerfunc;
}

void gfc_set_path(gfcrequest_t **gfr, const char *path)
{
  (*gfr)->req_path = path;
}

void gfc_set_writearg(gfcrequest_t **gfr, void *writearg)
{
  (*gfr)->writearg = writearg;
}

void gfc_set_writefunc(gfcrequest_t **gfr, void (*writefunc)(void *, size_t, void *))
{
  (*gfr)->writefunc = writefunc;
}

const char *gfc_strstatus(gfstatus_t status)
{
  const char *strstatus = "UNKNOWN";

  switch (status)
  {

  case GF_OK:
  {
    strstatus = "OK";
  }
  break;

  case GF_FILE_NOT_FOUND:
  {
    strstatus = "FILE_NOT_FOUND";
  }
  break;

  case GF_INVALID:
  {
    strstatus = "INVALID";
  }
  break;

  case GF_ERROR:
  {
    strstatus = "ERROR";
  }
  break;
  }

  return strstatus;
}
