#include "WalletService.h"
#include "ClientSession.h"
#include "MsgDispatcher.h"
#include "ThreadPool.h"
#include <QVariant>
#include <QDateTime>
#include <QDebug>

WalletService::~WalletService() { qDebug() << u8"♻️ [WalletService] 实例析构"; }

void WalletService::Init()
{
    std::weak_ptr<WalletService> weakSelf = shared_from_this();

    MsgDispatcher::Instance()->RegisterRoute(ServerApi::ID_GET_WALLET_REQ,
        [weakSelf](auto s, auto& h, auto& b) { if (auto self = weakSelf.lock()) self->OnGetWallet(s, h, b); });

    MsgDispatcher::Instance()->RegisterRoute(ServerApi::ID_GET_GOODS_REQ,
        [weakSelf](auto s, auto& h, auto& b) { if (auto self = weakSelf.lock()) self->OnGetGoods(s, h, b); });

    MsgDispatcher::Instance()->RegisterRoute(ServerApi::ID_GET_FLOW_REQ,
        [weakSelf](auto s, auto& h, auto& b) { if (auto self = weakSelf.lock()) self->OnGetFlow(s, h, b); });
}

void WalletService::OnGetWallet(std::shared_ptr<ClientSession> session, const ServerApi::PacketHeader& header, const QByteArray& bodyData)
{
    QString sql = "SELECT balance_points, total_recharged FROM t_user_wallet WHERE user_id = ?";
    QList<QVariant> params;
    params << session->GetAccountId();

    std::weak_ptr<ClientSession> weakSession = session;
    ThreadPool::Instance()->PostQueryTask(sql, [weakSession, seq_id = header.seq_id()](const QList<QVariantMap>& results) {
        auto s = weakSession.lock();
        if (!s) return;

        ServerApi::GetWalletRsp rsp;
        if (!results.isEmpty()) {
            rsp.set_current_points(results.first()["balance_points"].toLongLong());
            rsp.set_total_recharged(results.first()["total_recharged"].toLongLong());
        }
        else {
            rsp.set_current_points(0);
            rsp.set_total_recharged(0);
        }
        s->SendProtoMsg(ServerApi::ID_GET_WALLET_RSP, rsp, seq_id);
        }, true, params);
}

void WalletService::OnGetGoods(std::shared_ptr<ClientSession> session, const ServerApi::PacketHeader& header, const QByteArray& bodyData)
{
    QString sql = "SELECT goods_id, name, price_cents, points_reward FROM t_goods_sku WHERE status = 1 ORDER BY price_cents ASC";

    std::weak_ptr<ClientSession> weakSession = session;
    ThreadPool::Instance()->PostQueryTask(sql, [weakSession, seq_id = header.seq_id()](const QList<QVariantMap>& results) {
        auto s = weakSession.lock();
        if (!s) return;

        ServerApi::GetGoodsRsp rsp;
        for (const auto& row : results) {
            auto* info = rsp.add_goods_list();
            info->set_goods_id(row["goods_id"].toULongLong());
            info->set_goods_name(row["name"].toString().toStdString());
            info->set_price_cents(row["price_cents"].toUInt());
            info->set_points_reward(row["points_reward"].toUInt());
        }
        s->SendProtoMsg(ServerApi::ID_GET_GOODS_RSP, rsp, seq_id);
        }, true);
}

void WalletService::OnGetFlow(std::shared_ptr<ClientSession> session, const ServerApi::PacketHeader& header, const QByteArray& bodyData)
{
    ServerApi::GetFlowReq req;
    if (!req.ParseFromArray(bodyData.data(), bodyData.size())) return;

    uint32_t offset = (req.page_index() > 0 ? req.page_index() - 1 : 0) * (req.page_size() > 0 ? req.page_size() : 100);
    uint32_t pageSize = (req.page_size() > 0 ? req.page_size() : 100);

    QString countSql = "SELECT COUNT(*) AS total FROM t_point_flow WHERE user_id = ?";
    QString dataSql = "SELECT flow_type, points_change, balance_after, associated_id, create_time FROM t_point_flow WHERE user_id = ? ORDER BY create_time DESC LIMIT ?, ?";

    std::weak_ptr<ClientSession> weakSession = session;
    ThreadPool::Instance()->PostQueryTask(countSql, [dataSql, weakSession, seq_id = header.seq_id(), offset, pageSize](const QList<QVariantMap>& countRes) {
        auto s = weakSession.lock();
        if (!s) return;

        uint32_t total = countRes.isEmpty() ? 0 : countRes.first()["total"].toUInt();
        ThreadPool::Instance()->PostQueryTask(dataSql, [s, seq_id, total](const QList<QVariantMap>& dataRes) {
            ServerApi::GetFlowRsp rsp;
            rsp.set_total_count(total);
            for (const auto& row : dataRes) {
                auto* rec = rsp.add_records();
                rec->set_flow_type(row["flow_type"].toInt());
                rec->set_points_change(row["points_change"].toInt());
                rec->set_balance_after(row["balance_after"].toLongLong());
                rec->set_associated_id(row["associated_id"].toString().toStdString());
                rec->set_create_time(row["create_time"].toDateTime().toString("yyyy-MM-dd HH:mm:ss").toStdString());
            }
            s->SendProtoMsg(ServerApi::ID_GET_FLOW_RSP, rsp, seq_id);
            }, true, QList<QVariant>() << s->GetAccountId() << offset << pageSize);
        }, true, QList<QVariant>() << session->GetAccountId());
}