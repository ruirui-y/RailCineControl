#include "WalletWidget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QFile>
#include "CinemaTableWidget.h"
#include "CinemaPayDialog.h"
#include "CinemaMessageBox.h"
#include "ThreadPool.h"   


WalletWidget::WalletWidget(QWidget* parent)
    : QWidget(parent)
{
    BuildUI();

    // 绑定用户钱包信息和套餐以及支付记录
    connect(ThreadPool::Instance()->GetTCPMgr(), &TCPMgr::SigWalletReceived, this, &WalletWidget::OnWalletReceived);
    connect(ThreadPool::Instance()->GetTCPMgr(), &TCPMgr::SigGoodsListReceived, this, &WalletWidget::OnGoodsListReceived);
    connect(ThreadPool::Instance()->GetTCPMgr(), &TCPMgr::SigFlowRecordsReceived, this, &WalletWidget::OnFlowRecordsReceived);

    // 绑定支付信号
    TCPMgr* tcp = ThreadPool::Instance()->GetTCPMgr();
    connect(tcp, &TCPMgr::SigOrderCreated, this, &WalletWidget::OnOrderCreated);
    connect(tcp, &TCPMgr::SigOrderPaid, this, &WalletWidget::OnOrderPaid);
}

WalletWidget::~WalletWidget() {}

void WalletWidget::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);

    // 1. 请求钱包余额
    ServerApi::GetWalletReq wReq;
    ThreadPool::Instance()->GetTCPMgr()->SendProtoMsg(ServerApi::MsgId::ID_GET_WALLET_REQ, wReq);

    // 2. 请求商品列表
    ServerApi::GetGoodsReq gReq;
    ThreadPool::Instance()->GetTCPMgr()->SendProtoMsg(ServerApi::MsgId::ID_GET_GOODS_REQ, gReq);

    // 👑 3. 请求拉取资金流水 (拉最新的100条)
    ServerApi::GetFlowReq fReq;
    fReq.set_page_index(1);
    fReq.set_page_size(100);
    ThreadPool::Instance()->GetTCPMgr()->SendProtoMsg(ServerApi::MsgId::ID_GET_FLOW_REQ, fReq);
}

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

// =========================================================================================
// 👑 核心异步网络回调处理
// =========================================================================================

// 收到服务器响应：用户积分余额
void WalletWidget::OnWalletReceived(const ServerApi::GetWalletRsp& rsp)
{
    // 格式化数字，例如 1250 -> "1,250"
    m_lblPoints->setText(QLocale(QLocale::English).toString((long long)rsp.current_points()));
}

// 收到服务器响应：商品列表
void WalletWidget::OnGoodsListReceived(const ServerApi::GetGoodsRsp& rsp)
{
    // 1. 先清空旧的布局控件（如果有的话）
    QLayoutItem* child;
    while ((child = m_goodsLayout->takeAt(0)) != nullptr) {
        if (child->widget()) child->widget()->deleteLater();
        delete child;
    }

    // 2. 循环创建动态商品卡片
    for (int i = 0; i < rsp.goods_list_size(); ++i)
    {
        const auto& info = rsp.goods_list(i);

        QPushButton* goodsBtn = new QPushButton();
        goodsBtn->setObjectName("GoodsCardBtn");
        goodsBtn->setFixedSize(160, 100);

        QVBoxLayout* btnLayout = new QVBoxLayout(goodsBtn);

        // 积分标签 (如: "100 积分")
        QLabel* pLbl = new QLabel(QString::number(info.points_reward()) + u8" 积分", goodsBtn);
        pLbl->setObjectName("GoodsPointsLabel");

        // 名字标签 (如: "10元套餐")
        QLabel* nLbl = new QLabel(QString::fromStdString(info.goods_name()), goodsBtn);
        nLbl->setObjectName("GoodsNameLabel");
        nLbl->setAlignment(Qt::AlignCenter);
        nLbl->setWordWrap(true);

        btnLayout->addWidget(pLbl);
        btnLayout->addWidget(nLbl);
        btnLayout->setAlignment(Qt::AlignCenter);

        pLbl->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        nLbl->setAttribute(Qt::WA_TransparentForMouseEvents, true);

        m_goodsLayout->addWidget(goodsBtn, i / 4, i % 4);

        // 绑定点击支付逻辑
        uint64_t gId = info.goods_id();
        QString displayName = QString::fromStdString(info.goods_name());
        QString priceStr = QString("￥%1").arg(info.price_cents() / 100.0, 0, 'f', 2);

        connect(goodsBtn, &QPushButton::clicked, this, [this, gId, displayName, priceStr]() {
            m_currentPayName = displayName;
            m_currentPayPrice = priceStr;

            ServerApi::CreateOrderReq req;
            req.set_goods_id(gId);
            req.set_pay_method("WECHAT");
            ThreadPool::Instance()->GetTCPMgr()->SendProtoMsg(ServerApi::MsgId::ID_CREATE_ORDER_REQ, req);
            });
    }
}

// 收到服务器响应：订单流水记录
void WalletWidget::OnFlowRecordsReceived(const ServerApi::GetFlowRsp& rsp)
{
    // 清空现有表格内容，并重新设置行数
    m_flowTable->setRowCount(0);
    m_flowTable->setRowCount(rsp.records_size());

    for (int i = 0; i < rsp.records_size(); ++i) {
        const auto& rec = rsp.records(i);

        // 解析枚举类型
        QString flowTypeStr;
        switch (rec.flow_type()) {
        case 1: flowTypeStr = tr("充值收入"); break;
        case 2: flowTypeStr = tr("点播消费"); break;
        case 3: flowTypeStr = tr("游戏消费"); break;
        case 4: flowTypeStr = tr("系统赠送"); break;
        default: flowTypeStr = tr("未知交易"); break;
        }

        // 处理带符号的积分变动 (比如: "+600" 或 "-50")
        QString pointsChangeStr = (rec.points_change() > 0 ? "+" : "") + QString::number(rec.points_change());

        // 装填每一列数据
        m_flowTable->setItem(i, 0, new QTableWidgetItem(QString::fromStdString(rec.create_time())));
        m_flowTable->setItem(i, 1, new QTableWidgetItem(flowTypeStr));

        auto* changeItem = new QTableWidgetItem(pointsChangeStr);
        // 如果想骚一点，这里可以直接改变颜色的警示度，增加就变绿，扣除就变红
        // if(rec.points_change() < 0) changeItem->setForeground(QColor("#E81123"));
        // else changeItem->setForeground(QColor("#00E676"));
        m_flowTable->setItem(i, 2, changeItem);

        m_flowTable->setItem(i, 3, new QTableWidgetItem(QString::number(rec.balance_after())));
        m_flowTable->setItem(i, 4, new QTableWidgetItem(QString::fromStdString(rec.associated_id())));

        // 统一让所有单元格文字居中显示，更好看
        for (int col = 0; col < 5; ++col) {
            if (m_flowTable->item(i, col)) {
                m_flowTable->item(i, col)->setTextAlignment(Qt::AlignCenter);
            }
        }
    }
}

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