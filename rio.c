// Robust I/O package

#include "rio.h"
#include "DieWithMessage.h"

void rio_readinitb(rio_t *rp, int fd){
  rp->rio_fd = fd;
  rp->rio_cnt = 0;
  rp->rio_bufptr = rp->rio_buf;
}

ssize_t send_data(int fd, void *usrbuf, size_t n){
  size_t nleft = n;
  ssize_t nwritten;
  char *bufp = usrbuf;

  while (nleft > 0){
    if ((nwritten = write(fd, bufp, nleft)) <= 0){
      if (errno == EINTR)  /* interrupted by sig handler return */
        nwritten = 0;    /* and call write() again */
      else
        return -1;       /* errorno set by write() */
    }
    nleft -= nwritten;
    bufp += nwritten;
  }
  return n;
}


/*
 * rio_read - This is a wrapper for the Unix read() function that
 *    transfers min(n, rio_cnt) bytes from an internal buffer to a user
 *    buffer, where n is the number of bytes requested by the user and
 *    rio_cnt is the number of unread bytes in the internal buffer. On
 *    entry, rio_read() refills the internal buffer via a call to
 *    read() if the internal buffer is empty.
 */
/* $begin rio_read */

static ssize_t rio_read(rio_t *rp, char *usrbuf, size_t n){
  int cnt;
  while (rp->rio_cnt <= 0){  /* refill if buf is empty */

    rp->rio_cnt = read(rp->rio_fd, rp->rio_buf,
                       sizeof(rp->rio_buf));
    if (rp->rio_cnt < 0){
      if (errno != EINTR) /* interrupted by sig handler return */
        return -1;
    }
    else if (rp->rio_cnt == 0)  /* EOF */
        return 0;
    else
        rp->rio_bufptr = rp->rio_buf; /* reset buffer ptr */
  }

  /* Copy min(n, rp->rio_cnt) bytes from internal buf to user buf */
  cnt = n;
  if (rp->rio_cnt < n)
      cnt = rp->rio_cnt;
  memcpy(usrbuf, rp->rio_bufptr, cnt);
  rp->rio_bufptr += cnt;
  rp->rio_cnt -= cnt;
  return cnt;
}

/*
 * rio_readlineb - robustly read a text line (buffered)
 */
ssize_t rio_readlineb(rio_t *rp, void *usrbuf, size_t maxlen){
  int n, rc;
  char c, *bufp = usrbuf;

  for (n = 1; n < maxlen; n++){
    if ((rc = rio_read(rp, &c, 1)) == 1){
      *bufp++ = c;
      if (c == '\n')
        break;
    } else if (rc == 0){
      if (n == 1)
        return 0; /* EOF, no data read */
      else
        break;    /* EOF, some data was read */
    } else
      return -1;    /* error */
  }
  *bufp = 0;
  return n;
}

//Linux sendfile 
ssize_t sendfile_to(int out_fd, int in_fd, off_t *offset, size_t count)
{
  off_t orig;
  char buf[BUF_SIZE];
  size_t toRead, numRead, numSent, totSent;

  if (offset != NULL) {

    /* Save current file offset and set offset to value in '*offset' */

    orig = lseek(in_fd, 0, SEEK_CUR);
    if (orig == -1)
        return -1;
    if (lseek(in_fd, *offset, SEEK_SET) == -1)
        return -1;
  }

  totSent = 0;

  while (count > 0) {
    if(BUF_SIZE > count)
        toRead = count;
    else
        toRead = BUF_SIZE;

    numRead = read(in_fd, buf, toRead);
    if (numRead == -1)
        return -1;
    if (numRead == 0)
        break;                      /* EOF */

    numSent = write(out_fd, buf, numRead);
    if (numSent == -1)
        return -1;
    if (numSent == 0)               /* Should never happen */
       DieWithSystemMessage("sendfile: write() transferred 0 bytes");

    count -= numSent;
    totSent += numSent;
  }

  if (offset != NULL) {
    /* Return updated file offset in '*offset', and reset the file offset
       to the value it had when we were called. */

    *offset = lseek(in_fd, 0, SEEK_CUR);
    if (*offset == -1)
        return -1;
    if (lseek(in_fd, orig, SEEK_SET) == -1)
        return -1;
  }

  return totSent;
}

