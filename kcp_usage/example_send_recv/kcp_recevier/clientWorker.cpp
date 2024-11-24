#include "clientWorker.h"
#include <QDateTime>
#define RECV_IP "192.168.0.111"
#define SEND_IP "192.168.0.110"
#define RECV_PORT 12345
#define SEND_PORT 12346
#define KCP_SESSION 0x1
#define KCP_SEND_WND_SIZE 128
#define KCP_RECV_WND_SIZE 128

ClientWorker::ClientWorker(QObject *parent)
{
    //    const QHostAddress address(QString(RECV_IP));
    //    quint16 port = RECV_PORT;
    //    m_worker->bind(address, port);
    //    qDebug() << "[ClientWorker] bind";

    //    // creat kcp
    //    m_kcp = ikcp_create(KCP_SESSION, this);
    //    m_kcp->output = udp_send;

    //    // kcp参数
    //    ikcp_nodelay(m_kcp, 1, 20, 2, 1); // fast mode, disable congestion control
    //    ikcp_wndsize(m_kcp, KCP_SEND_WND_SIZE, KCP_RECV_WND_SIZE);

//    connect(m_worker, &QUdpSocket::readyRead, this, &ClientWorker::recvData);
//    connect(&m_timer, &QTimer::timeout, this, &ClientWorker::updateKcpTick);

//    m_timer.start(10);
}
ClientWorker::~ClientWorker()
{
    m_worker->close();
    m_timer->stop();
    m_worker->deleteLater();
    m_worker = nullptr;
}

void ClientWorker::onStartClient()
{
    qDebug() << "[ClientWorker::onStartClient] enter, Qthread=" << QThread::currentThread();
    m_udpRecvNum = 0;
    m_timer = new QTimer();
    m_worker = new QUdpSocket();
    const QHostAddress address(QString(RECV_IP));
    quint16 port = RECV_PORT;
    m_worker->bind(address, port);
    qDebug() << "[onStartClient] Socket bound to" << address.toString() << ":" << port;

    // creat kcp
    m_kcp = ikcp_create(KCP_SESSION, this);
    m_kcp->output = udp_send;

    // kcp参数
    ikcp_nodelay(m_kcp, 1, 20, 2, 1); // fast mode, disable congestion control
    ikcp_wndsize(m_kcp, KCP_SEND_WND_SIZE, KCP_RECV_WND_SIZE);

    connect(m_worker, &QUdpSocket::readyRead, this, &ClientWorker::recvData);
    connect(m_timer, &QTimer::timeout, this, &ClientWorker::updateKcpTick);

    m_timer->start(10);
}

//static int is_little_endian() {
//    uint16_t test = 0x1;
//    return *((uint8_t *)&test) == 0x1;
//}

//// 模拟实现 ntohl
//uint32_t custom_ntohl(uint32_t netlong) {
//    if (is_little_endian()) {
//        // 如果是小端序，转换为大端序
//        return ((netlong & 0xFF000000) >> 24) | // 高字节移到低字节
//               ((netlong & 0x00FF0000) >> 8)  | // 次高字节移到次低字节
//               ((netlong & 0x0000FF00) << 8)  | // 次低字节移到次高字节
//               ((netlong & 0x000000FF) << 24);  // 低字节移到高字节
//    } else {
//        // 如果是大端序，无需转换
//        return netlong;
//    }
//}
#include <winsock2.h>
//bool detectResetConnect(uint8_t* data, ikcpcb *kcp)
//{
//    uint32_t current_sn = ntohl(*(uint32_t *)(data + 4));
//    if (current_sn < kcp->rcv_nxt) {
//        printf("Sender restart detected! Previous rcv_nxt: %u, Current sn: %u\n", kcp->rcv_nxt, current_sn);
//        return true; // 返回 1 表示检测到重启
//    }
//    return false;
//}
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
    qDebug() << "[recvData] recv kcp";
    char tmpRecvBuf[0x10000] = {0};
    QMutexLocker locker(&m_locker);
//    ikcp_update(m_kcp, static_cast<IUINT32>(QDateTime::currentMSecsSinceEpoch()));

    while (m_worker->hasPendingDatagrams()) {
        QByteArray recvBuffer;
        recvBuffer.resize(m_worker->pendingDatagramSize());
        QHostAddress senderAddr;
        quint16 senderPort;
        m_worker->readDatagram(recvBuffer.data(), recvBuffer.size(), &senderAddr, &senderPort);
//        extract_sn_from_packet(recvBuffer.data(), KCP_HEADER_LEN+ 1);
        ikcp_input(m_kcp, recvBuffer.data(), recvBuffer.size());
//        bool ret = detectResetConnect((uint8_t*)(recvBuffer.data()), m_kcp);
//        if (ret) {
//            // TODO kcp reset
//            qDebug() << "[ClientWorker::recvData] kcp need reset";
//        }
    }

    int recvLen = 0;
    do {
        recvLen = ikcp_recv(m_kcp, tmpRecvBuf, sizeof(tmpRecvBuf));
        if (recvLen > 0) {
            m_udpRecvNum++;
            qDebug() << "[recv udp] success recvLen=" << recvLen << ",m_udpRecvNum=" << m_udpRecvNum;
        } else {
            qDebug() << "[recv udp] part recvLen=" << recvLen;
        }

    } while(recvLen > 0);
}

void ClientWorker::updateKcpTick()
{
    QMutexLocker locker(&m_locker);
    ikcp_update(m_kcp, static_cast<IUINT32>(QDateTime::currentMSecsSinceEpoch()));
}

int ClientWorker::udp_send(const char *buf, int len, struct IKCPCB *kcp, void *user)
{
    Q_UNUSED(kcp);
    ClientWorker *client = static_cast<ClientWorker *>(user);
    if (client && client->m_worker) {
        const QHostAddress address(QString(SEND_IP));
        client->m_worker->writeDatagram(buf, len, address, SEND_PORT);
        qDebug() << "[ClientWorker::udp_send] send ack";
        return 0;
    }

    return -1;
}
