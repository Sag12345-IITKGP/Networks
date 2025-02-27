#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include "ksocket.h"
#define BUFSIZE 1024

int main(int argc,char* argv[]){
  if(argc!=5){
    printf("Usage: %s <src_ip> <src_port> <dest_ip> <dest_port>\n",argv[0]);
    return 0;
  }
  int sockfd=k_socket(AF_INET,SOCK_KTP,0);
  if(sockfd<0){
    perror("Error in socket creation");
    exit(1);
  }
  printf("Socket created\n");

  char src_ip[16],dest_ip[16];
  strcpy(src_ip,argv[1]);
  strcpy(dest_ip,argv[3]);
  uint16_t src_port=atoi(argv[2]);
  uint16_t dest_port=atoi(argv[4]);

  if(k_bind(src_ip,src_port,dest_ip,dest_port)<0){
    perror("Error in binding");
    exit(1);
  }
  printf("Bind successful\n");

  struct sockaddr_in serveraddr;
  serveraddr.sin_family=AF_INET;
  serveraddr.sin_port=htons(dest_port);
  serveraddr.sin_addr.s_addr=inet_addr(dest_ip);

  int fd=open("file.txt",O_RDONLY);
  if(fd<0){
    perror("Error in opening file");
    exit(1);
  }
  printf("Sending file...\n");
  char buffer[BUFSIZE];
  int seq=1;
  int read_bytes;
  int sent_bytes;
  while((read_bytes=read(fd,buffer,BUFSIZE))>0){
    printf("Sending %d bytes\n",read_bytes);
    while((sent_bytes=k_sendto(sockfd,buffer,read_bytes,0,(struct sockaddr*)&serveraddr,sizeof(serveraddr)))<0 && errno==ENOSPACE){
      sleep(1);
    }
    if(sent_bytes<0){
      perror("Error in sending");
      exit(1);
    }
    printf("Sent %d bytes, seq=%d\n",sent_bytes,seq);
    seq=(seq+1)%10;
  }
  buffer[0]='#';
  while((sent_bytes=k_sendto(sockfd,buffer,1,0,(struct sockaddr*)&serveraddr,sizeof(serveraddr)))<0 && errno==ENOSPACE){
    sleep(1);
  }
  if(sent_bytes<0){
    perror("Error in sending");
    exit(1);
  }
  printf("Sent EOF\n");
  printf("File Successfully Sent\n");
  close(fd);

  if(k_close(sockfd)<0){
    perror("Error in closing socket");
    exit(1);
  }
  return 0;
}


