#include "Titlebar.h"
#include <QHBoxLayout>
#include <QToolButton>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QApplication>
#include <QScreen>
#include "UserMgr.h"
#include "Macro.h"
#include "AccountWidget.h"
#include "Global.h"


static QString AccountBtnStytle(R"(
   QPushButton{
       border:0; background:transparent; color:#BBBBBB;
       font-size:20px; font-weight:400; text-align:left; padding-left:8px;
   }
   QPushButton[styled="true"]:pressed{ color:#FFFFFF; }
)");

TitleBar::TitleBar(QWidget* parent) : QWidget(parent) 
{
    setFixedHeight(56);
    setAttribute(Qt::WA_StyledBackground, true);                                                        // 启用样式表渲染
    setStyleSheet("background:transparent;");                                                           // 背景透明

    QHBoxLayout* hLayout = new QHBoxLayout(this);
    hLayout->setContentsMargins(0, 0, 10, 0);
    hLayout->setSpacing(8);
    
    // 创建按钮
    InitBtns();

    hLayout->addWidget(m_btnName);
    hLayout->addStretch();
    hLayout->addSpacing(12);
    hLayout->addWidget(m_btnMin);
    hLayout->addWidget(m_btnClose);
    
    // 绑定信号槽
    connect(m_btnMin, &QToolButton::clicked, this, &TitleBar::minimizeRequested);
    connect(m_btnClose, &QToolButton::clicked, this, &TitleBar::closeRequested);
}

void TitleBar::mousePressEvent(QMouseEvent* e) 
{ 
    if (e->button() == Qt::LeftButton) 
    {
        // 计算相对顶层窗口左上的偏移
        m_dragOffset = e->globalPos() - window()->frameGeometry().topLeft();
        e->accept();
    }
    else 
    {
        QWidget::mousePressEvent(e);
    }
}

void TitleBar::mouseMoveEvent(QMouseEvent* e) 
{ 
    if (e->buttons() & Qt::LeftButton) 
    {
        // 拖动顶层窗口（MainWindow）
        window()->move(e->globalPos() - m_dragOffset);
        e->accept();
    }
    else 
    {
        QWidget::mouseMoveEvent(e);
    }
}

void TitleBar::InitBtns()
{
    m_btnMin = new QToolButton(this);
    m_btnClose = new QToolButton(this);
    m_btnName = new QPushButton(this);

    // 纯图标按钮
    for (QToolButton* btn : {m_btnMin, m_btnClose })
    {
        btn->setToolButtonStyle(Qt::ToolButtonIconOnly);
        btn->setAutoRaise(true);                                                                          // 扁平风格
        btn->setIconSize(QSize(18, 18));                                                                  // 图标像素大小
        btn->setCursor(Qt::PointingHandCursor);
        btn->setFixedSize(32, 28);                                                                        // 按钮本体大小（留出内边距）
        btn->setStyleSheet(
            "QToolButton{border:0; background:transparent;}"
        );
    }

    // 纯文本按钮
    m_btnName->setFlat(true);
    UserInfo userinfo = UserMgr::Instance()->getUserInfo();
    m_btnName->setText(userinfo.UserName);
    m_btnName->setCursor(Qt::PointingHandCursor);
    m_btnName->setFixedSize(HZ_LIST_WIDTH, 28);
    m_btnName->setStyleSheet(AccountBtnStytle);
    m_btnName->setProperty("styled", true);
    repolish(m_btnName);

    m_btnMin->setIcon(QIcon(":/MiNi/Images/MiNiWorld/min.png"));
    m_btnClose->setIcon(QIcon(":/MiNi/Images/MiNiWorld/close.png"));

    m_btnMin->setToolTip(QString::fromLocal8Bit("最小化"));
    m_btnClose->setToolTip(QString::fromLocal8Bit("关闭"));
    m_btnName->setToolTip(QString::fromLocal8Bit("账号信息"));

    auto accountWidget = new AccountWidget(m_btnName);      // 给按钮当父对象，自动释放
    accountWidget->setUserName(userinfo.UserName);

    connect(m_btnName, &QAbstractButton::clicked, m_btnName, [=] 
        {
            m_btnName->setProperty("styled", false);
            repolish(m_btnName);
            // 计算右下角对齐的位置：让弹层右边对齐按钮右边，顶边接在按钮底边
            accountWidget->adjustSize();
            const QSize sz = accountWidget->size();

            QPoint pt = m_btnName->mapToGlobal(QPoint(m_btnName->width(), m_btnName->height()));

            accountWidget->move(pt);
            accountWidget->show();                                                                  // Qt::accountWidget：点击外面自动关闭
        });
}