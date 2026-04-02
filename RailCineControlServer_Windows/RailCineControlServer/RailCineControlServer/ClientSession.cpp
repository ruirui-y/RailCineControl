#include "ClientSession.h"
#include <QDataStream>
#include <QDateTime>
#include <QDebug>
#include <QThread>
#include <QTimer>
#include "ThreadPool.h"

#define CHECK_TIMEOUT                                                           10000
#define CONN_TIME_OUT                                                           30000

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
    connect(m_tcpSocket, &QTcpSocket::disconnected, this, &ClientSession::onDisconnected);

    InitHandlers();                                                             // 注册路由表

    // 记录当前时间
    m_lastRecvTime = QDateTime::currentMSecsSinceEpoch();

    // 创建并启动定时器，每 10 秒体检一次
    m_heartbeatTimer = new QTimer(this);
    connect(m_heartbeatTimer, &QTimer::timeout, this, &ClientSession::CheckTimeout);
    m_heartbeatTimer->start(CHECK_TIMEOUT);

    qDebug() << u8"[ClientSession] 构造完成，当前线程:" << QThread::currentThreadId();
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

void ClientSession::onDisconnected()
{
    qDebug() << u8"[ClientSession] 客户端断开连接，账号:" << m_username;
    
    // 停掉定时器
    if (m_heartbeatTimer) {
        m_heartbeatTimer->stop();
    }

    // 把数据库里的 is_online 改回 0！
    if (m_isLogined && !m_username.isEmpty()) {
        QString sql = "UPDATE sys_account SET is_online = 0 WHERE username = ?";
        QList<QVariant> params;
        params << m_username;
        // 异步丢给数据库线程池，不用管结果
        ThreadPool::Instance()->PostUpdateTask(sql, [](bool) {}, true, params);
    }

    emit SigSessionClosed(this);                                                // 触发主服务器的回收机制
}

// =========================================================================================
// 1. 路由业务层 (解耦真正的业务逻辑)
// =========================================================================================
void ClientSession::InitHandlers()
{
    // ------------------------------------------------------------------
    // 处理客户端发来的 [登录请求]
    // ------------------------------------------------------------------
    m_router[ServerApi::ID_LOGIN_REQ] = [this](const ServerApi::PacketHeader& header, const QByteArray& bodyData) {
        uint64_t seq_id = header.seq_id();
        ServerApi::LoginReq req;
        if (!req.ParseFromArray(bodyData.data(), bodyData.size())) {
            return;
        }

        QString loginUser = QString::fromStdString(req.username());
        QString loginPwd = QString::fromStdString(req.password());

        qDebug() << u8"[ClientSession] 收到登录请求，账号:" << loginUser;

        // 🌟 核心防御：捕获弱引用，防止 SQL 回调时客户端已断开
        std::weak_ptr<ClientSession> weakSelf = weak_from_this();

        // 1. 构造查询语句 (查出我们需要风控的所有字段)
        QString sql = "SELECT id, password, shop_name, expire_time, status, is_online FROM sys_account WHERE username = ?";
        QList<QVariant> params;
        params << loginUser;

        // 2. 投递异步查询任务
        ThreadPool::Instance()->PostQueryTask(sql, [weakSelf, loginUser, loginPwd, seq_id](const QList<QVariantMap>& results) {

            // 🌟 异步回调第一步：尝试锁定，确保 Session 还活着
            auto strongSelf = weakSelf.lock();
            if (!strongSelf) return;

            ServerApi::LoginRsp emptyRsp; // 准备一个空包体用于发送失败回执

            // A. 账号不存在
            if (results.isEmpty()) {
                strongSelf->SendProtoMsg(ServerApi::ID_LOGIN_RSP, emptyRsp, seq_id, ServerApi::ERR_WRONG_PWD, u8"账号不存在");
                return;
            }

            // 提取数据库里的数据
            const QVariantMap& row = results.first();
            int accountId = row["id"].toInt();                                                                                      // 账号id
            QString dbPwd = row["password"].toString();                                                                             // 密码
            QString shopName = row["shop_name"].toString();                                                                         // 门店名
            QDateTime expireTime = row["expire_time"].toDateTime();                                                                 // 过期时间
            int status = row["status"].toInt();                                                                                     // 账号状态
            int isOnline = row["is_online"].toInt();                                                                                // 在线状态

            // B. 密码校验
            if (loginPwd != dbPwd) {
                strongSelf->SendProtoMsg(ServerApi::ID_LOGIN_RSP, emptyRsp, seq_id, ServerApi::ERR_WRONG_PWD, u8"密码错误");
                return;
            }

            // C. 封禁状态校验
            if (status == 0) {
                strongSelf->SendProtoMsg(ServerApi::ID_LOGIN_RSP, emptyRsp, seq_id, ServerApi::ERR_WRONG_PWD, u8"该账号已被停用，请联系厂家");
                return;
            }

            // D. 授权到期校验
            if (QDateTime::currentDateTime() > expireTime) {
                strongSelf->SendProtoMsg(ServerApi::ID_LOGIN_RSP, emptyRsp, seq_id, ServerApi::ERR_ACCOUNT_EXPIRED, u8"账号授权已过期，请联系续费");
                return;
            }

            // E. 防多开校验 (禁止重复登录)
            if (isOnline == 1) {
                strongSelf->SendProtoMsg(ServerApi::ID_LOGIN_RSP, emptyRsp, seq_id, ServerApi::ERR_ACCOUNT_IN_USE, u8"账号已在其他终端登录");
                return;
            }

            // ---------------------------------------------------------
            // 3. 验证全部通过，开始走成功逻辑！
            // ---------------------------------------------------------
            strongSelf->m_isLogined = true;
            strongSelf->m_username = loginUser;

            ServerApi::LoginRsp successRsp;
            successRsp.set_server_time(QDateTime::currentMSecsSinceEpoch());
            successRsp.set_shop_name(shopName.toStdString());

            strongSelf->SendProtoMsg(ServerApi::ID_LOGIN_RSP, successRsp, seq_id, ServerApi::ERR_SUCCESS, "");
            qDebug() << u8"[ClientSession] 账号登录成功:" << loginUser << u8"门店:" << shopName;

            // 4. 异步更新设备为“在线”状态，并刷新最后登录时间
            QString updateSql = "UPDATE sys_account SET is_online = 1, last_login_time = NOW() WHERE id = ?";
            QList<QVariant> updateParams;
            updateParams << accountId;

            ThreadPool::Instance()->PostUpdateTask(updateSql, [](bool) {}, true, updateParams);

            }, true, params); // bIsAsync = true
        };

    // ------------------------------------------------------------------
    // 处理客户端发来的 [心跳请求]
    // ------------------------------------------------------------------
    m_router[ServerApi::ID_HEARTBEAT] = [this](const ServerApi::PacketHeader& header, const QByteArray& bodyData) 
        {
            // 1. 网络层最高优先级：原样将心跳包打回去，绝对不阻塞
            ServerApi::Heartbeat hbRsp;
            hbRsp.set_timestamp(QDateTime::currentMSecsSinceEpoch());
            SendProtoMsg(ServerApi::ID_HEARTBEAT, hbRsp);

            // 2. 状态校验：如果没有登录，直接忽略数据库操作
            if (!m_isLogined || m_username.isEmpty()) {
                return;
            }

            // 3. 数据库节流层：限制频繁写盘 (例如：每 60 秒才真正更新一次 MySQL)
            qint64 currentMsecs = QDateTime::currentMSecsSinceEpoch();
            if (currentMsecs - m_lastDbUpdateTime > 60000) {
                m_lastDbUpdateTime = currentMsecs;

                // 构造异步更新语句
                QString updateSql = "UPDATE sys_account SET last_login_time = NOW() WHERE username = ?";
                QList<QVariant> params;
                params << m_username;

                // 扔给线程池去后台慢慢写，不需要回调关心结果 (fire-and-forget)
                ThreadPool::Instance()->PostUpdateTask(updateSql, [](bool) {}, true, params);

                // qDebug() << u8"[ClientSession] 已将账号活跃状态同步至数据库:" << m_username;
            }
        };
}

// =========================================================================================
// 3. 核心拆包引擎 (精准解决粘包/半包)
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

            if (m_router.contains(msgId)) {
                m_router[msgId](header, bodyData);                              // 执行路由业务
            }
            else {
                qDebug() << u8"[ClientSession] 未知的 MsgId:" << msgId;
            }
        }

        m_buffer.remove(0, totalLen);                                           // 剥离已处理的数据
    }
}

// =========================================================================================
// 4. 多线程安全的封包发送引擎
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