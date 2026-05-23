#pragma once

#include <QtWidgets/QMainWindow>
#include "ui_MainWindow.h"
#include "TcpServer.h"
#include <memory>

class TcpServer;
class AuthService;
class FileTransferService;
class MovieResourceService;
class MoviePlayRecordService;
class WalletService;
class PaymentService;
class GameResourceService;
class OrderManagementService;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private:
    Ui::MainWindowClass ui;
    std::unique_ptr<TcpServer> server;

    // 持有这些服务的强引用，确保其生命周期贯穿程序始终
    std::shared_ptr<AuthService> m_authService;
    std::shared_ptr<FileTransferService> m_fileService;
    std::shared_ptr<MovieResourceService> m_movieService;
    std::shared_ptr<MoviePlayRecordService> m_playRecordService;
    std::shared_ptr<WalletService> m_walletService;
    std::shared_ptr<PaymentService> m_paymentService;
    std::shared_ptr<GameResourceService> m_gameService;
    std::shared_ptr<OrderManagementService> m_orderMgmtService;
};