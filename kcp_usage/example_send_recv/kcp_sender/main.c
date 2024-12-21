#include "ikcp.h"

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>
#include <pthread.h>

#if defined(_WIN32) || defined(_WIN64)
#include <winsock2.h>
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/time.h>
#endif

#define SERVER_IP "192.168.0.111"
#define CLIENT_IP "192.168.0.110"
#define SERVER_PORT 12345
// #define CLIENT_PORT 12346
#define KCP_SESSION 0x1
#define KCP_SEND_WND_SIZE 128
#define KCP_RECV_WND_SIZE 128

#if defined(_WIN32) || defined(_WIN64)
#define SOCKET_PL SOCKET
#define PL_SOCKET_ERROR SOCKET_ERROR
#define PL_INVALID_SOCKET INVALID_SOCKET
#else
#define SOCKET_PL int
#define PL_SOCKET_ERROR -1
#define PL_INVALID_SOCKET -1
#endif

typedef struct
{
    char cmd;
    char opt;
    char reserve[2];
    uint32_t dataLen;
} pre_kcp_header;
typedef struct
{
    pre_kcp_header header;
    char data[0x10000];
} pre_kcp_pkt;

typedef enum
{
    CMD_TYPE_CTRL_CONN_REQ = 1,
    CMD_TYPE_CTRL_CLOSE_REQ,
    CMD_TYPE_KCP
} cmd_type_e;

SOCKET_PL gUdpSocket;
struct sockaddr_in gClientAddr;
int gKcpUser = 5;
pthread_t gRecvThread;
pthread_t gUpThread;

ikcpcb *gKcp = NULL;

pthread_mutex_t lock;

ikcpcb *get_kcp()
{
    return gKcp;
}

void set_kcp(ikcpcb *kcp)
{
    gKcp = kcp;
}
int *get_kcp_user()
{
    return &gKcpUser;
}

SOCKET_PL *get_udp_socket()
{
    return &gUdpSocket;
}

struct sockaddr_in *get_client_addr()
{
    return &gClientAddr;
}

static inline void itimeofday(long *sec, long *usec)
{
#if defined(__unix)
    struct timeval time;
    gettimeofday(&time, NULL);
    if (sec)
        *sec = time.tv_sec;
    if (usec)
        *usec = time.tv_usec;
#else
    static long mode = 0, addsec = 0;
    BOOL retval;
    static IINT64 freq = 1;
    IINT64 qpc;
    if (mode == 0)
    {
        retval = QueryPerformanceFrequency((LARGE_INTEGER *)&freq);
        freq = (freq == 0) ? 1 : freq;
        retval = QueryPerformanceCounter((LARGE_INTEGER *)&qpc);
        addsec = (long)time(NULL);
        addsec = addsec - (long)((qpc / freq) & 0x7fffffff);
        mode = 1;
    }
    retval = QueryPerformanceCounter((LARGE_INTEGER *)&qpc);
    retval = retval * 2;
    if (sec)
        *sec = (long)(qpc / freq) + addsec;
    if (usec)
        *usec = (long)((qpc % freq) * 1000000 / freq);
#endif
}

/* get clock in millisecond 64 */
static inline IINT64 iclock64(void)
{
    long s, u;
    IINT64 value;
    itimeofday(&s, &u);
    value = ((IINT64)s) * 1000 + (u / 1000);
    return value;
}

static inline IUINT32 iclock()
{
    return (IUINT32)(iclock64() & 0xfffffffful);
}

int do_udp_send(const char *buf, int len, cmd_type_e type)
{
    pre_kcp_pkt pkt;
    pkt.header.cmd = (char)type;
    pkt.header.opt = 0;
    pkt.header.dataLen = len;
    if (buf)
    {
        memcpy(pkt.data, buf, len);
    }
    // const QHostAddress address(QString(SEND_IP));
    // socket->writeDatagram((const char *)&pkt, len + sizeof(pre_kcp_header), address, SEND_PORT);
    SOCKET_PL *udp_socket = get_udp_socket();
    int ret = sendto(*udp_socket, &pkt, len + sizeof(pre_kcp_header), 0, (struct sockaddr *)get_client_addr(), sizeof(struct sockaddr_in));
    static int ix = 0;
    printf("real_udp_send....num=%d\n", ++ix);
    return ret;
}
int udp_send(const char *buf, int len, struct IKCPCB *kcp, void *user)
{
    // SOCKET_PL *udp_socket = get_udp_socket();
    // int ret = sendto(*udp_socket, buf, len, 0, (struct sockaddr *)get_client_addr(), sizeof(struct sockaddr));
    int ret = do_udp_send(buf, len, CMD_TYPE_KCP);
    return (ret < 0) ? -1 : 0;
}

void do_sleep(int msec)
{
#if defined(_WIN32) || defined(_WIN64)
    Sleep(msec);
#else
    usleep(msec * 1000);
#endif
}

pthread_t *get_recv_thread()
{
    return &gRecvThread;
}

pthread_t *get_update_thread()
{
    return &gUpThread;
}
void cleanup_resource()
{
    pthread_join(*get_recv_thread(), NULL);
    ikcpcb *kcp = get_kcp();
    if (kcp)
    {
        ikcp_release(kcp);
    }

    SOCKET_PL *udp_socket = get_udp_socket();
#if defined(_WIN32) || defined(_WIN64)
    closesocket(*udp_socket);
    WSACleanup();
#else
    close(*udp_socket);
#endif
}

IUINT32 get_tick_count()
{
#if defined(_WIN32) || defined(_WIN64)
    return GetTickCount();
#else
    return iclock();
#endif
}

void *update_thread(void *arg)
{
    while (1)
    {

        pthread_mutex_lock(&lock);
        if (get_kcp())
        {
            ikcp_update(get_kcp(), get_tick_count());
        }
        pthread_mutex_unlock(&lock);
        do_sleep(10);
    }
}

bool kcp_conn()
{
    ikcpcb *kcp = ikcp_create(KCP_SESSION, (void *)get_kcp_user());
    set_kcp(kcp);

    kcp->output = udp_send;

    // kcp参数
    ikcp_nodelay(kcp, 1, 20, 2, 1); // fast mode, disable congestion control
    ikcp_wndsize(kcp, KCP_SEND_WND_SIZE, KCP_RECV_WND_SIZE);
    return true;
}
bool kcp_disconn()
{
    if (get_kcp())
    {
        ikcp_release(get_kcp());
        set_kcp(NULL);
    }
    return true;
}

void *receiver_thread(void *arg)
{
    int sockfd = *get_udp_socket();

    char buf[2048];
    struct sockaddr_in sender_addr;
    socklen_t addr_len = sizeof(sender_addr);
    uint32_t ix = 0;
    while (1)
    {
        // if (!kcp)
        // {
        //     // printf("receiver_thread null....\n");
        //     do_sleep(20);
        //     continue;
        // }
        // ikcp_update(kcp, get_tick_count());
        // printf("receiver_thread....\n");
        // 接收 UDP 数据
        int n = recvfrom(sockfd, buf, sizeof(buf), 0, (struct sockaddr *)&sender_addr, &addr_len);
        if (n > 0)
        {
            // 将接收到的数据传递给 KCP

            pre_kcp_header *hdr = (pre_kcp_header *)buf;
            if (CMD_TYPE_KCP == hdr->cmd)
            {
                pthread_mutex_lock(&lock);
                if (n - sizeof(pre_kcp_header))
                {
                    if (get_kcp())
                    {
                        ikcp_input(get_kcp(), buf + sizeof(pre_kcp_header), n - sizeof(pre_kcp_header));
                    }
                }
                else
                {
                    printf("n - sizeof(pre_kcp_header) < 0\n");
                }

                pthread_mutex_unlock(&lock);
            }
            if (CMD_TYPE_CTRL_CONN_REQ == hdr->cmd)
            {
                pthread_mutex_lock(&lock);
                struct sockaddr_in *client_addr = get_client_addr();
                memcpy(client_addr, &sender_addr, addr_len);
                kcp_conn();
                pthread_mutex_unlock(&lock);
            }
            if (CMD_TYPE_CTRL_CLOSE_REQ == hdr->cmd)
            {
                pthread_mutex_lock(&lock);
                kcp_disconn();
                pthread_mutex_unlock(&lock);
            }

            printf("ikcp_input done, num=%d\n", ++ix);
        }
        // 短暂休眠，避免占用过多 CPU
        do_sleep(20);
    }
    return NULL;
}

int main(int argc, char **argv)
{
    atexit(cleanup_resource);

#if defined(_WIN32) || defined(_WIN64)
    // windows sock init
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif

    // create socket
    SOCKET_PL *udp_socket = get_udp_socket();
    *udp_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (*udp_socket == PL_INVALID_SOCKET)
    {
        perror("udp socket create fail");
        exit(EXIT_FAILURE);
    }

    // socket bind send port
    struct sockaddr_in server_addr = {0};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    server_addr.sin_addr.s_addr = inet_addr(CLIENT_IP);
    if (bind(*udp_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) == PL_SOCKET_ERROR)
    {
        perror("udp socket bind fail");
        exit(EXIT_FAILURE);
    }

    // config target addr
    // struct sockaddr_in *server_addr = get_server_addr();
    // server_addr->sin_family = AF_INET;
    // server_addr->sin_port = htons(SERVER_PORT);
    // server_addr->sin_addr.s_addr = inet_addr(SERVER_IP);

    // ikcp control block
    // ikcpcb *kcp = ikcp_create(KCP_SESSION, (void *)get_kcp_user());
    // set_kcp(kcp);

    // kcp->output = udp_send;

    // // kcp参数
    // ikcp_nodelay(kcp, 1, 20, 2, 1); // fast mode, disable congestion control
    // ikcp_wndsize(kcp, KCP_SEND_WND_SIZE, KCP_RECV_WND_SIZE);
    // ikcp_setmtu(kcp, 1400);

    if (pthread_create(get_update_thread(), NULL, update_thread, NULL) != 0)
    {
        perror("pthread_create failed");
        return -1;
    }
    // 接收线程处理ACK
    if (pthread_create(get_recv_thread(), NULL, receiver_thread, NULL) != 0)
    {
        perror("pthread_create failed");
        return -1;
    }

    // send kcp
    uint8_t data_buf[0xFFFF + 1] = {0};
    uint32_t ix = 0;
    while (true)
    {
        pthread_mutex_lock(&lock);
        if (get_kcp())
        {
            ((uint32_t *)data_buf)[0] = 0xFEFEFEFE;
            ((uint32_t *)data_buf)[1] = ix++;
            printf("udp_sending....num=%d\n", ix);
            int send_len = ikcp_send(get_kcp(), data_buf, 1000);
        }

        pthread_mutex_unlock(&lock);
        // ikcp_update(kcp, get_tick_count());
        do_sleep(500);
    }

    while (true)
    {
        // ikcp_update(kcp, get_tick_count());
        do_sleep(20);
    }
    return 0;
}
