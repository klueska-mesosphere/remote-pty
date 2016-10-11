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

volatile int sockfd = -1;
volatile int ttyfd = -1;
struct termios original_termios;
struct winsize original_winsize;


void sigterm(int sig)
{
   int result = tcsetattr(ttyfd, TCSANOW, &original_termios);
   if (result < 0) {
     error("ERROR setting original termios parameters");
   }

   result = ioctl(0, TIOCSWINSZ, &original_winsize);
   if (result < 0) {
     error("ERROR setting original winsize parameters");
   }

   _exit(0);
}


void sigwinch(int sig){
  signal(SIGWINCH, SIG_IGN);

  struct winsize winsize;
  int result = ioctl(0, TIOCGWINSZ, &winsize);
  if (result < 0) {
    error("ERROR getting winsize");
  }

  int n = send_winsize_msg(sockfd, &winsize);
  if (n < 0) {
    error("ERROR writing to sockfd");
  }

  signal(SIGWINCH, sigwinch);
}


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

  sockfd = socket(AF_INET, SOCK_STREAM, 0);
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

  int infd = STDIN_FILENO;
  int outfd = STDOUT_FILENO;
  int errfd = STDERR_FILENO;

  if (tty) {
    char *ttyname = ctermid(NULL);
    if (ttyname == NULL) {
      error("ERROR getting tty name");
    }

    ttyfd = open(ttyname, O_RDWR);
    if (ttyfd < 0) {
      error("ERROR opening tty device");
    }

    result = tcgetattr(ttyfd, &original_termios);
    if (result < 0) {
      error("ERROR getting termios");
    }

    result = ioctl(0, TIOCGWINSZ, &original_winsize);
    if (result < 0) {
      error("ERROR getting winsize");
    }

    infd = ttyfd;
    outfd = ttyfd;
    errfd = ttyfd;

    signal(SIGTERM, sigterm);
    signal(SIGWINCH, sigwinch);
  }

  int n = send_cmd_msg(
      sockfd,
      cmd,
      argc - cmd_start_idx,
      tty,
      &original_termios,
      &original_winsize);

  if (n < 0) {
    error("ERROR writing cmd to socket");
  }

  result = make_non_blocking(infd);
  if (result < 0) {
    error("ERROR making infd non blocking");
  }

  result = make_non_blocking(sockfd);
  if (result < 0) {
    error("ERROR making sockfd non blocking");
  }

  struct async_msg_state msg_state;
  memset(&msg_state, 0, sizeof(struct async_msg_state));

  int infd_n = -1, sockfd_n = -1;

  while (true) {
    fd_set fd_in;
    FD_ZERO(&fd_in);

    if (infd_n != 0) {
      FD_SET(infd, &fd_in);
    }
    if (sockfd_n != 0) {
      FD_SET(sockfd, &fd_in);
    }

    int maxfd = sockfd > infd ? sockfd : infd;

    int result = select(maxfd + 1, &fd_in, NULL, NULL, NULL);

    if (result < 0) {
      if (errno == EINTR) {
        continue;
      }
      error("ERROR waiting on select");
    }

    if (result == 0) {
      continue;
    }

    if (FD_ISSET(infd, &fd_in)) {
      char buffer[256];

      infd_n = read_all(infd, buffer, 256);
      if (infd_n < 0) {
        error("ERROR reading from infd");
      }

      if (infd_n > 0) {
        if (tty) {
          struct termios termios;
          int result = tcgetattr(ttyfd, &termios);
          if (result < 0) {
            error("ERROR getting termios");
          }

          int n = send_termios_msg(sockfd, &termios);
          if (n < 0) {
            perror("ERROR writing to newsockfd");
            break;
          }
        }

        n = send_io_msg(sockfd, STDIN_FILENO, buffer, infd_n);
        if (n < 0) {
          error("ERROR writing to sockfd");
        }
      }
    }

    if (FD_ISSET(sockfd, &fd_in)) {
      sockfd_n = recv_msg_async(sockfd, &msg_state);

      if (sockfd_n < 0) {
        error("ERROR reading from sockfd");
      }

      if (msg_state.finished) {
        switch (msg_state.message->type) {
          case TERMIOS_MSG: {
            int result = tcsetattr(
                ttyfd,
                TCSANOW,
                &msg_state.message->msg.termios.termios);

            if (result < 0) {
              error("ERROR setting termios parameters");
            }

            break;
          }
          case WINSIZE_MSG: {
            int result = ioctl(
                ttyfd,
                TIOCSWINSZ,
                &msg_state.message->msg.winsize.winsize);

            if (result < 0) {
              error("ERROR setting winsize parameters");
            }

            break;
          }
          case IO_MSG: {
            int destfd = -1;

            switch (msg_state.message->msg.io.destfd) {
              case STDIN_FILENO:
                destfd = infd;
                break;
              case STDOUT_FILENO:
                destfd = outfd;
                break;
              case STDERR_FILENO:
                destfd = errfd;
                break;
            }

            sockfd_n = write_all(
                destfd,
                msg_state.message->msg.io.data,
                msg_state.message->msg.io.data_size);

            if (sockfd_n < 0) {
              error("ERROR writing to stdout");
            }
            break;
          }
        }

        free(msg_state.message);
        memset(&msg_state, 0, sizeof(async_msg_state));
      }
    }

    if ((infd_n == 0) || (sockfd_n == 0)) {
      break;
    }
  }

  if (tty) {
    result = tcsetattr(ttyfd, TCSANOW, &original_termios);
    if (result < 0) {
      error("ERROR setting original termios parameters");
    }

    signal(SIGTERM, SIG_IGN);
    signal(SIGWINCH, SIG_IGN);
    close(ttyfd);
  }

  close(sockfd);

  return 0;
}
