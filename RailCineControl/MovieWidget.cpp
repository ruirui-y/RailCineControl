#include "MovieWidget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QToolButton>
#include <QButtonGroup>
#include <QMessageBox>
#include "UserMgr.h"
#include "TCPMgr.h"

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
    QToolButton* uploadPageBtn = createNavBtn(u8"资源上传", 2);
    playPageBtn->setChecked(true);

    navLayout->addWidget(playPageBtn);
    navLayout->addWidget(recordPageBtn);
    navLayout->addWidget(uploadPageBtn);
    navLayout->addStretch();

    // ================= 2. 堆栈装配与信号跨接 =================
    m_stackedWidget = new QStackedWidget(this);

    m_playbackPage = new PlaybackPage(this);                                // 实例化播控台
    m_recordPage = new RecordPage(this);                                    // 实例化记录台
    m_uploadPage = new UploadPage(this);                                    // 实例化影片上传窗口

    m_stackedWidget->addWidget(m_playbackPage);
    m_stackedWidget->addWidget(m_recordPage);
    m_stackedWidget->addWidget(m_uploadPage);

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
            m_recordPage->RequestAddRecord(date, name, start, end, UserMgr::Instance()->getUserInfo().UserName, type);
        });

    // 监听底层的上传成功信号
    connect(TCPMgr::Instance().get(), &TCPMgr::SigUploadSuccess, this, [this]()
        {
        // 1. 通知 UploadPage 恢复按钮状态，清空输入框
        m_uploadPage->ResetUI();                                                

        // 2. 通知 PlaybackPage 重新去服务器拉取最新的影片列表
        m_playbackPage->RefreshMovies();

        // 3. 弹窗提示，并切回播放页面
        QMessageBox::information(this, u8"成功", u8"影片已成功录入云端！");
        });

    // 监听底层的上传失败信号
    connect(TCPMgr::Instance().get(), &TCPMgr::SigUploadFailed, this, [this](QString errMsg) {
        // 恢复上传页面的按钮
        m_uploadPage->UnlockUI();
        QMessageBox::critical(this, u8"上传失败", errMsg);
        });
}

void MovieWidget::onNavButtonClicked(int index)
{
    m_stackedWidget->setCurrentIndex(index);                                // 页面切换
}