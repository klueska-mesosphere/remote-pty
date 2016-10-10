#ifndef COMMON_H
#define COMMON_H

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/wait.h>

static inline void error(const char *msg)
{
  perror(msg);
  exit(1);
}


static inline int max3(int a, int b, int c)
{
  int m = a;
  (m < b) && (m = b);
  (m < c) && (m = c);
  return m;
}


static inline int make_non_blocking(int fd)
{
  int flags = fcntl(fd, F_GETFL, 0);

  if(flags < 0) {
   return flags;
  }

  flags |= O_NONBLOCK;

  return fcntl(fd, F_SETFL, flags);
}


static inline int wait_all(int pid, int *status)
{
  while (true) {
    int status;
    int result = waitpid(-1, &status, 0);

    if (result < 0) {
      if (errno == EINTR) {
        continue;
      }
      return result;
    }

    if (WIFSTOPPED(status)) {
      continue;
    }

    if (pid == result) {
      break;
    }
  }

  return 0;
}


static inline int write_all(int fd, const char *buf, size_t count)
{
  size_t offset = 0;

  while (offset < count) {
    ssize_t length = write(fd, buf + offset, count - offset);

    if (length < 0) {
      if (errno == EINTR) {
        continue;
      }
      return length;
    }

    offset += length;
  }

  assert(offset == count);

  return offset;
}


static inline int read_all(int fd, char *buf, size_t count)
{
  size_t offset = 0;

  while (offset < count) {
    ssize_t length = read(fd, buf + offset, count - offset);

    if (length < 0) {
      if (errno == EINTR) {
        continue;
      }
      if (errno == EWOULDBLOCK) {
        return offset;
      }
      return length;
    }

    if (length == 0) {
      return offset;
    }

    offset += length;
  }

  assert(offset == count);

  return offset;
}


static inline int read_then_write(int infd, int outfd, int bufsize)
{

  char buffer[bufsize];

  int n = read_all(infd, buffer, bufsize);
  if (n < 0) {
    return n;
  }

  if (n == 0) {
    return n;
  }

  n = write_all(outfd, buffer, n);
  if (n < 0) {
    return n;
  }

  return n;
}

#endif // COMMON_H
