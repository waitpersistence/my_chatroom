/* --- tcp_server.c (TCP Version) --- */
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
#include <sys/select.h> 

typedef struct
{
    char type;      // 消息类型 L C Q W P
    char id[32];    // 用户id
    char text[128]; // 消息内容
} msg_t;

// 链表节点：存储客户端的“连接”和ID
typedef struct node_t
{
    int conn_fd; // [TCP] 不再是 caddr，而是连接文件描述符
    char id[32]; 
    struct node_t *next;
} list;

// 用于向“管理员”线程传递的参数
typedef struct
{
    list *head; // 共享的客户端链表
} admin_args_t;

// 用于向“客户端”线程传递的参数
typedef struct
{
    int conn_fd;
    list *head;
    struct sockaddr_in caddr; // 仍然传递它，用于打印日志
} client_args_t;

// --- 全局变量 ---
pthread_mutex_t list_mutex; // 保护链表的互斥锁
list *head;                 // [TCP] 将链表头设为全局，方便所有线程访问

// --- 函数声明 ---
list *list_create(void);
void *admin_handler(void *arg);   // 管理员线程 (从stdin读)
void *client_handler(void *arg);  // [TCP] 客户端服务线程 (从socket读)
void broadcast_msg(msg_t msg, int exclude_fd); // [TCP] 新的广播函数

// (我们不再需要 login, chat, quit, who, private_chat 这些单独的函数,
//  因为它们的逻辑将被合并到 client_handler 中)

int main(int argc, char const *argv[])
{
    if (argc != 2)
    {
        printf("usage:./server <port>\n");
        return -1;
    }

    int listen_fd; // [TCP] 这是“门卫”套接字
    int conn_fd;   // [TCP] 这是“专用连接”套接字
    struct sockaddr_in saddr, caddr;
    socklen_t len = sizeof(caddr);
    pthread_t tid;

    // 1. 创建 TCP 套接字
    listen_fd = socket(AF_INET, SOCK_STREAM, 0); // [TCP] 使用 SOCK_STREAM
    if (listen_fd < 0) {
        perror("socket error"); exit(1);
    }

    // 2. 端口复用
    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // 3. 绑定 (Bind)
    memset(&saddr, 0, sizeof(saddr));
    saddr.sin_family = AF_INET;
    saddr.sin_addr.s_addr = htonl(INADDR_ANY);
    saddr.sin_port = htons(atoi(argv[1]));
    
    if (bind(listen_fd, (struct sockaddr *)&saddr, sizeof(saddr)) < 0) {
        perror("bind error"); exit(1);
    }

    // 4. 监听 (Listen) [新!]
    if (listen(listen_fd, 10) < 0) { // 10 是“排队”队列长度
        perror("listen error"); exit(1);
    }
    printf("Server is listening on port %s...\n", argv[1]);

    // 5. 初始化全局链表和互斥锁
    head = list_create();
    if (head == NULL) exit(1);
    
    if (pthread_mutex_init(&list_mutex, NULL) != 0) {
        perror("mutex init error"); exit(1);
    }

    // 6. 创建“管理员”线程
    admin_args_t *admin_args = malloc(sizeof(admin_args_t));
    admin_args->head = head;
    if (pthread_create(&tid, NULL, admin_handler, admin_args) != 0) {
        perror("pthread_create (admin) error"); exit(1);
    }
    pthread_detach(tid);

    // 7. [TCP] 主线程的“门卫”循环 (Accept loop)
    while (1)
    {
        // Accept() 会阻塞，直到一个新客户端连接进来
        conn_fd = accept(listen_fd, (struct sockaddr *)&caddr, &len);
        if (conn_fd < 0) {
            perror("accept error");
            continue; // 继续等待下一个
        }

        printf("New client connected: IP=%s, Port=%d\n",
               inet_ntoa(caddr.sin_addr), ntohs(caddr.sin_port));

        // 准备参数，传递给新的“服务员”线程
        client_args_t *args = malloc(sizeof(client_args_t));
        args->conn_fd = conn_fd;
        args->head = head;
        args->caddr = caddr; // caddr 只是为了日志

        // 8. [TCP] 为这个新客户端创建一个专属线程
        if (pthread_create(&tid, NULL, client_handler, args) != 0) {
            perror("pthread_create (client) error");
            close(conn_fd); // 创建失败，关闭这个连接
            free(args);
        }
        pthread_detach(tid); // 自动回收线程
    }

    close(listen_fd);
    pthread_mutex_destroy(&list_mutex);
    return 0;
}

// 创建链表头节点
list *list_create(void)
{
    list *p = (list *)malloc(sizeof(list));
    if (p == NULL) {
        perror("malloc error"); return NULL;
    }
    p->next = NULL;
    // (注意：头节点是空的，不存数据)
    return p;
}

// [TCP] 新的“服务员”线程函数
void *client_handler(void *arg)
{
    // 1. 解析参数
    client_args_t *args = (client_args_t *)arg;
    int conn_fd = args->conn_fd;
    list *head = args->head;
    free(args); // 立即释放 malloc 的内存
    args = NULL;

    msg_t msg;
    char client_id[32];
    ssize_t n;

    // 2. [TCP] 处理登录：等待客户端发送的第一个包
    // ！！！警告：这是一个简化的实现，见文末说明
    n = recv(conn_fd, &msg, sizeof(msg), 0);
    if (n <= 0 || msg.type != 'L') {
        printf("Client login failed or disconnected.\n");
        close(conn_fd);
        pthread_exit(NULL);
    }
    
    // 登录成功，保存 ID
    strcpy(client_id, msg.id);

    // 3. 将新用户添加到全局链表，并广播“上线”消息
    list *new_node = (list *)malloc(sizeof(list));
    new_node->conn_fd = conn_fd;
    strcpy(new_node->id, client_id);
    new_node->next = NULL;

    sprintf(msg.text, "%s 已上线", client_id);
    // (我们把 "上线" 消息和 "添加自己" 合并到一次加锁中)

    pthread_mutex_lock(&list_mutex);
    list *p = head;
    while (p->next != NULL) {
        p = p->next;
        // 广播“上线”消息给其他已在线的人
        send(p->conn_fd, &msg, sizeof(msg), 0);
    }
    p->next = new_node; // 尾插法
    pthread_mutex_unlock(&list_mutex);
    
    printf("User '%s' logged in.\n", client_id);

    // 4. [TCP] 消息循环：等待这个客户端的后续消息
    while(1)
    {
        memset(&msg, 0, sizeof(msg));
        // recv() 会阻塞，直到这个客户端发来消息
        // ！！！警告：这仍然是简化的实现
        n = recv(conn_fd, &msg, sizeof(msg), 0);
        
        // 5. [TCP] 检查客户端是否断开
        if (n <= 0) {
            if (n == 0) {
                printf("User '%s' disconnected gracefully.\n", client_id);
            } else {
                perror("recv error");
            }
            break; // 退出循环，准备清理
        }

        // 6. 处理收到的消息
        strcpy(msg.id, client_id); // 确保 ID 是正确的

        if (msg.type == 'C') {
            printf("Chat Log [%s]: %s\n", msg.id, msg.text);
            broadcast_msg(msg, conn_fd); // 广播给除自己外的所有人
        } 
        else if (msg.type == 'W') {
            // --- 'who' 逻辑 ---
            msg_t response_msg;
            memset(&response_msg, 0, sizeof(response_msg));
            response_msg.type = 'C';
            strcpy(response_msg.id, "Server");
            strcpy(response_msg.text, "--- Online Users ---\n");

            pthread_mutex_lock(&list_mutex);
            list *p_who = head->next;
            while(p_who != NULL) {
                if (strlen(response_msg.text) + strlen(p_who->id) + 2 < sizeof(response_msg.text)) {
                    strcat(response_msg.text, p_who->id);
                    strcat(response_msg.text, "\n");
                }
                p_who = p_who->next;
            }
            pthread_mutex_unlock(&list_mutex);
            
            // 只发回给请求者
            send(conn_fd, &response_msg, sizeof(response_msg), 0);
        }
        else if (msg.type == 'P') {
            // --- 'private_chat' 逻辑 ---
            char target_id[32];
            char message_content[128];
            list *target_node = NULL;
            
            if (sscanf(msg.text, "%31s %[^\n]", target_id, message_content) < 2) {
                continue; // 格式错误，忽略
            }

            pthread_mutex_lock(&list_mutex);
            list *p_pm = head->next;
            while(p_pm != NULL) {
                if(strcmp(p_pm->id, target_id) == 0) {
                    target_node = p_pm; // 找到了
                    break;
                }
                p_pm = p_pm->next;
            }
            pthread_mutex_unlock(&list_mutex); // 查找完毕，先解锁

            if (target_node != NULL) {
                // 准备私聊消息
                msg_t private_msg;
                memset(&private_msg, 0, sizeof(private_msg));
                private_msg.type = 'C';
                strcpy(private_msg.text, message_content);
                snprintf(private_msg.id, sizeof(private_msg.id), "%s (private)", client_id); 
                // 只发给目标
                send(target_node->conn_fd, &private_msg, sizeof(private_msg), 0);
            } else {
                // 没找到，发回错误
                msg_t error_msg;
                memset(&error_msg, 0, sizeof(error_msg));
                error_msg.type = 'C';
                strcpy(error_msg.id, "Server");
                snprintf(error_msg.text, sizeof(error_msg.text), "User '%s' not found.", target_id);
                send(conn_fd, &error_msg, sizeof(error_msg), 0);
            }
        }
    } // end while(1)

    // 7. [TCP] 清理：客户端已断开
    close(conn_fd); // 关闭这个客户端的连接

    // 准备“下线”广播消息
    memset(&msg, 0, sizeof(msg));
    msg.type = 'C';
    strcpy(msg.id, "Server");
    sprintf(msg.text, "%s 已下线", client_id);
    
    // 从全局链表中移除自己，并广播“下线”
    pthread_mutex_lock(&list_mutex);
    list *p_del = head;
    while(p_del->next != NULL) {
        if (p_del->next->conn_fd == conn_fd) {
            list *dele = p_del->next;
            p_del->next = dele->next; // 断开链表
            free(dele); // 释放节点
            break; // 找到并删除后就退出
        }
        // 向其他人广播下线消息
        send(p_del->next->conn_fd, &msg, sizeof(msg), 0);
        p_del = p_del->next;
    }
    pthread_mutex_unlock(&list_mutex);

    printf("User '%s' cleaned up.\n", client_id);
    pthread_exit(NULL); // 结束这个“服务员”线程
}

// [TCP] 管理员线程函数 (修改为使用 send)
void *admin_handler(void *arg)
{
    admin_args_t *args = (admin_args_t *)arg;
    list *head = args->head;
    free(args);
    args = NULL;
    
    msg_t msg_s;
    char input_buf[128]; 

    memset(&msg_s, 0, sizeof(msg_s));
    strcpy(msg_s.id, "Server (Admin)");
    msg_s.type = 'C'; 

    printf("服务器消息发送线程启动...\n");
    while (1)
    {
        printf("server-admin: ");
        fflush(stdout);
        if (fgets(input_buf, sizeof(input_buf), stdin) == NULL) {
            continue;
        }
        
        input_buf[strcspn(input_buf, "\n")] = 0;
        if (strlen(input_buf) == 0) {
            continue;
        }
        strcpy(msg_s.text, input_buf);

        // 广播给所有在线用户
        broadcast_msg(msg_s, -1); // -1 表示不排除任何人
    }
    return NULL;
}

// [TCP] 新的广播工具函数
void broadcast_msg(msg_t msg, int exclude_fd)
{
    pthread_mutex_lock(&list_mutex);
    list *p = head->next;
    while (p != NULL)
    {
        if (p->conn_fd != exclude_fd) // 排除掉发送者自己
        {
            // [TCP] 使用 send
            send(p->conn_fd, &msg, sizeof(msg), 0);
        }
        p = p->next;
    }
    pthread_mutex_unlock(&list_mutex);
}