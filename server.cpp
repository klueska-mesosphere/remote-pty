#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <netinet/in.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>

#include "common.h"

volatile bool child_done;


void usage(char *cmd)
{
  fprintf(stderr, "Usage: %s <port>\n", cmd);
  exit(1);
}


void sigchld(int sig)
{
  child_done = true;
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

  signal (SIGCHLD, sigchld);

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

    int stderr_pipe[2];
    if (pipe(stderr_pipe) < 0) {
      error("ERROR creating a std_err pipe.");
    }

    struct cmd_msg *message;
    int n = read_cmd_msg(newsockfd, &message);
    if (n < 0) {
      error("ERROR reading cmd from socket");
    }

    child_done = false;

    int pid = fork();

    if (pid == 0) {
      char **cmd = build_cmd_array(message);

      if (dup2(stdin_pipe[0], STDIN_FILENO) != 0 ||
          dup2(stdout_pipe[1], STDOUT_FILENO) != 1 ||
          dup2(stderr_pipe[1], STDERR_FILENO) != 2 ) {
        error("ERROR duplicating socket for stdin/stdout/stderr");
      }

      close(newsockfd);
      close(sockfd);

      close(stdin_pipe[0]);
      close(stdin_pipe[1]);
      close(stdout_pipe[0]);
      close(stdout_pipe[1]);
      close(stderr_pipe[0]);
      close(stderr_pipe[1]);

      result = execvp(cmd[0], cmd);
      if (result < 0) {
        free(cmd);
        error("ERROR execing cmd");
      }
    }

    if (make_non_blocking(newsockfd) < 0) {
      error("ERROR making sockfd non blocking");
    }

    if (make_non_blocking(stdout_pipe[0]) < 0) {
      error("ERROR making stdout_pipe non blocking");
    }

    if (make_non_blocking(stderr_pipe[0]) < 0) {
      error("ERROR making stderr_pipe non blocking");
    }

    close(stdin_pipe[0]);
    close(stdout_pipe[1]);
    close(stderr_pipe[1]);

    while(true) {
      int sockfd_n = -1, stdout_n = -1, stderr_n = -1;

      fd_set readfds;
      FD_ZERO(&readfds);

      if (sockfd_n != 0) {
        FD_SET(newsockfd, &readfds);
      }
      if (stdout_n != 0) {
        FD_SET(stdout_pipe[0], &readfds);
      }
      if (stderr_n != 0) {
        FD_SET(stderr_pipe[0], &readfds);
      }

      int maxfd = max3(sockfd, stdout_pipe[0], stderr_pipe[0]);

      result = select(maxfd + 1, &readfds, NULL, NULL, NULL);

      if (result < 0) {
        error("ERROR waiting on select");
      }

      if (result == 0) {
        continue;
      }

      if (FD_ISSET(newsockfd, &readfds)) {
        sockfd_n = read_then_write(newsockfd, stdin_pipe[1], 256);
        if (sockfd_n < 0) {
          error("ERROR writing to stdin");
        }
      }

      if (FD_ISSET(stdout_pipe[0], &readfds)) {
        stdout_n = read_then_write(stdout_pipe[0], newsockfd, 256);
        if (stdout_n < 0) {
          error("ERROR reading from stdout");
        }
      }

      if (FD_ISSET(stderr_pipe[0], &readfds)) {
        stderr_n = read_then_write(stderr_pipe[0], newsockfd, 256);
        if (stderr_n < 0) {
          error("ERROR reading from stderr");
        }
      }

      if (sockfd_n == 0) {
        break;
      }

      if (child_done && stdout_n == 0 && stderr_n == 0) {
        break;
      }

    }

    close(newsockfd);

    close(stdin_pipe[1]);
    close(stdout_pipe[0]);
    close(stderr_pipe[0]);

    free(message);

    int status;
    result = wait_all(pid, &status);
    if (result < 0) {
      error("ERROR waiting for pid");
    }
  }

  close(sockfd);

  return 0;
}
