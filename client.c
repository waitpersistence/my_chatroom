#include <stdio.h>
#include <sys/types.h> /* See NOTES */
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h> /* superset of previous */
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
 
typedef struct
{
    char type;      //消息类型 L C Q
    char id[32];    //用户id
    char text[128]; //消息内容
} msg_t;
 
int main(int argc, char const *argv[])
{
    if (argc != 3)
    {
        printf("usage:./a.out <ip> <port> \n");
        return -1;
    }
 
    int sockfd;
    msg_t msg;
    struct sockaddr_in caddr;
    socklen_t len = sizeof(caddr);
    char buf[128];
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0)
    {
        perror("sock err.\n");
        return -1;
    }
    caddr.sin_family = AF_INET;
    caddr.sin_addr.s_addr = inet_addr(argv[1]);
    caddr.sin_port = htons(atoi(argv[2]));
 
    msg.type = 'L';
    printf("please imput your id\n");
    scanf("%[^\n]", msg.id);
    getchar();
 
    sendto(sockfd, &msg, sizeof(msg), 0, (struct sockaddr *)&caddr, len);
 
    pid_t pid = fork();
    if (pid < 0)
    {
        perror("fork err.\n");
        return -1;
    }
    else if (pid == 0) //子进程循环发送消息
    {
        while (1)
        {
 
            scanf("%[^\n]", msg.text);
            getchar();
            //printf("%s\n", msg.text);
 
            if (strncmp(msg.text, "quit", 4) == 0)
            {
                msg.type = 'Q';
                sendto(sockfd, &msg, sizeof(msg), 0, (struct sockaddr *)&caddr, len);
                kill(getppid(), SIGKILL);
                wait(NULL);
                exit(-1);
            }
            else if(strncmp(msg.text, "\\who",4)==0)
            {
                msg.type='W';
                sendto(sockfd,&msg,sizeof(msg),0,(struct sockaddr *) &caddr,len);
            }
            else
            {
                msg.type = 'C';
                sendto(sockfd, &msg, sizeof(msg), 0, (struct sockaddr *)&caddr, len);
            }
            //printf("sssssshu\n");
            
        }
    }
 
    else //父进程循环接受消息
    {
        int recvbyte;
        while (1)
        {
            recvbyte = recvfrom(sockfd, &msg, sizeof(msg), 0, NULL, NULL);
            if (recvbyte < 0)
            {
                perror("recvfrom err.\n");
                return -1;
            }
            printf("%s:%s\n", msg.id, msg.text);
        }
        wait(NULL);
    }
    close(sockfd);
    return 0;
}
