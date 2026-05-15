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
        QString sql = "SELECT id, password, shop_name, expire_time, status, last_heartbeat_time, user_permissions FROM sys_account WHERE username = ?";
        QList<QVariant> params;
        params << loginUser;

        // 2. 投递异步查询任务
        ThreadPool::Instance()->PostQueryTask(sql, [weakSelf, loginUser, loginPwd, seq_id](const QList<QVariantMap>& results) {

            // 🌟 异步回调第一步：尝试锁定，确保 Session 还活着
            auto strongSelf = weakSelf.lock();
            if (!strongSelf) return;

            ServerApi::LoginRsp emptyRsp;                                                                                           // 准备一个空包体用于发送失败回执

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
            int userPermissions = row["user_permissions"].toInt();                                                                  // 用户权限

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
            successRsp.set_permission(userPermissions);

            strongSelf->SendProtoMsg(ServerApi::ID_LOGIN_RSP, successRsp, seq_id, ServerApi::ERR_SUCCESS, "");
            qDebug() << u8"[ClientSession] 账号登录成功:" << loginUser << u8"门店:" << shopName;

            // 4. 异步更新设备为“在线”状态，并刷新最后登录时间
            // 登录成功即刻赋活心跳时间！(注意数据库要支持 NOW() 函数)
            QString updateSql = "UPDATE sys_account SET last_heartbeat_time = NOW(), last_login_time = NOW() WHERE id = ?";
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
            auto req = std::make_shared<ServerApi::UploadChunkReq>();
            if (!req->ParseFromArray(bodyData.data(), bodyData.size())) return;

            uint64_t seq_id = header.seq_id();
            QString fileMd5 = QString::fromStdString(req->file_md5());
            auto file_type = req->file_type();
            std::weak_ptr<ClientSession> weakSelf = weak_from_this();

            // 👑 1. 根据文件类型，智能分配存储目录和文件后缀
            QString dirPath = "./UploadedAssets";
            QString fileExt = ".bin";
            if (file_type == ServerApi::FILE_MOVIE) {
                dirPath += "/Movie";
                fileExt = ".mp4";
            }
            else if (file_type == ServerApi::FILE_GAME) {
                dirPath += "/Game";
                fileExt = ".tar";
            }
            QDir().mkpath(dirPath);
            QString filePath = dirPath + "/" + fileMd5 + fileExt;

            // 💡 通用的分片落盘 Lambda (现在使用动态的 filePath)
            auto saveChunkToDisk = [weakSelf, fileMd5, file_type, filePath](std::shared_ptr<ServerApi::UploadChunkReq> req, uint64_t seq_id)
                {
                    auto strongSelf = weakSelf.lock();
                    if (!strongSelf) return;

                    QIODevice::OpenMode openMode;
                    if (req->chunk_index() == 0) {
                        openMode = QIODevice::WriteOnly | QIODevice::Truncate; // 首块强行清零重写
                    }
                    else {
                        openMode = QIODevice::Append;
                    }

                    QFile file(filePath);
                    if (!file.open(openMode))
                    {
                        ServerApi::UploadChunkRsp emptyRsp;
                        emptyRsp.set_file_type(file_type);
                        strongSelf->SendProtoMsg(ServerApi::ID_UPLOAD_CHUNK_RSP, emptyRsp, seq_id, ServerApi::ERR_FILE_IO_FAILED, u8"服务器磁盘写入失败");
                        return;
                    }

                    file.seek(req->chunk_offset());
                    file.write(req->chunk_data().data(), req->chunk_data().size());
                    file.close();

                    ServerApi::UploadChunkRsp rsp;
                    rsp.set_file_md5(req->file_md5());
                    rsp.set_chunk_index(req->chunk_index());
                    rsp.set_is_complete(req->is_last());
                    rsp.set_file_type(file_type);
                    strongSelf->SendProtoMsg(ServerApi::ID_UPLOAD_CHUNK_RSP, rsp, seq_id);

                    if (req->is_last()) {
                        qDebug() << u8"[ClientSession] 物理文件接收完毕，类型:" << file_type << u8"MD5:" << fileMd5;
                    }
                };

            // ==========================================================
            // 👑 2. 核心逻辑：首块分片，查表判定“秒传” (区分不同业务表)
            // ==========================================================
            if (req->chunk_index() == 0) {
                QString sql;
                if (file_type == ServerApi::FILE_MOVIE) {
                    sql = "SELECT id FROM t_movie_resource WHERE file_md5 = ?";
                }
                else if (file_type == ServerApi::FILE_GAME) {
                    sql = "SELECT id FROM game_info WHERE package_md5 = ?"; // 游戏表字段叫 package_md5
                }
                else {
                    return; // 未知类型直接丢弃
                }

                QList<QVariant> params;
                params << fileMd5;

                ThreadPool::Instance()->PostQueryTask(sql, [weakSelf, req, seq_id, saveChunkToDisk, file_type, filePath](const QList<QVariantMap>& results) {
                    auto strongSelf = weakSelf.lock();
                    if (!strongSelf) return;

                    // A. 命中指纹库：触发秒传
                    if (!results.isEmpty()) {
                        qDebug() << u8"[ClientSession] 触发秒传机制，拦截上传，类型:" << file_type << u8"MD5:" << QString::fromStdString(req->file_md5());

                        ServerApi::UploadChunkRsp rsp;
                        rsp.set_file_md5(req->file_md5());
                        rsp.set_is_complete(true);
                        rsp.set_file_type(file_type);

                        // 使用对应的错误码告知客户端秒传成功
                        auto errCode = (file_type == ServerApi::FILE_GAME) ? ServerApi::ERR_GAME_EXISTS : ServerApi::ERR_MOVIE_EXISTS;
                        strongSelf->SendProtoMsg(ServerApi::ID_UPLOAD_CHUNK_RSP, rsp, seq_id, errCode, u8"秒传成功：服务器已存在该资源。");
                        return;
                    }

                    // B. 未命中：清理历史残留并全新落盘
                    QFile residualFile(filePath);
                    if (residualFile.exists()) {
                        if (residualFile.remove()) {
                            qDebug() << u8"[ClientSession] 清理历史残留坏文件成功:" << filePath;
                        }
                    }

                    saveChunkToDisk(req, seq_id);

                    }, true, params);
            }
            else {
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

            QString dirPath = "./UploadedAssets/Movie";
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
            auto file_type = req.file_type();

            // 👑 1. 根据文件类型，智能切换要查询的数据表
            QString sql;
            if (file_type == ServerApi::FILE_MOVIE) {
                sql = "SELECT cover_url FROM t_movie_resource WHERE file_md5 = ?";
            }
            else if (file_type == ServerApi::FILE_GAME) {
                sql = "SELECT cover_url FROM game_info WHERE package_md5 = ?"; // 游戏表字段叫 package_md5
            }
            else {
                return; // 未知类型直接抛弃
            }

            QList<QVariant> params;
            params << fileMd5;

            std::weak_ptr<ClientSession> weakSelf = weak_from_this();
            ThreadPool::Instance()->PostQueryTask(sql, [weakSelf, seq_id, fileMd5, file_type](const QList<QVariantMap>& results) {
                auto strongSelf = weakSelf.lock();
                if (!strongSelf) return;

                if (results.isEmpty()) return; // 没查到就不理它

                QString coverPath = results.first()["cover_url"].toString();
                QString realFileName = QFileInfo(coverPath).fileName();

                // 2. 直接打开硬盘里的图片，一口气读完！
                QFile file(coverPath);
                if (file.open(QIODevice::ReadOnly)) {
                    QByteArray coverData = file.readAll();
                    file.close();

                    ServerApi::DownloadCoverRsp rsp;
                    rsp.set_file_md5(fileMd5.toStdString());
                    rsp.set_cover_name(realFileName.toStdString());
                    rsp.set_cover_data(coverData.data(), coverData.size());
                    rsp.set_file_type(file_type); // 👑 带回文件类型

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
            auto file_type = req.file_type();

            // 👑 1. 根据文件类型，智能拼接不同的物理文件夹和文件后缀
            QString filePath = "./UploadedAssets";
            if (file_type == ServerApi::FILE_MOVIE) {
                filePath += "/Movie/" + fileMd5 + ".mp4";
            }
            else if (file_type == ServerApi::FILE_GAME) {
                filePath += "/Game/" + fileMd5 + ".tar";
            }
            else {
                return; // 未知类型直接抛弃
            }

            QFile file(filePath);

            // 2. 尝试以只读模式打开本地文件
            if (!file.open(QIODevice::ReadOnly)) {
                qDebug() << u8"[ClientSession] 下载失败，找不到物理文件:" << filePath;
                ServerApi::DownloadChunkRsp emptyRsp;
                emptyRsp.set_file_type(file_type); // 记得把类型带上
                SendProtoMsg(ServerApi::ID_DOWNLOAD_CHUNK_RSP, emptyRsp, seq_id, ServerApi::ERR_SERVER_INTERNAL, u8"云端文件丢失");
                return;
            }

            // 3. 计算偏移量并跳转 (约定每块 1MB)
            const int CHUNK_SIZE = 1048576;
            uint64_t offset = (uint64_t)chunkIndex * CHUNK_SIZE;

            if (offset >= file.size()) {
                file.close();
                return;
            }

            // 核心跳转：直接把硬盘磁头拨到目标位置
            file.seek(offset);

            // 4. 抽出一块水
            QByteArray chunkData = file.read(CHUNK_SIZE);
            bool isLast = file.atEnd();

            file.close();

            // 5. 将这块水装进 Protobuf，通过 TCP 扔回给客户端
            ServerApi::DownloadChunkRsp rsp;
            rsp.set_file_md5(req.file_md5());
            rsp.set_chunk_index(chunkIndex);
            rsp.set_chunk_data(chunkData.data(), chunkData.size());
            rsp.set_is_last(isLast);
            rsp.set_file_type(file_type); // 👑 带回文件类型

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

    // ------------------------------------------------------------------
    // 处理 [游戏上传与版本更新请求]
    // ------------------------------------------------------------------
    m_router[ServerApi::ID_UPLOAD_GAME_REQ] = [this](const ServerApi::PacketHeader& header, const QByteArray& bodyData)
        {
            uint64_t seq_id = header.seq_id();
            ServerApi::UploadGameReq req;
            if (!req.ParseFromArray(bodyData.data(), bodyData.size())) return;

            QString gameName = QString::fromStdString(req.game_name());
            QString version = QString::fromStdString(req.version());
            QString desc = QString::fromStdString(req.description());
            QString packageMd5 = QString::fromStdString(req.package_md5());
            QString suffix = QString::fromStdString(req.cover_suffix());
            QString exePath = QString::fromStdString(req.exe_path());

            // 1. 存入游戏海报
            QString dirPath = "./UploadedAssets/Game";
            QDir().mkpath(dirPath);
            QString coverPath = dirPath + "/" + packageMd5 + "_cover" + suffix;
            QFile coverFile(coverPath);
            if (coverFile.open(QIODevice::WriteOnly)) {
                coverFile.write(req.cover_data().data(), req.cover_data().size());
                coverFile.close();
            }

            // 2. 获取刚才传完的 .tar 包的大小
            QString tarPath = dirPath + "/" + packageMd5 + ".tar";
            qint64 packageSize = QFile(tarPath).size();

            qDebug() << u8"[ClientSession] 准备录入游戏:" << gameName << u8"版本:" << version << u8"大小:" << packageSize;

            std::weak_ptr<ClientSession> weakSelf = weak_from_this();

            // =========================================================================
            // 👑 神级 SQL 逻辑：利用 UNIQUE KEY (game_name) 实现热更新
            // 如果表里没这个游戏，就插入新记录；
            // 如果游戏名存在，就覆盖它的版本号、包MD5、大小、海报路径等，并刷新 update_time
            // =========================================================================
            QString sql = "INSERT INTO game_info "
                "(game_name, version, description, cover_url, package_md5, package_size, exe_path, create_time, update_time) "
                "VALUES (?, ?, ?, ?, ?, ?, ?, NOW(), NOW()) "
                "ON DUPLICATE KEY UPDATE "
                "version = VALUES(version), "
                "description = VALUES(description), "
                "cover_url = VALUES(cover_url), "
                "package_md5 = VALUES(package_md5), "
                "package_size = VALUES(package_size), "
                "exe_path = VALUES(exe_path), "
                "update_time = NOW()";

            QList<QVariant> params;
            params << gameName << version << desc << coverPath << packageMd5 << packageSize << exePath;

            ThreadPool::Instance()->PostUpdateTask(sql, [weakSelf, seq_id, gameName](bool success) {
                auto strongSelf = weakSelf.lock();
                if (!strongSelf) return;

                if (success) {
                    qDebug() << u8"[ClientSession] 游戏数据入库/更新成功:" << gameName;

                    ServerApi::UploadGameRsp rsp;
                    // 注意：因为使用了 ON DUPLICATE KEY，主键ID获取比较复杂，通常客户端刷新列表即可，这里返回0即可
                    rsp.set_game_id(0);

                    strongSelf->SendProtoMsg(ServerApi::ID_UPLOAD_GAME_RSP, rsp, seq_id);
                }
                else {
                    qDebug() << u8"[ClientSession] 游戏录入失败:" << gameName;
                    strongSelf->SendProtoMsg(ServerApi::ID_UPLOAD_GAME_RSP, ServerApi::UploadGameRsp(), seq_id,
                        ServerApi::ERR_SERVER_INTERNAL, u8"游戏配置数据写入数据库失败");
                }

                }, true, params);
        };

    // ------------------------------------------------------------------
    // 处理 [获取游戏列表请求]
    // ------------------------------------------------------------------
    m_router[ServerApi::ID_GET_GAME_LIST_REQ] = [this](const ServerApi::PacketHeader& header, const QByteArray& bodyData)
        {
            uint64_t seq_id = header.seq_id();
            ServerApi::GetGameListReq req;
            if (!req.ParseFromArray(bodyData.data(), bodyData.size())) return;

            // 1. 解析分页参数 (防御性编程，防止出现 0)
            uint32_t pageIndex = req.page_index() > 0 ? req.page_index() : 1;
            uint32_t pageSize = req.page_size() > 0 ? req.page_size() : 20;
            uint32_t offset = (pageIndex - 1) * pageSize;

            std::weak_ptr<ClientSession> weakSelf = weak_from_this();

            // =========================================================================
            // 🌊 异步瀑布流 Step 1：查询游戏总数 (total_count)
            // =========================================================================
            QString countSql = "SELECT COUNT(*) AS total FROM game_info";

            ThreadPool::Instance()->PostQueryTask(countSql, [weakSelf, seq_id, offset, pageSize](const QList<QVariantMap>& countResults) {
                auto strongSelf = weakSelf.lock();
                if (!strongSelf) return;

                uint32_t totalCount = 0;
                if (!countResults.isEmpty()) {
                    totalCount = countResults.first()["total"].toUInt();
                }

                // =========================================================================
                // 🌊 异步瀑布流 Step 2：查具体的分页数据 (按更新时间倒序，最新的在前面)
                // =========================================================================
                QString dataSql = "SELECT * FROM game_info ORDER BY update_time DESC LIMIT ?, ?";
                QList<QVariant> params;
                params << offset << pageSize;

                ThreadPool::Instance()->PostQueryTask(dataSql, [weakSelf, seq_id, totalCount](const QList<QVariantMap>& results) {
                    auto strongSelf2 = weakSelf.lock();
                    if (!strongSelf2) return;

                    ServerApi::GetGameListRsp rsp;
                    rsp.set_total_count(totalCount);

                    // 遍历数据库结果，组装 Protobuf 数组
                    for (const auto& row : results) {
                        ServerApi::GameInfo* game = rsp.add_games();
                        game->set_game_id(row["id"].toULongLong());
                        game->set_game_name(row["game_name"].toString().toStdString());
                        game->set_version(row["version"].toString().toStdString());
                        game->set_description(row["description"].toString().toStdString());
                        game->set_cover_url(row["cover_url"].toString().toStdString());
                        game->set_package_md5(row["package_md5"].toString().toStdString());
                        game->set_package_size(row["package_size"].toULongLong());
                        game->set_exe_path(row["exe_path"].toString().toStdString());
                    }

                    qDebug() << u8"[ClientSession] 成功获取游戏列表，总数:" << totalCount
                        << u8"当前页下发:" << results.size() << u8"条记录";

                    // 组装完毕，发射给客户端！
                    strongSelf2->SendProtoMsg(ServerApi::ID_GET_GAME_LIST_RSP, rsp, seq_id);

                    }, true, params); // 结束 Step 2

                }, true); // 结束 Step 1
        };

    // ------------------------------------------------------------------
    // 💰 处理客户端发来的 [创建支付订单请求]
    // ------------------------------------------------------------------
    m_router[ServerApi::ID_CREATE_ORDER_REQ] = [this](const ServerApi::PacketHeader& header, const QByteArray& bodyData)
        {
            uint64_t seq_id = header.seq_id();
            ServerApi::CreateOrderReq req;
            if (!req.ParseFromArray(bodyData.data(), bodyData.size())) return;

            uint64_t goods_id = req.goods_id();
            QString pay_method = QString::fromStdString(req.pay_method());
            std::weak_ptr<ClientSession> weakSelf = weak_from_this();

            qDebug() << u8"[ClientSession] 收到拉起支付请求，商品ID:" << goods_id << u8"渠道:" << pay_method;

            // =========================================================================
            // 🌊 异步瀑布流 Step 1：校验商品信息与金额 (绝不信任客户端传的价格)
            // =========================================================================
            QString sqlGoods = "SELECT price_cents, points_reward FROM t_goods_sku WHERE goods_id = ? AND status = 1";
            QList<QVariant> paramsGoods;
            paramsGoods << goods_id;

            ThreadPool::Instance()->PostQueryTask(sqlGoods, [weakSelf, seq_id, goods_id, pay_method](const QList<QVariantMap>& results) {
                auto strongSelf = weakSelf.lock();
                if (!strongSelf) return;

                // 1. 商品不存在或已下架
                if (results.isEmpty()) {
                    strongSelf->SendProtoMsg(ServerApi::ID_CREATE_ORDER_RSP, ServerApi::CreateOrderRsp(), seq_id,
                        ServerApi::ERR_SERVER_INTERNAL, u8"商品不存在或已下架");
                    return;
                }

                int priceCents = results.first()["price_cents"].toInt();

                // 2. 生成本地系统订单号 (格式: PAY + 年月日时分秒 + 账号ID)
                QString orderId = QString("PAY%1_%2")
                    .arg(QDateTime::currentDateTime().toString("yyyyMMddHHmmss"))
                    .arg(strongSelf->m_accountId);

                // =========================================================================
                // 🌊 异步瀑布流 Step 2：创建本地待支付订单
                // =========================================================================
                QString sqlOrder = "INSERT INTO t_pay_order "
                    "(order_id, user_id, goods_id, amount_cents, status, create_time) "
                    "VALUES (?, ?, ?, ?, 0, NOW())";
                QList<QVariant> paramsOrder;
                paramsOrder << orderId << strongSelf->m_accountId << goods_id << priceCents;

                ThreadPool::Instance()->PostUpdateTask(sqlOrder, [weakSelf, seq_id, orderId](bool success) {
                    auto innerSelf = weakSelf.lock();
                    if (!innerSelf) return;

                    if (success) {
                        qDebug() << u8"[ClientSession] 本地支付订单创建成功，订单号:" << orderId;

                        // 3. 组装返回数据给客户端弹窗
                        ServerApi::CreateOrderRsp rsp;
                        rsp.set_order_id(orderId.toStdString());
                        rsp.set_expire_time(300); // 二维码有效期 5 分钟

                        // 💡 注意：这里先给一个 mock (测试) 的二维码 URL 数据！
                        // TODO: 等你接入真实的微信支付 V3 API 时，在插入订单前调用微信接口获取真实 CodeUrl 填在这里
                        QString mockQrUrl = "weixin://wxpay/bizpayurl?pr=TEST_MOCK_PAY_" + orderId;
                        rsp.set_qr_code_url(mockQrUrl.toStdString());

                        innerSelf->SendProtoMsg(ServerApi::ID_CREATE_ORDER_RSP, rsp, seq_id);
                    }
                    else {
                        innerSelf->SendProtoMsg(ServerApi::ID_CREATE_ORDER_RSP, ServerApi::CreateOrderRsp(), seq_id,
                            ServerApi::ERR_SERVER_INTERNAL, u8"系统繁忙，创建订单失败");
                    }
                    }, true, paramsOrder); // 结束 Step 2

                }, true, paramsGoods); // 结束 Step 1
        };

    // ------------------------------------------------------------------
    // 💰 [获取钱包余额] 处理逻辑
    // ------------------------------------------------------------------
    m_router[ServerApi::ID_GET_WALLET_REQ] = [this](const ServerApi::PacketHeader& header, const QByteArray& bodyData)
        {
            uint64_t seq_id = header.seq_id();
            std::weak_ptr<ClientSession> weakSelf = weak_from_this();

            // 直接去 t_user_wallet 查这个人的余额
            QString sql = "SELECT balance_points, total_recharged FROM t_user_wallet WHERE user_id = ?";
            QList<QVariant> params;
            params << m_accountId;

            ThreadPool::Instance()->PostQueryTask(sql, [weakSelf, seq_id](const QList<QVariantMap>& results) {
                auto strongSelf = weakSelf.lock();
                if (!strongSelf) return;

                ServerApi::GetWalletRsp rsp;
                if (!results.isEmpty()) {
                    rsp.set_current_points(results.first()["balance_points"].toLongLong());
                    rsp.set_total_recharged(results.first()["total_recharged"].toLongLong());
                }
                else {
                    // 如果库里没记录，说明是新用户，默认0
                    rsp.set_current_points(0);
                    rsp.set_total_recharged(0);
                }
                strongSelf->SendProtoMsg(ServerApi::ID_GET_WALLET_RSP, rsp, seq_id);
                }, true, params);
        };

    // ------------------------------------------------------------------
    // 📦 [获取商品套餐] 处理逻辑
    // ------------------------------------------------------------------
    m_router[ServerApi::ID_GET_GOODS_REQ] = [this](const ServerApi::PacketHeader& header, const QByteArray& bodyData)
        {
            uint64_t seq_id = header.seq_id();
            std::weak_ptr<ClientSession> weakSelf = weak_from_this();

            // 只查询上架状态(status=1)的商品
            QString sql = "SELECT goods_id, name, price_cents, points_reward FROM t_goods_sku WHERE status = 1 ORDER BY price_cents ASC";

            ThreadPool::Instance()->PostQueryTask(sql, [weakSelf, seq_id](const QList<QVariantMap>& results) {
                auto strongSelf = weakSelf.lock();
                if (!strongSelf) return;

                ServerApi::GetGoodsRsp rsp;
                for (const auto& row : results) {
                    auto* info = rsp.add_goods_list();
                    info->set_goods_id(row["goods_id"].toULongLong());
                    info->set_goods_name(row["name"].toString().toStdString());
                    info->set_price_cents(row["price_cents"].toUInt());
                    info->set_points_reward(row["points_reward"].toUInt());
                }
                strongSelf->SendProtoMsg(ServerApi::ID_GET_GOODS_RSP, rsp, seq_id);
                }, true);
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