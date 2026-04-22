#include "ClientSession.h"
#include <QDataStream>
#include <QDateTime>
#include <QDebug>
#include <QThread>
#include <QTimer>
#include <QDir>
#include <QFile>
#include "ThreadPool.h"

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
        QString sql = "SELECT id, password, shop_name, expire_time, status, last_heartbeat_time FROM sys_account WHERE username = ?";
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
            strongSelf->m_accountId = row["id"].toInt();                                                                            // 账号id
            QString dbPwd = row["password"].toString();                                                                             // 密码
            QString shopName = row["shop_name"].toString();                                                                         // 门店名
            QDateTime expireTime = row["expire_time"].toDateTime();                                                                 // 过期时间
            int status = row["status"].toInt();                                                                                     // 账号状态

            // 提取最后一次心跳时间，并计算在线状态
            QDateTime lastHeartbeat = row["last_heartbeat_time"].toDateTime();
            QDateTime currentDateTime = QDateTime::currentDateTime();

            // 计算时间差 (秒)。如果数据库里为空(isValid为假)，则默认给个极大值让它直接放行
            qint64 diffSecs = lastHeartbeat.isValid() ? lastHeartbeat.secsTo(currentDateTime) : 999999;

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

            // E. 防多开校验
            if (diffSecs >= 0 && diffSecs <= HEARTBEAT_TIMEOUT) {
                strongSelf->SendProtoMsg(ServerApi::ID_LOGIN_RSP, emptyRsp, seq_id, ServerApi::ERR_ACCOUNT_IN_USE, u8"账号已在其他终端活跃，请稍后再试");
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
            // 登录成功即刻赋活心跳时间！(注意数据库要支持 NOW() 函数)
            QString updateSql = "UPDATE sys_account SET last_login_time = NOW() WHERE id = ?";
            QList<QVariant> updateParams;
            updateParams << strongSelf->m_accountId;

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

    // ------------------------------------------------------------------
    // 处理客户端发来的 [分片上传请求]
    // ------------------------------------------------------------------
    m_router[ServerApi::ID_UPLOAD_CHUNK_REQ] = [this](const ServerApi::PacketHeader& header, const QByteArray& bodyData)
        {
            // 1. 解析请求 (使用智能指针包装，方便跨线程捕获)
            auto req = std::make_shared<ServerApi::UploadChunkReq>();
            if (!req->ParseFromArray(bodyData.data(), bodyData.size()))
            {
                ServerApi::UploadChunkRsp emptyRsp;
                SendProtoMsg(ServerApi::ID_UPLOAD_CHUNK_RSP, emptyRsp, -1,
                    ServerApi::ERR_FILE_IO_FAILED, u8"分片上传请求解析失败");
                return;
            }

            uint64_t seq_id = header.seq_id();
            QString fileMd5 = QString::fromStdString(req->file_md5());
            std::weak_ptr<ClientSession> weakSelf = weak_from_this();

            // 💡 定义一个通用的分片落盘 Lambda 函数，用于复用
            auto saveChunkToDisk = [weakSelf, fileMd5](std::shared_ptr<ServerApi::UploadChunkReq> req, uint64_t seq_id)
                {
                    auto strongSelf = weakSelf.lock();
                    if (!strongSelf) return;

                    QString dirPath = "./UploadedAssets";
                    QDir().mkpath(dirPath);
                    QString filePath = dirPath + "/" + fileMd5 + ".mp4";

                    QFile file(filePath);
                    if (!file.open(QIODevice::ReadWrite)) 
                    {
                        ServerApi::UploadChunkRsp emptyRsp;
                        strongSelf->SendProtoMsg(ServerApi::ID_UPLOAD_CHUNK_RSP, emptyRsp, seq_id,
                            ServerApi::ERR_FILE_IO_FAILED, u8"服务器磁盘写入失败");
                        return;
                    }

                    file.seek(req->chunk_offset());
                    file.write(req->chunk_data().data(), req->chunk_data().size());
                    file.close();

                    // 回复确认
                    ServerApi::UploadChunkRsp rsp;
                    rsp.set_file_md5(req->file_md5());
                    rsp.set_chunk_index(req->chunk_index());
                    rsp.set_is_complete(req->is_last());
                    strongSelf->SendProtoMsg(ServerApi::ID_UPLOAD_CHUNK_RSP, rsp, seq_id);

                    if (req->is_last()) {
                        qDebug() << u8"[ClientSession] 视频文件接收完毕，MD5:" << fileMd5;
                    }
                };

            // ==========================================================
            // 👑 核心逻辑：如果是第一块分片，先查数据库判定“秒传”
            // ==========================================================
            if (req->chunk_index() == 0) {
                QString sql = "SELECT id FROM t_movie_resource WHERE file_md5 = ?";
                QList<QVariant> params;
                params << fileMd5;

                ThreadPool::Instance()->PostQueryTask(sql, [weakSelf, req, seq_id, saveChunkToDisk](const QList<QVariantMap>& results) {
                    auto strongSelf = weakSelf.lock();
                    if (!strongSelf) return;

                    // A. 命中指纹库：数据库里已经有这个 MD5 了
                    if (!results.isEmpty()) {
                        qDebug() << u8"[ClientSession] 触发秒传机制，拦截上传，MD5:" << QString::fromStdString(req->file_md5());

                        ServerApi::UploadChunkRsp rsp;
                        rsp.set_file_md5(req->file_md5());
                        rsp.set_is_complete(true); // 强行标记为已完成

                        // 按照你的要求：返回错误码 ERR_MOVIE_EXISTS，并在 err_msg 填入提示
                        // 客户端收到此错误后会停止 QTimer 抽水泵并进入下一管线
                        strongSelf->SendProtoMsg(ServerApi::ID_UPLOAD_CHUNK_RSP, rsp, seq_id,
                            ServerApi::ERR_MOVIE_EXISTS, u8"秒传成功：服务器已存在该资源。");
                        return;
                    }

                    // B. 未命中：正常执行第一块的物理写入
                    saveChunkToDisk(req, seq_id);

                    }, true, params);
            }
            else {
                // 非首块分片，直接落盘
                saveChunkToDisk(req, seq_id);
            }
        };

    // ------------------------------------------------------------------
    // 处理客户端发来的 [影片元数据录入请求]
    // ------------------------------------------------------------------
    m_router[ServerApi::ID_UPLOAD_MOVIE_REQ] = [this](const ServerApi::PacketHeader& header, const QByteArray& bodyData)
        {
            uint64_t seq_id = header.seq_id();
            ServerApi::UploadMovieReq req;
            if (!req.ParseFromArray(bodyData.data(), bodyData.size())) return;

            // 解析请求
            QString movieName = QString::fromStdString(req.movie_name());                                   // 影片名字
            QString desc = QString::fromStdString(req.description());                                       // 影片描述
            QString videoMd5 = QString::fromStdString(req.video_md5());                                     // 影片md5码
            QString suffix = QString::fromStdString(req.cover_suffix());                                    // 封面图片后缀
            QString encryptKey = QString::fromStdString(req.encrypt_key());                                 // 影片加密key
            uint32_t durationSec = req.duration_sec();                                                      // 影片总时长

            QString dirPath = "./UploadedAssets";
            QDir().mkpath(dirPath);

            // 1. 海报图片直接落盘
            QString coverPath = dirPath + "/" + videoMd5 + "_cover" + suffix;
            QFile coverFile(coverPath);
            if (coverFile.open(QIODevice::WriteOnly)) {
                coverFile.write(req.cover_data().data(), req.cover_data().size());
                coverFile.close();
            }

            QString videoPath = dirPath + "/" + videoMd5 + ".mp4";
            qint64 fileSize = QFile(videoPath).size();

            qDebug() << u8"[ClientSession] 准备录入影片配置:" << movieName << u8"文件大小:" << fileSize;

            std::weak_ptr<ClientSession> weakSelf = weak_from_this();

            // =========================================================================
            // 🌊 异步瀑布流 Step 1：将物理信息插入全局资源池 (兼容秒传)
            // =========================================================================
            // 💡 使用 INSERT IGNORE：如果 MD5 已存在（触发 uk_file_md5 唯一约束），不会报错，而是平稳度过
            QString sqlStep1 = "INSERT IGNORE INTO t_movie_resource "
                "(file_md5, original_name, cover_url, video_url, description, file_size, duration_sec, encrypt_key, upload_by, create_time) "
                "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, NOW())";

            QList<QVariant> paramsStep1;
            paramsStep1 << videoMd5
                << movieName
                << coverPath
                << videoPath
                << desc
                << fileSize
                << durationSec
                << (encryptKey.isEmpty() ? QVariant(QVariant::String) : encryptKey) // 如果没加密，写入 NULL 而不是空字符串
                << m_accountId;

            ThreadPool::Instance()->PostUpdateTask(sqlStep1, [weakSelf, seq_id, movieName, videoMd5](bool success1) {
                auto strongSelf1 = weakSelf.lock();
                if (!strongSelf1) return;

                if (!success1) {
                    strongSelf1->SendProtoMsg(ServerApi::ID_UPLOAD_MOVIE_RSP, ServerApi::UploadMovieRsp(), seq_id, ServerApi::ERR_SERVER_INTERNAL, u8"全局资源池录入异常");
                    return;
                }

                // =========================================================================
                // 🌊 异步瀑布流 Step 2：查询刚才操作的影片在全局库中的 MovieID
                // =========================================================================
                QString sqlStep2 = "SELECT id FROM t_movie_resource WHERE file_md5 = ?";
                QList<QVariant> paramsStep2;
                paramsStep2 << videoMd5;

                ThreadPool::Instance()->PostQueryTask(sqlStep2, [weakSelf, seq_id, movieName](const QList<QVariantMap>& results) {
                    auto strongSelf2 = weakSelf.lock();
                    if (!strongSelf2) return;

                    if (results.isEmpty()) {
                        strongSelf2->SendProtoMsg(ServerApi::ID_UPLOAD_MOVIE_RSP, ServerApi::UploadMovieRsp(), seq_id, ServerApi::ERR_SERVER_INTERNAL, u8"无法获取全局影片ID");
                        return;
                    }

                    // 拿到真正的全局影片主键 ID！
                    uint64_t movieId = results.first()["id"].toULongLong();

                    // =========================================================================
                    // 🌊 异步瀑布流 Step 3：将影片授权给当前用户 (存入 t_user_movie_rel)
                    // =========================================================================
                    // 💡 神级优化：使用 ON DUPLICATE KEY UPDATE。
                    // 假设同一个用户上传了两次同一个视频，但第二次改了名，它会自动更新别名，而不报重复错误！
                    QString sqlStep3 = "INSERT INTO t_user_movie_rel "
                        "(user_id, movie_id, custom_name, play_status, sort_order, auth_status, create_time) "
                        "VALUES (?, ?, ?, 0, 0, 1, NOW()) "
                        "ON DUPLICATE KEY UPDATE custom_name = VALUES(custom_name)";

                    QList<QVariant> paramsStep3;
                    paramsStep3 << strongSelf2->m_accountId << movieId << movieName;

                    ThreadPool::Instance()->PostUpdateTask(sqlStep3, [weakSelf, seq_id, movieName](bool success3) {
                        auto strongSelf3 = weakSelf.lock();
                        if (!strongSelf3) return;

                        ServerApi::UploadMovieRsp rsp;
                        if (success3) {
                            qDebug() << u8"[ClientSession] 影片入库及私人库授权全部成功:" << movieName;
                            strongSelf3->SendProtoMsg(ServerApi::ID_UPLOAD_MOVIE_RSP, rsp, seq_id);
                        }
                        else {
                            qDebug() << u8"[ClientSession] 影片私人库授权失败:" << movieName;
                            strongSelf3->SendProtoMsg(ServerApi::ID_UPLOAD_MOVIE_RSP, rsp, seq_id, ServerApi::ERR_SERVER_INTERNAL, u8"影片私人库授权绑定失败");
                        }

                        }, true, paramsStep3); // 结束 Step 3

                    }, true, paramsStep2); // 结束 Step 2

                }, true, paramsStep1); // 结束 Step 1
        };

    // ------------------------------------------------------------------
    // 处理客户端发来的 [获取影片列表请求]
    // ------------------------------------------------------------------
    m_router[ServerApi::ID_GET_MOVIE_LIST_REQ] = [this](const ServerApi::PacketHeader& header, const QByteArray& bodyData)
            {
                uint64_t seq_id = header.seq_id();
                std::weak_ptr<ClientSession> weakSelf = weak_from_this();

                qDebug() << u8"[ClientSession] 收到客户端 " << m_username << u8" 的 [获取影片列表请求]";

                // 👑 连表查询：把 MD5 和 file_size 也查出来！
                QString sql = R"(
                    SELECT 
                        r.id AS movie_id, 
                        IFNULL(rel.custom_name, r.original_name) AS display_name,
                        r.cover_url, 
                        r.file_md5,     -- 查出物理文件的 MD5
                        r.file_size,    -- 查出文件大小
                        r.encrypt_key,  -- 加密密钥
                        rel.play_status
                    FROM t_user_movie_rel rel
                    INNER JOIN t_movie_resource r ON rel.movie_id = r.id
                    WHERE rel.user_id = ? AND rel.auth_status = 1
                    ORDER BY rel.sort_order ASC, rel.create_time DESC
                )";

                QList<QVariant> params;
                params << m_accountId;

                ThreadPool::Instance()->PostQueryTask(sql, [weakSelf, seq_id](const QList<QVariantMap>& results) {
                    auto strongSelf = weakSelf.lock();
                    if (!strongSelf) return;

                    ServerApi::GetMovieListRsp rsp;

                    for (const auto& row : results) {
                        ServerApi::MovieInfo* movie = rsp.add_movies();
                        movie->set_movie_id(row["movie_id"].toULongLong());
                        movie->set_movie_name(row["display_name"].toString().toStdString());
                        movie->set_cover_url(row["cover_url"].toString().toStdString());

                        // 💡 把最核心的下载凭证赋给 Protobuf 发给客户端
                        movie->set_file_md5(row["file_md5"].toString().toStdString());
                        movie->set_file_size(row["file_size"].toULongLong());

                        // 加密密钥
                        movie->set_encrypt_key(row["encrypt_key"].toString().toStdString());

                        movie->set_play_status(row["play_status"].toInt());
                    }

                    strongSelf->SendProtoMsg(ServerApi::ID_GET_MOVIE_LIST_RSP, rsp, seq_id);
                    qDebug() << u8"[ClientSession] 发送 [获取影片列表响应] 给客户端 " << strongSelf->m_username;

                    }, true, params);
            };

    // ------------------------------------------------------------------
    // 💡 处理客户端发来的 [海报下载请求]
    // ------------------------------------------------------------------
    m_router[ServerApi::ID_DOWNLOAD_COVER_REQ] = [this](const ServerApi::PacketHeader& header, const QByteArray& bodyData)
            {
                uint64_t seq_id = header.seq_id();
                ServerApi::DownloadCoverReq req;
                if (!req.ParseFromArray(bodyData.data(), bodyData.size())) return;

                QString fileMd5 = QString::fromStdString(req.file_md5());

                // 1. 去数据库查一下这张海报的真实存放路径
                QString sql = "SELECT cover_url FROM t_movie_resource WHERE file_md5 = ?";
                QList<QVariant> params;
                params << fileMd5;

                std::weak_ptr<ClientSession> weakSelf = weak_from_this();
                ThreadPool::Instance()->PostQueryTask(sql, [weakSelf, seq_id, fileMd5](const QList<QVariantMap>& results) {
                    auto strongSelf = weakSelf.lock();
                    if (!strongSelf) return;

                    if (results.isEmpty()) return; // 没查到就不理它

                    QString coverPath = results.first()["cover_url"].toString();

                    // 👑 提取真实的文件名 (比如: "70b77e_cover.png")
                    QString realFileName = QFileInfo(coverPath).fileName();

                    // 2. 直接打开硬盘里的图片，一口气读完！
                    QFile file(coverPath);
                    if (file.open(QIODevice::ReadOnly)) {
                        QByteArray coverData = file.readAll();
                        file.close();

                        ServerApi::DownloadCoverRsp rsp;
                        rsp.set_file_md5(fileMd5.toStdString());
                        rsp.set_cover_name(realFileName.toStdString()); // 传回真实文件名
                        rsp.set_cover_data(coverData.data(), coverData.size());

                        strongSelf->SendProtoMsg(ServerApi::ID_DOWNLOAD_COVER_RSP, rsp, seq_id);
                    }
                    }, true, params);
            };

    // ------------------------------------------------------------------
    // 💡 处理客户端发来的 [分片下载请求]
    // ------------------------------------------------------------------
    m_router[ServerApi::ID_DOWNLOAD_CHUNK_REQ] = [this](const ServerApi::PacketHeader& header, const QByteArray& bodyData)
            {
                uint64_t seq_id = header.seq_id();
                ServerApi::DownloadChunkReq req;
                if (!req.ParseFromArray(bodyData.data(), bodyData.size())) return;

                QString fileMd5 = QString::fromStdString(req.file_md5());
                uint32_t chunkIndex = req.chunk_index();

                // 1. 绝对的 O(1) 物理寻址，直接拼接路径！
                QString filePath = "./UploadedAssets/" + fileMd5 + ".mp4";
                QFile file(filePath);

                // 2. 尝试以只读模式打开本地文件
                if (!file.open(QIODevice::ReadOnly)) {
                    qDebug() << u8"[ClientSession] 下载失败，找不到物理文件:" << filePath;
                    ServerApi::DownloadChunkRsp emptyRsp;
                    SendProtoMsg(ServerApi::ID_DOWNLOAD_CHUNK_RSP, emptyRsp, seq_id, ServerApi::ERR_SERVER_INTERNAL, u8"云端文件丢失");
                    return;
                }

                // 3. 计算偏移量并跳转 (假设客户端和服务器约定好了每块 1MB)
                const int CHUNK_SIZE = 1048576;                                                     // 1MB = 1024 * 1024 字节
                uint64_t offset = (uint64_t)chunkIndex * CHUNK_SIZE;

                // 如果客户端乱请求，超出了文件大小，直接拦截
                if (offset >= file.size()) {
                    file.close();
                    return;
                }

                // 👑 核心跳转：直接把硬盘磁头拨到目标位置
                file.seek(offset);

                // 4. 抽出一块水 (最多读 CHUNK_SIZE，如果到文件末尾了，会自动读剩下的部分)
                QByteArray chunkData = file.read(CHUNK_SIZE);
                bool isLast = file.atEnd();                                                         // 判断是不是被榨干了

                file.close();                                                                       // 用完立刻释放句柄

                // 5. 将这块水装进 Protobuf，通过 TCP 扔回给客户端
                ServerApi::DownloadChunkRsp rsp;
                rsp.set_file_md5(req.file_md5());
                rsp.set_chunk_index(chunkIndex);
                rsp.set_chunk_data(chunkData.data(), chunkData.size());
                rsp.set_is_last(isLast);

                SendProtoMsg(ServerApi::ID_DOWNLOAD_CHUNK_RSP, rsp, seq_id);
            };

    // ------------------------------------------------------------------
    // 💡 处理客户端发来的 [获取播放记录请求]
    // ------------------------------------------------------------------
    m_router[ServerApi::ID_GET_RECORDS_REQ] = [this](const ServerApi::PacketHeader& header, const QByteArray& bodyData)
            {
                uint64_t seq_id = header.seq_id();
                ServerApi::GetRecordsReq req;
                if (!req.ParseFromArray(bodyData.data(), bodyData.size())) return;

                QString targetDate = QString::fromStdString(req.target_date());
                std::weak_ptr<ClientSession> weakSelf = weak_from_this();

                // 1. 构建查询 SQL (基于当前会话绑定的账户 m_accountId)
                QString sql = "SELECT id, movie_name, play_date, start_time, end_time, operator_name, end_type "
                    "FROM t_movie_record WHERE user_id = ?";
                QList<QVariant> params;
                params << m_accountId;

                // 👑 动态条件拼接：如果客户端传了日期，就精准查询；如果不传，就不拼条件（查所有）
                if (!targetDate.isEmpty()) {
                    sql += " AND play_date = ?";
                    params << targetDate;
                }

                // 按日期和开始时间倒序排列，最新的在最上面
                sql += " ORDER BY play_date DESC, start_time DESC";

                // 2. 扔给线程池异步执行
                ThreadPool::Instance()->PostQueryTask(sql, [weakSelf, seq_id](const QList<QVariantMap>& results) {
                    auto strongSelf = weakSelf.lock();
                    if (!strongSelf) return;

                    ServerApi::GetRecordsRsp rsp;

                    // 3. 遍历组装 Protobuf 数据
                    for (const auto& row : results) {
                        ServerApi::PlayRecord* record = rsp.add_records();

                        record->set_record_id(row["id"].toULongLong());
                        record->set_movie_name(row["movie_name"].toString().toStdString());
                        record->set_play_date(row["play_date"].toString().toStdString());
                        record->set_start_time(row["start_time"].toString().toStdString());
                        record->set_end_time(row["end_time"].toString().toStdString());
                        record->set_operator_name(row["operator_name"].toString().toStdString());
                        record->set_end_type(row["end_type"].toString().toStdString());
                    }

                    rsp.set_total_count(results.size());

                    strongSelf->SendProtoMsg(ServerApi::ID_GET_RECORDS_RSP, rsp, seq_id);
                    }, true, params);
            };

    // ------------------------------------------------------------------
    // 💡 处理客户端发来的 [添加播放记录请求]
    // ------------------------------------------------------------------
    m_router[ServerApi::ID_ADD_RECORD_REQ] = [this](const ServerApi::PacketHeader& header, const QByteArray& bodyData)
            {
                uint64_t seq_id = header.seq_id();
                ServerApi::AddRecordReq req;
                if (!req.ParseFromArray(bodyData.data(), bodyData.size())) return;

                const ServerApi::PlayRecord& record = req.record();
                std::weak_ptr<ClientSession> weakSelf = weak_from_this();

                // 1. 构造插入 SQL，关联当前会话账号
                QString sql = "INSERT INTO t_movie_record "
                    "(user_id, movie_name, play_date, start_time, end_time, operator_name, end_type, create_time) "
                    "VALUES (?, ?, ?, ?, ?, ?, ?, NOW())";

                QList<QVariant> params;
                params << m_accountId
                    << QString::fromStdString(record.movie_name())
                    << QString::fromStdString(record.play_date())
                    << QString::fromStdString(record.start_time())
                    << QString::fromStdString(record.end_time())
                    << QString::fromStdString(record.operator_name())
                    << QString::fromStdString(record.end_type());

                // 2. 扔给线程池异步写库
                ThreadPool::Instance()->PostUpdateTask(sql, [weakSelf, seq_id](bool success) {
                    auto strongSelf = weakSelf.lock();
                    if (!strongSelf) return;

                    ServerApi::AddRecordRsp rsp;
                    if (success) {
                        strongSelf->SendProtoMsg(ServerApi::ID_ADD_RECORD_RSP, rsp, seq_id);
                    }
                    else {
                        strongSelf->SendProtoMsg(ServerApi::ID_ADD_RECORD_RSP, rsp, seq_id, ServerApi::ERR_SERVER_INTERNAL, u8"录入历史记录失败");
                    }
                    }, true, params);
            };

    // ------------------------------------------------------------------
    // 💡 处理客户端发来的 [删除播放记录请求]
    // ------------------------------------------------------------------
    m_router[ServerApi::ID_DELETE_RECORD_REQ] = [this](const ServerApi::PacketHeader& header, const QByteArray& bodyData)
            {
                uint64_t seq_id = header.seq_id();
                ServerApi::DeleteRecordReq req;
                if (!req.ParseFromArray(bodyData.data(), bodyData.size())) return;

                uint64_t recordId = req.record_id();
                std::weak_ptr<ClientSession> weakSelf = weak_from_this();

                // 👑 核心安全防御：删除时必须带上 user_id = ? 防越权操作 (防止张三发包删掉李四的记录)
                QString sql = "DELETE FROM t_movie_record WHERE id = ? AND user_id = ?";
                QList<QVariant> params;
                params << recordId << m_accountId;

                // 执行删除操作
                ThreadPool::Instance()->PostUpdateTask(sql, [weakSelf, seq_id, recordId](bool success) {
                    auto strongSelf = weakSelf.lock();
                    if (!strongSelf) return;

                    ServerApi::DeleteRecordRsp rsp;
                    if (success) {
                        rsp.set_deleted_id(recordId); // 把删掉的 ID 回传给客户端，方便其在 UI 擦除该行
                        strongSelf->SendProtoMsg(ServerApi::ID_DELETE_RECORD_RSP, rsp, seq_id);
                    }
                    else {
                        strongSelf->SendProtoMsg(ServerApi::ID_DELETE_RECORD_RSP, rsp, seq_id, ServerApi::ERR_SERVER_INTERNAL, u8"删除失败，记录不存在或无权限");
                    }
                    }, true, params);
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