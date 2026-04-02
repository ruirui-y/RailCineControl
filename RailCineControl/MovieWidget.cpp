#include "MovieWidget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QToolButton>
#include <QButtonGroup>
#include "UserMgr.h"

MovieWidget::MovieWidget(QWidget* parent) : QWidget(parent)
{
    BuildUI();
}

void MovieWidget::BuildUI()
{
    QVBoxLayout* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(30, 20, 30, 30);
    rootLayout->setSpacing(20);

    // ================= 1. 顶部内部导航栏 =================
    QHBoxLayout* navLayout = new QHBoxLayout();
    QButtonGroup* btnGroup = new QButtonGroup(this);
    btnGroup->setExclusive(true);

    auto createNavBtn = [&](const QString& text, int id) -> QToolButton* {
        QToolButton* btn = new QToolButton(this);
        btn->setText(text);
        btn->setObjectName("navBtn");                                       // 绑定导航QSS
        btn->setCheckable(true);
        btn->setCursor(Qt::PointingHandCursor);
        btnGroup->addButton(btn, id);
        return btn;
        };

    QToolButton* playPageBtn = createNavBtn(u8"影片播放", 0);
    QToolButton* recordPageBtn = createNavBtn(u8"播放记录", 1);
    playPageBtn->setChecked(true);

    navLayout->addWidget(playPageBtn);
    navLayout->addWidget(recordPageBtn);
    navLayout->addStretch();

    // ================= 2. 堆栈装配与信号跨接 =================
    m_stackedWidget = new QStackedWidget(this);

    m_playbackPage = new PlaybackPage(this);                                // 实例化播控台
    m_recordPage = new RecordPage(this);                                    // 实例化记录台

    m_stackedWidget->addWidget(m_playbackPage);
    m_stackedWidget->addWidget(m_recordPage);

    rootLayout->addLayout(navLayout);
    rootLayout->addWidget(m_stackedWidget, 1);

    connect(btnGroup, QOverload<int>::of(&QButtonGroup::buttonClicked),
        this, &MovieWidget::onNavButtonClicked);

    // =========================================================================
    // 👑 核心数据桥梁：将播放台的信号，接入记录台的插入方法
    // 无论它是“强制结束”还是“正常结束”，这里统一拦截并分发给底层去写 JSON 表格
    // =========================================================================
    connect(m_playbackPage, &PlaybackPage::playbackFinishedRecord,
        this, [this](QString date, QString name, QString start, QString end, QString type) 
        {
            m_recordPage->AddRecordRow(date, name, start, end, UserMgr::Instance()->getUserInfo().UserName, type);
        });
}

void MovieWidget::onNavButtonClicked(int index)
{
    m_stackedWidget->setCurrentIndex(index);                                // 页面切换
}