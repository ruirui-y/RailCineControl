#include "MainWindow.h"
#include "Global.h"
#include "AuthService.h"
#include "FileTransferService.h"
#include "MovieResourceService.h"
#include "MoviePlayRecordService.h"
#include "WalletService.h"
#include "PaymentService.h"
#include "GameResourceService.h"
#include "MidPlatformManager.h"
#include "OrderManagementService.h"
#include "ThreadPool.h"


MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
      server(std::make_unique<TcpServer>())
{
    ui.setupUi(this);
    setWindowTitle("RailCineControlServer");

    // =========================================================================
    // 1. 业务服务层实例化 (Service Instantiation)
    // 使用 std::shared_ptr 持有，确保服务生命周期与 MainWindow 一致
    // =========================================================================
    m_authService          = std::make_shared<AuthService>();                                            // 用户认证服务：登录、心跳
    m_fileService          = std::make_shared<FileTransferService>();                                    // 文件传输服务：上传、下载
    m_movieService         = std::make_shared<MovieResourceService>();                                   // 影片管理服务：入库、列表
    m_playRecordService    = std::make_shared<MoviePlayRecordService>();                                 // 播放记录服务：增删查
    m_walletService        = std::make_shared<WalletService>();                                          // 钱包服务：余额、套餐、积分
    m_paymentService       = std::make_shared<PaymentService>(*server);                                  // 支付服务：创建订单、支付对接
    m_gameService          = std::make_shared<GameResourceService>();                                    // 游戏资源服务：版本、游戏库
    m_orderMgmtService     = std::make_shared<OrderManagementService>(*server);                          // 订单管理服务：定时巡检、自动闭单

    // =========================================================================
    // 2. 路由注册 (Service Registration)
    // 每个服务主动向全局 MsgDispatcher 注册自己的业务处理函数 (Callback Routing)
    // =========================================================================
    m_authService->Init();
    m_fileService->Init();
    m_movieService->Init();
    m_playRecordService->Init();
    m_walletService->Init();
    m_paymentService->Init();
    m_gameService->Init();
    
    // 👑 启动订单后台守护任务
    m_orderMgmtService->Init();

    // 👑 优雅的绑定：直接将回调信号交给 PaymentService 处理，TcpServer 彻底清空！
    connect(ThreadPool::Instance()->GetHttpMgr(), &HttpServerMgr::SigPaymentResult,
        m_paymentService.get(), &PaymentService::ProcessPaymentResult);

    connect(MidPlatformManager::Instance().get(), &MidPlatformManager::SigPaymentResult,
        m_paymentService.get(), &PaymentService::ProcessPaymentResult);

    // =========================================================================
    // 3. 基础设施启动 (Infrastructure Startup)
    // 待所有业务逻辑就绪后，启动 TcpServer 开始监听端口，接收客户端请求
    // =========================================================================
    server->StartServer(GlobalConfig::Instance()->GetTcpPort());
    
    qDebug() << u8"🚀 [System] 服务端核心架构启动完毕，已注册" << 8 << u8"个业务模块。";
}

MainWindow::~MainWindow()
{}