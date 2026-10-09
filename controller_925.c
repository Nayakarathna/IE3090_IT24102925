#define _POSIX_C_SOURCE 200809L
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#define PORT 9410
#define TOKEN "OPS-2925"
#define MAX_LINE 4096
static int fd=-1;
static int send_all(int s,const void *data,size_t len){const char*p=data;while(len){ssize_t n=send(s,p,len,0);if(n<0){if(errno==EINTR)continue;return -1;}if(!n)return -1;p+=n;len-=(size_t)n;}return 0;}
static int recv_all(int s,void*data,size_t len){char*p=data;while(len){ssize_t n=recv(s,p,len,0);if(n<0){if(errno==EINTR)continue;return -1;}if(!n)return -1;p+=n;len-=(size_t)n;}return 0;}
static ssize_t read_line(int s,char*b,size_t cap){size_t n=0;while(n+1<cap){char c;ssize_t r=recv(s,&c,1,0);if(r==0)return n?(ssize_t)n:0;if(r<0){if(errno==EINTR)continue;return -1;}if(c=='\n')break;if(c!='\r')b[n++]=c;}b[n]=0;return (ssize_t)n;}
static int send_cmd(const char *s){char line[MAX_LINE];snprintf(line,sizeof(line),"%s\n",s);return send_all(fd,line,strlen(line));}
static void *udp_listener(void *arg){int port=*(int*)arg;free(arg);int s=socket(AF_INET,SOCK_DGRAM,0);if(s<0){perror("UDP socket");return NULL;}struct sockaddr_in a={0};a.sin_family=AF_INET;a.sin_addr.s_addr=INADDR_ANY;a.sin_port=htons((unsigned short)port);if(bind(s,(struct sockaddr*)&a,sizeof(a))<0){perror("UDP bind");close(s);return NULL;}printf("UDP monitoring listener active on port %d\n",port);while(1){char b[512];ssize_t n=recv(s,b,sizeof(b)-1,0);if(n<=0)break;b[n]=0;printf("\n[UDP] %s\n> ",b);fflush(stdout);}close(s);return NULL;}
static void help(void){puts("\nCommands:\n  SYSINFO\n  LISTPROC\n  EXEC DATE|UPTIME|DISKFREE|HOSTNAME|WHOAMI\n  PUT <local_filename>\n  GET <filename>  (downloads to ./downloads/)\n  MONITOR START <udp_port>\n  MONITOR STOP\n  QUIT\n");}
int main(int argc,char **argv){
 const char *host=argc>1?argv[1]:"127.0.0.1";
 fd=socket(AF_INET,SOCK_STREAM,0);if(fd<0){perror("socket");return 1;}
 struct sockaddr_in a={0};a.sin_family=AF_INET;a.sin_port=htons(PORT);
 if(inet_pton(AF_INET,host,&a.sin_addr)!=1){fprintf(stderr,"Use an IPv4 address, e.g. 127.0.0.1\n");return 1;}
 if(connect(fd,(struct sockaddr*)&a,sizeof(a))<0){perror("connect");return 1;}
 char line[MAX_LINE];snprintf(line,sizeof(line),"AUTH %s",TOKEN);send_cmd(line);if(read_line(fd,line,sizeof(line))<=0){puts("No authentication response");close(fd);return 1;}puts(line);
 if(strstr(line,"ERR")){close(fd);return 1;}
 help();
 char input[MAX_LINE];
 while(1){
  printf("> ");fflush(stdout);
  if(!fgets(input,sizeof(input),stdin))break;
  input[strcspn(input,"\r\n")]=0;
  if(!*input)continue;
  if(!strncmp(input,"PUT ",4)){
    char *name=input+4;FILE*f=fopen(name,"rb");if(!f){perror("open local file");continue;}
    fseek(f,0,SEEK_END);long sz=ftell(f);rewind(f);if(sz<0){fclose(f);continue;}
    char *base=strrchr(name,'/');base=base?base+1:name;
    char cmd[MAX_LINE];snprintf(cmd,sizeof(cmd),"PUT %s %ld",base,sz);send_cmd(cmd);
    char chunk[8192];size_t n;while((n=fread(chunk,1,sizeof(chunk),f))>0)if(send_all(fd,chunk,n)<0)break;fclose(f);
    if(read_line(fd,line,sizeof(line))>0)puts(line);else puts("Connection closed");continue;
  }
  if(!strncmp(input,"GET ",4)){
    char *name=input+4;char cmd[MAX_LINE];snprintf(cmd,sizeof(cmd),"GET %s",name);send_cmd(cmd);
    if(read_line(fd,line,sizeof(line))<=0){puts("Connection closed");break;}
    puts(line);
    if(strncmp(line,"OK FILE_SEND ",13)==0){
      char fname[256];long sz=0;if(sscanf(line+13,"%255s %ld",fname,&sz)!=2||sz<0){puts("Invalid file response");break;}
      mkdir("downloads",0755);char path[512];snprintf(path,sizeof(path),"downloads/%s",fname);FILE*f=fopen(path,"wb");if(!f){perror("create download");break;}
      char chunk[8192];long left=sz;int fail=0;while(left>0){size_t want=left<(long)sizeof(chunk)?(size_t)left:sizeof(chunk);ssize_t n=recv(fd,chunk,want,0);if(n<=0){fail=1;break;}if(fwrite(chunk,1,(size_t)n,f)!=(size_t)n){fail=1;break;}left-=n;}fclose(f);printf("%s (%ld bytes)\n",fail?"Download incomplete":"Saved",sz);
    }continue;
  }
  if(!strncmp(input,"MONITOR START ",14)){
    char *p=input+14;int port=atoi(p);if(port<1||port>65535){puts("Invalid UDP port");continue;}
    int *arg=malloc(sizeof(int));*arg=port;pthread_t t;if(pthread_create(&t,NULL,udp_listener,arg)!=0){free(arg);puts("Cannot start UDP listener");continue;}pthread_detach(t);
    if(send_cmd(input)<0){puts("Send failed");break;}if(read_line(fd,line,sizeof(line))>0)puts(line);continue;
  }
  if(send_cmd(input)<0){puts("Send failed");break;}
  if(read_line(fd,line,sizeof(line))<=0){puts("Connection closed");break;}
  puts(line);
  if(!strcmp(input,"QUIT"))break;
 }
 close(fd);return 0;
}
