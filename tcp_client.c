#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>

int main()
{
    // 1. 创建套接字 (SOCK_STREAM)
    // 申请一部“电话机”
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);

    // 2. 准备服务器地址 (和 UDP 一样)
    struct sockaddr_in saddr;
    memset(&saddr, 0, sizeof(saddr));
    saddr.sin_family = AF_INET;
    saddr.sin_addr.s_addr = inet_addr("127.0.0.1"); // 服务器 IP
    saddr.sin_port = htons(8888); // 服务器 Port

    // 3. 连接 (Connect) [新函数!]
    // “拨号”！这个函数会阻塞，直到服务器的 accept() 接听了你
    // “三次握手”就在这里发生
    connect(sockfd, (struct sockaddr *)&saddr, sizeof(saddr));
    printf("Connected to server!\n");

    // 4. 发送 (Send) [新函数!]
    // 你不再需要 `sendto` 和地址了，因为 sockfd 已经“接通”了
    char *msg = "Hello TCP!";
    send(sockfd, msg, strlen(msg), 0);

    // 5. 关闭
    close(sockfd); // 挂断电话
    
    return 0;


}