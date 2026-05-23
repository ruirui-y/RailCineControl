#include "MoviePlayRecordService.h"
#include "ClientSession.h"
#include "MsgDispatcher.h"
#include "ThreadPool.h"

MoviePlayRecordService::~MoviePlayRecordService() { qDebug() << u8"♻️ [MoviePlayRecordService] 析构"; }

void MoviePlayRecordService::Init()
{
    std::weak_ptr<MoviePlayRecordService> weakSelf = shared_from_this();

    MsgDispatcher::Instance()->RegisterRoute(ServerApi::ID_GET_RECORDS_REQ,
        [weakSelf](auto s, auto& h, auto& b) { if (auto self = weakSelf.lock()) self->OnGetRecords(s, h, b); });

    MsgDispatcher::Instance()->RegisterRoute(ServerApi::ID_ADD_RECORD_REQ,
        [weakSelf](auto s, auto& h, auto& b) { if (auto self = weakSelf.lock()) self->OnAddRecord(s, h, b); });

    MsgDispatcher::Instance()->RegisterRoute(ServerApi::ID_DELETE_RECORD_REQ,
        [weakSelf](auto s, auto& h, auto& b) { if (auto self = weakSelf.lock()) self->OnDeleteRecord(s, h, b); });
}

void MoviePlayRecordService::OnGetRecords(std::shared_ptr<ClientSession> session, const ServerApi::PacketHeader& header, const QByteArray& bodyData)
{
    ServerApi::GetRecordsReq req;
    if (!req.ParseFromArray(bodyData.data(), bodyData.size())) return;

    QString targetDate = QString::fromStdString(req.target_date());
    QString sql = "SELECT id, movie_name, play_date, start_time, end_time, operator_name, end_type FROM t_movie_record WHERE user_id = ?";
    QList<QVariant> params;
    params << session->GetAccountId();

    if (!targetDate.isEmpty()) {
        sql += " AND play_date = ?";
        params << targetDate;
    }
    sql += " ORDER BY play_date DESC, start_time DESC";

    std::weak_ptr<ClientSession> weakSession = session;
    ThreadPool::Instance()->PostQueryTask(sql, [weakSession, seq_id = header.seq_id()](const QList<QVariantMap>& results) {
        auto strongSession = weakSession.lock();
        if (!strongSession) return;

        ServerApi::GetRecordsRsp rsp;
        for (const auto& row : results) {
            auto* record = rsp.add_records();
            record->set_record_id(row["id"].toULongLong());
            record->set_movie_name(row["movie_name"].toString().toStdString());
            record->set_play_date(row["play_date"].toString().toStdString());
            record->set_start_time(row["start_time"].toString().toStdString());
            record->set_end_time(row["end_time"].toString().toStdString());
            record->set_operator_name(row["operator_name"].toString().toStdString());
            record->set_end_type(row["end_type"].toString().toStdString());
        }
        rsp.set_total_count(results.size());
        strongSession->SendProtoMsg(ServerApi::ID_GET_RECORDS_RSP, rsp, seq_id);
        }, true, params);
}

void MoviePlayRecordService::OnAddRecord(std::shared_ptr<ClientSession> session, const ServerApi::PacketHeader& header, const QByteArray& bodyData)
{
    ServerApi::AddRecordReq req;
    if (!req.ParseFromArray(bodyData.data(), bodyData.size())) return;

    const auto& rec = req.record();
    QString sql = "INSERT INTO t_movie_record (user_id, movie_name, play_date, start_time, end_time, operator_name, end_type, create_time) VALUES (?, ?, ?, ?, ?, ?, ?, NOW())";
    QList<QVariant> params;
    params << session->GetAccountId() << QString::fromStdString(rec.movie_name()) << QString::fromStdString(rec.play_date())
        << QString::fromStdString(rec.start_time()) << QString::fromStdString(rec.end_time())
        << QString::fromStdString(rec.operator_name()) << QString::fromStdString(rec.end_type());

    std::weak_ptr<ClientSession> weakSession = session;
    ThreadPool::Instance()->PostUpdateTask(sql, [weakSession, seq_id = header.seq_id()](bool success) {
        if (auto s = weakSession.lock()) {
            s->SendProtoMsg(ServerApi::ID_ADD_RECORD_RSP, ServerApi::AddRecordRsp(), seq_id, success ? ServerApi::ERR_SUCCESS : ServerApi::ERR_SERVER_INTERNAL);
        }
        }, true, params);
}

void MoviePlayRecordService::OnDeleteRecord(std::shared_ptr<ClientSession> session, const ServerApi::PacketHeader& header, const QByteArray& bodyData)
{
    ServerApi::DeleteRecordReq req;
    if (!req.ParseFromArray(bodyData.data(), bodyData.size())) return;

    // 👑 核心安全：WHERE 必须带上 user_id，严防越权删除！
    QString sql = "DELETE FROM t_movie_record WHERE id = ? AND user_id = ?";
    QList<QVariant> params;
    params << req.record_id() << session->GetAccountId();

    std::weak_ptr<ClientSession> weakSession = session;
    ThreadPool::Instance()->PostUpdateTask(sql, [weakSession, seq_id = header.seq_id(), recordId = req.record_id()](bool success) {
        if (auto s = weakSession.lock()) {
            ServerApi::DeleteRecordRsp rsp;
            if (success) {
                rsp.set_deleted_id(recordId);
                s->SendProtoMsg(ServerApi::ID_DELETE_RECORD_RSP, rsp, seq_id);
            }
            else {
                s->SendProtoMsg(ServerApi::ID_DELETE_RECORD_RSP, rsp, seq_id, ServerApi::ERR_SERVER_INTERNAL, u8"删除失败，记录不存在或无权限");
            }
        }
        }, true, params);
}