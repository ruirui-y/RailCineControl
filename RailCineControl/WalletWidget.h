#pragma once

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QTabWidget>
#include <QGridLayout>
#include "server_msg.pb.h"

class CinemaTableWidget;
class CinemaPayDialog;

class WalletWidget : public QWidget
{
    Q_OBJECT

public:
    explicit WalletWidget(QWidget* parent = nullptr);
    ~WalletWidget();

protected:
    virtual void showEvent(QShowEvent* event);

private:
    // UI 构建与样式分离
    void BuildUI();

private slots:
    void OnWalletReceived(const ServerApi::GetWalletRsp& rsp);                                              // 当用户积分余额获取成功
    void OnGoodsListReceived(const ServerApi::GetGoodsRsp& rsp);                                            // 当充值套餐获取成功

    // 接收服务器返回的“订单创建成功” (准备展示二维码)
    void OnOrderCreated(const QString& orderId, const QString& qrUrl, int expireTime);

    // 接收服务器异步推送的“支付成功” (关闭弹窗，刷新余额)
    void OnOrderPaid(const QString& orderId, qint64 newPoints);

private:
    // 顶部资产区
    QLabel* m_lblUsername;
    QLabel* m_lblPoints;

    // 底部标签与页面
    QTabWidget* m_tabWidget;
    QWidget* m_rechargePage;
    QWidget* m_flowPage;

    // 充值页的核心布局
    QGridLayout* m_goodsLayout;

    // 流水页的表格
    CinemaTableWidget* m_flowTable;

    // 暂存当前正在购买的商品信息 (弹窗时需要用到)
    QString m_currentPayName;
    QString m_currentPayPrice;

    // 保存支付弹窗的指针，方便在收到支付成功推送时自动把它关掉
    CinemaPayDialog* m_payDialog = nullptr;
};