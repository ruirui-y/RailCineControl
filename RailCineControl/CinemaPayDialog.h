#pragma once

#include "CinemaDialogBase.h"
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>

class CinemaPayDialog : public CinemaDialogBase
{
    Q_OBJECT
public:
    // 传入父类指针、商品名称（如"首充特惠"）、显示价格（如"￥50.00"）
    explicit CinemaPayDialog(QWidget* parent, const QString& goodsName, const QString& priceText)
        : CinemaDialogBase(parent)
    {
        this->resize(380, 480); // 支付窗口稍微高一点，用来放二维码
        this->SetDialogTitle(tr("💳 扫码安全支付"));

        QVBoxLayout* content = this->GetContentLayout();
        content->setSpacing(15);
        content->setAlignment(Qt::AlignTop | Qt::AlignHCenter);

        // 1. 顶部：商品名称
        QLabel* lblGoods = new QLabel(goodsName, this);
        lblGoods->setObjectName("PayGoodsName");
        lblGoods->setAlignment(Qt::AlignCenter);

        // 2. 价格展示 (醒目的大字号)
        QLabel* lblPrice = new QLabel(priceText, this);
        lblPrice->setObjectName("PayPriceText");
        lblPrice->setAlignment(Qt::AlignCenter);

        // 3. 核心：二维码占位区域
        QLabel* lblQRCode = new QLabel(tr("二维码生成中..."), this);
        lblQRCode->setObjectName("PayQRCode");
        lblQRCode->setFixedSize(220, 220);
        lblQRCode->setAlignment(Qt::AlignCenter);
        // TODO: 后续集成 libqrencode 后，在这里用 lblQRCode->setPixmap() 替换为真实二维码

        // 4. 底部：提示与倒计时
        QLabel* lblTips = new QLabel(tr("请使用 微信 / 支付宝 扫码完成支付"), this);
        lblTips->setObjectName("PayTipsDesc");
        lblTips->setAlignment(Qt::AlignCenter);

        QLabel* lblCountdown = new QLabel(tr("二维码有效时间: 05:00"), this);
        lblCountdown->setObjectName("PayCountdown");
        lblCountdown->setAlignment(Qt::AlignCenter);

        // 5. 底部取消按钮 (取消支付)
        QPushButton* btnCancel = new QPushButton(tr("取消支付"), this);
        btnCancel->setObjectName("btnCinemaCancel"); // 直接复用我们之前写好的低调取消按钮样式
        btnCancel->setFixedSize(140, 40);
        connect(btnCancel, &QPushButton::clicked, this, &CinemaPayDialog::reject);

        // 组装到布局
        content->addSpacing(10);
        content->addWidget(lblGoods);
        content->addWidget(lblPrice);
        content->addSpacing(10);
        content->addWidget(lblQRCode, 0, Qt::AlignHCenter); // 确保二维码绝对水平居中
        content->addSpacing(10);
        content->addWidget(lblTips);
        content->addWidget(lblCountdown);
        content->addStretch();
        content->addWidget(btnCancel, 0, Qt::AlignHCenter);
    }
};