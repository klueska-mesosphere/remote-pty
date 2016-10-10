#include <netdb.h> 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <netinet/in.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>

#include "common.h"
#include "msgs.h"

void usage(char *cmd)
{
  fprintf(stderr,
          "Usage: %s <hostname> <port> [--tty] <cmd> [<args...>]\n",
          cmd);
  exit(1);
}


int main(int argc, char *argv[])
{
  if (argc < 4) {
    usage(argv[0]);
  }

  if ((strcmp(argv[3], "--tty") == 0) && (argc < 5)) {
    usage(argv[0]);
  }

  struct hostent *server = gethostbyname(argv[1]);
  if (server == NULL) {
    fprintf(stderr, "ERROR, no such host\n");
    exit(1);
  }

  int portno = atoi(argv[2]);

  bool tty = false;
  int cmd_start_idx = 3;
  if (strcmp(argv[3], "--tty") == 0) {
    tty = true;
    cmd_start_idx = 4;
  }

  char **cmd = &argv[cmd_start_idx];

  int sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd < 0)  {
    error("ERROR opening socket");
  }

  struct sockaddr_in serv_addr;
  memset((char *) &serv_addr, 0, sizeof(serv_addr));
  serv_addr.sin_family = AF_INET;
  memcpy((char *)&serv_addr.sin_addr.s_addr,
         (char *)server->h_addr,
         server->h_length);
  serv_addr.sin_port = htons(portno);

  int result = connect(
      sockfd,
      (struct sockaddr *) &serv_addr,
      sizeof(serv_addr));

  if (result < 0) {
    error("ERROR connecting");
  }

  int n = send_cmd_msg(sockfd, cmd, argc - cmd_start_idx, tty, NULL, NULL);
  if (n < 0) {
    error("ERROR writing cmd to socket");
  }

  result = make_non_blocking(STDIN_FILENO);
  if (result < 0) {
    error("ERROR making stdin non blocking");
  }

  result = make_non_blocking(sockfd);
  if (result < 0) {
    error("ERROR making sockfd non blocking");
  }

  while (true) {
    fd_set fd_in;
    FD_ZERO(&fd_in);

    FD_SET(STDIN_FILENO, &fd_in);
    FD_SET(sockfd, &fd_in);

    int result = select(sockfd + 1, &fd_in, NULL, NULL, NULL);

    if (result < 0) {
      error("ERROR waiting on select");
    }

    if (result == 0) {
      continue;
    }

    int stdin_n = -1, sockfd_n = -1;

    if (FD_ISSET(STDIN_FILENO, &fd_in)) {
      char buffer[256];

      stdin_n = read_all(STDIN_FILENO, buffer, 256);
      if (stdin_n < 0) {
        error("ERROR reading from stdin");
      }

      int n = send_io_msg(sockfd, STDIN_FILENO, buffer, stdin_n);
      if (n < 0) {
        error("ERROR writing to sockfd");
      }
    }
 
    if (FD_ISSET(sockfd, &fd_in)) {
      sockfd_n = read_then_write(sockfd, STDOUT_FILENO, 256);
      if (sockfd_n < 0) {
        error("ERROR writing to stdout");
      }
    }

    if ((stdin_n == 0) || (sockfd_n == 0)) {
      break;
    }
  }

  close(sockfd);

  return 0;
}
