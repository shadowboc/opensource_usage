/*
 * @Author: chenbo shadowboc0302@gmail.com
 * @Date: 2024-12-22 16:22:49
 * @LastEditors: chenbo shadowboc0302@gmail.com
 * @LastEditTime: 2024-12-22 23:37:04
 * @FilePath: /kcp_sender/skcp.c
 * @Description: 这是默认设置,请设置`customMade`, 打开koroFileHeader查看配置 进行设置: https://github.com/OBKoro1/koro1FileHeader/wiki/%E9%85%8D%E7%BD%AE
 */
#include "skcp.h"
#include "kprintf.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ifaddrs.h>
#include <arpa/inet.h>

#define ETH_INTERFACE "eth"  // 假设有线网卡接口名称包含 "eth"（如 eth0）
#define WLAN_INTERFACE "wlan"  // 假设无线网卡接口名称包含 "wlan"（如 wlan0）
#define KCP_STREAM_PORT 9998

static int init_cli_network(skcp_t *skcp)
{
    // 设置客户端
    // 创建socket对象
    skcp->fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (skcp->fd < 0) {
        K_PRINTF_ERROR("client socket create failed");
        return -1;
    }
    // 设置为非阻塞
    if (-1 == fcntl(skcp->fd, F_SETFL, fcntl(skcp->fd, F_GETFL) | O_NONBLOCK))
    {
        K_PRINTF_ERROR("client socket set nonblock failed");
        close(skcp->fd);
        return -1;
    }

    skcp->servaddr.sin_family = AF_INET;
    skcp->servaddr.sin_port = htons(skcp->conf.port);
    skcp->servaddr.sin_addr.s_addr = inet_addr(skcp->conf.ipaddr);

    K_PRINTF_ERROR("%s, done", __func__);
    return 0;
}

static int init_serv_network(skcp_t *skcp)
{
    // 设置服务端
    skcp->fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (-1 == skcp->fd)
    {
        K_PRINTF_ERROR("serve socket create failed");
        return -1;
    }
    // 设置为非阻塞
    if (-1 == fcntl(skcp->fd, F_SETFL, fcntl(skcp->fd, F_GETFL) | O_NONBLOCK))
    {
        K_PRINTF_ERROR("client socket set nonblock failed");
        close(skcp->fd);
        return -1;
    }

    struct sockaddr_in servaddr;
    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    if (NULL == skcp->conf.ipaddr)
    {
        servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    }
    else
    {
        servaddr.sin_addr.s_addr = inet_addr(skcp->conf.ipaddr);
    }
    servaddr.sin_port = htons(skcp->conf.port);

    if (-1 == bind(skcp->fd, (struct sockaddr *)&servaddr, sizeof(servaddr)))
    {
        K_PRINTF_ERROR("server socket bind failed");
        close(skcp->fd);
        return -1;
    }

    K_PRINTF_INFO("%s, done", __func__);
    return 0;
}

/**
 * brief: 释放ikcp会话, ikcp_release
*/
static void free_conn(skcp_t *skcp, skcp_conn_t *conn)
{
    if (!skcp || !conn)
    {
        return;
    }

    // if (skcp->conns)
    // {
    //     HASH_DEL(skcp->conns, conn);
    // }

    if (conn->kcp)
    {
        ikcp_release(conn->kcp);
        conn->kcp = NULL;
    }

    conn->status = SKCP_CONN_ST_OFF;
    conn->cid = 0;
    conn->user_data = NULL;

    free(conn);
}

static uint32_t get_local_ip()
{
    struct ifaddrs *ifAddrStruct = NULL;
    struct ifaddrs *ifa = NULL;
    void *tmpAddrPtr = NULL;
    uint32_t ip_address;
    uint32_t wip_address = 0;

    // 获取所有网络接口信息
    if (getifaddrs(&ifAddrStruct) == 0) {
        for (ifa = ifAddrStruct; ifa != NULL; ifa = ifa->ifa_next) {
            // 如果是IPv4地址
            if (ifa->ifa_addr->sa_family == AF_INET) {
                tmpAddrPtr = &((struct sockaddr_in *)ifa->ifa_addr)->sin_addr;

                // 将 IP 地址转为字符串
                // inet_ntop(AF_INET, tmpAddrPtr, ip_address, INET_ADDRSTRLEN);
                inet_pton(AF_INET, inet_ntoa(*(struct in_addr*)tmpAddrPtr), &ip_address);

                // 优先选择有线网卡，通常以 "eth" 开头
                if (strstr(ifa->ifa_name, ETH_INTERFACE) != NULL) {
                    // 找到以太网接口，返回该接口的 IP 地址
                    freeifaddrs(ifAddrStruct);  // 释放资源
                    return ip_address;
                }
                // 无线网卡，则做记录
                if (strstr(ifa->ifa_name, WLAN_INTERFACE) != NULL) {
                    // 找到以太网接口，返回该接口的 IP 地址
                   wip_address = ip_address;
                }
            }
        }

        if (ifAddrStruct != NULL) {
            freeifaddrs(ifAddrStruct);  // 释放资源
        }
    }
    if(wip_address) {
        return wip_address;
    }
    return NULL;  // 如果没有找到有效的 IP 地址
}

static skcp_conn_t* find_conn(skcp_t *skcp, uint32_t cid)
{
    if (!skcp) {
        return NULL;
    }
    skcp_conn_t* conn = skcp->conns;
    return conn;
}

static void remove_conn(skcp_t *skcp, skcp_conn_t* conn)
{
    if (!skcp) {
        K_PRINTF_ERROR("%s, fail, skcp is null", __func__);
        return;
    }
    skcp->conns = NULL;
}

static void add_conn(skcp_t *skcp, skcp_conn_t* conn)
{
    if (!skcp) {
        K_PRINTF_ERROR("%s, fail, skcp is null", __func__);
        return;
    }
    skcp->conns = conn;
}

static int do_udp_send(skcp_t *skcp, skcp_pkt_t *pkt, struct sockaddr_in target_addr)
{
    int ret = sendto(skcp->fd, &pkt, pkt->header.dataLen + sizeof(skcp_header_t), 0, (struct sockaddr *)&target_addr, sizeof(struct sockaddr_in));
    if (ret <= 0) {
        K_PRINTF_ERROR("%s, sendto fail, ret=%d, errnostr=%s", __func__, ret, strerror(errno));
        return -1;
    }
    return ret;
}

static int kcp_output(const char *buf, int len, struct IKCPCB *kcp, void *user)
{
    skcp_conn_t *conn = (skcp_conn_t *)user;
    if (!conn) {
        return -1;
    }
    skcp_pkt_t pkt;
    pkt.header.cmd = (char)CMD_TYPE_KCP;
    pkt.header.opt = 0;
    pkt.header.dataLen = len;
    if (buf)
    {
        memcpy(pkt.data, buf, len);
    }
    int ret = do_udp_send(conn->skcp, &pkt, conn->target_addr);
    if (ret > 0) {
        conn->last_w_tm = get_tick_count();
    }
    return (ret < 0) ? -1 : 0;
}

static void skcp_update_thread(void *arg)
{
    skcp_conn_t *conn = (skcp_conn_t *)(arg);
    ikcp_update(conn->kcp, get_tick_count());

    // check timeout
    uint64_t now = get_tick_count();
    if (now - conn->last_r_tm > conn->skcp->conf.keepalive * 1000)
    {
        // _LOG("timeout cid: %u", conn->id);
        skcp_disconnect(conn->skcp, conn->cid);
        return;
    }
}

/*********************************************************************************************************************************************************************************/
skcp_t *SKCP::skcp_init(skcp_conf_t *conf, void *user_data, skcp_role_e role)
{
    if (!conf)
    {
        K_PRINTF_ERROR("conf is nullptr");
        return NULL;
    }

    skcp_t *skcp = (skcp_t *)calloc(1, sizeof(skcp_t));
    if (!m_skcp) {
        K_PRINTF_ERROR("malloc skcp_t fail");
        return NULL;
    }

    memcpy(&skcp->conf, conf, sizeof(skcp_conf_t));
    if (skcp->conf.mtu > KCP_MAX_MTU) {
        skcp->conf.mtu = KCP_MAX_MTU;
        K_PRINTF_ERROR("skcp conf mtu too big, limit to %d", skcp->conf.mtu);
    }

    skcp->user_data = user_data;
    skcp->role = role;
    if (SKCP_ROLE_CLIENT == skcp->role) {
        if (init_cli_network(skcp) != 0)
        {
            free(skcp);
            return NULL;
        }
    } else {
        if (init_serv_network(skcp) != 0)
        {
            free(skcp);
            return NULL;
        }
    }

    return skcp;
}

void SKCP::skcp_free(skcp_t *skcp)
{
    if (!skcp) {
        return;
    }

    if (skcp->fd > 0) {
        close(skcp->fd);
        skcp->fd = 0;
    }

    if (skcp->conns) {
        skcp_disconnect(skcp, skcp->conns->cid);
        skcp->conns = NULL;
    }

    memset(&skcp->conf, 0x0, sizeof(skcp_conf_t));
    skcp->user_data = NULL;
    free(skcp);
}

void SKCP::skcp_connect(skcp_t *skcp, uint32_t cid, struct sockaddr_in target_addr, void *user_data)
{
    if (!skcp) {
        return;
    }

    // 释放
    skcp_conn_t* conn = find_conn(skcp, cid);
    if (conn) {
        remove_conn(skcp, conn);
        delete conn;
    }
    skcp_conn_t* conn = (skcp_conn_t*)calloc(1, sizeof(skcp_conn_t));
    if (!conn) {
        K_PRINTF_ERROR("%s, calloc fail", __func__);
        return;
    }
    add_conn(skcp, conn);
    uint32_t ts = get_tick_count();
    conn->last_r_tm = ts;
    conn->last_w_tm = ts;
    conn->status = SKCP_CONN_ST_ON; // SKCP_CONN_ST_READY;
    conn->skcp = skcp;
    conn->user_data = user_data;
    conn->cid = cid;
    conn->target_addr = target_addr;

    ikcpcb *kcp = ikcp_create(cid, conn);
    skcp_conf_t *conf = &(skcp->conf);
    kcp->output = kcp_output;
    ikcp_wndsize(kcp, conf->sndwnd, conf->rcvwnd);
    ikcp_nodelay(kcp, conf->nodelay, conf->interval, conf->nodelay, conf->nc);
    ikcp_setmtu(kcp, conf->mtu);
}

void SKCP::skcp_disconnect(skcp_t *skcp, uint32_t cid)
{
    skcp_conn_t *conn = skcp_get_conn(skcp, cid);
    if (!conn)
    {
        return;
    }

    K_PRINTF_INFO("skcp_close_conn cid: %u", cid);
    skcp->conf.on_close(skcp, cid);

    free_conn(skcp, conn);
}

skcp_conn_t *SKCP::skcp_get_conn(skcp_t *skcp, uint32_t cid)
{
    if (!skcp) {
        return NULL;
    }
    skcp_conn_t* conn = skcp->conns;
    return conn;
}

int SKCP::skcp_send(skcp_t *skcp, uint32_t cid, const char *buf, int len)
{
    skcp_conn_t * conn = skcp_get_conn(skcp, cid);
    if (!conn || !buf || len <= 0 || conn->status != SKCP_CONN_ST_ON)
    {
        return -1;
    }

    int rt = ikcp_send(conn->kcp, buf, len); 
    if (rt < 0)
    {
        // 发送失败
        return -1;
    }
    // ikcp_update(conn->kcp, getms());
    return rt;
}

uint32_t SKCP::gen_cid(skcp_t *skcp)
{
    if (!skcp) {
        return 0;
    }
    skcp->cid_seed++;
    uint32_t local_ip = get_local_ip();
    // [31-0] = (lastip 8bit) | (port 16bit)
    uint32_t cid = ((local_ip & 0xFF) << 16) | KCP_STREAM_PORT;
    K_PRINTF_INFO("%s, cid: %u, local_ip=%u", __func__, cid, local_ip);
    return cid;
}


