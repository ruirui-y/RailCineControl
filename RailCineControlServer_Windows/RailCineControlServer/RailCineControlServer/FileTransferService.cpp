#include "FileTransferService.h"
#include "ClientSession.h"
#include "MsgDispatcher.h"
#include "ThreadPool.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>

FileTransferService::~FileTransferService() { qDebug() << u8"♻️ [FileTransferService] 析构"; }

void FileTransferService::Init()
{
    std::weak_ptr<FileTransferService> weakSelf = shared_from_this();

    // 注册上传路由
    MsgDispatcher::Instance()->RegisterRoute(ServerApi::ID_UPLOAD_CHUNK_REQ,
        [weakSelf](auto s, auto& h, auto& b) { if (auto self = weakSelf.lock()) self->OnUploadChunk(s, h, b); });

    // 注册下载路由
    MsgDispatcher::Instance()->RegisterRoute(ServerApi::ID_DOWNLOAD_CHUNK_REQ,
        [weakSelf](auto s, auto& h, auto& b) { if (auto self = weakSelf.lock()) self->OnDownloadChunk(s, h, b); });

    // 注册海报下载路由
    MsgDispatcher::Instance()->RegisterRoute(ServerApi::ID_DOWNLOAD_COVER_REQ,
        [weakSelf](auto s, auto& h, auto& b) { if (auto self = weakSelf.lock()) self->OnDownloadCover(s, h, b); });
}

void FileTransferService::OnUploadChunk(std::shared_ptr<ClientSession> session, const ServerApi::PacketHeader& header, const QByteArray& bodyData)
{
    auto req = std::make_shared<ServerApi::UploadChunkReq>();
    if (!req->ParseFromArray(bodyData.data(), bodyData.size())) return;

    uint64_t seq_id = header.seq_id();
    QString fileMd5 = QString::fromStdString(req->file_md5());
    auto file_type = req->file_type();

    QString dirPath = "./UploadedAssets";
    QString fileExt = (file_type == ServerApi::FILE_MOVIE) ? ".mp4" : ".tar";
    QString subDir = (file_type == ServerApi::FILE_MOVIE) ? "/Movie" : "/Game";

    QDir().mkpath(dirPath + subDir);
    QString filePath = dirPath + subDir + "/" + fileMd5 + fileExt;

    // 首块判定秒传
    if (req->chunk_index() == 0) {
        QString sql = (file_type == ServerApi::FILE_MOVIE) ?
            "SELECT id FROM t_movie_resource WHERE file_md5 = ?" :
            "SELECT id FROM game_info WHERE package_md5 = ?";

        QList<QVariant> params; params << fileMd5;

        std::weak_ptr<ClientSession> weakSession = session;
        std::weak_ptr<FileTransferService> weakSelf = shared_from_this();

        ThreadPool::Instance()->PostQueryTask(sql, [weakSession, weakSelf, req, seq_id, filePath, file_type](const QList<QVariantMap>& results) {
            auto strongSession = weakSession.lock();
            auto strongSelf = weakSelf.lock();
            if (!strongSession || !strongSelf) return;

            if (!results.isEmpty()) {
                ServerApi::UploadChunkRsp rsp;
                rsp.set_file_md5(req->file_md5());
                rsp.set_is_complete(true);
                rsp.set_file_type(file_type);
                auto err = (file_type == ServerApi::FILE_GAME) ? ServerApi::ERR_GAME_EXISTS : ServerApi::ERR_MOVIE_EXISTS;
                strongSession->SendProtoMsg(ServerApi::ID_UPLOAD_CHUNK_RSP, rsp, seq_id, err, u8"秒传成功");
                return;
            }
            // 清理并保存
            QFile(filePath).remove();
            strongSelf->SaveChunkToDisk(strongSession, req, seq_id, filePath);
            }, true, params);
    }
    else {
        SaveChunkToDisk(session, req, seq_id, filePath);
    }
}

void FileTransferService::SaveChunkToDisk(std::shared_ptr<ClientSession> session, std::shared_ptr<ServerApi::UploadChunkReq> req, uint64_t seq_id, const QString& filePath)
{
    QFile file(filePath);
    QIODevice::OpenMode mode = (req->chunk_index() == 0) ? (QIODevice::WriteOnly | QIODevice::Truncate) : QIODevice::Append;

    if (!file.open(mode)) {
        ServerApi::UploadChunkRsp rsp;
        rsp.set_file_type(req->file_type());
        session->SendProtoMsg(ServerApi::ID_UPLOAD_CHUNK_RSP, rsp, seq_id, ServerApi::ERR_FILE_IO_FAILED, u8"磁盘写入失败");
        return;
    }
    file.seek(req->chunk_offset());
    file.write(req->chunk_data().data(), req->chunk_data().size());
    file.close();

    ServerApi::UploadChunkRsp rsp;
    rsp.set_file_md5(req->file_md5());
    rsp.set_chunk_index(req->chunk_index());
    rsp.set_is_complete(req->is_last());
    rsp.set_file_type(req->file_type());
    session->SendProtoMsg(ServerApi::ID_UPLOAD_CHUNK_RSP, rsp, seq_id);
}

void FileTransferService::OnDownloadChunk(std::shared_ptr<ClientSession> session, const ServerApi::PacketHeader& header, const QByteArray& bodyData)
{
    ServerApi::DownloadChunkReq req;
    if (!req.ParseFromArray(bodyData.data(), bodyData.size())) return;

    QString filePath = "./UploadedAssets" + QString(req.file_type() == ServerApi::FILE_MOVIE ? "/Movie/" : "/Game/")
        + QString::fromStdString(req.file_md5()) + (req.file_type() == ServerApi::FILE_MOVIE ? ".mp4" : ".tar");

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        ServerApi::DownloadChunkRsp rsp;
        rsp.set_file_type(req.file_type());
        session->SendProtoMsg(ServerApi::ID_DOWNLOAD_CHUNK_RSP, rsp, header.seq_id(), ServerApi::ERR_SERVER_INTERNAL, u8"云端文件丢失");
        return;
    }

    const int CHUNK_SIZE = 1048576;
    file.seek((uint64_t)req.chunk_index() * CHUNK_SIZE);
    QByteArray chunkData = file.read(CHUNK_SIZE);

    ServerApi::DownloadChunkRsp rsp;
    rsp.set_file_md5(req.file_md5());
    rsp.set_chunk_index(req.chunk_index());
    rsp.set_chunk_data(chunkData.data(), chunkData.size());
    rsp.set_is_last(file.atEnd());
    rsp.set_file_type(req.file_type());

    session->SendProtoMsg(ServerApi::ID_DOWNLOAD_CHUNK_RSP, rsp, header.seq_id());
}

void FileTransferService::OnDownloadCover(std::shared_ptr<ClientSession> session, const ServerApi::PacketHeader& header, const QByteArray& bodyData)
{
    uint64_t seq_id = header.seq_id();
    ServerApi::DownloadCoverReq req;
    if (!req.ParseFromArray(bodyData.data(), bodyData.size())) return;

    QString fileMd5 = QString::fromStdString(req.file_md5());
    auto file_type = req.file_type();

    // 👑 智能表路由：根据类型决定去哪查海报地址
    QString sql = (file_type == ServerApi::FILE_MOVIE) ?
        "SELECT cover_url FROM t_movie_resource WHERE file_md5 = ?" :
        "SELECT cover_url FROM game_info WHERE package_md5 = ?";

    QList<QVariant> params;
    params << fileMd5;

    // 🌟 强引用捕获 session，确保回调时 session 依然可用
    std::weak_ptr<ClientSession> weakSession = session;

    ThreadPool::Instance()->PostQueryTask(sql, [weakSession, seq_id, fileMd5, file_type](const QList<QVariantMap>& results) {
        auto strongSession = weakSession.lock();
        if (!strongSession || results.isEmpty()) return;

        QString coverPath = results.first()["cover_url"].toString();

        // 👑 物理 I/O 操作：直接在这里读取，不经过 Session，Session 只负责回包
        QFile file(coverPath);
        if (file.open(QIODevice::ReadOnly)) {
            QByteArray coverData = file.readAll();
            file.close();

            ServerApi::DownloadCoverRsp rsp;
            rsp.set_file_md5(fileMd5.toStdString());
            rsp.set_cover_name(QFileInfo(coverPath).fileName().toStdString());
            rsp.set_cover_data(coverData.data(), coverData.size());
            rsp.set_file_type(file_type);

            strongSession->SendProtoMsg(ServerApi::ID_DOWNLOAD_COVER_RSP, rsp, seq_id);
        }
        else {
            qDebug() << u8"❌ [FileTransferService] 海报读取失败:" << coverPath;
        }
        }, true, params);
}