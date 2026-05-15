#pragma once

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QTabWidget>
#include <QGridLayout>

class CinemaTableWidget;

class WalletWidget : public QWidget
{
    Q_OBJECT

public:
    explicit WalletWidget(QWidget* parent = nullptr);
    ~WalletWidget();

private:
    // UI 构建与样式分离
    void BuildUI();
    void LoadStyle();

    // 初始化假数据（方便前期看效果）
    void InitMockData();

private:
    // 顶部资产区
    QLabel* m_lblUsername;
    QLabel* m_lblPoints;

    // 底部标签与页面
    QTabWidget* m_tabWidget;
    QWidget* m_rechargePage;
    QWidget* m_flowPage;

    // 充值页的核心布局
    QGridLayout* m_goodsLayout;

    // 流水页的表格
    CinemaTableWidget* m_flowTable;
};