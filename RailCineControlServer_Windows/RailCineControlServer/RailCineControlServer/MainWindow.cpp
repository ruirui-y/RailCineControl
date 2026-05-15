#include "MainWindow.h"
#include "Global.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
    server(std::make_unique<TcpServer>())
{
    ui.setupUi(this);
    setWindowTitle("RailCineControlServer");
    server->StartServer(GlobalConfig::Instance()->GetTcpPort());
}

MainWindow::~MainWindow()
{}