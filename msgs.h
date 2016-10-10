#ifndef MSGS_H
#define MSGS_H

#include <termios.h>

#include <sys/ioctl.h>

#include "common.h"

enum msg_type
{
  CMD_MSG,
  IO_MSG,
  TERMIOS_MSG,
  WINSIZE_MSG,
};


struct cmd_msg
{
  bool tty;
  struct termios termios;
  struct winsize winsize;
  int num_cmd_strings;
  int strtab_size;
  char strtab[];
};


struct io_msg
{
  int destfd;
  int data_size;
  char data[];
};


struct termios_msg
{
  struct termios termios;
};


struct winsize_msg
{
  struct winsize winsize;
};


struct msg_wrapper
{
  int type;
  union {
    struct cmd_msg cmd;
    struct io_msg io;
    struct termios_msg termios;
    struct winsize_msg winsize;
  } msg;
};


static inline int send_cmd_msg(
    int fd,
    char **cmd,
    int num_elements,
    bool tty,
    struct termios *termios,
    struct winsize *winsize)
{
  struct cmd_msg message = {0};
  int string_lengths[num_elements];

  message.tty = tty;

  if (termios != NULL) {
    message.termios = *termios;
  }

  if (winsize != NULL) {
    message.winsize = *winsize;
  }

  message.num_cmd_strings = num_elements;

  for (int i = 0; i < num_elements; i++) {
    string_lengths[i] = strlen(cmd[i]) + 1;
    message.strtab_size += string_lengths[i];
  }

  int type = CMD_MSG;
  int n = write_all(fd, (char *)&type, sizeof(int));
  if (n < 0) {
    return n;
  }

  if (n < sizeof(type)) {
    return -1;
  }

  n = write_all(fd, (char *)&message, sizeof(cmd_msg));
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


static inline int send_io_msg(int fd, int destfd, char *buffer, int size)
{
  struct msg_wrapper message;
  message.type = IO_MSG;
  message.msg.io.destfd = destfd;
  message.msg.io.data_size = size;

  int n = write_all(fd, (char*)&message, sizeof(message.type));
  if (n < 0) {
    return n;
  }

  n = write_all(fd, (char*)&message.msg.io, sizeof(struct io_msg));
  if (n < 0) {
    return n;
  }

  n = write_all(fd, buffer, size);
  if (n < 0) {
    return n;
  }

  return 0;
}


static inline int send_termios_msg(int fd, struct termios *termios)
{
  struct msg_wrapper message;
  message.type = TERMIOS_MSG;
  message.msg.termios.termios = *termios;

  int n = write_all(fd, (char*)&message, sizeof(message.type));
  if (n < 0) {
    return n;
  }

  n = write_all(fd, (char*)&message.msg.termios, sizeof(struct termios_msg));
  if (n < 0) {
    return n;
  }

  return 0;
}


static inline int send_winsize_msg(int fd, struct winsize *winsize)
{
  struct msg_wrapper message;
  message.type = WINSIZE_MSG;
  message.msg.winsize.winsize = *winsize;

  int n = write_all(fd, (char*)&message, sizeof(message.type));
  if (n < 0) {
    return n;
  }

  n = write_all(fd, (char*)&message.msg.winsize, sizeof(struct winsize_msg));
  if (n < 0) {
    return n;
  }

  return 0;
}


static inline int recv_cmd_msg(int fd, struct msg_wrapper **message)
{
  struct cmd_msg cmd_msg;

  int n = read_all(fd, (char *)&cmd_msg, sizeof(struct cmd_msg));
  if (n < 0) {
    return n;
  }

  if (n < sizeof(cmd_msg)) {
    return -1;
  }

  *message = (struct msg_wrapper*) malloc(
      sizeof(struct msg_wrapper) + cmd_msg.strtab_size);

  (*message)->msg.cmd = cmd_msg;

  n = read_all(
    fd,
    (*message)->msg.cmd.strtab,
    (*message)->msg.cmd.strtab_size);

  if (n < 0) {
    return n;
  }

  if (n < ((*message)->msg.cmd.strtab_size)) {
    return -1;
  }

  return 0;
}


static inline int recv_msg(int fd, struct msg_wrapper **message)
{
  int type;

  int n = read_all(fd, (char *)&type, sizeof(type));
  if (n < 0) {
    return n;
  }

  if (n < sizeof(type)) {
    return -1;
  }

  switch (type) {
    case CMD_MSG:
      return recv_cmd_msg(fd, message);
    case IO_MSG:
      break;
    case TERMIOS_MSG:
      break;
    case WINSIZE_MSG:
      break;
  }

  assert(0);
}


struct async_msg_state
{
  int type;
  int phase;
  int phase_total;
  int msg_total;
  bool finished;
  struct msg_wrapper *message;
  char buffer[sizeof((struct msg_wrapper*)0)->msg];
};


int recv_msg_async(
    int fd,
    struct async_msg_state *state)
{
  int phase_size = 0;
  char *phase_dst = NULL;

  while (true) {
    switch (state->phase) {
      case 0:
        phase_size = sizeof(state->type);
        phase_dst = state->buffer;
        break;
      case 1:
        state->type = *((int*)state->buffer);

        if (state->type == CMD_MSG) {
          phase_size = sizeof(struct cmd_msg);
        }
        if (state->type == IO_MSG) {
          phase_size = sizeof(struct io_msg);
        }
        if (state->type == TERMIOS_MSG) {
          phase_size = sizeof(struct termios_msg);
        }
        if (state->type == WINSIZE_MSG) {
          phase_size = sizeof(struct winsize_msg);
        }

        phase_dst = state->buffer;

        break;
      case 2:
        if (state->type == CMD_MSG) {
          phase_size = ((struct cmd_msg*)state->buffer)->strtab_size;

          if (state->phase_total == 0) {
            state->message = (struct msg_wrapper*) malloc(
                sizeof(struct msg_wrapper) + phase_size);

            state->message->type = state->type;
            state->message->msg.cmd =
              *((struct cmd_msg*)state->buffer);

            phase_dst = (char*) state->message->msg.cmd.strtab;
          }
        }
        if (state->type == IO_MSG) {
          phase_size = ((struct io_msg*)state->buffer)->data_size;

          if (state->phase_total == 0) {
            state->message = (struct msg_wrapper*) malloc(
                sizeof(struct msg_wrapper) + phase_size);

            state->message->type = state->type;
            state->message->msg.io =
              *((struct io_msg*)state->buffer);

            phase_dst = (char*) state->message->msg.io.data;
          }
        }
        if (state->type == TERMIOS_MSG) {
          if (state->phase_total == 0) {
            state->message = (struct msg_wrapper*) malloc(
                sizeof(struct msg_wrapper));

            state->message->type = state->type;
            state->message->msg.termios =
              *((struct termios_msg*)state->buffer);

            state->finished = true;
            return 0;
          }
        }
        if (state->type == WINSIZE_MSG) {
          if (state->phase_total == 0) {
            state->message = (struct msg_wrapper*) malloc(
                sizeof(struct msg_wrapper));

            state->message->type = state->type;
            state->message->msg.winsize =
              *((struct winsize_msg*)state->buffer);

            state->finished = true;
            return 0;
          }
        }
        break;
      case 3:
        state->finished = true;
        return 0;
    }

    int n = read_all(
      fd,
      phase_dst + state->phase_total,
      phase_size - state->phase_total);

    if (n < 0) {
      return n;
    }

    if (n == 0) {
      return state->msg_total;
    }

    state->phase_total += n;
    state->msg_total += n;

    if (state->phase_total < phase_size) {
      return state->msg_total;
    }

    assert(state->phase_total == phase_size);

    state->phase_total = 0;
    state->phase++;
  }
}


static inline char **build_cmd_array(struct cmd_msg *message)
{
  char **cmd = (char **)malloc(message->num_cmd_strings + 1);
  char *strtab_ptr = message->strtab;

  for (int i = 0; i < message->num_cmd_strings; i++) {
    cmd[i] = strtab_ptr;
    strtab_ptr += strlen(strtab_ptr) + 1;
  }
  cmd[message->num_cmd_strings] = NULL;

  return cmd;
}


static inline void dump_cmd_msg(struct cmd_msg *message)
{
  printf("tty: %s\n", message->tty ? "true" : "false");
  printf("num_cmd_strings: %d\n", message->num_cmd_strings);
  printf("strtab_size: %d\n", message->strtab_size);

  char *strtab_ptr = message->strtab;
  for (int i = 0; i < message->num_cmd_strings; i++) {
    printf("string[%d]: %s\n", i, strtab_ptr);
    strtab_ptr += strlen(strtab_ptr) + 1;
  }
}


static inline void dump_io_msg(struct io_msg *message)
{
  printf("destfd: %d\n", message->destfd);
  printf("data_size: %d\n", message->data_size);
  printf("data: %s\n", message->data);
}

#endif // MSGS_H
