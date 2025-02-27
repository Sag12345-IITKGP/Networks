#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <signal.h>
#include <pthread.h>
#include "ksocket.h"



void *GC(){
  while(1){
    sleep(T);
    P(semid_SM);
    for(int i=0;i<N;i++){
      if(SM[i].free==0){
        if(kill(SM[i].pid,0)==-1){
          SM[i].free=1;
        }
      }
    }
    V(semid_SM);
  }
}

void *R(){
  fd_set readfds;
  FD_ZERO(&readfds);
  int mxfd=0;
  while(1){
    fd_set read2fds=readfds;

    struct timeval tv;
    tv.tv_sec=T;
    tv.tv_usec=0;
    int ret=select(mxfd+1,&read2fds,NULL,NULL,&tv);
    if(ret==-1){
      perror("select");
    }
    if(ret==0){
      FD_ZERO(&readfds);
      P(semid_SM);
      mxfd=0;
      for(int i=0;i<N;i++){
        if(!SM[i].free){
          FD_SET(SM[i].udp_sockid,&readfds);
          if(SM[i].udp_sockid>mxfd)mxfd=SM[i].udp_sockid;
          if(SM[i].nospace==1 && SM[i].rwnd.size>0){
            SM[i].nospace=0;
          }
          int last_ack_seq=(SM[i].rwnd.start-1+MAX_SEQ_NUM)%MAX_SEQ_NUM;
          struct sockaddr_in clientaddr;
          clientaddr.sin_family=AF_INET;
          clientaddr.sin_port=htons(SM[i].port);
          clientaddr.sin_addr.s_addr=inet_addr(SM[i].ip_addr);
          char ack[13];
          ack[0]='0';//acknowledgement
          for(int j = 0; j < 8; j++){
            ack[8 - j] = ((last_ack_seq >> j) & 1) + '0';
          }
          for (int j = 0; j < 4; j++){
            ack[12 - j] = ((SM[i].rwnd.size >> j) & 1) + '0';
          }
          sendto(SM[i].udp_sockid,ack,13,0,(struct sockaddr*)&clientaddr,sizeof(clientaddr));
        }
      }
      V(semid_SM);
    }
    if(ret>0){
      P(semid_SM);
      for(int i=0;i<N;i++){
        if(FD_ISSET(SM[i].udp_sockid,&read2fds)){
          char buffer[531];
          struct sockaddr_in clientaddr;
          socklen_t len=sizeof(clientaddr);
          int n=recvfrom(SM[i].udp_sockid,buffer,531,0,(struct sockaddr*)&clientaddr,&len);
          if(drop_Message()){
            continue;
          }
          if(n<0){
            perror("recvfrom");
          }else{
            //printf("Here\n");
            if(buffer[0]=='1'){
              //Data
              int seq=0;
              int len=0;
              //extract seq from buffer
              for(int j=1;j<=8;j++){
                seq=(seq<<1)|(buffer[j]-'0');
              }
              for(int j=9;j<=18;j++){
                len=(len<<1)|(buffer[j]-'0');
              }

              if(seq==SM[i].rwnd.start){
                int buff_idx=SM[i].rwnd.wnd[seq];
                memcpy(SM[i].recv_buffer[buff_idx],buffer+19,len);
                SM[i].recv_buffer_live[buff_idx]=1;
                SM[i].rwnd.size--;
                SM[i].rcvbufferlen[buff_idx]=len;
                while(SM[i].rwnd.wnd[SM[i].rwnd.start]>=0 && SM[i].recv_buffer_live[SM[i].rwnd.wnd[SM[i].rwnd.start]]==1){
                  SM[i].rwnd.start=(SM[i].rwnd.start+1)%MAX_SEQ_NUM;
                }
              }else{
                if(SM[i].rwnd.wnd[seq]>=0 && SM[i].recv_buffer_live[SM[i].rwnd.wnd[seq]]==0){
                  int buff_idx=SM[i].rwnd.wnd[seq];
                  memcpy(SM[i].recv_buffer[buff_idx],buffer+19,len);
                  SM[i].rwnd.size--;
                  SM[i].rcvbufferlen[buff_idx]=len;
                  SM[i].recv_buffer_live[buff_idx]=1;
                }
              }
              if(SM[i].rwnd.size==0){
                SM[i].nospace=1;
              }
              int last_ack_seq=(SM[i].rwnd.start-1+MAX_SEQ_NUM)%MAX_SEQ_NUM;
              char ack[13];
              ack[0]='0';//acknowledgement
              for(int j = 0; j < 8; j++){
                ack[8 - j] = ((last_ack_seq >> j) & 1) + '0';
              }
              for (int j = 0; j < 4; j++){
                ack[12 - j] = ((SM[i].rwnd.size >> j) & 1) + '0';
              }
              sendto(SM[i].udp_sockid,ack,13,0,(struct sockaddr*)&clientaddr,sizeof(clientaddr));
            }
            else{
              //ACK
              int seq=0;
              int rwnd_size=0;
              
              for(int j=1;j<=8;j++){
                seq=(seq<<1)|(buffer[j]-'0');
              }
              for(int j=9;j<=12;j++){
                rwnd_size=(rwnd_size<<1)|(buffer[j]-'0');
              }

              if(SM[i].swnd.wnd[seq]>=0){
                int base=SM[i].swnd.start;
                while(base!=(seq+1)%MAX_SEQ_NUM){
                  SM[i].swnd.wnd[base]=-1;
                  SM[i].send_buffer_size++;
                  base=(base+1)%MAX_SEQ_NUM;
                }
                SM[i].swnd.start=(seq+1)%MAX_SEQ_NUM;
              }
              SM[i].swnd.size=rwnd_size;
            }
          }
        }
      }
      V(semid_SM);
    }
  }
}

void *S(){
  while(1){
    sleep(T/2);
    P(semid_SM);
    for(int i=0;i<N;i++){
      if(!SM[i].free){
        struct sockaddr_in serveraddr;
        serveraddr.sin_family=AF_INET;
        serveraddr.sin_port=htons(SM[i].port);
        serveraddr.sin_addr.s_addr=inet_addr(SM[i].ip_addr);
        int timeout=0;
        int seq=SM[i].swnd.start;
        printf("Here\n");
        while(seq!=(SM[i].swnd.start+SM[i].swnd.size)%MAX_SEQ_NUM){
          if(SM[i].last_send_time[seq]!=-1 && time(NULL)-SM[i].last_send_time[seq]>T){
            timeout=1;
            break;
          }
          seq=(seq+1)%MAX_SEQ_NUM;
        }
        printf("Here_2\n");
        if(timeout){
          printf("Timeout\n");
          seq=SM[i].swnd.start;
          while(seq!=(SM[i].swnd.start+SM[i].swnd.size)%MAX_SEQ_NUM){
            if(SM[i].swnd.wnd[seq]!=-1){
              char buffer[531];
              buffer[0]='1';//Data
              for(int j=0;j<8;j++){
                buffer[8-j]=((seq>>j)&1)+'0';
              }
              int len=SM[i].sendbufferlen[SM[i].swnd.wnd[seq]];
              for(int j=0;j<10;j++){
                buffer[18-j]=((len>>j)&1)+'0';
              }
              memcpy(buffer+19,SM[i].send_buffer[SM[i].swnd.wnd[seq]],len);
              sendto(SM[i].udp_sockid,buffer,len+19,0,(struct sockaddr*)&serveraddr,sizeof(serveraddr));
              SM[i].last_send_time[seq]=time(NULL);
            }
            seq=(seq+1)%MAX_SEQ_NUM;
          }
        }
      }
    }
    V(semid_SM);
  }
}

void sigHandler(int sig){
  if(sig==SIGINT){
    shmdt(sock_info);
    shmdt(SM);
    shmctl(shmid_sock_info,IPC_RMID,NULL);
    shmctl(shmid_SM,IPC_RMID,NULL);
    semctl(semid_sock_info,0,IPC_RMID);
    semctl(semid_SM,0,IPC_RMID);
    semctl(semid_init,0,IPC_RMID);
    semctl(semid_ktp,0,IPC_RMID);
    exit(0);
  }
}




int main(){
  signal(SIGINT, sigHandler);
  srand(time(NULL));
  pop.sem_num = 0;
  pop.sem_op = -1;
  pop.sem_flg = 0;

  vop.sem_num = 0;
  vop.sem_op = 1;
  vop.sem_flg = 0;

  int key_shmid_sock_info,key_semid_sock_info,key_shmid_SM,key_semid_SM,key_semid_init,key_semid_ktp;
  key_shmid_sock_info=ftok("/",'A');
  key_semid_sock_info=ftok("/",'B');
  key_shmid_SM=ftok("/",'C');
  key_semid_SM=ftok("/",'D');
  key_semid_init=ftok("/",'E');
  key_semid_ktp=ftok("/",'F');

  shmid_sock_info=shmget(key_shmid_sock_info,sizeof(NET_SOCKET),0666|IPC_CREAT);
  semid_sock_info=semget(key_semid_sock_info,1,0666|IPC_CREAT);
  shmid_SM=shmget(key_shmid_SM,sizeof(SHARED_MEM)*N,0666|IPC_CREAT);
  semid_SM=semget(key_semid_SM,1,0666|IPC_CREAT);
  semid_init=semget(key_semid_init,1,0666|IPC_CREAT);
  semid_ktp=semget(key_semid_ktp,1,0666|IPC_CREAT);

  sock_info=(NET_SOCKET*)shmat(shmid_sock_info,NULL,0);
  SM=(SHARED_MEM*)shmat(shmid_SM,NULL,0);

  semctl(semid_sock_info,0,SETVAL,1);
  semctl(semid_SM,0,SETVAL,1);
  semctl(semid_init,0,SETVAL,0);
  semctl(semid_ktp,0,SETVAL,0);

  for(int i=0;i<N;i++)SM[i].free=1;

  memset(sock_info,0,sizeof(NET_SOCKET));

  pthread_t Rthread,Sthread,GCthread;
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
  pthread_create(&Rthread,&attr,R,NULL);
  pthread_create(&Sthread,&attr,S,NULL);
  pthread_create(&GCthread,&attr,GC,NULL);
  printf("READY\n");
  while(1){
    P(semid_init);
    P(semid_sock_info);
    printf("Started...\n");
    if(sock_info->sock_id==0 && sock_info->port==0){
      int sockfd=socket(AF_INET,SOCK_DGRAM,0);
      if(sockfd<0){
        sock_info->sock_id=-1;
        sock_info->err_code=errno;
      }else{
        sock_info->sock_id=sockfd;
      }
    }
    else{
      struct sockaddr_in serveraddr;
      serveraddr.sin_family=AF_INET;
      serveraddr.sin_port=htons(sock_info->port);
      serveraddr.sin_addr.s_addr=inet_addr(sock_info->ip_addr);
      if(bind(sock_info->sock_id,(struct sockaddr*)&serveraddr,sizeof(serveraddr))<0){
        sock_info->sock_id=-1;
        sock_info->sock_id=errno;
      }
    }
    V(semid_sock_info);
    V(semid_ktp);
    printf("Done\n");
  }

  return 0;
}


