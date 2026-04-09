#include "mainWindow.h"
#include <QStackedWidget>
#include "LoginWidget.h"
#include "TCPMgr.h"
#include "ControlHubWindow.h"
#include "Titlebar.h"
#include "SignalSend.h"
#include "JsonTool.h"
#include "Global.h"
#include "ThreadPool.h"
#include "Macro.h"
#include "UserMgr.h"
#include "UdpManager.h"
#include "LocalStreamServer.h"

mainWindow::mainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    ui.setupUi(this);

    m_pages = new QStackedWidget(this);
    setCentralWidget(m_pages);

    _LoginWidget = new LoginWidget(this);
    
    m_pages->addWidget(_LoginWidget);
    m_pages->setCurrentWidget(_LoginWidget);

    setFixedSize(_LoginWidget->size());

    setWindowTitle("Demand Station");

    BindSlots();
    hide();
    MoveManagerToThread();
}

mainWindow::~mainWindow()
{
}

void mainWindow::BindSlots()
{
    connect(TCPMgr::Instance().get(), &TCPMgr::SigLoginSuccess, this, &mainWindow::SlotSwitchToControlHubWidget);
    connect(TCPMgr::Instance().get(), &TCPMgr::SigConnectClose, this, &mainWindow::SlotSwitchToLoginWidget);
}

void mainWindow::SetFrameless(bool on)
{
    // 只切换是否无边框，其它保持默认
    setWindowFlag(Qt::FramelessWindowHint, on);

    // 登录页通常需要系统菜单/最小化/关闭按钮，游戏页则不需要
    setWindowFlag(Qt::WindowSystemMenuHint, !on);
    setWindowFlag(Qt::WindowMinMaxButtonsHint, !on);
    setWindowFlag(Qt::WindowCloseButtonHint, true);

    // 重新显示以应用新 flags（很重要）
    if (isVisible()) showNormal(); else show();
}

void mainWindow::MoveManagerToThread()
{
    ThreadPool::Instance()->DispatchToWorker(
        []()
        {
            TCPMgr::Instance();
            qDebug() << "TCPMgr created in thread:" << QThread::currentThread();
        });

    ThreadPool::Instance()->DispatchToWorker(
        []()
        {
            UdpManager::Instance();
            qDebug() << "UdpManager created in thread:" << QThread::currentThread();
        });

    ThreadPool::Instance()->DispatchToWorker(
        []()
        {
            LocalStreamServer::Instance()->StartServer(LOCAL_HTTP_SERVER_PORT);
            qDebug() << "LocalStreamServer created in thread:" << QThread::currentThread();
        });
}

void mainWindow::SlotSwitchToLoginWidget()
{
    // 有边框
    SetFrameless(false);
    setAutoFillBackground(true);                                // 登录页一般不需要透明背景

    // 清除配置文件
    JsonTool::Instance()->clearJsonFile(LoginConfigPath);

    // 切换界面
    m_pages->removeWidget(_ControlHub);
    _ControlHub->deleteLater();
    _ControlHub = nullptr;

    // 清空用户数据
    UserMgr::Instance()->ClearUser();

    m_pages->setCurrentWidget(_LoginWidget);
    emit _LoginWidget->sig_connect_tcp();
    _LoginWidget->EnableBtn(true);

    setFixedSize(_LoginWidget->size());

    qDebug() << "Switch to LoginWidget";
}

void mainWindow::SlotSwitchToControlHubWidget()
{
    if (_ControlHub == nullptr)
    {
        _ControlHub = new ControlHubWindow(this);
        m_pages->addWidget(_ControlHub);
    }

    // 无边框 + 自绘背景
    SetFrameless(true);
    setAutoFillBackground(false);

    m_pages->setCurrentWidget(_ControlHub);

    // 获取标题栏
    TitleBar* title = _ControlHub->GetTitle();
    connect(title, &TitleBar::minimizeRequested, this, &QWidget::showMinimized);                                // 最小化
    connect(title, &TitleBar::closeRequested, this, &mainWindow::CloseWidget);                                  // 关闭

    setFixedSize(QSize(1100, 800));
    show();
}

void mainWindow::CloseWidget()
{
    TCPMgr::Instance()->SetInitiateDisCon(true);                                                                // 主动断开连接
    close();
}