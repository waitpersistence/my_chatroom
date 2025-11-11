#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>//for close()
#include <string.h>
int main(){
    int listen_fd=socket(AF_INET,SOCK_STREAM,0);
    struct sockaddr_in saddr;
    memset(&saddr,0,sizeof(saddr));
    saddr.sin_family=AF_INET;
    saddr.sin_addr.s_addr=htonl(INADDR_ANY);
    saddr.sin_port=htons(8888);

    bind(listen_fd,(struct sockaddr *)&saddr,sizeof(saddr));

    listen(listen_fd,5);
    printf("Server is listening on port 8888...\n");

    struct sockaddr_in caddr;
    socklen_t len=sizeof(caddr);
    int conn_fd = accept(listen_fd, (struct sockaddr *)&caddr, &len);
    printf("A new client connected!\n");

    char buffer[128];
    memset(buffer, 0, sizeof(buffer));

    
}