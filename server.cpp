#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <netinet/in.h>

#include <sys/types.h> 
#include <sys/socket.h>

void error(const char *msg)
{
  perror(msg);
  exit(1);
}


int main(int argc, char *argv[])
{
  if (argc < 2) {
    fprintf(stderr, "Usage: %s <port>\n", argv[0]);
    exit(1);
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

  int result = bind(
      sockfd,
      (struct sockaddr *) &serv_addr,
      sizeof(serv_addr));

  if (result < 0) {
    error("ERROR on binding");
  }

  listen(sockfd, 5);

  struct sockaddr_in cli_addr;
  socklen_t clilen = sizeof(cli_addr);

  int newsockfd = accept(
      sockfd, 
      (struct sockaddr *) &cli_addr, 
      &clilen);

  if (newsockfd < 0)  {
    error("ERROR on accept");
  }

  char buffer[256];
  memset(buffer, 0, 256);

  int n = read(newsockfd, buffer, 255);
  if (n < 0) {
    error("ERROR reading from socket");
  }

  printf("Here is the message: %s\n",buffer);

  n = write(newsockfd, "I got your message", 18);

  if (n < 0) {
    error("ERROR writing to socket");
  }

  close(newsockfd);
  close(sockfd);

  return 0; 
}
