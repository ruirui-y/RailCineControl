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
    connect(tcp, &TCPMgr::SigOrderFailed, this, &WalletWidget::OnOrderFailed);                          // 绑定订单创建失败的信号
    connect(tcp, &TCPMgr::SigOrderCreated, this, &WalletWidget::OnOrderCreated);                        // 绑定订单创建成功的信号
    connect(tcp, &TCPMgr::SigOrderNotifyReceived, this, &WalletWidget::OnOrderNotifyReceived);          // 绑定支付结果
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

            // 1. 如果之前有残留的弹窗，先清理掉
            if (m_payDialog) {
                m_payDialog->deleteLater();
                m_payDialog = nullptr;
            }

            // 2. 👑 立即创建并弹出窗口！此时内部的 Label 会显示 "二维码生成中..."
            m_payDialog = new CinemaPayDialog(this, m_currentPayName, m_currentPayPrice);

            // 3. 发送网络请求获取二维码
            ServerApi::CreateOrderReq req;
            req.set_goods_id(gId);
            req.set_pay_method("WECHAT");
            ThreadPool::Instance()->GetTCPMgr()->SendProtoMsg(ServerApi::MsgId::ID_CREATE_ORDER_REQ, req);

            // 4. 阻塞显示弹窗 (此时界面已展示，等待底层发来更新信号)
            m_payDialog->exec();
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
        // 增加变绿，扣除变红
        if(rec.points_change() < 0) changeItem->setForeground(QColor("#E81123"));
        else changeItem->setForeground(QColor("#00E676"));
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

void WalletWidget::OnOrderFailed(int errorCode, const QString& errorMsg)
{
    // 👑 如果弹窗正在显示“生成中...”，遇到网络错误必须立刻关闭它！
    if (m_payDialog) {
        m_payDialog->reject(); // 关闭支付弹窗
        m_payDialog = nullptr;
    }

    QString title = tr("充值提示");
    QString displayMsg = errorMsg; // 默认使用服务端传来的兜底消息

    // 👑 根据精细化的错误码，覆盖为针对 C 端用户更友好的 UI 提示文案
    switch (errorCode)
    {
    case ServerApi::ERR_GOODS_OFFLINE:
        displayMsg = tr("该充值套餐已下架或价格发生变动，请刷新页面后重试。");
        break;

    case ServerApi::ERR_PAY_API_FAILED:
        // 中台通讯失败，不给用户看底层报错，只提示网络拥挤
        displayMsg = tr("支付通道拥挤，拉取微信二维码失败，请稍后再试。");
        break;

    case ServerApi::ERR_GENERATE_TOKEN_FAILED:
    case ServerApi::ERR_CREATE_ORDER_FAILED:
    case ServerApi::ERR_SERVER_INTERNAL:
        displayMsg = tr("系统繁忙，创建订单失败，请联系网管或管理员。");
        break;

    default:
        // 其他未知错误，直接展示服务端的 errorMsg
        if (displayMsg.isEmpty()) {
            displayMsg = tr("发生未知错误，充值失败。");
        }
        break;
    }

    // 调用你封装好的 CinemaMessageBox 弹出警告
    CinemaMessageBox::ShowWarning(this, title, displayMsg);
}

// 收到服务器响应：订单创建成功，下发了二维码 URL
void WalletWidget::OnOrderCreated(const QString& orderId, const QString& qrUrl, int expireTime)
{
    // 👑 此时 m_payDialog 已经存在并且正在屏幕上显示着
    if (m_payDialog) {
        // 直接调用我们写好的更新函数，瞬间把 "生成中..." 替换成真实的黑白二维码，并启动倒计时！
        m_payDialog->UpdateQRCode(orderId, qrUrl, expireTime);
    }
}

// 收到服务器异步推送：客户付款成功！（可能是几秒、几分钟后）
void WalletWidget::OnOrderNotifyReceived(const QString& orderId, bool isSuccess, qint64 currentPoints)
{
    // =========================================================================
    // 1. 通用操作：无论成功还是失败，只要订单有了最终状态，立刻掐掉二维码弹窗
    // =========================================================================
    if (m_payDialog) {
        m_payDialog->accept(); // 模拟弹窗正常关闭退出事件循环
        m_payDialog->deleteLater();
        m_payDialog = nullptr;
    }

    // =========================================================================
    // 2. 状态分支：根据服务端推送的最终结果渲染 UI
    // =========================================================================
    if (isSuccess) {
        // A. 刷新左上角的积分余额 UI
        m_lblPoints->setText(QString::number(currentPoints));

        // B. 弹个极其华丽的成功提示！
        QString successMsg = tr("您的订单 [%1] 已支付成功！\n感谢您的充值，当前可用影院金: %2")
            .arg(orderId)
            .arg(currentPoints);
        CinemaMessageBox::ShowInfo(this, tr("充值成功"), successMsg);

        // C. (可选) 重新拉取一次流水列表，刷新下方的表格，让用户立刻看到这笔进账
        // ServerApi::GetFlowReq req;
        // ThreadPool::Instance()->GetTCPMgr()->SendProtoMsg(ServerApi::MsgId::ID_GET_FLOW_REQ, req);
    }
    else {
        // 订单被取消、中台超时关闭、或发生退款
        QString failMsg = tr("订单 [%1] 已失效或被取消。\n如需充值，请重新选择套餐并发起支付。")
            .arg(orderId);
        CinemaMessageBox::ShowWarning(this, tr("支付已失效"), failMsg);
    }
}