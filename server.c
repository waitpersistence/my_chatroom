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
    char type;      // 消息类型 L C Q W
    char id[32];    // 用户id
    char text[128]; // 消息内容
} msg_t;

// 链表节点：存储客户端地址
typedef struct node_t
{
    struct sockaddr_in caddr;
    struct node_t *next;
    char id[32];  //增加id
} list;

// <-- 修正 1: 创建一个新的结构体，用于向handler线程传递参数
typedef struct
{
    int sockfd;
    list *head; // 我们需要传递链表头指针
} thread_args_t;

// 全局变量声明（供handler线程使用）
struct sockaddr_in saddr, caddr;  // 注意：main中不要重复定义
pthread_mutex_t list_mutex; // <-- 修正 2: 定义一个全局互斥锁
// 线程函数声明（必须在main前声明）
void *handler(void *arg);

// 函数声明
list *list_create(void);
void login(int sockfd, msg_t msg, list *p, struct sockaddr_in caddr);
void chat(int sockfd, msg_t msg, list *p, struct sockaddr_in caddr);
void quit(int sockfd, msg_t msg, list *p, struct sockaddr_in caddr);
void who(int sockfd, msg_t msg, list *p, struct sockaddr_in caddr);
void private_chat(int sockfd, msg_t msg, list *p, struct sockaddr_in caddr); // <-- 新增
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
    int listen_fd=socket(AF_INET,SOCK_STREAM,0);
    struct sockaddr_in saddr;
    memset(&saddr,0,sizeof(saddr));
    saddr.sin_family=AF_INET;
    saddr.sin_addr.s_addr=htonl(INADDR_ANY);
    saddr.sin_port=htons(8888);

    bind(listen_fd,(struct sockaddr *)&saddr,sizeof(saddr));

    listen(listen_fd,5);
    
    // 创建UDP socket
    //sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0)
    {
        perror("socket error");
        return -1;
    }
    //端口复用
    // <-- 修正：添加端口复用选项
    int opt = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
    {
        perror("setsockopt error");
        close(sockfd);
        return -1;
    }
    // <-- 修正结束
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
    // <-- 修正 3: 初始化互斥锁
    if (pthread_mutex_init(&list_mutex, NULL) != 0)
    {
        perror("mutex init error");
        close(sockfd);
        return -1;
    }
    // <-- 修正 4: 准备传递给线程的参数
    // 我们必须使用 malloc，因为 'args' 必须在 main 退出后仍然有效
    thread_args_t *args = malloc(sizeof(thread_args_t));
    if (args == NULL)
    {
        perror("malloc args error");
        close(sockfd);
        return -1;
    }
    args->sockfd = sockfd;
    args->head = head; // 传递真正的 head 指针
    // 创建handler线程（用于服务器主动发送消息）
    pthread_t tid;
    if (pthread_create(&tid, NULL, handler, args) != 0)
    {
        perror("pthread_create error");
        close(sockfd);
        free(args); // 创建失败时释放内存
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
            // <-- 修正：在这里添加服务器日志
            printf("Chat Log [%s]: %s\n", msg.id, msg.text);
            chat(sockfd, msg, head, caddr);
        }
        else if (msg.type == 'Q')  // 退出
        {
            printf("收到退出消息：IP=%s, Port=%d, ID=%s\n",
                   inet_ntoa(caddr.sin_addr), ntohs(caddr.sin_port), msg.id);
            quit(sockfd, msg, head, caddr);
        }
        else if(msg.type =='W')//\who
        {
            who(sockfd, msg, head, caddr);
        }
        else if(msg.type=='P')
        {
            private_chat(sockfd,msg,head,caddr);
        }
    }

    close(sockfd);
    pthread_mutex_destroy(&list_mutex); // <-- 修正 6: 销毁互斥锁 (尽管此程序中不会执行到)
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
    strcpy(new_node->id, msg.id);
    new_node->next = NULL;
    // <-- 修正 7: 在访问链表前加锁
    pthread_mutex_lock(&list_mutex);
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
    // <-- 修正 8: 完成访问后解锁
    pthread_mutex_unlock(&list_mutex);
    printf("新用户登录：ID=%s, IP=%s, Port=%d\n",
           msg.id, inet_ntoa(caddr.sin_addr), ntohs(caddr.sin_port));
}

// 处理聊天消息（群发）
void chat(int sockfd, msg_t msg, list *head, struct sockaddr_in caddr)
{   
    // <-- 修正 9: 加锁
    pthread_mutex_lock(&list_mutex);
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
    // <-- 修正 10: 解锁
    pthread_mutex_unlock(&list_mutex);
}

// 处理退出消息
void quit(int sockfd, msg_t msg, list *head, struct sockaddr_in caddr)
{   
    // <-- 修正 11: 加锁
    pthread_mutex_lock(&list_mutex);
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
    // <-- 修正 12: 解锁
    pthread_mutex_unlock(&list_mutex);
}

//处理/who
void who(int sockfd, msg_t msg, list *head, struct sockaddr_in caddr)
{
    pthread_mutex_lock(&list_mutex);
    msg_t response_msg;
    memset(&response_msg,0,sizeof(response_msg));
    response_msg.type='C';
    strcpy(response_msg.id,"Server");
    strcpy(response_msg.text,"---Online Users ---\n");
    
    list *p = head->next;  // 跳过头节点
    while(p!=NULL){
        //防止超过缓冲区
        if(strlen(response_msg.text)+strlen(p->id)+2<sizeof(response_msg.text))
        {
            strcat(response_msg.text, p->id); // 附加 "alice"
            strcat(response_msg.text, "\n");  // 附加一个换行符
        }
        p=p->next;
   }
    pthread_mutex_unlock(&list_mutex);
    sendto(sockfd, &response_msg, sizeof(msg), 0, 
                (const struct sockaddr *)&caddr, sizeof(p->caddr));
    
    
}
// 线程函数：服务器主动发送消息（例如管理员消息）
void *handler(void *arg)
{   
    // <-- 修正 13: 接收参数
    thread_args_t *args = (thread_args_t *)arg;
    int sockfd = args->sockfd;  // 从参数获取socket描述符
    list *head = args->head;     // 从参数获取 真正的链表头
    free(args); // 已经获取了参数，释放结构体内存
    args = NULL;
    msg_t msg_s;
    char input_buf[128]; // 用于 fgets
    memset(&msg_s, 0, sizeof(msg_s));
    strcpy(msg_s.id, "server");  // 服务器ID
    msg_s.type = 'C';  // 标记为聊天消息

    printf("服务器消息发送线程启动（输入消息并回车发送给所有在线用户）\n");
    while (1)
    {
        // 读取服务器输入（注意：scanf格式修正）
        printf("server: ");
        fflush(stdout);  // 刷新缓冲区，确保提示正常显示
        if (fgets(input_buf, sizeof(input_buf), stdin) == NULL)
        {
            perror("fgets error");
            continue; // 出错或EOF(Ctrl+D)
        }
        // 移除 fgets 带来的换行符
        input_buf[strcspn(input_buf, "\n")] = 0;
        // 如果只输入了回车，则跳过
        if (strlen(input_buf) == 0)
        {
            continue;
        }
        strcpy(msg_s.text, input_buf);
        // <-- 修正 15: 在访问链表前加锁
        pthread_mutex_lock(&list_mutex);
        // 群发消息给所有在线用户
      
        list *p = head->next;
        while (p != NULL)
        {
            sendto(sockfd, &msg_s, sizeof(msg_s), 0,
                   (struct sockaddr *)&(p->caddr), sizeof(p->caddr));
            p = p->next;
        }
        // <-- 修正 16: 完成访问后解锁
        pthread_mutex_unlock(&list_mutex);
    }
    return NULL;
}
//私聊功能
void private_chat(int sockfd, msg_t msg, list *head, struct sockaddr_in caddr)
{
    char target_id[32];
    char message_content[128];
    list *target_node = NULL;
    if(sscanf(msg.text,"%31s %[^\n]",target_id,message_content)<2)
    {
        return;
    }
    pthread_mutex_lock(&list_mutex);
    list *p=head->next;
    while(p!=NULL){
        if(strcmp(p->id,target_id)==0)
        {
            target_node=p;//对应人
            break;
        }
        p=p->next;
    }
    if(target_node!=NULL)
    {
        msg_t private_msg;
        memset(&private_msg,0,sizeof(private_msg));
        private_msg.type='C';
        strcpy(private_msg.text,message_content);
        snprintf(private_msg.id, sizeof(private_msg.id), "%s (private)", msg.id);  
        sendto(sockfd, &private_msg, sizeof(private_msg), 0,
               (struct sockaddr *)&(target_node->caddr), sizeof(target_node->caddr));    
    }
    else{
        //没找到
        msg_t error_msg;
        memset(&error_msg,0,sizeof(error_msg));
        error_msg.type='C';
        strcpy(error_msg.id,"Server");
        snprintf(error_msg.text,sizeof(error_msg.text),"User '%s' not found or offline",target_id);
        sendto(sockfd,&error_msg,sizeof(error_msg),0,(struct sockaddr*)&caddr,sizeof(caddr));
    }
    pthread_mutex_unlock(&list_mutex);
}