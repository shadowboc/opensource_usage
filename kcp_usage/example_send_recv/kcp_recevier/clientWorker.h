#ifndef CLIENTWORKER_H
#define CLIENTWORKER_H
#include "ikcp.h"
#include <QObject>
#include <QThread>
#include <QUdpSocket>
#include <QTimer>
#include <QMutexLocker>
#include <QMutex>

typedef struct {
    char cmd;
    char opt;
    char reserve[2];
    uint32_t dataLen;
} pre_kcp_header;
typedef struct {
    pre_kcp_header header;
    char data[0x10000];
} pre_kcp_pkt;

typedef enum {
    CMD_TYPE_CTRL_CONN_REQ = 1,
    CMD_TYPE_CTRL_CLOSE_REQ,
    CMD_TYPE_KCP
} cmd_type_e;

class ClientWorker : public QObject
{
    Q_OBJECT

public:
    ClientWorker(QObject *parent = nullptr);
    ~ClientWorker();

public slots:
    void recvData();
    void updateKcpTick();
    void onStartClient();
    void onStopClient();
private:
    static int udp_send(const char *buf, int len, struct IKCPCB *kcp, void *user);
private:
    QUdpSocket* m_worker;
    QTimer* m_timer;
//    int m_kcp_user;
    ikcpcb *m_kcp;
    QByteArray m_dataRecv;
    quint32 m_udpRecvNum;
    QMutex m_locker;
//    QMutexLocker<QMutex> m_locker;
};
#endif // CLIENTWORKER_H
