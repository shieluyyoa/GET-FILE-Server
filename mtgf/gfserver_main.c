#include <stdlib.h>

#include "gfserver-student.h"
#include "steque.h"
#include "pthread.h"

#define USAGE                                                                                \
  "usage:\n"                                                                                 \
  "  gfserver_main [options]\n"                                                              \
  "options:\n"                                                                               \
  "  -h                  Show this help message.\n"                                          \
  "  -t [nthreads]       Number of threads (Default: 16)\n"                                  \
  "  -m [content_file]   Content file mapping keys to content files (Default: content.txt\n" \
  "  -p [listen_port]    Listen port (Default: 39474)\n"                                     \
  "  -d [delay]          Delay in content_get, default 0, range 0-5000000 "                  \
  "(microseconds)\n "

/* OPTIONS DESCRIPTOR ====================================================== */
static struct option gLongOptions[] = {
    {"content", required_argument, NULL, 'm'},
    {"port", required_argument, NULL, 'p'},
    {"nthreads", required_argument, NULL, 't'},
    {"delay", required_argument, NULL, 'd'},
    {"help", no_argument, NULL, 'h'},
    {NULL, 0, NULL, 0}};

extern unsigned long int content_delay;

extern gfh_error_t gfs_handler(gfcontext_t **ctx, const char *path, void *arg);

extern ssize_t gfs_transfer_file(gfcontext_t **ctx, const char *path);

static void _sig_handler(int signo)
{
  if ((SIGINT == signo) || (SIGTERM == signo))
  {
    exit(signo);
  }
}

static pthread_t *workers;
pthread_cond_t gfs_cond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t gfs_mutex = PTHREAD_MUTEX_INITIALIZER;
steque_t *queue;

typedef struct gfs_queue_ctx
{
  gfcontext_t *ctx;
  char *path;
} gfs_queue_ctx;

static void *gfs_process_req(void *arg)
{
  while (1)
  {
    gfs_queue_ctx *ctx = NULL;

    pthread_mutex_lock(&gfs_mutex);
    while (steque_isempty(queue))
    {
      pthread_cond_wait(&gfs_cond, &gfs_mutex);
    }
    ctx = steque_pop(queue);
    pthread_mutex_unlock(&gfs_mutex);

    if (NULL == ctx)
    {
      break;
    }

    printf("processing request for %s\n", ctx->path);
    gfs_transfer_file(&(ctx->ctx), ctx->path);

    free(ctx);
  }

  return NULL;
}

// static void enqueue_gfs_req(gfcontext_t **ctx, char *path)
// {
//   gfs_queue_ctx *new_ctx = NULL;

//   new_ctx = malloc(sizeof(gfs_queue_ctx));
//   new_ctx->ctx = ctx;
//   new_ctx->path = path;

//   pthread_mutex_lock(&gfs_mutex);
//   steque_enqueue(queue, new_ctx);
//   pthread_cond_signal(&gfs_cond);
//   pthread_mutex_unlock(&gfs_mutex);
// }

void init_threads(size_t nthreads)
{
  workers = malloc(sizeof(pthread_t) * nthreads);
  for (int i = 0; i < nthreads; i++)
  {
    if (pthread_create(&workers[i], NULL, gfs_process_req, NULL) != 0)
    {
      fprintf(stderr, "Can't create thread %d\n", i);
      exit(1);
    }

    printf("Created thread %d\n", i);
  }
}

void cleanup_threads(size_t nthreads, gfserver_t *gfs)
{
  for (int i = 0; i < nthreads; i++)
  {
    pthread_join(workers[i], NULL);
  }

  free(workers);
  steque_destroy(queue);
  free(queue);
  free(gfs);

  content_destroy();
}

/* Main ========================================================= */
int main(int argc, char **argv)
{
  char *content_map = "content.txt";
  gfserver_t *gfs = NULL;
  int nthreads = 16;
  unsigned short port = 39474;
  int option_char = 0;

  setbuf(stdout, NULL);

  if (SIG_ERR == signal(SIGINT, _sig_handler))
  {
    fprintf(stderr, "Can't catch SIGINT...exiting.\n");
    exit(EXIT_FAILURE);
  }

  if (SIG_ERR == signal(SIGTERM, _sig_handler))
  {
    fprintf(stderr, "Can't catch SIGTERM...exiting.\n");
    exit(EXIT_FAILURE);
  }

  // Parse and set command line arguments
  while ((option_char = getopt_long(argc, argv, "p:d:rhm:t:", gLongOptions,
                                    NULL)) != -1)
  {
    switch (option_char)
    {
    case 'h': /* help */
      fprintf(stdout, "%s", USAGE);
      exit(0);
      break;
    case 'p': /* listen-port */
      port = atoi(optarg);
      break;
    case 'd': /* delay */
      content_delay = (unsigned long int)atoi(optarg);
      break;
    case 't': /* nthreads */
      nthreads = atoi(optarg);
      break;
    case 'm': /* file-path */
      content_map = optarg;
      break;
    default:
      fprintf(stderr, "%s", USAGE);
      exit(1);
    }
  }

  /* not useful, but it ensures the initial code builds without warnings */
  if (nthreads < 1)
  {
    nthreads = 1;
  }

  if (content_delay > 5000000)
  {
    fprintf(stderr, "Content delay must be less than 5000000 (microseconds)\n");
    exit(__LINE__);
  }

  content_init(content_map);

  /* Initialize thread management */
  queue = malloc(sizeof(steque_t));
  steque_init(queue);

  /* Initialize thread pool */
  init_threads(nthreads);

  /*Initializing server*/
  gfs = gfserver_create();

  // Setting options
  gfserver_set_port(&gfs, port);
  gfserver_set_maxpending(&gfs, 24);
  gfserver_set_handler(&gfs, gfs_handler);
  gfserver_set_handlerarg(&gfs, NULL); // doesn't have to be NULL!

  /*Loops forever*/
  gfserver_serve(&gfs);

  cleanup_threads(nthreads, gfs);
}
