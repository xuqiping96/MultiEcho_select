#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <pthread.h>

#define BUF_SIZE 1024
#define SERV_ADR "127.0.0.1"
#define PORT 10000
#define MAX_CLNT 5

typedef struct {
    int clnt_sock;
    FILE *read_fp;
    FILE *write_fp;
} Clnt;

Clnt clnts[MAX_CLNT];
pthread_t clnt_tid;
pthread_mutex_t mutex_lock;

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
 * @brief 初始化客户端数组中套接字为0
 * 
 */
void clnt_socks_init();

/**
 * @brief 将客户端套接字记录进套接字数组，并打开对应的流
 * 
 * @return 返回客户端在数组中的索引
 */
int add_clnt_sock(int clnt_sock);

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

void clnt_socks_init()
{
    for(int i = 0; i < MAX_CLNT; i++)
    {
        clnts[i].clnt_sock = 0;
    }
}

int add_clnt_sock(int clnt_sock)
{
    pthread_mutex_lock(&mutex_lock);

    for(int i = 0; i < MAX_CLNT; i++)
    {
        if(clnts[i].clnt_sock == 0)
        {
            clnts[i].clnt_sock = clnt_sock;
            clnts[i].read_fp = fdopen(clnt_sock, "r");
            clnts[i].write_fp = fdopen(dup(clnt_sock), "w");
            
            pthread_mutex_unlock(&mutex_lock);

            return i;
        }
    }

    pthread_mutex_unlock(&mutex_lock);
}

void send_message_to_clnts(char *message, int clnt_sock)
{
    pthread_mutex_lock(&mutex_lock);

    for(int i = 0; i < MAX_CLNT; i++)
    {
        if(clnts[i].clnt_sock != 0 && clnts[i].clnt_sock != clnt_sock)
        {
            fputs(message, clnts[i].write_fp);
            fflush(clnts[i].write_fp);
        }
    }

    pthread_mutex_unlock(&mutex_lock);
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

void *clnt_handler(void *arg)
{
    char get_name[BUF_SIZE];
    char *name;
    char read_message[BUF_SIZE];
    char send_message[BUF_SIZE];
    Clnt *clnt = (Clnt *)arg;

    //获得客户端名字并发送给其他客户端
    fgets(get_name, BUF_SIZE, clnt->read_fp);
    name = strtok(get_name, "\n");
    sprintf(send_message, "%s has joined\n", name);
    printf("%s", send_message);
    send_message_to_clnts(send_message, clnt->clnt_sock);
    
    //接收对应客户端的消息并转发给其他客户端
    while(1)
    {
        //读消息
        fgets(read_message, BUF_SIZE, clnt->read_fp);
        sprintf(send_message, "Message from %s: %s", name, read_message);
        printf("%s", send_message);
        //把消息发给其他客户端
        send_message_to_clnts(send_message, clnt->clnt_sock);
        //判断是否是BYE，如果是，则关闭连接，退出循环
        if(is_saying_bye(read_message))
        {
            printf("Closing down connection ...\n");

            fclose(clnt->write_fp);
            fclose(clnt->read_fp);
            //从数组中移除当前客户端
            pthread_mutex_lock(&mutex_lock);
            for(int i = 0; i < MAX_CLNT; i++)
            {
                if(clnts[i].clnt_sock == clnt->clnt_sock)
                {
                    clnts[i].clnt_sock = 0;
                    break;
                }
            }
            pthread_mutex_unlock(&mutex_lock);

            break;
        }
    }
    
    return NULL;  
}

////////////////////主函数入口////////////////////
int main(int argc, char *argv[])
{
    int serv_sock, clnt_sock;
    struct sockaddr_in serv_adr;
    struct sockaddr_in clnt_adr;
    socklen_t clnt_adr_sz;
    int clnt_idx;

    int err;

    //创建套接字
    serv_sock = socket(AF_INET, SOCK_STREAM, 0);
    if(serv_sock == -1)
    {
        error_handler("socket()");
    }
    //初始化服务器地址并绑定套接字
    server_addr_init(&serv_adr, SERV_ADR, PORT, serv_sock);
    //监听连接
    err = listen(serv_sock, MAX_CLNT);
    if(err != 0)
    {
        error_handler("listen()");
    }
    //接收新的客户端连接
    clnt_socks_init();

    //初始化互斥锁
    err = pthread_mutex_init(&mutex_lock, NULL);
    if(err != 0)
    {
        error_handler("pthread_mutex_init()");
    }

    //接受新连接并创建对应线程
    while(1)
    {
        printf("Listening for connection ...\n");
        clnt_sock = accept(serv_sock, (struct sockaddr *)&clnt_adr, &clnt_adr_sz);
        printf("New client accepted\n");
        clnt_idx = add_clnt_sock(clnt_sock);
        printf("Connection successful\n");
        printf("Listening for input ...\n");
        //为客户端创建新线程
        err = pthread_create(&clnt_tid, NULL, clnt_handler, (void *)&clnts[clnt_idx]);
        if(err != 0)
        {
            error_handler("pthread_create()");
        }

        err = pthread_detach(clnt_tid);
        if(err != 0)
        {
            error_handler("pthread_detach()");
        }
    }

    return 0;
}