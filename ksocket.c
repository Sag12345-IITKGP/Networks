#include "ksocket.h"


SHARED_MEM* SM;
NET_SOCKET* sock_info;
int semid_SM,semid_sock_info;
int shmid_SM,shmid_sock_info;
int semid_init,semid_ktp;
struct sembuf pop,vop;


void retrieve_shared_mem(){
  int key_shmid_sock_info,key_semid_sock_info,key_shmid_SM,key_semid_SM,key_semid_init,key_semid_ktp;
  key_shmid_sock_info=ftok("/",'A');
  key_semid_sock_info=ftok("/",'B');
  key_shmid_SM=ftok("/",'C');
  key_semid_SM=ftok("/",'D');
  key_semid_init=ftok("/",'E');
  key_semid_ktp=ftok("/",'F');

  shmid_sock_info=shmget(key_shmid_sock_info,sizeof(NET_SOCKET),0666);
  semid_sock_info=semget(key_semid_sock_info,1,0666);
  shmid_SM=shmget(key_shmid_SM,sizeof(SHARED_MEM)*N,0666);
  semid_SM=semget(key_semid_SM,1,0666);
  semid_init=semget(key_semid_init,1,0666);
  semid_ktp=semget(key_semid_ktp,1,0666);

  if(shmid_sock_info==-1 || semid_sock_info==-1 || shmid_SM==-1 || semid_SM==-1 || semid_init==-1 || semid_ktp==-1){
    perror("Error in retrieving shared memory");
    exit(1);
  }

  sock_info=(NET_SOCKET*)shmat(shmid_sock_info,0,0);
  SM=(SHARED_MEM*)shmat(shmid_SM,0,0);
}

void init_sembuf(){
  pop.sem_num=0;
  pop.sem_op=-1;
  pop.sem_flg=0;
  vop.sem_num=0;
  vop.sem_op=1;
  vop.sem_flg=0;
}

int is_free_SM(){
  P(semid_SM);
  for(int i=0;i<N;i++){
    if(SM[i].free==1){
      V(semid_SM);
      return i;
    }
  }
  V(semid_SM);
  return -1;
}



int k_socket(int domain,int type,int protocol){
  retrieve_shared_mem();
  init_sembuf();
  if(type!=SOCK_KTP){
    errno=EINVAL;
    return -1;
  }
  int index=is_free_SM();
  P(semid_sock_info);
  if(index==-1){
    errno=ENOSPACE;
    memset(sock_info,0,sizeof(NET_SOCKET));
    V(semid_sock_info);
    return -1;
  }
  V(semid_sock_info);

  V(semid_init);
  P(semid_ktp);
  P(semid_sock_info);
  if(sock_info->sock_id==-1){
    errno=sock_info->err_code;
    memset(sock_info,0,sizeof(NET_SOCKET));
    V(semid_sock_info);
    return -1;
  }
  V(semid_sock_info);


  P(semid_SM);
  SM[index].free=0;
  SM[index].pid=getpid();
  SM[index].udp_sockid=sock_info->sock_id;
  for(int i=0;i<MAX_SEQ_NUM;i++){
    SM[index].swnd.wnd[i]=-1;
    if(i<10){
      SM[index].rwnd.wnd[i]=i;
    }else{
      SM[index].rwnd.wnd[i]=-1;
    }
    SM[index].last_send_time[i]=-1;
  }
  SM[index].swnd.size=10;
  SM[index].rwnd.size=10;
  SM[index].swnd.start=0;
  SM[index].rwnd.start=0;
  SM[index].send_buffer_size=10;
  for(int i=0;i<10;i++){
    SM[index].recv_buffer_live[i]=0;
  }
  SM[index].recv_buffer_base=0;
  SM[index].nospace=0;
  V(semid_SM);

  P(semid_sock_info);
  memset(sock_info,0,sizeof(NET_SOCKET));
  V(semid_sock_info);

  return index;
}

int k_bind(char src_ip[],uint16_t src_port,char dest_ip[],uint16_t dest_port){
  retrieve_shared_mem();
  init_sembuf();
  int idx=-1;
  P(semid_SM);
  for(int i=0;i<N; i++){
    if(!SM[i].free && SM[i].pid==getpid()){
      idx=i;
      break;
    }
  }

  P(semid_sock_info);
  sock_info->sock_id=SM[idx].udp_sockid;
  strcpy(sock_info->ip_addr,src_ip);
  sock_info->port=src_port;
  V(semid_sock_info);
  V(semid_init);
  P(semid_ktp);

  P(semid_sock_info);
  if(sock_info->sock_id==-1){
    errno=sock_info->err_code;
    memset(sock_info,0,sizeof(NET_SOCKET));
    V(semid_sock_info);
    V(semid_SM);
    return -1;
  }
  memset(sock_info,0,sizeof(NET_SOCKET));
  V(semid_sock_info);

  strcpy(SM[idx].ip_addr,dest_ip);
  SM[idx].port=dest_port;
  V(semid_SM);

  return 0;
}

ssize_t k_sendto(int sockfd, const void *buf, size_t len, int flags, const struct sockaddr *dest_addr, socklen_t addrlen){
  retrieve_shared_mem();
  init_sembuf();
  if(sockfd<0 || sockfd>=N || SM[sockfd].free){
    errno=EINVAL;
    return -1;
  }

  char dest_ip[INET_ADDRSTRLEN];
  struct sockaddr_in *addr = (struct sockaddr_in *)dest_addr;
  if (inet_ntop(AF_INET, &addr->sin_addr, dest_ip, INET_ADDRSTRLEN) == NULL) {
      perror("inet_ntop");
  }

  uint16_t dest_port;
  dest_port=ntohs(((struct sockaddr_in*)dest_addr)->sin_port);

  P(semid_SM);
  if(strcmp(SM[sockfd].ip_addr,dest_ip) || SM[sockfd].port!=dest_port){
    errno=ENOTBOUND;
    V(semid_SM);
    return -1;
  }

  if(SM[sockfd].send_buffer_size==0){
    errno=ENOBUFS;
    V(semid_SM);
    return -1;
  }
  int seq_num=SM[sockfd].swnd.start;
  while(SM[sockfd].swnd.wnd[seq_num]!=-1){
    seq_num=(seq_num+1)%MAX_SEQ_NUM;
  }
  int buff_idx=-1;
  for(int i=0;i<10;i++){
    int occupied=0;
    for(int j=0;j<MAX_SEQ_NUM;j++){
      if(SM[sockfd].swnd.wnd[j]==i){
        occupied=1;
        break;
      }
    }
    if(!occupied){
      buff_idx=i;
      break;
    }
  }
  if(buff_idx==-1){
    errno=ENOBUFS;
    V(semid_SM);
    return -1;
  }

  SM[sockfd].swnd.wnd[seq_num]=buff_idx;
  memcpy(SM[sockfd].send_buffer[buff_idx],buf,len);
  SM[sockfd].send_buffer_size--;
  SM[sockfd].sendbufferlen[buff_idx]=len;
  SM[sockfd].last_send_time[seq_num]=-1;
  V(semid_SM);
  return len;
}

void print_SM(int sockfd){
  printf("free=%d\n",SM[sockfd].free);
  printf("pid=%d\n",SM[sockfd].pid);
  printf("udp_sockid=%d\n",SM[sockfd].udp_sockid);
  printf("ip_addr=%s\n",SM[sockfd].ip_addr);
  printf("port=%d\n",SM[sockfd].port);
  // printf("send_buffer_size=%d\n",SM[sockfd].send_buffer_size);
  // for(int i=0;i<10;i++){
  //   printf("send_buffer[%d]=%s\n",i,SM[sockfd].send_buffer[i]);
  //   printf("sendbufferlen[%d]=%d\n",i,SM[sockfd].sendbufferlen[i]);
  // }
  // for(int i=0;i<MAX_SEQ_NUM;i++){
  //   printf("last_send_time[%d]=%ld\n",i,SM[sockfd].last_send_time[i]);
  // }
  // for(int i=0;i<10;i++){
  //   printf("recv_buffer[%d]=%s\n",i,SM[sockfd].recv_buffer[i]);
  //   printf("recv_buffer_live[%d]=%d\n",i,SM[sockfd].recv_buffer_live[i]);
  //   printf("rcvbufferlen[%d]=%d\n",i,SM[sockfd].rcvbufferlen[i]);
  // }
  // printf("recv_buffer_base=%d\n",SM[sockfd].recv_buffer_base);
  // for(int i=0;i<MAX_SEQ_NUM;i++){
  //   printf("swnd.wnd[%d]=%d\n",i,SM[sockfd].swnd.wnd[i]);
  // }
  // printf("swnd.size=%d\n",SM[sockfd].swnd.size);
  // printf("swnd.start=%d\n",SM[sockfd].swnd.start);
  // for(int i=0;i<MAX_SEQ_NUM;i++){
  //   printf("rwnd.wnd[%d]=%d\n",i,SM[sockfd].rwnd.wnd[i]);
  // }
  // printf("rwnd.size=%d\n",SM[sockfd].rwnd.size);
  // printf("rwnd.start=%d\n",SM[sockfd].rwnd.start);
  // printf("nospace=%d\n",SM[sockfd].nospace);
}

ssize_t k_recvfrom(int sockfd, void* buf, size_t len, int flags, struct sockaddr * src_addr, socklen_t *addrlen){
  
  retrieve_shared_mem();
  init_sembuf();
  
  P(semid_SM);
  print_SM(sockfd);
  if(sockfd<0 || sockfd>=N || SM[sockfd].free){
    printf("In if_error\n");
    errno=EINVAL;
    V(semid_SM);
    return -1;
  }
  printf("checked buffer,sockfd=%d\n",sockfd);
  if(SM[sockfd].recv_buffer_live[SM[sockfd].recv_buffer_base]){
    printf("In if\n");
    int mxlen=SM[sockfd].rcvbufferlen[SM[sockfd].recv_buffer_base];
    if(mxlen>len)mxlen=len;
    memcpy(buf,SM[sockfd].recv_buffer[SM[sockfd].recv_buffer_base],mxlen);

    SM[sockfd].recv_buffer_live[SM[sockfd].recv_buffer_base]=0;
    int seq;
    for(int i=0;i<MAX_SEQ_NUM;i++){
      if(SM[sockfd].rwnd.wnd[i]==SM[sockfd].recv_buffer_base){
        seq=i;
        //break;
      }
    }
    SM[sockfd].rwnd.wnd[seq]=-1;
    SM[sockfd].rwnd.wnd[(seq+10)%MAX_SEQ_NUM]=SM[sockfd].recv_buffer_base;
    SM[sockfd].recv_buffer_base=(SM[sockfd].recv_buffer_base+1)%10;
    SM[sockfd].rwnd.size++;
    V(semid_SM);
    return mxlen;
  }
  errno=ENOMESSAGE;
  V(semid_SM);
  return -1;
 
}


int k_close(int sockfd){
  retrieve_shared_mem();
  init_sembuf();
  P(semid_SM);
  SM[sockfd].free=1;
  V(semid_SM);
  return 0;
}

int drop_Message(){
  float r=(float) rand()/(float)RAND_MAX;
  if(r<p){
    return 1;
  }
  return 0;
}



