#include "WalletWidget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QFile>
#include "CinemaTableWidget.h"
#include "CinemaPayDialog.h"


WalletWidget::WalletWidget(QWidget* parent)
    : QWidget(parent)
{
    BuildUI();
    LoadStyle();
    InitMockData(); // 注入假数据看 UI 效果
}

WalletWidget::~WalletWidget() {}

void WalletWidget::BuildUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 20, 20, 20);
    mainLayout->setSpacing(20);

    // ==========================================
    // 1. 顶部资产卡片
    // ==========================================
    QWidget* topCard = new QWidget(this);
    topCard->setObjectName("WalletTopCard");
    QHBoxLayout* topLayout = new QHBoxLayout(topCard);
    topLayout->setContentsMargins(30, 20, 30, 20);

    QVBoxLayout* infoLayout = new QVBoxLayout();
    m_lblUsername = new QLabel(tr("尊敬的用户：Admin"), this);
    m_lblUsername->setObjectName("WalletUsername");

    QLabel* pointsTitle = new QLabel(tr("当前可用积分 (点)"), this);
    pointsTitle->setObjectName("WalletPointsTitle");

    m_lblPoints = new QLabel("0", this);
    m_lblPoints->setObjectName("WalletPointsValue");

    infoLayout->addWidget(m_lblUsername);
    infoLayout->addWidget(pointsTitle);
    infoLayout->addWidget(m_lblPoints);

    topLayout->addLayout(infoLayout);
    topLayout->addStretch(1); // 把字挤到左边，右边留白可以放个金币图标

    mainLayout->addWidget(topCard);

    // ==========================================
    // 2. 中下部：标签页管理 (充值 / 流水)
    // ==========================================
    m_tabWidget = new QTabWidget(this);
    m_tabWidget->setObjectName("WalletTab");

    // --- 充值页 ---
    m_rechargePage = new QWidget();
    m_goodsLayout = new QGridLayout(m_rechargePage);
    m_goodsLayout->setContentsMargins(20, 20, 20, 20);
    m_goodsLayout->setSpacing(60);
    m_goodsLayout->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_tabWidget->addTab(m_rechargePage, tr("💰 充值特惠"));

    // --- 流水页 ---
    m_flowPage = new QWidget();
    QVBoxLayout* flowLayout = new QVBoxLayout(m_flowPage);
    m_flowTable = new CinemaTableWidget(this);
    m_flowTable->setHeaders({ tr("时间"), tr("类型"), tr("变动积分"), tr("交易后余额"), tr("关联单号") });

    flowLayout->addWidget(m_flowTable);
    m_tabWidget->addTab(m_flowPage, tr("📜 资金流水"));

    mainLayout->addWidget(m_tabWidget);
}

void WalletWidget::InitMockData()
{
    // 1. 模拟资产
    m_lblPoints->setText("1,250");

    // 2. 模拟商品卡片填充
    QStringList goodsNames = { "10元", "首充 50元", "尊享 100元", "超级 500元" };
    QStringList goodsPoints = { "100 积分", "600 积分", "1500 积分", "8000 积分" };
    QStringList prices = { "￥10.00", "￥50.00", "￥100.00", "￥500.00" }; // 增加对应的真实人民币价格

    for (int i = 0; i < 4; ++i) {
        QPushButton* goodsBtn = new QPushButton();
        goodsBtn->setObjectName("GoodsCardBtn");
        goodsBtn->setFixedSize(160, 100);

        QVBoxLayout* btnLayout = new QVBoxLayout(goodsBtn);
        QLabel* pLbl = new QLabel(goodsPoints[i], goodsBtn);
        pLbl->setObjectName("GoodsPointsLabel");
        pLbl->setAlignment(Qt::AlignCenter);

        QLabel* nLbl = new QLabel(goodsNames[i], goodsBtn);
        nLbl->setObjectName("GoodsNameLabel");
        nLbl->setAlignment(Qt::AlignCenter);

        btnLayout->addWidget(pLbl);
        btnLayout->addWidget(nLbl);
        btnLayout->setAlignment(Qt::AlignCenter);

        // 每行放 3 个商品，计算行列
        m_goodsLayout->addWidget(goodsBtn, i / 4, i % 4);

        QString name = goodsNames[i] + " (" + goodsPoints[i] + ")";
        QString price = prices[i];
        connect(goodsBtn, &QPushButton::clicked, this, [this, name, price]() {
            // 实例化并弹出我们的自定义支付窗口！
            CinemaPayDialog payDialog(this, name, price);
            payDialog.exec();

            // TODO: 后续在这里处理如果 payDialog.exec() 返回成功后的刷新流水逻辑
            });
    }

    // 3. 模拟流水数据
    m_flowTable->setRowCount(2);
    m_flowTable->setItem(0, 0, new QTableWidgetItem("2026-05-15 14:00"));
    m_flowTable->setItem(0, 1, new QTableWidgetItem("充值"));
    m_flowTable->setItem(0, 2, new QTableWidgetItem("+600"));
    m_flowTable->setItem(0, 3, new QTableWidgetItem("1250"));
    m_flowTable->setItem(0, 4, new QTableWidgetItem("PAY20260515_001"));

    m_flowTable->setItem(1, 0, new QTableWidgetItem("2026-05-15 14:30"));
    m_flowTable->setItem(1, 1, new QTableWidgetItem("看电影"));
    m_flowTable->setItem(1, 2, new QTableWidgetItem("-50"));
    m_flowTable->setItem(1, 3, new QTableWidgetItem("1200"));
    m_flowTable->setItem(1, 4, new QTableWidgetItem("MOVIE_9921"));
}

void WalletWidget::LoadStyle()
{
    // 这里采用分离式 QSS 挂载，或者你直接把它加到你的全局 main.qss 里也可以
    QFile file(":/MiNi/Style/Wallet.qss"); // 假设你放在资源文件中
    if (file.open(QFile::ReadOnly)) {
        this->setStyleSheet(QString::fromUtf8(file.readAll()));
        file.close();
    }
}