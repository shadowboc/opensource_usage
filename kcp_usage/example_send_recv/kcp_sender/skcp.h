/*
 * @Author: chenbo shadowboc0302@gmail.com
 * @Date: 2024-12-22 16:22:45
 * @LastEditors: chenbo shadowboc0302@gmail.com
 * @LastEditTime: 2024-12-22 23:37:30
 * @FilePath: /kcp_sender/skcp.h
 * @Description: 这是默认设置,请设置`customMade`, 打开koroFileHeader查看配置 进行设置: https://github.com/OBKoro1/koro1FileHeader/wiki/%E9%85%8D%E7%BD%AE
 */
#ifndef __SKCP_H__
#define __SKCP_H__

#include "ikcp.h"

#include <stdint.h>
#include <arpa/inet.h>

typedef enum
{
    CMD_TYPE_CTRL_CONN_REQ = 1,
    CMD_TYPE_CTRL_CLOSE_REQ,
    CMD_TYPE_KCP
} cmd_type_e;

typedef enum
{
    SKCP_ROLE_SERVER = 1,
    SKCP_ROLE_CLIENT,
} skcp_role_e;

typedef enum
{
    SKCP_CONN_ST_ON = 1,
    SKCP_CONN_ST_OFF,
} skcp_conn_st_e;

typedef struct skcp skcp_t;

typedef struct skcp_conf
{
    int mtu;
    int interval;
    int nodelay;
    int resend;
    int nc;
    int sndwnd;
    int rcvwnd;

    int keepalive;
    char *ipaddr;
    uint16_t port;
    void (*on_accept)(skcp_t *skcp, uint32_t cid);
    void (*on_recv_cid)(skcp_t *skcp, uint32_t cid);
    void (*on_recv_data)(skcp_t *skcp, uint32_t cid, char *buf, int len);
    void (*on_close)(skcp_t *skcp, uint32_t cid);
} skcp_conf_t;

typedef struct skcp_conn
{
    skcp_t *skcp;
    void *user_data;
    uint32_t cid;
    uint64_t last_r_tm;
    uint64_t last_w_tm;
    uint64_t estab_tm;
    ikcpcb *kcp;
    skcp_conn_st_e status;
    struct sockaddr_in target_addr;
} skcp_conn_t;

struct skcp
{
    skcp_conf_t conf;
    skcp_conn_t *conns;
    skcp_role_e role;
    uint32_t cid_seed;
    int fd;
    struct sockaddr_in servaddr;
    void *user_data;
};

typedef struct
{
    char cmd;
    char opt;
    char reserve[2];
    uint32_t cid;
    uint32_t dataLen;
} skcp_header_t;

#define MAX_UDP_SIZE 0x10000
#define KCP_MAX_MTU (MAX_UDP_SIZE - sizeof(skcp_header_t))

typedef struct
{
    skcp_header_t header;
    char data[KCP_MAX_MTU];
} skcp_pkt_t;

class SKCP {
public:
    /**
     * brief: 创建网络socket, 配置kcp
     */
    static skcp_t *skcp_init(skcp_conf_t *conf, void *user_data, skcp_role_e role);

    /**
     * brief: 关闭socket, 关闭kcp
     */
    static void skcp_free(skcp_t *skcp);

    /**
     * brief: 创建kcp会话
     */
    static void skcp_connect(skcp_t *skcp, uint32_t cid, struct sockaddr_in target_addr, void *user_data);

    /**
     * brief: 关闭kcp会话
     */
    static void skcp_disconnect(skcp_t *skcp, uint32_t cid);

    /**
     * brief: 获取指定kcp会话
     */
    static skcp_conn_t *skcp_get_conn(skcp_t *skcp, uint32_t cid);

    /**
     * brief: 发送kcp消息
     */
    int skcp_send(skcp_t *skcp, uint32_t cid, const char *buf, int len);

private:
    /**
     * 生成cid
    */
    uint32_t gen_cid(skcp_t *skcp);
private:
    skcp_t* m_skcp;
};

#endif
