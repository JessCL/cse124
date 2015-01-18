#include <unistd.h>
#include <errno.h>

#define RIO_BUFSIZE 1024
#define BUF_SIZE 4096

typedef struct {
  int rio_fd;                 /* descriptor for this buf */
  int rio_cnt;                /* unread byte in this buf */
  char *rio_bufptr;           /* next unread byte in this buf */
  char rio_buf[RIO_BUFSIZE];  /* internal buffer */
} rio_t;

void rio_readinitb(rio_t *rp, int fd);
ssize_t send_data(int fd, void *usrbuf, size_t n);
static ssize_t rio_read(rio_t *rp, char *usrbuf, size_t n);
ssize_t rio_readlineb(rio_t *rp, void *usrbuf, size_t maxlen);
ssize_t sendfile_to(int out_fd, int in_fd, off_t *offset, size_t count);
