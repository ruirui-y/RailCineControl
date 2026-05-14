#include "GameWidget.h"
#include "GameLauncherPage.h"
#include "GameUploadPage.h"
#include "UserMgr.h"
#include "TCPMgr.h"
#include "ThreadPool.h"
#include "CinemaMessageBox.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QToolButton>
#include <QButtonGroup>

GameWidget::GameWidget(QWidget* parent) : QWidget(parent)
{
    setAttribute(Qt::WA_StyledBackground, true);
    setObjectName("GameWidget");
    BuildUI();
}

void GameWidget::BuildUI()
{
    QVBoxLayout* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(30, 20, 30, 30);
    rootLayout->setSpacing(20);

    // 1. 顶部导航
    QHBoxLayout* navLayout = new QHBoxLayout();
    QButtonGroup* btnGroup = new QButtonGroup(this);

    auto createNavBtn = [&](const QString& text, int id) -> QToolButton* {
        QToolButton* btn = new QToolButton(this);
        btn->setText(text);
        btn->setObjectName("navBtn");
        btn->setCheckable(true);
        btn->setCursor(Qt::PointingHandCursor);
        btnGroup->addButton(btn, id);
        return btn;
        };

    QToolButton* launchBtn = createNavBtn(tr("游戏启动"), 0);
    navLayout->addWidget(launchBtn);

    bool bIsAdmin = UserMgr::Instance()->GetPermission() == UserMgr::Role::ADMIN;
    QToolButton* uploadBtn = nullptr;
    if (bIsAdmin) {
        uploadBtn = createNavBtn(tr("游戏上传"), 1);
        navLayout->addWidget(uploadBtn);
    }

    launchBtn->setChecked(true);
    navLayout->addStretch();

    // 2. 堆栈页面
    m_stackedWidget = new QStackedWidget(this);
    m_launcherPage = new GameLauncherPage(this);
    m_stackedWidget->addWidget(m_launcherPage);

    if (bIsAdmin) {
        m_uploadPage = new GameUploadPage(this);
        m_stackedWidget->addWidget(m_uploadPage);
        BindAdminSignals();
    }

    rootLayout->addLayout(navLayout);
    rootLayout->addWidget(m_stackedWidget, 1);

    connect(btnGroup, QOverload<int>::of(&QButtonGroup::buttonClicked),
        this, &GameWidget::onNavButtonClicked);
}

void GameWidget::BindAdminSignals()
{
    if (!m_uploadPage) return;
    // 注册成功后刷新列表
    connect(ThreadPool::Instance()->GetTCPMgr(), &TCPMgr::SigGameUploadSuccess, this, [this]() {
        m_uploadPage->ResetUI();
        m_launcherPage->RefreshGames();
        CinemaMessageBox::ShowInfo(this, tr("成功"), tr("游戏已成功录入云端！"));
        });
}

void GameWidget::onNavButtonClicked(int index) {
    m_stackedWidget->setCurrentIndex(index);
}