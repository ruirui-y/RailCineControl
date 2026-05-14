#include "TCPMgr.h"
#include <QDataStream>
#include <QDateTime>
#include <QDebug>
#include <QThread>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include "Macro.h"
#include "Global.h"
#include "UserMgr.h"


TCPMgr::TCPMgr(QObject* parent) : QObject(parent)
{
    m_Host = "127.0.0.1";
    m_Port = 5486;

    InitTcpSocket();
    InitHearbeatTimer();
    InitHandlers();                                                                     // 注册路由表
    qDebug() << "Tcp Thread id = " << QThread::currentThread()->objectName();
}

TCPMgr::~TCPMgr()
{
    m_bIsInitiateDisCon = true;
    if (m_TcpSocket) {
        m_TcpSocket->abort();
        m_TcpSocket->deleteLater();
    }
    if (m_hearbeatTimer) {
        m_hearbeatTimer->stop();
        m_hearbeatTimer->deleteLater();
    }
}

// =========================================================================================
// 1. 初始化层
// =========================================================================================
void TCPMgr::InitTcpSocket()
{
    m_TcpSocket = new QTcpSocket(this);

    // 连接成功
    connect(m_TcpSocket, &QTcpSocket::connected, this, [this]() {
        qDebug() << u8"[TCPMgr] 连接服务器成功:" << m_Host << m_Port;
        m_buffer.clear();                                                               // 清空历史缓存，防止脏数据
        emit SigConnectSuccess(true);
        });

    // 连接断开
    connect(m_TcpSocket, &QTcpSocket::disconnected, this, [this]() {
        qDebug() << u8"[TCPMgr] 连接已断开";
        StopHeartBeat();
        emit SigConnectClose();

        // 如果不是主动断开，可以考虑在这里做断线重连逻辑
        if (!m_bIsInitiateDisCon) {
            // QTimer::singleShot(3000, this, &TCPMgr::SlotTcpConnect);
        }
        });

    // 核心：数据到达
    connect(m_TcpSocket, &QTcpSocket::readyRead, this, &TCPMgr::onReadyRead);

    // 错误处理
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    connect(m_TcpSocket, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError err) {
        qDebug() << "[TCPMgr] Socket Error:" << err << m_TcpSocket->errorString();
        });
#else
    connect(m_TcpSocket, QOverload<QAbstractSocket::SocketError>::of(&QAbstractSocket::error), this, [this](QAbstractSocket::SocketError err) {
        qDebug() << "[TCPMgr] Socket Error:" << err << m_TcpSocket->errorString();
        });
#endif
}

void TCPMgr::InitHearbeatTimer()
{
    m_hearbeatTimer = new QTimer(this);
    connect(m_hearbeatTimer, &QTimer::timeout, this, &TCPMgr::onHeartbeatTick);
}

// =========================================================================================
// 2. 路由注册层 (业务分发中心)
// =========================================================================================
void TCPMgr::InitHandlers()
{
    // ------------------------------------------------------------------
    // 注册 [登录响应] 的处理逻辑
    // ------------------------------------------------------------------
    m_router[ServerApi::ID_LOGIN_RSP] = [this](const ServerApi::PacketHeader& header, const QByteArray& bodyData) {
        // 1. 检查 Header 里的全局错误码
        if (header.error_code() != ServerApi::ErrorCode::ERR_SUCCESS) 
        {
            // 登录失败，关闭连接
            m_TcpSocket->disconnectFromHost();

            // 转发登录失败信号
            qDebug() << u8"[TCPMgr] 登录失败:" << header.error_msg().c_str();
            emit SigLoginFailed(header.error_code(), QString::fromStdString(header.error_msg()));

            return;
        }

        // 2. 错误码为0，解析 Body 拿业务数据
        ServerApi::LoginRsp rsp;
        if (rsp.ParseFromArray(bodyData.data(), bodyData.size()))
        {
            // 更新用户权限信息
            UserMgr::Instance()->SetPermission(rsp.permission());

            qDebug() << u8"[TCPMgr] 登录成功! 服务器时间:" << rsp.server_time();

            StartHeartBeat();                                                           // 登录成功后启动心跳保活
            emit SigLoginSuccess();
        }
        };

    // ------------------------------------------------------------------
    // 注册 [心跳响应] 的处理逻辑
    // ------------------------------------------------------------------
    m_router[ServerApi::ID_HEARTBEAT] = [this](const ServerApi::PacketHeader& header, const QByteArray& bodyData) {
        // 一般心跳不需要复杂处理，只需知道服务器活着即可
        // qDebug() << "[TCPMgr] 收到服务器心跳回应";
        };

    // ------------------------------------------------------------------
    // 注册 [分片上传响应 (ACK)] 的处理逻辑
    // ------------------------------------------------------------------
    m_router[ServerApi::ID_UPLOAD_CHUNK_RSP] = [this](const ServerApi::PacketHeader& header, const QByteArray& bodyData) 
        {
            ServerApi::UploadChunkRsp rsp;
            ServerApi::FileType fileType = ServerApi::FILE_UNKNOWN;

            // 1. 尝试解析，提取出枚举类型
            if (rsp.ParseFromArray(bodyData.data(), bodyData.size())) {
                fileType = rsp.file_type();
            }

            // 2. 拦截全局错误
            if (header.error_code() != ServerApi::ErrorCode::ERR_SUCCESS) {
                qDebug() << u8"[TCPMgr] 分片上传致命错误:" << header.error_msg().c_str();
                emit SigChunkUploadFailed(fileType, QString::fromStdString(header.error_msg()));
                return;
            }

            // 3. 安全业务分发
            if (rsp.is_complete()) {
                qDebug() << u8"[TCPMgr] 收到服务端最终分片确认 (is_complete=true), 引擎类型:" << fileType;
                emit SigAllChunksAcked(fileType);
            }
            else {
                emit SigChunkUploadSuccess(fileType);
            }
        };

    // ------------------------------------------------------------------
    // 注册 [影片元数据录入响应] 的处理逻辑
    // ------------------------------------------------------------------
    m_router[ServerApi::ID_UPLOAD_MOVIE_RSP] = [this](const ServerApi::PacketHeader& header, const QByteArray& bodyData) {

        // 1. 检查有没有业务报错 (比如 MD5 重复)
        if (header.error_code() != ServerApi::ErrorCode::ERR_SUCCESS) {
            qDebug() << u8"[TCPMgr] 影片录入失败:" << header.error_msg().c_str();
            emit SigMovieUploadFailed(QString::fromStdString(header.error_msg()));
            return;
        }

        // 2. 解析成功数据
        ServerApi::UploadMovieRsp rsp;
        if (rsp.ParseFromArray(bodyData.data(), bodyData.size())) {
            qDebug() << u8"[TCPMgr] 影片完美录入云端! 数据库分配 ID:" << rsp.new_movie_id();
            emit SigMovieUploadSuccess();
        }
        };

    // ------------------------------------------------------------------
    // 注册 [获取影片列表响应] 的处理逻辑
    // ------------------------------------------------------------------
    m_router[ServerApi::ID_GET_MOVIE_LIST_RSP] = [this](const ServerApi::PacketHeader& header, const QByteArray& bodyData) {

        if (header.error_code() != ServerApi::ErrorCode::ERR_SUCCESS) {
            qDebug() << u8"[TCPMgr] 拉取影片列表失败:" << header.error_msg().c_str();
            // 可以抛出一个失败信号让 UI 提示用户
            return;
        }

        ServerApi::GetMovieListRsp rsp;
        if (rsp.ParseFromArray(bodyData.data(), bodyData.size())) {
            qDebug() << u8"[TCPMgr] 成功拉取影片列表，共" << rsp.movies_size() << u8"部影片";

            // 把携带了所有影片数据的 Proto 对象通过信号抛出去
            emit SigMovieListReceived(rsp);
        }
        };

    // ------------------------------------------------------------------
    // 处理[海报下载响应]
    // ------------------------------------------------------------------
    m_router[ServerApi::ID_DOWNLOAD_COVER_RSP] = [this](const ServerApi::PacketHeader& header, const QByteArray& bodyData)
        {
            if (header.error_code() != ServerApi::ErrorCode::ERR_SUCCESS) return;

            ServerApi::DownloadCoverRsp rsp;
            if (rsp.ParseFromArray(bodyData.data(), bodyData.size())) {

                QString fileMd5 = QString::fromStdString(rsp.file_md5());
                QString coverName = QString::fromStdString(rsp.cover_name());
                QByteArray coverData(rsp.cover_data().data(), rsp.cover_data().size());

                // 👑 1. 提取出极其关键的文件类型
                ServerApi::FileType fileType = rsp.file_type();

                // 👑 2. 动态路由：根据类型分配不同的海报缓存目录
                QString targetDirPath;
                if (fileType == ServerApi::FILE_MOVIE) {
                    targetDirPath = MovieCoverPath;
                }
                else if (fileType == ServerApi::FILE_GAME) {
                    targetDirPath = GameCoverPath;
                }
                else {
                    qDebug() << u8"[TCPMgr] 警告：收到未知类型的海报数据，直接丢弃";
                    return;
                }

                // 3. 拼接真实的海报本地路径
                QDir().mkpath(targetDirPath); // 确保对应类型的目录存在
                QString localCoverPath = targetDirPath + "/" + coverName;

                // 4. 写入本地磁盘
                QFile file(localCoverPath);
                if (file.open(QIODevice::WriteOnly))
                {
                    file.write(coverData);
                    file.close();
                    qDebug() << u8"[TCPMgr] 海报异步下载完成，类型:" << fileType << u8"保存至:" << localCoverPath;

                    // 💡 5. 抛出带类型的信号，各自页面的 Lambda 拦截网会精确捕获
                    emit SigCoverDownloaded(fileType, fileMd5, localCoverPath);
                }
            }
        };

    // ------------------------------------------------------------------
    // 处理[大文件分片下载响应] (通用)
    // ------------------------------------------------------------------
    m_router[ServerApi::ID_DOWNLOAD_CHUNK_RSP] = [this](const ServerApi::PacketHeader& header, const QByteArray& bodyData)
        {
            ServerApi::DownloadChunkRsp rsp;
            ServerApi::FileType fileType = ServerApi::FILE_UNKNOWN;

            // 1. 尝试解析，提取出枚举类型
            if (rsp.ParseFromArray(bodyData.data(), bodyData.size()))
            {
                fileType = rsp.file_type();
            }

            // 判断当前分片是否下载成功
            if (header.error_code() != ServerApi::ErrorCode::ERR_SUCCESS)
            {
                qDebug() << u8"[TCPMgr] 分片下载中断:" << header.error_msg().c_str();
                emit SigDownloadFailed(fileType, QString::fromStdString(header.error_msg()));
                return;
            }

            QString fileMd5 = QString::fromStdString(rsp.file_md5());
            uint32_t chunkIndex = rsp.chunk_index();
            QByteArray chunkData(rsp.chunk_data().data(), rsp.chunk_data().size());
            bool isLast = rsp.is_last();

            // ==========================================================
            // 👑 动态路由：根据类型分配不同的保存目录和后缀名
            // ==========================================================
            QString targetDirPath;
            QString fileSuffix;

            if (fileType == ServerApi::FILE_MOVIE) {
                targetDirPath = MovieVideoPath;
                fileSuffix = ".mp4";
            }
            else if (fileType == ServerApi::FILE_GAME) {
                targetDirPath = GameTarPath;        // 存入游戏压缩包临时目录
                fileSuffix = ".tar";
            }
            else {
                qDebug() << u8"[TCPMgr] 警告：收到未知类型的分片数据，直接丢弃";
                return;
            }

            QDir().mkpath(targetDirPath);           // 确保动态目录存在

            // 1. 拼接绝对物理路径
            QString targetFilePath = targetDirPath + "/" + fileMd5 + fileSuffix;
            QFile file(targetFilePath);

            //  精准的打开模式 (解决文件残留导致的数据损坏)
            QIODevice::OpenMode openMode;
            if (chunkIndex == 0) {
                // 如果是第一块，哪怕有残留文件，也直接清空它 (Truncate) 并重新写入
                openMode = QIODevice::WriteOnly | QIODevice::Truncate;
            }
            else {
                // 后续的分片，使用追加模式
                openMode = QIODevice::Append;
            }

            // 执行打开与写入
            if (file.open(openMode)) {
                file.write(chunkData);
                file.close();
            }
            else
            {
                // 打印真正的底层错误原因！
                QString errorCause = file.errorString();
                qDebug() << u8"[TCPMgr] 文件下载中断，本地磁盘写入失败。原因:" << errorCause << u8"路径:" << targetFilePath;

                emit SigDownloadFailed(fileType, u8"写入失败: " + errorCause);
                return;
            }

            // 2. 抛出进度信号给 UI 层更新进度条
            emit SigDownloadProgress(fileType, fileMd5, chunkData.size()); // UI 层累加大小计算百分比

            // 3. 核心：如果没下完，自动找服务器要下一块！(抽水泵循环)
            if (!isLast) {
                ServerApi::DownloadChunkReq nextReq;
                nextReq.set_file_md5(fileMd5.toStdString());
                nextReq.set_chunk_index(chunkIndex + 1); // 索要下一块
                nextReq.set_file_type(fileType);         // 👑 循环请求时，一定要把类型再带上！
                SendProtoMsg(ServerApi::MsgId::ID_DOWNLOAD_CHUNK_REQ, nextReq);
            }
            else {
                qDebug() << u8"[TCPMgr] 🎉 物理文件下载彻底完成! 类型:" << fileType << u8"MD5:" << fileMd5;
                // 抛出完成信号，UI 层的进度条满 100%，【下载】按钮变成【播放/启动】按钮
                emit SigDownloadFinished(fileType, fileMd5);
            }
        };

    // ------------------------------------------------------------------
    // 💡 处理服务器返回的 [获取播放记录响应]
    // ------------------------------------------------------------------
    m_router[ServerApi::ID_GET_RECORDS_RSP] = [this](const ServerApi::PacketHeader& header, const QByteArray& bodyData)
        {
            if (header.error_code() != ServerApi::ErrorCode::ERR_SUCCESS) {
                qDebug() << u8"[TCPMgr] 获取播放记录失败:" << header.error_msg().c_str();
                // 视需求可抛出失败信号给 UI 弹窗
                return;
            }

            ServerApi::GetRecordsRsp rsp;
            if (rsp.ParseFromArray(bodyData.data(), bodyData.size())) {
                qDebug() << u8"[TCPMgr] 成功拉取云端播放记录，共" << rsp.records_size() << u8"条";

                // 抛出信号给 RecordPage 进行表格渲染
                emit SigRecordsReceived(rsp);
            }
        };

    // ------------------------------------------------------------------
    // 💡 处理服务器返回的 [添加播放记录响应]
    // ------------------------------------------------------------------
    m_router[ServerApi::ID_ADD_RECORD_RSP] = [this](const ServerApi::PacketHeader& header, const QByteArray& bodyData)
        {
            if (header.error_code() != ServerApi::ErrorCode::ERR_SUCCESS) {
                qDebug() << u8"[TCPMgr] 添加播放记录失败:" << header.error_msg().c_str();
                return;
            }

            ServerApi::AddRecordRsp rsp;
            if (rsp.ParseFromArray(bodyData.data(), bodyData.size())) {
                qDebug() << u8"[TCPMgr] 播放记录云端录入成功!";

                // 抛出信号，通知 RecordPage 刷新当天列表
                emit SigRecordAdded(rsp);
            }
        };

    // ------------------------------------------------------------------
    // 💡 处理服务器返回的 [删除播放记录响应]
    // ------------------------------------------------------------------
    m_router[ServerApi::ID_DELETE_RECORD_RSP] = [this](const ServerApi::PacketHeader& header, const QByteArray& bodyData)
        {
            if (header.error_code() != ServerApi::ErrorCode::ERR_SUCCESS) {
                qDebug() << u8"[TCPMgr] 删除播放记录失败:" << header.error_msg().c_str();
                return;
            }

            ServerApi::DeleteRecordRsp rsp;
            if (rsp.ParseFromArray(bodyData.data(), bodyData.size())) {
                qDebug() << u8"[TCPMgr] 播放记录云端删除成功! ID:" << rsp.deleted_id();

                // 抛出信号，通知 RecordPage 在 UI 上精确抹除这一行
                emit SigRecordDeleted(rsp);
            }
        };

    // ------------------------------------------------------------------
    // 注册 [游戏元数据录入响应] 的处理逻辑
    // ------------------------------------------------------------------
    m_router[ServerApi::ID_UPLOAD_GAME_RSP] = [this](const ServerApi::PacketHeader& header, const QByteArray& bodyData) {

        // 1. 检查全局错误码 (处理如：数据库写入失败、游戏名冲突等报错)
        if (header.error_code() != ServerApi::ErrorCode::ERR_SUCCESS) {
            qDebug() << u8"[TCPMgr] 游戏录入失败:" << header.error_msg().c_str();

            // 抛出游戏专属的失败信号，通知 GameUploadPage 解锁 UI 并弹窗
            emit SigGameUploadFailed(QString::fromStdString(header.error_msg()));
            return;
        }

        // 2. 解析 Body 数据
        ServerApi::UploadGameRsp rsp;
        if (rsp.ParseFromArray(bodyData.data(), bodyData.size())) {
            // 这里的 game_id 是服务器入库后返回的，用于日志追踪
            qDebug() << u8"[TCPMgr] 游戏资源及配置录入成功! 云端处理 ID:" << rsp.game_id();

            // 3. 👑 发射游戏专属成功信号
            // 该信号会被 GameWidget 捕捉，从而触发 UploadPage 的 ResetUI 和 LauncherPage 的刷新
            emit SigGameUploadSuccess();
        }
        };

    // ------------------------------------------------------------------
    // 注册 [获取游戏列表响应] 的处理逻辑
    // ------------------------------------------------------------------
    m_router[ServerApi::ID_GET_GAME_LIST_RSP] = [this](const ServerApi::PacketHeader& header, const QByteArray& bodyData)
        {
            // 1. 检查服务器返回的全局错误码
            if (header.error_code() != ServerApi::ErrorCode::ERR_SUCCESS) {
                qDebug() << u8"[TCPMgr] 拉取游戏列表失败:" << header.error_msg().c_str();
                return;
            }

            // 2. 解析 Body 里的游戏列表数据
            ServerApi::GetGameListRsp rsp;
            if (rsp.ParseFromArray(bodyData.data(), bodyData.size()))
            {
                qDebug() << u8"[TCPMgr] 成功拉取游戏列表，共" << rsp.games_size()
                    << u8"款游戏，云端总数:" << rsp.total_count();

                // 3. 👑 核心：抛出信号，通知 GameLauncherPage 进行卡片渲染
                // 注意：已经在 RegisterMetaTypes 中注册了该类型，跨线程传递是安全的
                emit SigGameListReceived(rsp);
            }
        };
}

// =========================================================================================
// 3. 核心：拆包与分发引擎 (彻底解决粘包/半包)
// =========================================================================================
void TCPMgr::onReadyRead()
{
    // 将底层缓冲区的所有数据追加到我们的缓存中
    m_buffer.append(m_TcpSocket->readAll());

    // 只要缓存区大于等于 4 字节，说明至少包含了一个 TotalLen 的头
    while (m_buffer.size() >= 4) {
        QDataStream stream(&m_buffer, QIODevice::ReadOnly);
        stream.setByteOrder(QDataStream::BigEndian);                                    // 统一大端网络字节序

        quint32 totalLen;
        stream >> totalLen;                                                             // 读出此包的总长度

        // 如果缓冲区的数据不够一个完整的包，说明是"半包"，跳出循环等待后续数据到达
        if (m_buffer.size() < totalLen) {
            break;
        }

        // --- 此时我们已经拥有了一个完美、完整的物理包！ ---

        quint16 headerLen;
        stream >> headerLen;                                                            // 读出 Header 的长度 (2字节)

        // 截取 RpcHeader 的字节流，并反序列化
        QByteArray headerData = m_buffer.mid(6, headerLen);
        ServerApi::PacketHeader header;

        if (header.ParseFromArray(headerData.data(), headerData.size())) {
            // 根据公式算出 Body 的长度，并截取 Body
            int bodyLen = totalLen - 4 - 2 - headerLen;
            QByteArray bodyData = m_buffer.mid(6 + headerLen, bodyLen);

            // O(1) 复杂度的极速路由分发
            ServerApi::MsgId msgId = header.msg_id();
            if (m_router.contains(msgId)) {
                m_router[msgId](header, bodyData);                                      // 触发你写的 Lambda 回调
            }
            else {
                qDebug() << u8"[TCPMgr] 未知的 MsgId:" << msgId << u8"丢弃该数据包";
            }
        }
        else {
            qDebug() << u8"[TCPMgr] 包头 Protobuf 解析失败，数据可能被篡改！";
        }

        // 从缓存中剔除已经处理完的这个包，进入下一个循环处理“粘包”
        m_buffer.remove(0, totalLen);
    }
}

// =========================================================================================
// 4. 核心：多线程安全的封包发送引擎
// =========================================================================================
void TCPMgr::SendProtoMsg(ServerApi::MsgId msgId, const google::protobuf::Message& protoMsg, uint64_t seqId)
{
    // 1. 将业务 Message 序列化为 Body 字节流
    QByteArray bodyData;
    bodyData.resize(protoMsg.ByteSizeLong());
    protoMsg.SerializeToArray(bodyData.data(), bodyData.size());

    // 2. 组装并序列化 PacketHeader
    ServerApi::PacketHeader header;
    header.set_msg_id(msgId);
    header.set_seq_id(seqId);
    // error_code 客户端发给服务端一般默认0，由服务端填错误码

    QByteArray headerData;
    headerData.resize(header.ByteSizeLong());
    header.SerializeToArray(headerData.data(), headerData.size());

    // 3. 计算总长度：TotalLen(4) + HeaderLen(2) + HeaderSize + BodySize
    quint32 totalLen = 4 + 2 + headerData.size() + bodyData.size();

    // 4. 拼装最终的二进制 TCP 数据流
    QByteArray finalPacket;
    QDataStream stream(&finalPacket, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);

    stream << totalLen;                                                                 // 写入 4 字节总长
    stream << (quint16)headerData.size();                                               // 写入 2 字节头长
    finalPacket.append(headerData);                                                     // 拼接 Header 数据
    finalPacket.append(bodyData);                                                       // 拼接 Body 数据

    // 5. 跨线程安全的发送机制
    // 不管你在哪个子线程调用 TCPMgr::Instance()->SendProtoMsg，它都会安全地切回主线程去调用 socket 写入
    QMetaObject::invokeMethod(this, [this, finalPacket, msgId]() 
        {
            if (m_TcpSocket && m_TcpSocket->state() == QAbstractSocket::ConnectedState)
            {
                if(ServerApi::MsgId ::ID_GET_MOVIE_LIST_REQ == msgId)
                    qDebug() << "ID_GET_MOVIE_LIST_REQ";
                m_TcpSocket->write(finalPacket);
                m_TcpSocket->flush();
            }
            else 
            {
                qDebug() << u8"[TCPMgr] 发送失败，TCP 未连接！MsgId:" << finalPacket.mid(6, 2).toHex();
            }
        }, Qt::QueuedConnection);
}

// =========================================================================================
// 5. 业务操作控制层
// =========================================================================================
void TCPMgr::SlotTcpConnect()
{
    if (m_TcpSocket->state() != QAbstractSocket::ConnectedState) {
        m_bIsInitiateDisCon = false;
        qDebug() << u8"[TCPMgr] 正在连接服务器...";
        m_TcpSocket->connectToHost(m_Host, m_Port);
    }
}

void TCPMgr::Login(QString username, QString password)
{
    ServerApi::LoginReq login_req;
    login_req.set_username(username.toStdString());
    login_req.set_password(password.toStdString());

    SendProtoMsg(ServerApi::MsgId::ID_LOGIN_REQ, login_req);
}

void TCPMgr::AccountLoginOut()
{
    m_bIsInitiateDisCon = true;
    StopHeartBeat();
    if (m_TcpSocket) {
        m_TcpSocket->disconnectFromHost();
    }
}

void TCPMgr::StartHeartBeat()
{
    if (m_hearbeatTimer && !m_hearbeatTimer->isActive()) {
        m_hearbeatTimer->start(5000);                                                   // 每 5 秒发一次心跳
    }
}

void TCPMgr::StopHeartBeat()
{
    if (m_hearbeatTimer && m_hearbeatTimer->isActive()) {
        m_hearbeatTimer->stop();
    }
}

void TCPMgr::onHeartbeatTick()
{
    // 发送心跳包
    ServerApi::Heartbeat hb;
    hb.set_timestamp(QDateTime::currentMSecsSinceEpoch());

    SendProtoMsg(ServerApi::ID_HEARTBEAT, hb);
}