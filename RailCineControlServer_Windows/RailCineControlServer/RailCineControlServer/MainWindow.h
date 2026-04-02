#pragma once

#include <QtWidgets/QMainWindow>
#include "ui_MainWindow.h"
#include "TcpServer.h"
#include <memory>

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private:
    Ui::MainWindowClass ui;
    std::unique_ptr<TcpServer> server;
};