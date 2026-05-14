#include "MovieWidget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QToolButton>
#include <QButtonGroup>
#include "CinemaMessageBox.h"
#include "UserMgr.h"
#include "ThreadPool.h"

MovieWidget::MovieWidget(QWidget* parent) : QWidget(parent)
{
    setAttribute(Qt::WA_StyledBackground, true);
    setObjectName("MovieWidget");

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

    QToolButton* playPageBtn = createNavBtn(tr("影片播放"), 0);
    QToolButton* recordPageBtn = createNavBtn(tr("播放记录"), 1);
    navLayout->addWidget(playPageBtn);
    navLayout->addWidget(recordPageBtn);

    // 👑 权限硬拦截：只有管理员能看到“资源上传”按钮
    bool bIsAdmin = UserMgr::Instance()->GetPermission() == UserMgr::Role::ADMIN;
    QToolButton* uploadPageBtn = nullptr;
    if (bIsAdmin)
    {
        uploadPageBtn = createNavBtn(tr("资源上传"), 2);
        navLayout->addWidget(uploadPageBtn);
    }

    playPageBtn->setChecked(true);
    navLayout->addStretch();

    // ================= 2. 堆栈装配与信号跨接 =================
    m_stackedWidget = new QStackedWidget(this);

    m_playbackPage = new PlaybackPage(this);                                // 实例化播控台
    m_recordPage = new RecordPage(this);                                    // 实例化记录台
    m_stackedWidget->addWidget(m_playbackPage);
    m_stackedWidget->addWidget(m_recordPage);

    if (bIsAdmin)
    {
        m_uploadPage = new UploadPage(this);                                // 实例化影片上传窗口
        m_stackedWidget->addWidget(m_uploadPage);
        BindAdminSignals();                                                 // 绑定管理员权限的信号
    }

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
            m_recordPage->RequestAddRecord(date, name, start, end, UserMgr::Instance()->GetUserName(), type);
        });
}

void MovieWidget::BindAdminSignals()
{
    if (!m_uploadPage) return;

    auto tcpMgr = ThreadPool::Instance()->GetTCPMgr();

    // ------------------------------------------------------------
    // 1. 影片信息（元数据）上传成功
    // ------------------------------------------------------------
    connect(tcpMgr, &TCPMgr::SigMovieUploadSuccess, this, [this]()
        {
            // 1. 通知 UploadPage 恢复按钮状态，清空输入框
            m_uploadPage->ResetUI();

            // 2. 通知 PlaybackPage 重新去服务器拉取最新的影片列表
            m_playbackPage->RefreshMovies();

            // 3. 弹窗提示，并切回播放页面
            CinemaMessageBox::ShowInfo(this, tr("成功"), tr("影片已成功录入云端！"));
        });

    // ------------------------------------------------------------
    // 2. 影片信息（元数据）上传失败
    // ------------------------------------------------------------
    connect(tcpMgr, &TCPMgr::SigMovieUploadFailed, this, [this](QString errMsg)
        {
            // 恢复上传页面的按钮和输入框可编辑状态
            m_uploadPage->UnlockUI();
            CinemaMessageBox::ShowError(this, tr("影片登记失败"), errMsg);
        });

    // ------------------------------------------------------------
    // 3. 底层分片传输失败 (网络断开/服务器磁盘满/MD5不匹配等)
    // ------------------------------------------------------------
    connect(tcpMgr, &TCPMgr::SigChunkUploadFailed, this, [this](ServerApi::FileType fileType, QString errMsg)
        {
            // 必须进行类型鉴别，只处理电影频道引发的坠毁事故
            if (fileType == ServerApi::FileType::FILE_MOVIE)
            {
                // 停止切片抽水泵，关闭文件指针，恢复上传按钮
                m_uploadPage->UnlockUI();
                CinemaMessageBox::ShowError(this, tr("视频传输中断"), errMsg);
            }
        });
}

void MovieWidget::onNavButtonClicked(int index)
{
    m_stackedWidget->setCurrentIndex(index);                                // 页面切换
}