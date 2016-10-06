#include <netdb.h> 
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
  if (argc < 3) {
    fprintf(stderr, "Usage: %s <hostname> <port>\n", argv[0]);
    exit(1);
  }

  struct hostent *server = gethostbyname(argv[1]);
  if (server == NULL) {
    fprintf(stderr, "ERROR, no such host\n");
    exit(1);
  }

  int portno = atoi(argv[2]);

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

  printf("Please enter the message: ");

  char buffer[256];
  memset(buffer, 0, 256);

  fgets(buffer, 255, stdin);

  int n = write(sockfd,buffer,strlen(buffer));
  if (n < 0) {
    error("ERROR writing to socket");
  }

  memset(buffer, 0, 256);

  n = read(sockfd, buffer, 255);
  if (n < 0)  {
    error("ERROR reading from socket");
  }

  printf("%s\n",buffer);

  close(sockfd);

  return 0;
}
