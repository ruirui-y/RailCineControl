#include "GameResourceService.h"
#include "ClientSession.h"
#include "MsgDispatcher.h"
#include "ThreadPool.h"
#include <QDir>
#include <QFile>

GameResourceService::~GameResourceService() { qDebug() << u8"♻️ [GameResourceService] 析构"; }

void GameResourceService::Init()
{
    std::weak_ptr<GameResourceService> weakSelf = shared_from_this();

    MsgDispatcher::Instance()->RegisterRoute(ServerApi::ID_UPLOAD_GAME_REQ,
        [weakSelf](auto s, auto& h, auto& b) { if (auto self = weakSelf.lock()) self->OnUploadGame(s, h, b); });

    MsgDispatcher::Instance()->RegisterRoute(ServerApi::ID_GET_GAME_LIST_REQ,
        [weakSelf](auto s, auto& h, auto& b) { if (auto self = weakSelf.lock()) self->OnGetGameList(s, h, b); });
}

void GameResourceService::OnUploadGame(std::shared_ptr<ClientSession> session, const ServerApi::PacketHeader& header, const QByteArray& bodyData)
{
    ServerApi::UploadGameReq req;
    if (!req.ParseFromArray(bodyData.data(), bodyData.size())) return;

    QString dirPath = "./UploadedAssets/Game";
    QDir().mkpath(dirPath);
    QString coverPath = dirPath + "/" + QString::fromStdString(req.package_md5()) + "_cover" + QString::fromStdString(req.cover_suffix());

    QFile coverFile(coverPath);
    if (coverFile.open(QIODevice::WriteOnly)) {
        coverFile.write(req.cover_data().data(), req.cover_data().size());
        coverFile.close();
    }

    QString sql = "INSERT INTO game_info (game_name, version, description, cover_url, package_md5, package_size, exe_path, create_time, update_time) VALUES (?, ?, ?, ?, ?, ?, ?, NOW(), NOW()) ON DUPLICATE KEY UPDATE version = VALUES(version), description = VALUES(description), cover_url = VALUES(cover_url), package_md5 = VALUES(package_md5), package_size = VALUES(package_size), exe_path = VALUES(exe_path), update_time = NOW()";

    QList<QVariant> params;
    params << QString::fromStdString(req.game_name()) << QString::fromStdString(req.version()) << QString::fromStdString(req.description())
        << coverPath << QString::fromStdString(req.package_md5()) << QFile(dirPath + "/" + QString::fromStdString(req.package_md5()) + ".tar").size()
        << QString::fromStdString(req.exe_path());

    std::weak_ptr<ClientSession> weakSession = session;
    ThreadPool::Instance()->PostUpdateTask(sql, [weakSession, seq_id = header.seq_id(), gameName = QString::fromStdString(req.game_name())](bool success) {
        if (auto s = weakSession.lock()) {
            ServerApi::UploadGameRsp rsp;
            rsp.set_game_id(0);
            s->SendProtoMsg(ServerApi::ID_UPLOAD_GAME_RSP, rsp, seq_id, success ? ServerApi::ERR_SUCCESS : ServerApi::ERR_SERVER_INTERNAL);
        }
        }, true, params);
}

void GameResourceService::OnGetGameList(std::shared_ptr<ClientSession> session, const ServerApi::PacketHeader& header, const QByteArray& bodyData)
{
    ServerApi::GetGameListReq req;
    if (!req.ParseFromArray(bodyData.data(), bodyData.size())) return;

    uint32_t offset = (req.page_index() > 0 ? req.page_index() - 1 : 0) * (req.page_size() > 0 ? req.page_size() : 20);
    uint32_t limit = (req.page_size() > 0 ? req.page_size() : 20);

    std::weak_ptr<ClientSession> weakSession = session;
    ThreadPool::Instance()->PostQueryTask("SELECT COUNT(*) AS total FROM game_info", [weakSession, seq_id = header.seq_id(), offset, limit](const QList<QVariantMap>& countRes) {
        auto sSession = weakSession.lock();
        if (!sSession) return;
        uint32_t total = countRes.isEmpty() ? 0 : countRes.first()["total"].toUInt();

        ThreadPool::Instance()->PostQueryTask("SELECT * FROM game_info ORDER BY update_time DESC LIMIT ?, ?", [sSession, seq_id, total](const QList<QVariantMap>& dataRes) {
            ServerApi::GetGameListRsp rsp;
            rsp.set_total_count(total);
            for (const auto& row : dataRes) {
                auto* g = rsp.add_games();
                g->set_game_id(row["id"].toULongLong());
                g->set_game_name(row["game_name"].toString().toStdString());
                g->set_version(row["version"].toString().toStdString());
                g->set_cover_url(row["cover_url"].toString().toStdString());
                g->set_package_md5(row["package_md5"].toString().toStdString());
                g->set_package_size(row["package_size"].toULongLong());
                g->set_exe_path(row["exe_path"].toString().toStdString());
            }
            sSession->SendProtoMsg(ServerApi::ID_GET_GAME_LIST_RSP, rsp, seq_id);
            }, true, QList<QVariant>() << offset << limit);
        }, true);
}