#include "OrderManagementService.h"
#include "ThreadPool.h"
#include "MidPlatformManager.h"
#include "Enum.h"
#include <QDebug>

OrderManagementService::~OrderManagementService() {
    if (m_timer) m_timer->stop();
    qDebug() << u8"♻️ [OrderManagementService] 析构";
}

void OrderManagementService::Init() 
{
    m_timer = new QTimer();
    connect(m_timer, &QTimer::timeout, this, &OrderManagementService::CheckUnpaidOrders);
    m_timer->start(10 * 1000);                                                                                  // 10秒巡检一次
}

void OrderManagementService::CheckUnpaidOrders() {
    QString sqlSelect = QString("SELECT order_id FROM t_pay_order WHERE status = %1 AND create_time <= DATE_SUB(NOW(), INTERVAL 5 MINUTE)")
        .arg(ORDER_STATUS_PENDING);

    ThreadPool::Instance()->PostQueryTask(sqlSelect, [this](const QList<QVariantMap>& results) {
        if (results.isEmpty()) return;

        qDebug() << u8"🔍 [清理守护] 开始巡检，发现" << results.size() << u8"个超时未支付订单";

        for (const QVariantMap& row : results) {
            QString orderId = row["order_id"].toString();
            int realStatus = MidPlatformManager::Instance()->CheckOrderFromMidPlatform(orderId);

            if (realStatus == PAY_STATUS_UNPAID) {
                QString sqlClose = QString("UPDATE t_pay_order SET status = %1 WHERE order_id = ?").arg(ORDER_STATUS_CLOSED);
                ThreadPool::Instance()->PostUpdateTask(sqlClose, [orderId](int rows) {
                    if (rows > 0) qDebug() << u8"🗑️ [清理守护] 订单作废:" << orderId;
                    }, true, QList<QVariant>() << orderId);
            }
            else if (realStatus == PAY_STATUS_ERROR) {
                QString sqlAnomaly = QString("UPDATE t_pay_order SET status = %1 WHERE order_id = ?").arg(ORDER_STATUS_ANOMALY);
                ThreadPool::Instance()->PostUpdateTask(sqlAnomaly, [orderId](int rows) {
                    if (rows > 0) qDebug() << u8"🚨 [清理守护] 查单异常，挂起:" << orderId;
                    }, true, QList<QVariant>() << orderId);
            }
        }
        }, true);

    // 清理过期数据
    QString sqlDelete = QString("DELETE FROM t_pay_order WHERE status = %1 AND create_time <= DATE_SUB(NOW(), INTERVAL 3 DAY)")
        .arg(ORDER_STATUS_CLOSED);
    ThreadPool::Instance()->PostUpdateTask(sqlDelete, [](int) {}, true);
}