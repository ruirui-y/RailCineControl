#include "MainWindow.h"

#define LISTEN_PORT                                 5486

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
    server(std::make_unique<TcpServer>())
{
    ui.setupUi(this);
    setWindowTitle("RailCineControlServer");
    server->StartServer(LISTEN_PORT);
}

MainWindow::~MainWindow()
{}