#ifndef KSOCKET_H
#define KSOCKET_H

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <errno.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>


#define T 5
#define p 0.05
#define SOCK_KTP 3
#define N 10
#define P(s) semop(s, &pop, 1)
#define V(s) semop(s, &vop, 1)
#define MAX_SEQ_NUM 256

//Custom errornames
#define ENOTBOUND 200
#define ENOSPACE 201
#define ENOMESSAGE 202


typedef struct net_socket{
  int sock_id;
  char ip_addr[INET_ADDRSTRLEN];
  uint16_t port;
  int err_code;
}NET_SOCKET;


struct window{
  int wnd[256];
  int size;
  int start;
};

typedef struct Shared_Mem{
  int free;
  pid_t pid;
  int udp_sockid;
  char ip_addr[INET_ADDRSTRLEN];
  uint16_t port;
  char send_buffer[10][512];
  int send_buffer_size;
  int sendbufferlen[10];
  time_t last_send_time[256];
  char recv_buffer[10][512];
  int recv_buffer_live[10];
  int rcvbufferlen[10];
  int recv_buffer_base;
  struct window swnd;
  struct window rwnd;
  int nospace;
}SHARED_MEM;



extern SHARED_MEM *SM;
extern NET_SOCKET* sock_info;
extern struct sembuf pop,vop;
extern int semid_SM,semid_sock_info;
extern int shmid_SM,shmid_sock_info;
extern int semid_init,semid_ktp;

// Function Prototypes
int k_socket(int domain,int type,int protocol);
int k_bind(char src_ip[],uint16_t src_port,char dest_ip[],uint16_t dest_port);
ssize_t k_sendto(int sockfd, const void *buf, size_t len, int flags, const struct sockaddr *dest_addr, socklen_t addrlen);
ssize_t k_recvfrom(int sockfd, void* buf, size_t len, int flags, struct sockaddr * src_addr, socklen_t *addrlen);
int k_close(int sockfd);
int drop_Message();

#endif
