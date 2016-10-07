#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <netinet/in.h>

#include <sys/types.h> 
#include <sys/socket.h>

#include "common.h"

void usage(char *cmd)
{
  fprintf(stderr, "Usage: %s <port>\n", cmd);
  exit(1);
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

  struct sockaddr_in cli_addr;
  socklen_t clilen = sizeof(cli_addr);

  int newsockfd = accept(
      sockfd, 
      (struct sockaddr *) &cli_addr, 
      &clilen);

  if (newsockfd < 0)  {
    error("ERROR on accept");
  }

  struct cmd_msg *message;
  int n = read_cmd_msg(newsockfd, &message);
  if (n < 0) {
    error("ERROR reading cmd from socket");
  }

  char **cmd = build_cmd_array(message);

  dump_cmd_msg(message);

  fclose(stdin);
  fclose(stdout);
  fclose(stderr);

  if (dup(newsockfd) != 0 || dup(newsockfd) != 1 || dup(newsockfd) != 2 ) {
    error("ERROR duplicating socket for stdin/stdout/stderr");
  }

  close(newsockfd);
  close(sockfd);

  result = execvp(cmd[0], cmd);
  if (result < 0) {
    error("ERROR execing cmd");
  }

  return 0; 
}
