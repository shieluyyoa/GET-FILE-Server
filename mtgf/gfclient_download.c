#include <stdlib.h>

#include "gfclient-student.h"
#include "steque.h"
#include "pthread.h"

#define MAX_THREADS 1024
#define PATH_BUFFER_SIZE 512

#define USAGE                                                             \
  "usage:\n"                                                              \
  "  gfclient_download [options]\n"                                       \
  "options:\n"                                                            \
  "  -h                  Show this help message\n"                        \
  "  -s [server_addr]    Server address (Default: 127.0.0.1)\n"           \
  "  -p [server_port]    Server port (Default: 39474)\n"                  \
  "  -w [workload_path]  Path to workload file (Default: workload.txt)\n" \
  "  -t [nthreads]       Number of threads (Default 8 Max: 1024)\n"       \
  "  -n [num_requests]   Request download total (Default: 16)\n"

/* OPTIONS DESCRIPTOR ====================================================== */
static struct option gLongOptions[] = {
    {"help", no_argument, NULL, 'h'},
    {"server", required_argument, NULL, 's'},
    {"port", required_argument, NULL, 'p'},
    {"workload", required_argument, NULL, 'w'},
    {"nthreads", required_argument, NULL, 't'},
    {"nrequests", required_argument, NULL, 'n'},
    {NULL, 0, NULL, 0}};

static void Usage() { fprintf(stderr, "%s", USAGE); }

static void localPath(char *req_path, char *local_path)
{
  static int counter = 0;

  sprintf(local_path, "%s-%06d", &req_path[1], counter++);
}

static FILE *openFile(char *path)
{
  char *cur, *prev;
  FILE *ans;

  /* Make the directory if it isn't there */
  prev = path;
  while (NULL != (cur = strchr(prev + 1, '/')))
  {
    *cur = '\0';

    if (0 > mkdir(&path[0], S_IRWXU))
    {
      if (errno != EEXIST)
      {
        perror("Unable to create directory");
        exit(EXIT_FAILURE);
      }
    }

    *cur = '/';
    prev = cur;
  }

  if (NULL == (ans = fopen(&path[0], "w")))
  {
    perror("Unable to open file");
    exit(EXIT_FAILURE);
  }

  return ans;
}

/* Callbacks ========================================================= */
static void writecb(void *data, size_t data_len, void *arg)
{
  FILE *file = (FILE *)arg;
  fwrite(data, 1, data_len, file);
}

static pthread_t *workers;
static short port;
static char *server;
static int exit_flag = 0;
pthread_cond_t gfc_cond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t gfc_mutex = PTHREAD_MUTEX_INITIALIZER;
steque_t *queue;

void *gfc_send_req(void *i)
{
  char *req_path = NULL;
  char local_path[PATH_BUFFER_SIZE];
  gfcrequest_t *gfr = NULL;
  FILE *file = NULL;
  int returncode = 0;
  int thread_id = *(int *)i;
  /* Build your queue of requests here */
  while (1)
  {
    /* Note that when you have a worker thread pool, you will need to move this
     * logic into the worker threads */
    pthread_mutex_lock(&gfc_mutex);
    while (steque_isempty(queue))
    {
      if (exit_flag)
      {
        pthread_mutex_unlock(&gfc_mutex);
        pthread_cond_signal(&gfc_cond);
        return NULL;
      }
      pthread_cond_wait(&gfc_cond, &gfc_mutex);
    }

    req_path = steque_pop(queue);
    pthread_mutex_unlock(&gfc_mutex);

    printf("thread %d requesting %s\n", thread_id, req_path);

    localPath(req_path, local_path);

    file = openFile(local_path);

    gfr = gfc_create();
    gfc_set_path(&gfr, req_path);

    gfc_set_port(&gfr, port);
    gfc_set_server(&gfr, server);
    gfc_set_writearg(&gfr, file);
    gfc_set_writefunc(&gfr, writecb);

    // fprintf(stdout, "Requesting %s%s\n", server, req_path);

    if (0 > (returncode = gfc_perform(&gfr)))
    {
      fprintf(stdout, "gfc_perform returned an error %d\n", returncode);
      fclose(file);
      if (0 > unlink(local_path))
        fprintf(stderr, "warning: unlink failed on %s\n", local_path);
    }
    else
    {
      fclose(file);
    }

    if (gfc_get_status(&gfr) != GF_OK)
    {
      if (0 > unlink(local_path))
      {
        fprintf(stderr, "warning: unlink failed on %s\n", local_path);
      }
    }

    printf("thread %d finished\n", thread_id);
    fprintf(stdout, "Status: %s\n", gfc_strstatus(gfc_get_status(&gfr)));
    fprintf(stdout, "Received %zu of %zu bytes\n", gfc_get_bytesreceived(&gfr),

            gfc_get_filelen(&gfr));

    gfc_cleanup(&gfr);

    /*
     * note that when you move the above logic into your worker thread, you will
     * need to coordinate with the boss thread here to effect a clean shutdown.
     */
  }

  return NULL;
}

void init_threads(size_t nthreads)
{
  workers = malloc(sizeof(pthread_t) * nthreads);
  int thread_id[nthreads];

  for (int i = 0; i < nthreads; i++)
  {
    thread_id[i] = i;
    if (pthread_create(&workers[i], NULL, gfc_send_req, &thread_id[i]) != 0)
    {
      fprintf(stderr, "Can't create thread %d\n", i);
      exit(1);
    }

    printf("Created thread %d\n", i);
  }
}

void cleanup_threads(size_t nthreads)
{
  for (int i = 0; i < nthreads; i++)
  {
    pthread_join(workers[i], NULL);
  }

  free(workers);
  steque_destroy(queue);
  free(queue);
}

/* Main ========================================================= */
int main(int argc, char **argv)
{
  /* COMMAND LINE OPTIONS ============================================= */
  char *workload_path = "workload.txt";
  server = "localhost";
  int option_char = 0;
  port = 39474;
  int nthreads = 8;
  int nrequests = 14;
  char *req_path;

  setbuf(stdout, NULL); // disable caching

  // Parse and set command line arguments
  while ((option_char = getopt_long(argc, argv, "p:n:hs:t:r:w:", gLongOptions,
                                    NULL)) != -1)
  {
    switch (option_char)
    {

    case 's': // server
      server = optarg;
      break;
    case 'w': // workload-path
      workload_path = optarg;
      break;
    case 'r': // nrequests
    case 'n': // nrequests
      nrequests = atoi(optarg);
      break;
    case 't': // nthreads
      nthreads = atoi(optarg);
      break;
    case 'p': // port
      port = atoi(optarg);
      break;
    default:
      Usage();
      exit(1);

    case 'h': // help
      Usage();
      exit(0);
    }
  }

  if (EXIT_SUCCESS != workload_init(workload_path))
  {
    fprintf(stderr, "Unable to load workload file %s.\n", workload_path);
    exit(EXIT_FAILURE);
  }
  if (port > 65331)
  {
    fprintf(stderr, "Invalid port number\n");
    exit(EXIT_FAILURE);
  }
  if (nthreads < 1 || nthreads > MAX_THREADS)
  {
    fprintf(stderr, "Invalid amount of threads\n");
    exit(EXIT_FAILURE);
  }
  gfc_global_init();

  queue = malloc(sizeof(steque_t));
  steque_init(queue);

  // add your threadpool creation here
  init_threads(nthreads);

  /* Build your queue of requests here */
  for (int i = 0; i < nrequests; i++)
  {
    /* Note that when you have a worker thread pool, you will need to move this
     * logic into the worker threads */
    req_path = workload_get_path();

    if (strlen(req_path) > PATH_BUFFER_SIZE)
    {
      fprintf(stderr, "Request path exceeded maximum of %d characters\n.", PATH_BUFFER_SIZE);
      exit(EXIT_FAILURE);
    }

    pthread_mutex_lock(&gfc_mutex);
    steque_enqueue(queue, req_path);
    pthread_mutex_unlock(&gfc_mutex);
    pthread_cond_signal(&gfc_cond);

    /*
     * note that when you move the above logic into your worker thread, you will
     * need to coordinate with the boss thread here to effect a clean shutdown.
     */
  }

  exit_flag = 1;
  pthread_cond_signal(&gfc_cond);

  cleanup_threads(nthreads);
  gfc_global_cleanup(); /* use for any global cleanup for AFTER your thread
                         pool has terminated. */

  return 0;
}
