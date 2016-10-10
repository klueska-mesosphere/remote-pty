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


#endif // MSGS_H
