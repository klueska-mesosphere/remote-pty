#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/wait.h>

struct cmd_msg
{
  int size;
  bool tty;
  int num_strings;
  char strtab[];
};


static inline void error(const char *msg)
{
  perror(msg);
  exit(1);
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


static inline int send_cmd_msg(int fd, char **cmd, int num_elements, bool tty)
{
  int strtab_size = 0;
  int string_lengths[num_elements];

  for (int i = 0; i < num_elements; i++) {
    string_lengths[i] = strlen(cmd[i]) + 1;
    strtab_size += string_lengths[i];
  }

  int message_size = sizeof(cmd_msg) + strtab_size;

  struct cmd_msg message;
  message.size = message_size;
  message.tty = tty;
  message.num_strings = num_elements;

  int n = write_all(fd, (char *)&message, sizeof(cmd_msg));
  if (n < 0) {
    return n;
  }

  for (int i = 0; i < num_elements; i++) {
    int n = write_all(fd, cmd[i], string_lengths[i]);
    if (n < 0) {
      return n;
    }
  }

  return 0;
}


static inline int read_cmd_msg(int fd, struct cmd_msg** message)
{
  struct cmd_msg message_;

  int n = read_all(fd, (char *)&message_, sizeof(cmd_msg));
  if (n < 0) {
    return n;
  }

  if (n < sizeof(cmd_msg)) {
    return -1;
  }

  *message = (struct cmd_msg*)malloc(message_.size);

  **message = message_;

  n = read_all(fd, (*message)->strtab, (*message)->size - sizeof(cmd_msg));
  if (n < 0) {
    return n;
  }

  if (n < ((*message)->size - sizeof(cmd_msg))) {
    return -1;
  }

  return 0;
}

static inline char **build_cmd_array(struct cmd_msg* message)
{
  char **cmd = (char **)malloc(message->num_strings + 1);
  char *strtab_ptr = message->strtab;

  for (int i = 0; i < message->num_strings; i++) {
    cmd[i] = strtab_ptr;
    strtab_ptr += strlen(strtab_ptr) + 1;
  }
  cmd[message->num_strings] = NULL;

  return cmd;
}

static inline void dump_cmd_msg(struct cmd_msg* message)
{
  printf("size: %d\n", message->size);
  printf("tty: %s\n", message->tty ? "true" : "false");
  printf("num_strings: %d\n", message->num_strings);

  char *strtab_ptr = message->strtab;
  for (int i = 0; i < message->num_strings; i++) {
    printf("string[%d]: %s\n", i, strtab_ptr);
    strtab_ptr += strlen(strtab_ptr) + 1;
  }
}
