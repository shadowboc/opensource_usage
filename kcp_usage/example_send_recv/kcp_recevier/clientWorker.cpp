#include "clientWorker.h"
#include <QDateTime>
#define RECV_IP "192.168.0.111"
#define SEND_IP "192.168.0.110"
#define LOCAL_IP "192.168.0.111"
//#define RECV_PORT 12345
#define SEND_PORT 12345
#define KCP_SESSION 0x1
#define KCP_SEND_WND_SIZE 128
#define KCP_RECV_WND_SIZE 128

void do_udp_send(QUdpSocket* socket, const char *buf, int len, cmd_type_e type)
{
    pre_kcp_pkt pkt;
    pkt.header.cmd = (char)type;
    pkt.header.opt = 0;
    pkt.header.dataLen = len;
    if (buf) {
        memcpy(pkt.data, buf, len);
    }
    const QHostAddress address(QString(SEND_IP));
    socket->writeDatagram((const char*)&pkt, len + sizeof(pre_kcp_header), address, SEND_PORT);
}

ClientWorker::ClientWorker(QObject *parent):m_worker(nullptr), m_timer(nullptr), m_kcp(nullptr)
{

}
ClientWorker::~ClientWorker()
{
    m_worker->close();
    m_timer->stop();
    m_worker->deleteLater();
    m_worker = nullptr;
}

void ClientWorker::onStopClient()
{
    if (!m_timer || !m_worker) {
        return;
    }

    do_udp_send(m_worker, nullptr, 0, CMD_TYPE_CTRL_CLOSE_REQ);

    m_timer->stop();
    if (m_kcp) {
        QMutexLocker locker(&m_locker);
        ikcp_release(m_kcp);
        m_kcp = nullptr;
        qDebug() << "[ClientWorker::onStopClient] ikcp_release";
    }
    disconnect(m_worker, &QUdpSocket::readyRead, this, &ClientWorker::recvData);
    disconnect(m_timer, &QTimer::timeout, this, &ClientWorker::updateKcpTick);
}

void ClientWorker::onStartClient()
{
    qDebug() << "[ClientWorker::onStartClient] enter";
    m_udpRecvNum = 0;

    if (m_timer) {
        delete m_timer;
    }
    m_timer = new QTimer();
    if (m_worker) {
        delete m_worker;
    }
    m_worker = new QUdpSocket();

    do_udp_send(m_worker, nullptr, 0, CMD_TYPE_CTRL_CONN_REQ);
    QMutexLocker locker(&m_locker);
    // creat kcp
    ikcpcb*  kcp = ikcp_create(KCP_SESSION, this);
    kcp->output = udp_send;
    qDebug() << "[ClientWorker::onStartClient] ikcp_create";

    // kcp参数
    ikcp_nodelay(kcp, 1, 20, 2, 1); // fast mode, disable congestion control
    ikcp_wndsize(kcp, KCP_SEND_WND_SIZE, KCP_RECV_WND_SIZE);

    connect(m_worker, &QUdpSocket::readyRead, this, &ClientWorker::recvData);
    connect(m_timer, &QTimer::timeout, this, &ClientWorker::updateKcpTick);

    m_timer->start(10);
    m_kcp = kcp;
}

#define KCP_HEADER_LEN 12 // KCP 头部长度，包含 conv, cmd, frg, wnd, sn 等
#define KCP_SN_OFFSET 4   // 序列号偏移位置（从数据包起始算起）
uint32_t extract_sn_from_packet(const char *data, int len) {
    if (len >= KCP_HEADER_LEN) {
        const uint32_t *sn_ptr = (const uint32_t *)(data + KCP_SN_OFFSET);
        qDebug() << "[extract_sn_from_packet] sn=" << *sn_ptr;
    }
    return 0; // 数据不足，返回默认值
}
void ClientWorker::recvData()
{
//    qDebug() << "[recvData] recv kcp";
    char tmpRecvBuf[0x10000] = {0};

    while (m_worker->hasPendingDatagrams()) {
        QByteArray recvBuffer;
        recvBuffer.resize(m_worker->pendingDatagramSize());
        QHostAddress senderAddr;
        quint16 senderPort;
        m_worker->readDatagram(recvBuffer.data(), recvBuffer.size(), &senderAddr, &senderPort);
        qDebug() << "[ClientWorker::recvData] recv udp data size=" << recvBuffer.size();
//        extract_sn_from_packet(recvBuffer.data(), KCP_HEADER_LEN+ 1);
        char cmd = recvBuffer[0];
        if (CMD_TYPE_KCP == cmd) {
            QMutexLocker locker(&m_locker);
            if (m_kcp) {
                ikcp_input(m_kcp, recvBuffer.data() + sizeof(pre_kcp_header), recvBuffer.size() - sizeof(pre_kcp_header));
            }

        } else {
            qDebug() << "[ClientWorker::recvData] recv pure udp data";
        }
    }

    int recvLen = 0;
    do {
        QMutexLocker locker(&m_locker);
        if (m_kcp) {
            recvLen = ikcp_recv(m_kcp, tmpRecvBuf, sizeof(tmpRecvBuf));
        }
        if (recvLen > 0) {
            m_udpRecvNum++;
            qDebug() << "[recv kcp] success recvLen=" << recvLen << ",m_udpRecvNum=" << m_udpRecvNum;
        } else {
            qDebug() << "[recv kcp] unfinish pkt, ret=" << recvLen;
        }

    } while(recvLen > 0);
}

void ClientWorker::updateKcpTick()
{
    QMutexLocker locker(&m_locker);
    if (m_kcp)
        ikcp_update(m_kcp, static_cast<IUINT32>(QDateTime::currentMSecsSinceEpoch()));
}

int ClientWorker::udp_send(const char *buf, int len, struct IKCPCB *kcp, void *user)
{
    Q_UNUSED(kcp);
    ClientWorker *client = static_cast<ClientWorker *>(user);
    if (client && client->m_worker) {
        do_udp_send(client->m_worker, buf, len, CMD_TYPE_KCP);
        qDebug() << "[ClientWorker::udp_send] send ack";
        return 0;
    }

    return -1;
}
