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
#include <pthread.h>
#include <sys/select.h>  // 可选：如果需要更完善的IO处理

typedef struct
{
    char type;      // 消息类型 L C Q
    char id[32];    // 用户id
    char text[128]; // 消息内容
} msg_t;

// 链表节点：存储客户端地址
typedef struct node_t
{
    struct sockaddr_in caddr;
    struct node_t *next; 
} list;

// 全局变量声明（供handler线程使用）
struct sockaddr_in saddr, caddr;  // 注意：main中不要重复定义

// 线程函数声明（必须在main前声明）
void *handler(void *arg);

// 函数声明
list *list_create(void);
void login(int sockfd, msg_t msg, list *p, struct sockaddr_in caddr);
void chat(int sockfd, msg_t msg, list *p, struct sockaddr_in caddr);
void quit(int sockfd, msg_t msg, list *p, struct sockaddr_in caddr);

int main(int argc, char const *argv[])
{
    if (argc != 2)
    {
        printf("usage:./server <port>\n");
        return -1;
    }

    int sockfd;
    socklen_t len = sizeof(caddr);  // 客户端地址长度
    msg_t msg;

    // 创建UDP socket
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0)
    {
        perror("socket error");
        return -1;
    }

    // 初始化服务器地址
    memset(&saddr, 0, sizeof(saddr));  // 清空结构体
    saddr.sin_family = AF_INET;
    saddr.sin_addr.s_addr = htonl(INADDR_ANY);  // 绑定所有网卡
    saddr.sin_port = htons(atoi(argv[1]));

    // 绑定端口
    if (bind(sockfd, (struct sockaddr *)&saddr, sizeof(saddr)) < 0)
    {
        perror("bind error");
        close(sockfd);
        return -1;
    }
    printf("Server bind ok! Port: %s\n", argv[1]);

    // 创建客户端链表（头节点）
    list *head = list_create();
    if (head == NULL)
    {
        close(sockfd);
        return -1;
    }

    // 创建handler线程（用于服务器主动发送消息）
    pthread_t tid;
    if (pthread_create(&tid, NULL, handler, &sockfd) != 0)
    {
        perror("pthread_create error");
        close(sockfd);
        return -1;
    }
    pthread_detach(tid);  // 分离线程，自动回收资源

    // 主循环：接收客户端消息并处理
    while (1)
    {
        // 接收客户端消息
        memset(&msg, 0, sizeof(msg));  // 清空消息结构体
        memset(&caddr, 0, sizeof(caddr));  // 清空客户端地址
        ssize_t recvbyte = recvfrom(sockfd, &msg, sizeof(msg), 0, 
                                   (struct sockaddr *)&caddr, &len);
        if (recvbyte < 0)
        {
            perror("recvfrom error (可能是超时)");
            continue;  // 超时不退出，继续循环
        }

        // 根据消息类型处理
        if (msg.type == 'L')  // 登录
        {
            login(sockfd, msg, head, caddr);
        }
        else if (msg.type == 'C')  // 聊天
        {
            chat(sockfd, msg, head, caddr);
        }
        else if (msg.type == 'Q')  // 退出
        {
            printf("收到退出消息：IP=%s, Port=%d, ID=%s\n",
                   inet_ntoa(caddr.sin_addr), ntohs(caddr.sin_port), msg.id);
            quit(sockfd, msg, head, caddr);
        }
    }

    close(sockfd);
    return 0;
}

// 创建链表头节点
list *list_create(void)
{
    list *p = (list *)malloc(sizeof(list));
    if (p == NULL)
    {
        perror("malloc error");
        return NULL;
    }
    p->next = NULL;
    return p;
}

// 处理登录消息
void login(int sockfd, msg_t msg, list *head, struct sockaddr_in caddr)
{
    list *new_node = (list *)malloc(sizeof(list));
    if (new_node == NULL)
    {
        perror("malloc error");
        return;
    }

    // 保存新客户端地址
    new_node->caddr = caddr;
    new_node->next = NULL;

    // 尾插法加入链表
    list *p = head;
    while (p->next != NULL)
    {
        p = p->next;
        // 向已在线用户广播新用户登录消息
        sprintf(msg.text, "%s 已上线", msg.id);
        sendto(sockfd, &msg, sizeof(msg), 0, 
               (struct sockaddr *)&(p->caddr), sizeof(p->caddr));
    }
    p->next = new_node;
    printf("新用户登录：ID=%s, IP=%s, Port=%d\n",
           msg.id, inet_ntoa(caddr.sin_addr), ntohs(caddr.sin_port));
}

// 处理聊天消息（群发）
void chat(int sockfd, msg_t msg, list *head, struct sockaddr_in caddr)
{
    list *p = head->next;  // 跳过头节点
    while (p != NULL)
    {
        // 不向发送者本人转发
        if (memcmp(&(p->caddr), &caddr, sizeof(caddr)) != 0)
        {
            sendto(sockfd, &msg, sizeof(msg), 0, 
                   (struct sockaddr *)&(p->caddr), sizeof(p->caddr));
        }
        p = p->next;
    }
}

// 处理退出消息
void quit(int sockfd, msg_t msg, list *head, struct sockaddr_in caddr)
{
    list *p = head;
    list *dele = NULL;

    while (p->next != NULL)
    {
        // 找到要删除的客户端节点
        if (memcmp(&(p->next->caddr), &caddr, sizeof(caddr)) == 0)
        {
            dele = p->next;
            p->next = dele->next;  // 断开链表
            free(dele);  // 释放节点
            dele = NULL;
            printf("用户退出：ID=%s\n", msg.id);
        }
        else
        {
            // 向其他用户广播退出消息
            sprintf(msg.text, "%s 已下线", msg.id);
            sendto(sockfd, &msg, sizeof(msg), 0, 
                   (struct sockaddr *)&(p->next->caddr), sizeof(p->next->caddr));
            p = p->next;
        }
    }
}

// 线程函数：服务器主动发送消息（例如管理员消息）
void *handler(void *arg)
{
    int sockfd = *(int *)arg;  // 从参数获取socket描述符
    msg_t msg_s;
    memset(&msg_s, 0, sizeof(msg_s));
    strcpy(msg_s.id, "server");  // 服务器ID
    msg_s.type = 'C';  // 标记为聊天消息

    printf("服务器消息发送线程启动（输入消息并回车发送给所有在线用户）\n");
    while (1)
    {
        // 读取服务器输入（注意：scanf格式修正）
        printf("server: ");
        fflush(stdout);  // 刷新缓冲区，确保提示正常显示
        if (scanf("%[^\n]", msg_s.text) != 1)  // 读取一行（不含换行符）
        {
            perror("scanf error");
            memset(msg_s.text, 0, sizeof(msg_s.text));
        }
        getchar();  // 吸收换行符

        // 群发消息给所有在线用户
        list *head = list_create();  // 注意：这里需要访问main中的head！！（此处有问题，见说明）
        list *p = head->next;
        while (p != NULL)
        {
            sendto(sockfd, &msg_s, sizeof(msg_s), 0,
                   (struct sockaddr *)&(p->caddr), sizeof(p->caddr));
            p = p->next;
        }
    }
    return NULL;
}
