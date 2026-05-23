#include "MovieResourceService.h"
#include "ClientSession.h"
#include "MsgDispatcher.h"
#include "ThreadPool.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDateTime>

MovieResourceService::~MovieResourceService() { qDebug() << u8"♻️ [MovieResourceService] 析构"; }

void MovieResourceService::Init()
{
    std::weak_ptr<MovieResourceService> weakSelf = shared_from_this();

    MsgDispatcher::Instance()->RegisterRoute(ServerApi::ID_UPLOAD_MOVIE_REQ,
        [weakSelf](auto s, auto& h, auto& b) { if (auto self = weakSelf.lock()) self->OnUploadMovie(s, h, b); });

    MsgDispatcher::Instance()->RegisterRoute(ServerApi::ID_GET_MOVIE_LIST_REQ,
        [weakSelf](auto s, auto& h, auto& b) { if (auto self = weakSelf.lock()) self->OnGetMovieList(s, h, b); });
}

void MovieResourceService::OnUploadMovie(std::shared_ptr<ClientSession> session, const ServerApi::PacketHeader& header, const QByteArray& bodyData)
{
    uint64_t seq_id = header.seq_id();
    ServerApi::UploadMovieReq req;
    if (!req.ParseFromArray(bodyData.data(), bodyData.size())) return;

    QString movieName = QString::fromStdString(req.movie_name());
    QString videoMd5 = QString::fromStdString(req.video_md5());

    // 1. 物理落盘 (保持原有的海报保存逻辑)
    QString dirPath = "./UploadedAssets/Movie";
    QDir().mkpath(dirPath);
    QString coverPath = dirPath + "/" + videoMd5 + "_cover" + QString::fromStdString(req.cover_suffix());
    QFile coverFile(coverPath);
    if (coverFile.open(QIODevice::WriteOnly)) {
        coverFile.write(req.cover_data().data(), req.cover_data().size());
        coverFile.close();
    }

    // 2. 异步瀑布流录入
    std::weak_ptr<ClientSession> weakSession = session;
    std::weak_ptr<MovieResourceService> weakSelf = shared_from_this();

    QString sqlStep1 = "INSERT IGNORE INTO t_movie_resource (file_md5, original_name, cover_url, video_url, description, file_size, duration_sec, encrypt_key, upload_by, create_time) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, NOW())";
    QList<QVariant> params1;
    params1 << videoMd5 << movieName << coverPath << (dirPath + "/" + videoMd5 + ".mp4") << QString::fromStdString(req.description())
        << QFile(dirPath + "/" + videoMd5 + ".mp4").size() << req.duration_sec()
        << (req.encrypt_key().empty() ? QVariant(QVariant::String) : QString::fromStdString(req.encrypt_key())) << session->GetAccountId();

    ThreadPool::Instance()->PostUpdateTask(sqlStep1, [weakSession, weakSelf, seq_id, movieName, videoMd5](bool success) {
        auto strongSession = weakSession.lock();
        auto strongSelf = weakSelf.lock();
        if (!strongSession || !strongSelf) return;

        if (!success) {
            strongSession->SendProtoMsg(ServerApi::ID_UPLOAD_MOVIE_RSP, ServerApi::UploadMovieRsp(), seq_id, ServerApi::ERR_SERVER_INTERNAL, u8"录入异常");
            return;
        }

        // 查询 ID 并授权 (Step 2 & 3)
        QString sqlStep2 = "SELECT id FROM t_movie_resource WHERE file_md5 = ?";
        ThreadPool::Instance()->PostQueryTask(sqlStep2, [weakSession, weakSelf, seq_id, movieName](const QList<QVariantMap>& results) {
            auto sSession = weakSession.lock();
            if (!sSession || results.isEmpty()) return;

            uint64_t movieId = results.first()["id"].toULongLong();
            QString sqlStep3 = "INSERT INTO t_user_movie_rel (user_id, movie_id, custom_name, play_status, sort_order, auth_status, create_time) VALUES (?, ?, ?, 0, 0, 1, NOW()) ON DUPLICATE KEY UPDATE custom_name = VALUES(custom_name)";
            ThreadPool::Instance()->PostUpdateTask(sqlStep3, [sSession, seq_id, movieName](bool ok) {
                if (ok) sSession->SendProtoMsg(ServerApi::ID_UPLOAD_MOVIE_RSP, ServerApi::UploadMovieRsp(), seq_id);
                }, true, QList<QVariant>() << sSession->GetAccountId() << movieId << movieName);
            }, true, QList<QVariant>() << videoMd5);
        }, true, params1);
}

void MovieResourceService::OnGetMovieList(std::shared_ptr<ClientSession> session, const ServerApi::PacketHeader& header, const QByteArray& bodyData)
{
    QString sql = R"(SELECT r.id AS movie_id, IFNULL(rel.custom_name, r.original_name) AS display_name, r.cover_url, r.file_md5, r.file_size, r.encrypt_key, rel.play_status 
                     FROM t_user_movie_rel rel INNER JOIN t_movie_resource r ON rel.movie_id = r.id 
                     WHERE rel.user_id = ? AND rel.auth_status = 1 ORDER BY rel.sort_order ASC, rel.create_time DESC)";

    std::weak_ptr<ClientSession> weakSession = session;
    ThreadPool::Instance()->PostQueryTask(sql, [weakSession, seq_id = header.seq_id()](const QList<QVariantMap>& results) {
        auto strongSession = weakSession.lock();
        if (!strongSession) return;

        ServerApi::GetMovieListRsp rsp;
        for (const auto& row : results) {
            auto* m = rsp.add_movies();
            m->set_movie_id(row["movie_id"].toULongLong());
            m->set_movie_name(row["display_name"].toString().toStdString());
            m->set_cover_url(row["cover_url"].toString().toStdString());
            m->set_file_md5(row["file_md5"].toString().toStdString());
            m->set_file_size(row["file_size"].toULongLong());
            m->set_encrypt_key(row["encrypt_key"].toString().toStdString());
            m->set_play_status(row["play_status"].toInt());
        }
        strongSession->SendProtoMsg(ServerApi::ID_GET_MOVIE_LIST_RSP, rsp, seq_id);
        }, true, QList<QVariant>() << session->GetAccountId());
}