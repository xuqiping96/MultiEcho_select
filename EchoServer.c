#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <errno.h>

#define BUF_SIZE 1024
#define SERV_ADR "127.0.0.1"
#define PORT 10000
#define MAX_CLNT 5
#define NAME_LEN 10

typedef struct {
    int clnt_sock;
    char *name;
    FILE *read_fp;
    FILE *write_fp;
} Clnt;

int listen_sock, conn_sock;
struct sockaddr_in serv_adr;
struct sockaddr_in clnt_adr;
socklen_t clnt_adr_sz;

Clnt clnts[FD_SETSIZE];
int maxi, maxfd;
int nready;
fd_set read_set, cpy_read_set;

pthread_t clnt_tid;

////////////////////函数声明////////////////////
/**
 * @brief 显示错误信息
 * 
 */
void error_handler(char *error_msg);

/**
 * @brief 初始化服务器地址并绑定套接字
 * 
 */
void server_addr_init(struct sockaddr_in *erv_adr, char *addr, int port, int serv_sock);

/**
 * @brief 初始化描述符集和客户端数组
 * 
 */
void clnt_set_init();

/**
 * @brief 将客户端加入客户端数组，并在描述符集中设置对应的位
 * 
 */
void add_clnt_sock(int clnt_sock);

/**
 * @brief 将客户端从数组中移除，并在描述符集中设置对应位
 * 
 */
void remove_clnt_sock(Clnt *clnt);

/**
 * @brief 向除了当前客户端以外的其他客户端发送消息
 * 
 */
void send_message_to_clnts(char *message, int clnt_sock);

/**
 * @brief 判断是否需要断开连接
 * 
 * @return 如果是则返回1，否则返回0
 */
int is_saying_bye(char *message);

/**
 * @brief 客户端线程的执行函数
 * 
 */
void *clnt_handler(void *clnt_sock);

////////////////////函数定义////////////////////
void error_handler(char *error_msg)
{
    fprintf(stderr, "%s error.\n", error_msg);
    exit(EXIT_FAILURE);
}

void server_addr_init(struct sockaddr_in *serv_adr, char *addr, int port, int serv_sock)
{
    int err;

    memset(serv_adr, 0, sizeof(*serv_adr));
    serv_adr->sin_family = AF_INET;
    serv_adr->sin_port = htons(port);
    err = inet_pton(AF_INET, addr, &(serv_adr->sin_addr));
    if(err != 1)
    {
        error_handler("inet_pton()");
    }

    err = bind(serv_sock, (struct sockaddr *)serv_adr, sizeof(*serv_adr));
    if(err == -1)
    {
        error_handler("bind()");
    }
}

void clnt_set_init()
{
    for(int i = 0; i < FD_SETSIZE; i++)
    {
        clnts[i].clnt_sock = -1;
        clnts[i].name = NULL;
    }
    
    maxfd = listen_sock;
    maxi = -1;

    FD_ZERO(&read_set);
    FD_SET(listen_sock, &read_set);
}

void add_clnt_sock(int clnt_sock)
{
    int i;

    for(i = 0; i < FD_SETSIZE  ; i++)
    {
        if(clnts[i].clnt_sock < 0)
        {
            clnts[i].clnt_sock = clnt_sock;
            clnts[i].read_fp = fdopen(clnt_sock, "r");
            clnts[i].write_fp = fdopen(clnt_sock, "w");

            break;
        }
    }

    FD_SET(clnt_sock, &read_set);

    if(clnt_sock > maxfd)
    {
        maxfd = clnt_sock;
    }

    if(i > maxi)
    {
        maxi = i;
    }
}

void remove_clnt_sock(Clnt *clnt)
{
    fclose(clnt->read_fp);
    fclose(clnt->write_fp);

    free(clnt->name);
    clnt->name = NULL;

    FD_CLR(clnt->clnt_sock, &read_set);
    clnt->clnt_sock = -1;
}

void send_message_to_clnts(char *message, int clnt_sock)
{
    for(int i = 0; i < FD_SETSIZE; i++)
    {
        if(clnts[i].clnt_sock > 0 && clnts[i].clnt_sock != clnt_sock)
        {
            fputs(message, clnts[i].write_fp);
            fflush(clnts[i].write_fp);
        }
    }
}

int is_saying_bye(char *message)
{
    if(strcmp(message, "BYE\n") == 0)
    {
        return 1;
    } else
    {
        return 0;
    }
}

void* clnt_handler(void *arg)
{
    char read_message[BUF_SIZE];
    char send_message[BUF_SIZE];
    Clnt *clnt = (Clnt *)arg;
    char *name;

    //从客户端获得消息并发送给其他客户端
    fgets(read_message, BUF_SIZE, clnt->read_fp);
    if(clnt->name == NULL)
    {
        name = strtok(read_message, "\n");
        clnt->name = (char *)calloc(NAME_LEN + 1, sizeof(char));
        strcpy(clnt->name, name);

        snprintf(send_message ,BUF_SIZE, "%s has joind\n", clnt->name);
    } else
    {
        snprintf(send_message, BUF_SIZE, "Message from %s: %s", clnt->name, read_message);
    }
    
    printf("%s", send_message);
    send_message_to_clnts(send_message, clnt->clnt_sock);

    if(is_saying_bye(read_message))
    {
        printf("Closing down connection ...\n");
        remove_clnt_sock(clnt);
    }

    return NULL;
}

////////////////////主函数入口////////////////////
int main(int argc, char *argv[])
{
    int err;

    //创建套接字
    listen_sock = socket(AF_INET, SOCK_STREAM, 0);
    if(listen_sock == -1)
    {
        error_handler("socket()");
    }
    //初始化服务器地址并绑定套接字
    server_addr_init(&serv_adr, SERV_ADR, PORT, listen_sock);
    //监听连接
    err = listen(listen_sock, MAX_CLNT);
    if(err != 0)
    {
        error_handler("listen()");
    }
    //初始化监听集和客户端数组
    clnt_set_init();

    //接受新连接并创建对应线程
    clnt_adr_sz = sizeof(clnt_adr);
    printf("Listening for connection ...\n");
    while(1)
    {
        cpy_read_set = read_set;
        nready = select(maxfd + 1, &cpy_read_set, NULL, NULL, NULL);

        if(nready == -1)
        {
            error_handler("select()");
        }
        
        if(FD_ISSET(listen_sock, &cpy_read_set))
        {
            conn_sock = accept(listen_sock, (struct sockaddr *)&clnt_adr, &clnt_adr_sz);
            printf("New client accepted\n");
            add_clnt_sock(conn_sock);
            printf("Connection successful\n");
            printf("Listening for input ...\n");
            printf("Listening for connection ...\n");

            if(--nready <= 0)
            {
                continue;
            }
        }

        for(int i = 0; i <= maxi; i++)
        {
            if(clnts[i].clnt_sock < 0)
            {
                continue;
            }

            if(FD_ISSET(clnts[i].clnt_sock, &cpy_read_set))
            {
                clnt_handler(&clnts[i]);
                
                /*
                //创建新线程来完成一次对客户端的读写
                err = pthread_create(&clnt_tid, NULL, clnt_handler, (void *)&clnts[i]);
                if(err != 0)
                {
                    error_handler("pthread_create()");
                }

                err = pthread_detach(clnt_tid);
                if(err != 0)
                {
                    error_handler("pthread_detach()");
                }
                */

                if(--nready <= 0)
                {
                    break;
                }
            }
        }
    }

    return 0;
}