#include "WalletWidget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QFile>
#include "CinemaTableWidget.h"
#include "CinemaPayDialog.h"
#include "CinemaMessageBox.h"
#include "ThreadPool.h"    
#include "server_msg.pb.h"


WalletWidget::WalletWidget(QWidget* parent)
    : QWidget(parent)
{
    BuildUI();
    InitMockData(); // 注入假数据看 UI 效果

    // 绑定支付信号
    TCPMgr* tcp = ThreadPool::Instance()->GetTCPMgr();
    connect(tcp, &TCPMgr::SigOrderCreated, this, &WalletWidget::OnOrderCreated);
    connect(tcp, &TCPMgr::SigOrderPaid, this, &WalletWidget::OnOrderPaid);
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
    QList<uint64_t> goodsIds = { 1001, 1002, 1003, 1004 };
    QStringList goodsNames = { "10元", "首充 50元", "尊享 100元", "超级 500元" };
    QStringList goodsPoints = { "100 积分", "600 积分", "1500 积分", "8000 积分" };
    QStringList prices = { "￥10.00", "￥50.00", "￥100.00", "￥500.00" }; // 增加对应的真实人民币价格

    for (int i = 0; i < 4; ++i) 
    {
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

        // 让标签对鼠标透明，防止阻挡按钮点击
        pLbl->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        nLbl->setAttribute(Qt::WA_TransparentForMouseEvents, true);

        m_goodsLayout->addWidget(goodsBtn, i / 4, i % 4);

        // 提取要用到的变量，由于 Lambda 的特性，必须按值捕获
        uint64_t gId = goodsIds[i];
        QString name = goodsNames[i] + " (" + goodsPoints[i] + ")";
        QString price = prices[i];

        // 👑 真实网络请求流：点击 -> 封包 -> 发送
        connect(goodsBtn, &QPushButton::clicked, this, [this, gId, name, price]() {
            // 先把当前点击的商品名称和价格暂存起来，等服务端返回二维码后再用
            m_currentPayName = name;
            m_currentPayPrice = price;

            // 组装 Protobuf 请求
            ServerApi::CreateOrderReq req;
            req.set_goods_id(gId);
            req.set_pay_method("WECHAT"); // 假定默认走微信

            // 通过 TCPMgr 发送给服务器
            ThreadPool::Instance()->GetTCPMgr()->SendProtoMsg(ServerApi::MsgId::ID_CREATE_ORDER_REQ, req);

            // 可选：此时可以将主界面加个转圈圈动画，防止网络卡顿时用户狂点
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

// =========================================================================================
// 👑 核心异步网络回调处理
// =========================================================================================

// 收到服务器响应：订单创建成功，下发了二维码 URL
void WalletWidget::OnOrderCreated(const QString& orderId, const QString& qrUrl, int expireTime)
{
    // 如果之前有残留的弹窗，先干掉
    if (m_payDialog) {
        m_payDialog->deleteLater();
        m_payDialog = nullptr;
    }

    // 此时才安全地弹出带有刚才保存名字的支付窗口！
    m_payDialog = new CinemaPayDialog(this, m_currentPayName, m_currentPayPrice);

    // TODO: 集成 qrencode 库后，调用类似 m_payDialog->SetQRCode(qrUrl);
    // TODO: 可以把 expireTime 传给弹窗去跑倒计时

    m_payDialog->exec(); // 阻塞弹窗，等待用户扫码
}

// 收到服务器异步推送：客户付款成功！（可能是几秒、几分钟后）
void WalletWidget::OnOrderPaid(const QString& orderId, qint64 newPoints)
{
    // 1. 关闭正在展示的二维码支付弹窗
    if (m_payDialog) {
        m_payDialog->accept(); // 模拟用户点击了确定/正常关闭
        m_payDialog->deleteLater();
        m_payDialog = nullptr;
    }

    // 2. 刷新左上角的积分余额 UI
    m_lblPoints->setText(QString::number(newPoints));

    // 3. 弹个极其华丽的成功提示！
    CinemaMessageBox::ShowInfo(this, tr("支付成功"), tr("感谢您的充值，当前可用积分: %1").arg(newPoints));

    // 4. (可选) 重新拉取一次流水列表，更新下方的表格
}