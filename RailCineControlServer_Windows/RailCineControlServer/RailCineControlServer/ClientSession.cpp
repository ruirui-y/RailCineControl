#include "ClientSession.h"
#include <QDataStream>
#include <QDateTime>
#include <QDebug>
#include <QThread>
#include <QTimer>
#include <QDir>
#include <QFile>
#include <QJsonObject>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QEventLoop>
#include <QUrl>
#include "ThreadPool.h"
#include "MidPlatformManager.h"
#include "Global.h"
#include "MsgDispatcher.h"

#define CHECK_TIMEOUT                                                           10000
#define CONN_TIME_OUT                                                           30000
#define HEARTBEAT_TIMEOUT                                                       60

ClientSession::ClientSession(qintptr socketDescriptor, QObject* parent)
    : QObject(parent), m_socketDescriptor(socketDescriptor)
{
    // 此时因为是由 CreateQObject 在 Worker 线程中调用的 new
    // 所以这里已经是 Worker 线程环境，可以直接创建 Socket！
    m_tcpSocket = new QTcpSocket(this);

    if (!m_tcpSocket->setSocketDescriptor(m_socketDescriptor)) 
    {
        // 如果句柄失效，直接标记，onReadyRead 也就永远不会触发了
        // 注意：构造函数里不能 emit 信号让外部销毁，建议在外部判空或通过后续逻辑处理
        qDebug() << u8"[ClientSession] 句柄复活失败！";
        return;
    }

    // 绑定信号槽
    connect(m_tcpSocket, &QTcpSocket::readyRead, this, &ClientSession::onReadyRead);
    connect(this, &ClientSession::SigRequestDisconnect, this, &ClientSession::onDoDisconnect, Qt::QueuedConnection);
    connect(m_tcpSocket, &QTcpSocket::disconnected, this, &ClientSession::onDisconnected);

    // 记录当前时间
    m_lastRecvTime = QDateTime::currentMSecsSinceEpoch();

    // 创建并启动定时器，每 10 秒体检一次
    m_heartbeatTimer = new QTimer(this);
    connect(m_heartbeatTimer, &QTimer::timeout, this, &ClientSession::CheckTimeout);
    m_heartbeatTimer->start(CHECK_TIMEOUT);

    qDebug() << u8"ClientSession创建，当前执行线程ID:" << QThread::currentThreadId()
    << u8"当前线程名字:" << QThread::currentThread()->objectName();
}

ClientSession::~ClientSession()
{
    if (m_tcpSocket) {
        m_tcpSocket->abort();
        m_tcpSocket->deleteLater();
    }
    qDebug() << u8"[ClientSession] 会话已安全销毁，账号:" << m_username << u8"线程:" << QThread::currentThreadId();
}

void ClientSession::CheckTimeout()
{
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    // 假设设定 30 秒没收到包就认为掉线 (留出网络抖动的余地)
    if (now - m_lastRecvTime > CONN_TIME_OUT)
    {
        qDebug() << u8"[ClientSession] 发现设备假死/心跳超时，强行踢下线，账号:" << m_username;
        // 🌟 这一刀切下去，会自动触发底下的 onDisconnected 槽函数
        if (m_tcpSocket) 
        {
            m_tcpSocket->disconnectFromHost();
        }
    }
}

void ClientSession::onDoDisconnect()
{
    if (m_tcpSocket && m_tcpSocket->state() != QAbstractSocket::UnconnectedState)
    {
        m_tcpSocket->disconnectFromHost();
    }
}

void ClientSession::onDisconnected()
{
    qDebug() << u8"[ClientSession] 客户端断开连接，账号:" << m_username;
    
    // 停掉定时器
    if (m_heartbeatTimer) {
        m_heartbeatTimer->stop();
    }

    // 正常断开时的“瞬间释放”机制
    // 强制把心跳时间重置到远古时代
    if (m_isLogined && m_accountId > 0) {
        // 将时间改回 2000 年，确保他立刻重连时，时间差绝对大于 60 秒，实现 0 延迟登录！
        QString sql = "UPDATE sys_account SET last_heartbeat_time = '2000-01-01 00:00:00' WHERE id = ?";
        QList<QVariant> params;
        params << m_accountId;                                                  // 用主键 ID 更新比用 username 性能高得多

        // 异步丢给数据库线程池，不用管结果
        ThreadPool::Instance()->PostUpdateTask(sql, [](bool) {}, true, params);

        m_isLogined = false;                                                    // 状态清空
    }

    emit SigSessionClosed(m_socketDescriptor);                                  // 触发主服务器的回收机制
}

// =========================================================================================
// 1. 核心拆包引擎 (精准解决粘包/半包)
// =========================================================================================
void ClientSession::onReadyRead()
{
    m_buffer.append(m_tcpSocket->readAll());

    while (m_buffer.size() >= 4) {
        QDataStream stream(&m_buffer, QIODevice::ReadOnly);
        stream.setByteOrder(QDataStream::BigEndian);                            // 统一大端网络字节序

        quint32 totalLen;
        stream >> totalLen;                                                     // 读出此包的总长度

        if (m_buffer.size() < totalLen) {
            break;                                                              // 半包，等待后续数据
        }

        quint16 headerLen;
        stream >> headerLen;                                                    // 读出 Header 的长度

        QByteArray headerData = m_buffer.mid(6, headerLen);
        ServerApi::PacketHeader header;

        if (header.ParseFromArray(headerData.data(), headerData.size())) {
            int bodyLen = totalLen - 4 - 2 - headerLen;
            QByteArray bodyData = m_buffer.mid(6 + headerLen, bodyLen);

            ServerApi::MsgId msgId = header.msg_id();

            // 更新心跳
            m_lastRecvTime = QDateTime::currentMSecsSinceEpoch();

            // 未登录拦截：除了登录请求，其他所有请求都必须先登录才能处理
            if (!m_isLogined && msgId != ServerApi::ID_LOGIN_REQ) {
                qDebug() << u8"[ClientSession] 非法请求，未登录尝试发送协议:" << msgId;
                m_tcpSocket->disconnectFromHost();                              // 直接踢掉
                return;
            }

            // =====================================================================
            // 👑 绝杀：隐式心跳与数据库防暴击机制 (Throttling)
            // =====================================================================
            if (m_isLogined) {
                qint64 currentSecs = m_lastRecvTime / 1000;
                // 限流拦截：距离上次写库超过 15 秒，才允许再次 UPDATE 数据库
                if (currentSecs - m_lastDbSyncTime >= 15) {
                    m_lastDbSyncTime = currentSecs;                             // 刷新限流时间戳

                    QString updateSql = "UPDATE sys_account SET last_heartbeat_time = NOW() WHERE id = ?";
                    QList<QVariant> params;
                    params << m_accountId; // 前提是你在登录成功时把 m_accountId 存入了 Session

                    // 异步投递，绝对不卡 TCP 解析线程
                    ThreadPool::Instance()->PostUpdateTask(updateSql, [](bool) {}, true, params);
                }
            }

            // 采用事件分发器 分发事件
            MsgDispatcher::Instance()->Dispatch(shared_from_this(), msgId, header, bodyData);
        }

        m_buffer.remove(0, totalLen);                                           // 剥离已处理的数据
    }
}

// =========================================================================================
// 2. 多线程安全的封包发送引擎
// =========================================================================================
void ClientSession::SendProtoMsg(ServerApi::MsgId msgId, const google::protobuf::Message& protoMsg, uint64_t seqId, ServerApi::ErrorCode errCode, const QString& errMsg)
{
    QByteArray bodyData;
    bodyData.resize(protoMsg.ByteSizeLong());
    protoMsg.SerializeToArray(bodyData.data(), bodyData.size());

    ServerApi::PacketHeader header;
    header.set_msg_id(msgId);
    header.set_error_code(errCode);                                             // 动态写入错误码
    header.set_seq_id(seqId);                                                   // 动态写入请求序列号
    if (!errMsg.isEmpty()) {
        header.set_error_msg(errMsg.toStdString());                             // 动态写入错误信息
    }

    QByteArray headerData;
    headerData.resize(header.ByteSizeLong());
    header.SerializeToArray(headerData.data(), headerData.size());

    quint32 totalLen = 4 + 2 + headerData.size() + bodyData.size();

    QByteArray finalPacket;
    QDataStream stream(&finalPacket, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);

    stream << totalLen;                                                         // 写入 4 字节总长
    stream << (quint16)headerData.size();                                       // 写入 2 字节头长
    finalPacket.append(headerData);                                             // 拼接 Header 数据
    finalPacket.append(bodyData);                                               // 拼接 Body 数据

    // 跨线程安全写入
    QMetaObject::invokeMethod(this, [this, finalPacket]() {
        if (m_tcpSocket && m_tcpSocket->state() == QAbstractSocket::ConnectedState) {
            m_tcpSocket->write(finalPacket);
            m_tcpSocket->flush();
        }
        }, Qt::QueuedConnection);
}