/* --- tcp_client.c (TCP Version) --- */
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>

typedef struct
{
    char type;      // 消息类型 L C Q W P
    char id[32];    // 用户id
    char text[128]; // 消息内容
} msg_t;

int main(int argc, char const *argv[])
{
    if (argc != 3) {
        printf("usage:./client <ip> <port> \n");
        return -1;
    }

    int sockfd;
    msg_t msg;
    struct sockaddr_in saddr; // [TCP] 这是服务器地址

    // 1. 创建 TCP 套接字
    sockfd = socket(AF_INET, SOCK_STREAM, 0); // [TCP] SOCK_STREAM
    if (sockfd < 0) {
        perror("socket error"); return -1;
    }

    // 2. 准备服务器地址
    memset(&saddr, 0, sizeof(saddr));
    saddr.sin_family = AF_INET;
    saddr.sin_addr.s_addr = inet_addr(argv[1]);
    saddr.sin_port = htons(atoi(argv[2]));

    // 3. [TCP] 连接 (Connect)
    if (connect(sockfd, (struct sockaddr *)&saddr, sizeof(saddr)) < 0) {
        perror("connect error");
        return -1;
    }
    printf("Connected to server!\n");

    // 4. [TCP] 发送登录包
    memset(&msg, 0, sizeof(msg));
    msg.type = 'L';
    printf("Please input your id: ");
    scanf("%[^\n]", msg.id);
    getchar();

    if (send(sockfd, &msg, sizeof(msg), 0) < 0) { // [TCP] 使用 send
        perror("send login error");
        return -1;
    }

    // 5. fork() 分裂
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork error"); return -1;
    }
    else if (pid == 0) // 子进程 (嘴巴) - 负责发送
    {
        char input_buf[160]; // 缓冲区

        while (1)
        {
            memset(&msg, 0, sizeof(msg));
            memset(input_buf, 0, sizeof(input_buf));

            scanf("%[^\n]", input_buf);
            getchar(); // 吸收换行符

            // 检查是否为 "quit"
            if (strncmp(input_buf, "quit", 4) == 0)
            {
                msg.type = 'Q'; // 虽然服务器不会处理'Q'，但客户端用它来退出
                // [TCP] send()
                send(sockfd, &msg, sizeof(msg), 0); // 发送一个空包，让服务器的recv>0
                kill(getppid(), SIGKILL); // 杀死父进程
                break; // 子进程退出
            }
            // 检查是否为 "\who"
            else if(strncmp(input_buf, "\\who", 4) == 0) {
                msg.type = 'W';
            }
            // 检查是否为 "/msg" (私聊)
            else if(strncmp(input_buf, "/msg ", 5) == 0) {
                msg.type = 'P';
                strcpy(msg.text, input_buf + 5); 
            }
            // 否则，就是普通聊天
            else {
                msg.type = 'C';
                strcpy(msg.text, input_buf);
            }
            
            // [TCP] 统一使用 send()
            if (send(sockfd, &msg, sizeof(msg), 0) < 0) {
                perror("send error");
                break; // 发送失败，退出
            }
        }
        exit(0);
    }
    else // 父进程 (耳朵) - 负责接收
    {
        ssize_t n;
        while (1)
        {
            memset(&msg, 0, sizeof(msg));
            // [TCP] 使用 recv()
            n = recv(sockfd, &msg, sizeof(msg), 0);

            // 6. [TCP] 检查服务器是否断开
            if (n <= 0) {
                if (n == 0) {
                    printf("Server has closed the connection.\n");
                } else {
                    perror("recv error");
                }
                kill(pid, SIGKILL); // 杀死子进程
                break; // 退出循环
            }
            
            // 收到消息，打印
            printf("%s: %s\n", msg.id, msg.text);
        }
        wait(NULL);
    }
    close(sockfd);
    return 0;
}