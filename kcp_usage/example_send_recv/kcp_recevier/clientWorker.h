#ifndef CLIENTWORKER_H
#define CLIENTWORKER_H
#include "ikcp.h"
#include <QObject>
#include <QThread>
#include <QUdpSocket>
#include <QTimer>
#include <QMutexLocker>
#include <QMutex>

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
