#pragma once
#include "CinemaDialogBase.h"
#include <QLabel>
#include <QVBoxLayout>
#include <QPushButton>
#include <QPainter>
#include <QImage>
#include <QPixmap>
#include <QTimer>     
#include <qrencode.h>
#include <QDebug>

class CinemaPayDialog : public CinemaDialogBase
{
    Q_OBJECT
public:
    explicit CinemaPayDialog(QWidget* parent, const QString& goodsName, const QString& priceText)
        : CinemaDialogBase(parent), m_remainSeconds(0) // 初始化倒计时秒数
    {
        this->resize(380, 480);
        this->SetDialogTitle(tr("💳 扫码安全支付"));

        QVBoxLayout* content = this->GetContentLayout();
        content->setSpacing(15);
        content->setAlignment(Qt::AlignTop | Qt::AlignHCenter);

        QLabel* lblGoods = new QLabel(goodsName, this);
        lblGoods->setObjectName("PayGoodsName");
        lblGoods->setAlignment(Qt::AlignCenter);

        QLabel* lblPrice = new QLabel(priceText, this);
        lblPrice->setObjectName("PayPriceText");
        lblPrice->setAlignment(Qt::AlignCenter);

        m_lblQRCode = new QLabel(tr("二维码生成中..."), this);
        m_lblQRCode->setObjectName("PayQRCode");
        m_lblQRCode->setFixedSize(220, 220);
        m_lblQRCode->setAlignment(Qt::AlignCenter);

        QLabel* lblTips = new QLabel(tr("请使用 微信 扫码完成支付"), this);
        lblTips->setObjectName("PayTipsDesc");
        lblTips->setAlignment(Qt::AlignCenter);

        m_lblCountdown = new QLabel(tr("二维码有效时间: 05:00"), this);
        m_lblCountdown->setObjectName("PayCountdown");
        m_lblCountdown->setAlignment(Qt::AlignCenter);

        QPushButton* btnCancel = new QPushButton(tr("取消支付"), this);
        btnCancel->setObjectName("btnCinemaCancel");
        btnCancel->setFixedSize(140, 40);
        connect(btnCancel, &QPushButton::clicked, this, &CinemaPayDialog::reject);

        content->addSpacing(10);
        content->addWidget(lblGoods);
        content->addWidget(lblPrice);
        content->addSpacing(10);
        content->addWidget(m_lblQRCode, 0, Qt::AlignHCenter);
        content->addSpacing(10);
        content->addWidget(lblTips);
        content->addWidget(m_lblCountdown);
        content->addStretch();
        content->addWidget(btnCancel, 0, Qt::AlignHCenter);

        // =========================================================================
        // 👑 定时器核心逻辑
        // =========================================================================
        m_timer = new QTimer(this);
        connect(m_timer, &QTimer::timeout, this, [this]() {
            if (m_remainSeconds > 0) {
                m_remainSeconds--;

                // 换算成分和秒
                int m = m_remainSeconds / 60;
                int s = m_remainSeconds % 60;

                // 格式化输出，不足两位补0 (例如 04:09)
                m_lblCountdown->setText(tr("二维码有效时间: %1:%2")
                    .arg(m, 2, 10, QChar('0'))
                    .arg(s, 2, 10, QChar('0')));
            }
            else {
                // 倒计时归零，停止定时器
                m_timer->stop();
                m_lblCountdown->setText(tr("二维码已失效，请重新发起支付"));
                m_lblCountdown->setStyleSheet("color: red; font-weight: bold;"); // 标红提示

                // 🛡️ 防御机制：清空二维码，防止用户扫码付款到过期订单
                if (m_lblQRCode) {
                    m_lblQRCode->clear();
                    m_lblQRCode->setText(tr("已过期"));
                    m_lblQRCode->setStyleSheet("background-color: #EEEEEE; color: #999999; font-size: 18px;");
                }
            }
            });
        qDebug() << "支付窗口创建";
    }

    // 供外部调用的更新二维码函数
    void UpdateQRCode(const QString& orderId, const QString& qrUrl, int expireTime)
    {
        if (m_lblQRCode && !qrUrl.isEmpty()) {
            QPixmap qrPixmap = GenerateQRCode(qrUrl, 220);
            m_lblQRCode->setPixmap(qrPixmap);
        }

        // 👑 启动倒计时 (从服务端传来的 expireTime，通常是 300 秒)
        if (expireTime > 0) {
            m_remainSeconds = expireTime;

            // 手动触发一次 UI 更新，防止等待1秒后才刷新
            int m = m_remainSeconds / 60;
            int s = m_remainSeconds % 60;
            m_lblCountdown->setText(tr("二维码有效时间: %1:%2")
                .arg(m, 2, 10, QChar('0'))
                .arg(s, 2, 10, QChar('0')));

            m_timer->start(1000); // 每 1000 毫秒（1秒）触发一次 timeout
        }
        qDebug() << "二维码数据渲染成功";
    }

private:
    QPixmap GenerateQRCode(const QString& text, int width)
    {
        if (text.isEmpty()) return QPixmap();

        QRcode* qr = QRcode_encodeString(text.toStdString().c_str(), 0, QR_ECLEVEL_Q, QR_MODE_8, 1);
        if (!qr) return QPixmap();

        QImage image(qr->width, qr->width, QImage::Format_Mono);
        QPainter painter(&image);
        painter.fillRect(0, 0, qr->width, qr->width, Qt::white);

        painter.setPen(Qt::NoPen);
        painter.setBrush(Qt::black);
        for (int y = 0; y < qr->width; y++) {
            for (int x = 0; x < qr->width; x++) {
                if (qr->data[y * qr->width + x] & 1) {
                    painter.drawRect(x, y, 1, 1);
                }
            }
        }
        QRcode_free(qr);

        return QPixmap::fromImage(image).scaled(width, width, Qt::KeepAspectRatio, Qt::FastTransformation);
    }

private:
    QLabel* m_lblQRCode = nullptr;
    QLabel* m_lblCountdown = nullptr;

    // 👑 新增定时器相关成员
    QTimer* m_timer = nullptr;
    int m_remainSeconds = 0;
};