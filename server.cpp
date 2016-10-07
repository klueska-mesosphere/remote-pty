#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <netinet/in.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>

#include "common.h"

void usage(char *cmd)
{
  fprintf(stderr, "Usage: %s <port>\n", cmd);
  exit(1);
}

int max3(int a, int b, int c)
{
  int m = a;
  (m < b) && (m = b);
  (m < c) && (m = c);
  return m;
}

int main(int argc, char *argv[])
{
  if (argc < 2) {
    usage(argv[0]);
  }

  int portno = atoi(argv[1]);

  int sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd < 0)  {
    error("ERROR opening socket");
  }

  struct sockaddr_in serv_addr;
  memset((char *) &serv_addr, 0, sizeof(serv_addr));
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_addr.s_addr = INADDR_ANY;
  serv_addr.sin_port = htons(portno);

  int yes = 1;
  int result = setsockopt(
      sockfd,
      SOL_SOCKET,
      SO_REUSEADDR,
      &yes,
      sizeof(yes));

  if (result < 0) {
    error("ERROR on setsockopt");
  }

  result = bind(
      sockfd,
      (struct sockaddr *) &serv_addr,
      sizeof(serv_addr));

  if (result < 0) {
    error("ERROR on binding");
  }

  result = listen(sockfd, 64);

  if (result < 0) {
    error("ERROR on listen");
  }

  while (true) {
    struct sockaddr_in cli_addr;
    socklen_t clilen = sizeof(cli_addr);

    int newsockfd = accept(
        sockfd,
        (struct sockaddr *) &cli_addr,
        &clilen);

    if (newsockfd < 0)  {
      error("ERROR on accept");
    }

    int stdin_pipe[2];
    if (pipe(stdin_pipe) < 0) {
      error("ERROR creating a std_in pipe.");
    }

    int stdout_pipe[2];
    if (pipe(stdout_pipe) < 0) {
      error("ERROR creating a std_out pipe.");
    }
    if (make_non_blocking(stdout_pipe[0]) < 0) {
      error("ERROR making stdout_pipe non blocking");
    }

    int stderr_pipe[2];
    if (pipe(stderr_pipe) < 0) {
      error("ERROR creating a std_err pipe.");
    }
    if (make_non_blocking(stderr_pipe[0]) < 0) {
      error("ERROR making stderr_pipe non blocking");
    }

    if (make_non_blocking(newsockfd) < 0) {
      error("ERROR making sockfd non blocking");
    }

    int pid = fork();

    if (pid == 0) {
      struct cmd_msg *message;
      int n = read_cmd_msg(newsockfd, &message);
      if (n < 0) {
        error("ERROR reading cmd from socket");
      }

      char **cmd = build_cmd_array(message);

      if (dup2(stdin_pipe[0], STDIN_FILENO) != 0 ||
          dup2(stdout_pipe[1], STDOUT_FILENO) != 1 ||
          dup2(stderr_pipe[1], STDERR_FILENO) != 2 ) {
        error("ERROR duplicating socket for stdin/stdout/stderr");
      }

      close(newsockfd);
      close(sockfd);

      close(stdin_pipe[0]);
      close(stdout_pipe[0]);
      close(stderr_pipe[0]);
      close(stdin_pipe[1]);
      close(stdout_pipe[1]);
      close(stderr_pipe[1]);

      result = execvp(cmd[0], cmd);
      if (result < 0) {
        error("ERROR execing cmd");
      }
    }

    while(true) {
      fd_set readfds;
      FD_ZERO(&readfds);
 
      FD_SET(stdout_pipe[0], &readfds);
      FD_SET(stderr_pipe[0], &readfds);

      result = select(
        max3(
          sockfd, 
          stdout_pipe[0], 
          stderr_pipe[0]) + 1, 
        &readfds, 
        NULL,
        NULL,
        NULL);

      if (result < 0) {
        error("ERROR waiting on select");
      }

      if (result == 0) {
        continue;
      }

      int infd, outfd;

      if (FD_ISSET(stdout_pipe[0], &readfds)) {
        infd = stdout_pipe[0];
        outfd = newsockfd;
      }
      if (FD_ISSET(stderr_pipe[0], &readfds)) {
        infd = stderr_pipe[0];
        outfd = newsockfd;
      }

      char buffer[256];

      int n = read_all(infd, buffer, 256);
      if (n < 0) {
        error("ERROR reading from infd");
      }

      n = write_all(outfd, buffer, n);
      if (n < 0) {
        error("ERROR writing to outfd");
      }

      if (n < sizeof(buffer)/sizeof(buffer[0])) {
        break;
      }
    }

    close(newsockfd);
    close(stdin_pipe[0]);
    close(stdout_pipe[0]);
    close(stderr_pipe[0]);
    close(stdin_pipe[1]);
    close(stdout_pipe[1]);
    close(stderr_pipe[1]);  
  }

  close(sockfd);

  return 0;
}
