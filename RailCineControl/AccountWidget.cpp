#include "AccountWidget.h"
#include <QVBoxLayout>
#include <QLabel>
#include <QFormLayout>
#include <QPushButton>
#include <QPainter>
#include <QPixmap>
#include "ImageBgButton.h"
#include "TipWidget.h"
#include "TCPMgr.h"


static QString StyleStr(R"(
    QWidget { border:0; }
    QLabel { background:#111; color:#fff; font-size:17px;font-weight:400;}
    ImageBgButton 
	{
        color:#862800; border:none;
    }
)");

AccountWidget::AccountWidget(QWidget* parent)
    : QWidget(parent, Qt::Popup | Qt::FramelessWindowHint)
{
    BuildUI();
}

void AccountWidget::BuildUI()
{
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAutoFillBackground(false);                     // 不要自动刷背景

    // 内容面板
    auto panel = new QWidget(this);
    panel->setObjectName("panel");
    auto lay = new QVBoxLayout(panel);
    lay->setContentsMargins(14, 14, 14, 14);
    lay->setSpacing(10);

    auto title = new QLabel(QStringLiteral("账号管理"), panel);
    title->setAlignment(Qt::AlignCenter);

    auto form = new QFormLayout;
    m_name = new QLabel(panel);
    form->addRow(QStringLiteral("账号:"), m_name);

    // 切换按钮
    switchBtn = new ImageBgButton(QStringLiteral("切换账号"), ":/MiNi/Images/MiNiWorld/Installed.png", panel);
    switchBtn->setCursor(Qt::PointingHandCursor);
    switchBtn->setFlat(true);
    connect(switchBtn, &QPushButton::clicked, this, &AccountWidget::SlotLoginOut);

    lay->addWidget(title);
    lay->addLayout(form);
    lay->addWidget(switchBtn);

    auto root = new QVBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);                               // 外层留出圆角描边
    root->addWidget(panel);

    // 简单样式（你可替换为自己的 QSS）
    setStyleSheet(StyleStr);
}

void AccountWidget::SlotLoginOut()
{
    if (TipWidget::confirm(switchBtn, QString::fromLocal8Bit("确定要退出当前账号吗？")))
    {
        // 用户请求下线
        TCPMgr::Instance()->AccountLoginOut();
    }
}

void AccountWidget::setUserName(const QString& name)
{
	m_name->setText(name);
}

void AccountWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    QPixmap bg(":/MiNi/Images/MiNiWorld/AccountPanel.png");
    if (!bg.isNull())
        p.drawPixmap(rect(), bg);
}