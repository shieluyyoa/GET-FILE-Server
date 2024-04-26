#include "gfserver-student.h"
#include "gfserver.h"
#include "workload.h"
#include "content.h"
#include "pthread.h"
#include "steque.h"
#include "stdlib.h"
#include <sys/stat.h>
#include <fcntl.h>

#define BUFSIZE 2048

//
//  The purpose of this function is to handle a get request
//
//  The ctx is a pointer to the "context" operation and it contains connection state
//  The path is the path being retrieved
//  The arg allows the registration of context that is passed into this routine.
//  Note: you don't need to use arg. The test code uses it in some cases, but
//        not in others.
//
extern pthread_mutex_t gfs_mutex;
extern pthread_cond_t gfs_cond;
extern steque_t *queue;

typedef struct gfs_queue_ctx
{
	gfcontext_t *ctx;
	const char *path;
} gfs_queue_ctx;

static void enqueue_gfs_req(gfcontext_t *ctx, const char *path)
{
	gfs_queue_ctx *new_ctx = NULL;

	new_ctx = malloc(sizeof(gfs_queue_ctx));
	new_ctx->ctx = ctx;
	new_ctx->path = path;

	pthread_mutex_lock(&gfs_mutex);
	steque_enqueue(queue, new_ctx);
	pthread_mutex_unlock(&gfs_mutex);
	pthread_cond_signal(&gfs_cond);
}

gfh_error_t gfs_handler(gfcontext_t **ctx, const char *path, void *arg)
{
	if (path == NULL)
	{
		return gfh_failure;
	}

	if (ctx == NULL)
	{
		return gfh_failure;
	}

	enqueue_gfs_req(*ctx, path);
	*ctx = NULL;
	return gfh_success;
}

int sendall(int s, const void *buf, size_t len)
{
	size_t total = 0;		// how many bytes we've sent
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

ssize_t gfs_transfer_file(gfcontext_t **ctx, const char *path)
{
	int fd;
	ssize_t bytes_sent, file_len;
	char buffer[BUFSIZE];

	fd = content_get(path);
	if (fd < 0)
	{
		gfs_sendheader(ctx, GF_FILE_NOT_FOUND, 0);
		printf("Error: file not found\n");
		return -1;
	}

	struct stat st;
	if (fstat(fd, &st) < 0)
	{
		gfs_sendheader(ctx, GF_FILE_NOT_FOUND, 0);
		printf("Error: file not found\n");
		return -1;
	}
	file_len = st.st_size;
	printf("File length %ld\n", file_len);
	gfs_sendheader(ctx, GF_OK, file_len);
	printf("Sending file %s\n", path);
	bytes_sent = 0;
	while (bytes_sent < file_len)
	{
		if (pread(fd, buffer, BUFSIZE, bytes_sent) <= 0)
		{
			printf("Error reading file\n");
			gfs_abort(ctx);
			return -1;
		}

		bytes_sent += gfs_send(ctx, buffer, BUFSIZE);
	}

	return bytes_sent;
}