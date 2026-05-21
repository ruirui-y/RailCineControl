#pragma once
#include <QObject>
#include <QString>
#include "singletion.h" 

class MidPlatformManager : public QObject, public Singleton<MidPlatformManager>
{
    Q_OBJECT
        friend class Singleton<MidPlatformManager>;

public:
    explicit MidPlatformManager(QObject* parent = nullptr);
    ~MidPlatformManager();

    // =========================================================================
    // 👑 中台 API 接口列表
    // =========================================================================

    // 1. 获取中台访问令牌 (阻塞式)
    QString GetAccessToken();

    // 2. 向中台查询订单真实支付状态 (阻塞式)
    // 返回值: 1 (支付成功), 0 (等待支付), -1 (失败/关闭/异常)
    int CheckOrderFromMidPlatform(const QString& orderId);

    // 3. 专门用于弥补 Webhook 缺失的微信流水号
    QString FetchTransactionId(const QString& orderId);

signals:
    // 转发查单结果的信号
    void SigPaymentResult(const QString& out_trade_no, const QString& transaction_id, int payment_status);
};