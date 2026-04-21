#include "PlaybackPage.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>
#include <QGuiApplication>
#include <QScreen>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFileInfo>
#include <QDir>
#include <filesystem> 
#include <QMessageBox>
#include <QTimer>
#include <QDebug>
#include "TCPMgr.h"
#include "Global.h"
#include "JsonTool.h"
#include "LocalStreamServer.h"

PlaybackPage::PlaybackPage(QWidget* parent) : QWidget(parent)
{
    setAttribute(Qt::WA_StyledBackground, true);
    setObjectName("PlaybackPage");

    m_player = new QMediaPlayer(this);                                      // 实例化多媒体引擎
    m_player->setVolume(100);                                               // 默认最大音量

    m_videoWidget = new QVideoWidget();                                     // 实例化投屏幕布
    m_videoWidget->setStyleSheet("background-color: black;");
    m_videoWidget->setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);

    m_player->setVideoOutput(m_videoWidget);                                // 引擎绑定幕布

    BuildUI();                                                              // 搭建UI

    // 监听进度条
    connect(m_player, &QMediaPlayer::positionChanged, this, [this](qint64 pos) {
        int s = (pos / 1000) % 60;
        int m = (pos / 60000) % 60;
        int h = (pos / 3600000) % 24;
        QTime time(h, m, s);
        m_countdownLabel->setText(time.toString("HH:mm:ss"));               // 实时更新 LCD 时间
        });
    
    // 监听播放器状态，捕捉“影片自然播放完毕”的瞬间
    connect(m_player, &QMediaPlayer::mediaStatusChanged, this, &PlaybackPage::onMediaStatusChanged);

    // 跨页/跨层监听数据返回
    connect(TCPMgr::Instance().get(), &TCPMgr::SigMovieListReceived, this, &PlaybackPage::onMovieListReceived);
    // 👑 绑定下载引擎的四大核心信号
    connect(TCPMgr::Instance().get(), &TCPMgr::SigCoverDownloaded, this, &PlaybackPage::onCoverDownloaded);
    connect(TCPMgr::Instance().get(), &TCPMgr::SigDownloadFailed, this, &PlaybackPage::onDownloadFailed);
    connect(TCPMgr::Instance().get(), &TCPMgr::SigDownloadProgress, this, &PlaybackPage::onDownloadProgress);
    connect(TCPMgr::Instance().get(), &TCPMgr::SigDownloadFinished, this, &PlaybackPage::onDownloadFinished);

    // 页面初始化时，自动拉取一次
    LoadMoviesFromServer();
}

PlaybackPage::~PlaybackPage() {}

void PlaybackPage::BuildUI()
{
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    // ================= 1. 影片列表区 =================
    m_movieList = new QListWidget(this);
    m_movieList->setViewMode(QListView::IconMode);
    m_movieList->setResizeMode(QListView::Adjust);
    m_movieList->setSpacing(15);

    connect(m_movieList, &QListWidget::currentItemChanged,
        this, &PlaybackPage::onMovieSelected);

    // ================= 2. 底部播控区 =================
    QFrame* controlPanel = new QFrame(this);
    controlPanel->setFixedHeight(80);
    QHBoxLayout* controlLayout = new QHBoxLayout(controlPanel);

    m_countdownLabel = new QLabel("00:00:00", controlPanel);
    m_countdownLabel->setObjectName("statusLcd");

    auto createCtrlBtn = [&](const QString& text) -> QPushButton* {
        QPushButton* btn = new QPushButton(text, controlPanel);
        btn->setObjectName("controlBtn");
        btn->setFixedSize(100, 45);
        return btn;
        };

    m_btnPlay = createCtrlBtn(u8"▶ 播放");
    m_btnDownload = createCtrlBtn(u8"📥 下载到本地");
    m_btnDownload->setFixedSize(150, 45);
    m_btnPause = createCtrlBtn(u8"⏸ 暂停/继续");
    m_btnRewind = createCtrlBtn(u8"⏪ 快退");
    m_btnForward = createCtrlBtn(u8"⏩ 快进");
    m_btnStop = createCtrlBtn(u8"⏹ 强制结束");

    // 💡 初始状态：什么都没选中，全部隐藏！
    m_btnPlay->hide();
    m_btnDownload->hide();
    m_btnPause->hide();
    m_btnRewind->hide();
    m_btnForward->hide();
    m_btnStop->hide();

    controlLayout->addWidget(m_countdownLabel);
    controlLayout->addStretch();
    controlLayout->addWidget(m_btnRewind);
    controlLayout->addWidget(m_btnPlay);
    controlLayout->addWidget(m_btnDownload);                                                    // 下载按钮 (和播放按钮互斥显示)
    controlLayout->addWidget(m_btnPause);
    controlLayout->addWidget(m_btnForward);
    controlLayout->addSpacing(20);
    controlLayout->addWidget(m_btnStop);

    layout->addWidget(m_movieList, 1);
    layout->addWidget(controlPanel);

    connect(m_btnPlay, &QPushButton::clicked, this, &PlaybackPage::onPlayClicked);
    connect(m_btnDownload, &QPushButton::clicked, this, &PlaybackPage::onDownloadClicked);
    connect(m_btnPause, &QPushButton::clicked, this, &PlaybackPage::onPauseClicked);
    connect(m_btnStop, &QPushButton::clicked, this, &PlaybackPage::onStopClicked);
    connect(m_btnForward, &QPushButton::clicked, this, &PlaybackPage::onForwardClicked);
    connect(m_btnRewind, &QPushButton::clicked, this, &PlaybackPage::onRewindClicked);

    // ================= 3. 最底部：全局下载进度条 =================
    m_downloadProgress = new QProgressBar(this);
    m_downloadProgress->setObjectName("Progress");
    m_downloadProgress->setFixedHeight(15);
    m_downloadProgress->setRange(0, 100);
    m_downloadProgress->setValue(0);
    m_downloadProgress->setTextVisible(false);
    m_downloadProgress->hide();                                                                 // 默认隐藏，开始下载时显示

    // 组装总 Layout
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_movieList, 1);
    layout->addWidget(controlPanel);
    layout->addWidget(m_downloadProgress);                                                      // 放在最底下
}

void PlaybackPage::LoadMoviesFromJson()
{
    QString configPath = MovieConfigPath;                                   // 依赖 Global.h 的宏
    QJsonDocument doc;
    QString errMsg;

    if (JsonTool::Instance()->readJsonFile(configPath, doc, &errMsg)) {
        if (doc.isArray()) {
            QJsonArray arr = doc.array();
            for (int i = 0; i < arr.size(); ++i) {
                QJsonObject obj = arr[i].toObject();
                QString name = obj["name"].toString();
                QString status = obj["status"].toString();
                QString path = obj["path"].toString();
                //AddMovieCard(name, status, path);                           // 动态生成卡片
            }
        }
    }
    else {
        qDebug() << u8"加载影片配置失败：" << errMsg;
    }
}

void PlaybackPage::LoadMoviesFromServer()
{
    ServerApi::GetMovieListReq req;
    req.set_page_index(1);                                                                  // 默认拉取第 1 页
    req.set_page_size(100);                                                                 // 默认拉取 100 条 (够跑满目前绝大多数场景了)

    // 发送拉取请求
    TCPMgr::Instance()->SendProtoMsg(ServerApi::MsgId::ID_GET_MOVIE_LIST_REQ, req);
}

void PlaybackPage::RefreshMovies()
{
    m_movieList->clear();                                                                   // 刷新前先清空旧的卡片
    LoadMoviesFromServer();                                                                 // 重新走一遍拉取和渲染流程
}

void PlaybackPage::AddMovieCard(uint64_t id, const QString& name, const QString& coverUrl,
    const QString& localPath, bool isDownloaded, int playStatus,
    const QString& fileMd5, uint64_t expectedSize, const QString& encryptKey)
{
    QFrame* card = new QFrame();
    card->setObjectName("gameCard");
    card->setProperty("selected", false);

    QVBoxLayout* layout = new QVBoxLayout(card);

    // 1. 海报渲染
    QLabel* cover = new QLabel(card);
    cover->setObjectName("cardCover");
    cover->setFixedSize(160, 220);
    // (注：这里假设单机同硬盘测试，如果是跨网，海报也需要一套本地校验与异步下载)
    QPixmap pixmap(coverUrl);
    if (!pixmap.isNull()) {
        cover->setPixmap(pixmap.scaled(160, 220, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
    }
    else {
        cover->setText(u8"海报加载中...");
        cover->setAlignment(Qt::AlignCenter);
    }

    // 2. 标题
    QLabel* title = new QLabel(name, card);
    title->setObjectName("cardTitle");

    // 3. 状态子标题 (动态组合状态)
    QString statusText;
    if (isDownloaded) {
        statusText = u8"🟢 已下载";
    }
    else {
        statusText = u8"☁️ 未下载";
    }

    if (playStatus == 1) {
        statusText += u8" | ▶️ 播放中";
    }

    QLabel* subTitle = new QLabel(statusText, card);
    subTitle->setObjectName("cardSub");

    // 👑 绝杀：利用 Qt 动态属性机制，将状态交给外部 QSS 去渲染
    subTitle->setProperty("downloaded", isDownloaded);

    layout->addWidget(cover);
    layout->addWidget(title);
    layout->addWidget(subTitle);

    // 4. 将所有隐藏数据绑定到 Item 上
    QListWidgetItem* item = new QListWidgetItem(m_movieList);
    item->setSizeHint(QSize(180, 300));

    // 👑 高级技巧：利用 Qt::UserRole 藏入极度详细的运行上下文
    item->setData(Qt::UserRole, id);                                    // 影片的唯一 ID
    item->setData(Qt::UserRole + 1, name);                              // 影片名
    item->setData(Qt::UserRole + 2, localPath);                         // 不管下没下完，这就是它该在的本地路径
    item->setData(Qt::UserRole + 3, isDownloaded);                      // 是否已下载
    item->setData(Qt::UserRole + 4, fileMd5);                           // 下载请求凭证 MD5
    item->setData(Qt::UserRole + 5, expectedSize);                      // 用于计算进度条
    item->setData(Qt::UserRole + 6, encryptKey);                        // 运行时的专属解密密钥

    m_movieList->addItem(item);
    m_movieList->setItemWidget(item, card);
}

void PlaybackPage::SwitchControlPanelState(bool isDownloaded)
{
    if (isDownloaded) {
        // 🟢 状态 A：已下载 (展示所有播控按钮，藏起下载按钮)
        m_btnDownload->hide();

        m_btnPlay->show();
        m_btnPlay->setEnabled(true);
        m_btnPause->show();
        m_btnRewind->show();
        m_btnForward->show();
        m_btnStop->show();
    }
    else {
        // ☁️ 状态 B：未下载 (清空所有播控按钮，只保留一个下载按钮)
        m_btnPlay->hide();
        m_btnPause->hide();
        m_btnRewind->hide();
        m_btnForward->hide();
        m_btnStop->hide();

        m_btnDownload->show();
        m_btnDownload->setEnabled(true);
        m_btnDownload->setText(u8"📥 下载到本地"); // 确保复位文字
    }
}

void PlaybackPage::onMovieSelected(QListWidgetItem* current, QListWidgetItem* previous)
{
    // 👑 状态锁拦截：如果正在下载，强行剥夺用户的切换能力，直到下载完成！
    if (m_isDownloading) 
    {
        // 让 UI 的选中虚线强制弹回上一个选项 (保持视觉一致性)
        if (previous) {
            m_movieList->setCurrentItem(previous);
        }
        return;
    }

    if (previous) {
        QWidget* preWidget = m_movieList->itemWidget(previous);
        preWidget->setProperty("selected", false);
        preWidget->style()->unpolish(preWidget);
        preWidget->style()->polish(preWidget);
    }

    if (current) {
        QWidget* curWidget = m_movieList->itemWidget(current);
        curWidget->setProperty("selected", true);
        curWidget->style()->unpolish(curWidget);
        curWidget->style()->polish(curWidget);

        // 提取影片数据
        m_selectedMovieName = current->data(Qt::UserRole + 1).toString();
        m_selectedMoviePath = current->data(Qt::UserRole + 2).toString();
        m_selectedIsDownloaded = current->data(Qt::UserRole + 3).toBool();
        m_selectedMovieMd5 = current->data(Qt::UserRole + 4).toString();
        m_selectedMovieSize = current->data(Qt::UserRole + 5).toULongLong();
        m_selectedMovieEncryptKey = current->data(Qt::UserRole + 6).toString();

        // 👑 智能显隐：互斥展示 播放/下载 按钮
        SwitchControlPanelState(m_selectedIsDownloaded);
    }
}

void PlaybackPage::onPlayClicked()
{
    if (m_selectedMoviePath.isEmpty()) return;

    if (m_player->state() == QMediaPlayer::PausedState) {
        m_player->play();                                                   // 恢复播放
        return;
    }

    m_playStartTime = QDateTime::currentDateTime();                         // 记录播放开始的真实时间
    m_isPlayingRecord = true;

    // ==============================================================================
    // 👑 终极流媒体魔法开始
    // ==============================================================================

    // 1. 告诉你的暗网代理：我要播这个物理文件了，这是它的解密钥匙
    auto server = LocalStreamServer::Instance();
    QString safePath = m_selectedMoviePath;
    QString safeKey = m_selectedMovieEncryptKey;
    QMetaObject::invokeMethod(server.get(), [server, safePath, safeKey]()
        {
            // 这段代码才会在 WorkerThread-2 中安全执行！
            server->SetCurrentMedia(safePath, safeKey);
        }, Qt::QueuedConnection);

    // 2. 欺骗傻白甜 QMediaPlayer：别去读硬盘了，去给我请求这个网址！
    // GetPlayUrl() 返回的其实就是 "http://127.0.0.1:12345/play_secure.mp4"
    m_player->setMedia(QUrl(LocalStreamServer::Instance()->GetPlayUrl()));
    // ==============================================================================

    QList<QScreen*> screens = QGuiApplication::screens();
    if (screens.count() > 1) {
        QRect screenGeometry = screens.at(1)->geometry();                   // 获取副屏坐标
        m_videoWidget->setGeometry(screenGeometry);                         // 直接覆盖副屏坐标
    }
    else {
        m_videoWidget->setGeometry(screens.at(0)->geometry());              // 单屏保底
    }

    m_videoWidget->show();
    m_player->play();
}

void PlaybackPage::onDownloadClicked()
{
    // 防御性拦截
    if (m_selectedMovieMd5.isEmpty()) return;

    // 1. 锁定界面与按钮 (防手贱连点)
    m_currentDownloadBytes = 0;
    m_isDownloading = true;
    m_btnDownload->setEnabled(false);
    m_btnDownload->setText(u8"正在建立传输通道...");

    m_downloadProgress->setValue(0);
    m_downloadProgress->show();

    // 2. 准备本地文件 (创建或清空旧的残缺文件)
    QFile localFile(m_selectedMoviePath);
    if (!localFile.open(QIODevice::WriteOnly)) {
        m_btnDownload->setText(u8"本地磁盘错误");
        return;
    }
    localFile.close(); // 清空完毕，马上关闭，等待接收数据时用 Append 模式写入

    // 3. 组装下载请求：给我这个 MD5 视频的第 0 块数据！
    ServerApi::DownloadChunkReq req;
    req.set_file_md5(m_selectedMovieMd5.toStdString());
    req.set_chunk_index(0); // 🚀 泵机启动，抽第一口水

    // 发射给服务器
    TCPMgr::Instance()->SendProtoMsg(ServerApi::MsgId::ID_DOWNLOAD_CHUNK_REQ, req);

    qDebug() << u8"🚀 发起下载任务，请求 MD5:" << m_selectedMovieMd5 << u8"块索引: 0";
}

void PlaybackPage::onPauseClicked()
{
    if (m_player->state() == QMediaPlayer::PlayingState) {
        m_player->pause();
    }
    else if (m_player->state() == QMediaPlayer::PausedState) {
        m_player->play();
    }
}

void PlaybackPage::onForwardClicked()
{
    if (m_player) {
        qint64 currentPos = m_player->position();
        qint64 duration = m_player->duration();
        qint64 targetPos = currentPos + 15000;                              // 快进 15 秒

        if (targetPos > duration) targetPos = duration;                     // 防越界
        m_player->setPosition(targetPos);
    }
}

void PlaybackPage::onRewindClicked()
{
    if (m_player) {
        qint64 currentPos = m_player->position();
        qint64 targetPos = currentPos - 15000;                              // 快退 15 秒

        if (targetPos < 0) targetPos = 0;                                   // 防负数
        m_player->setPosition(targetPos);
    }
}

// 强制结束逻辑
void PlaybackPage::onStopClicked()
{
    if (m_player) m_player->stop();
    if (m_countdownLabel) m_countdownLabel->setText("00:00:00");
    if (m_videoWidget && m_videoWidget->isVisible()) m_videoWidget->close();

    // 只有在有效播放状态下，才抛出记录信号
    if (m_isPlayingRecord) {
        QDateTime endTime = QDateTime::currentDateTime();
        QString dateStr = m_playStartTime.toString("yyyy-MM-dd");
        QString startStr = m_playStartTime.toString("HH:mm:ss");
        QString endStr = endTime.toString("HH:mm:ss");

        emit playbackFinishedRecord(dateStr, m_selectedMovieName, startStr, endStr, u8"强制结束");
        m_isPlayingRecord = false;                                          // 💡 结算完成，标志位复位
    }
}

// 自然结束逻辑
void PlaybackPage::onMediaStatusChanged(QMediaPlayer::MediaStatus status)
{
    // 如果播放器报告：媒体已到达末尾 (EndOfMedia)
    if (status == QMediaPlayer::EndOfMedia) 
    {
        if (m_countdownLabel) m_countdownLabel->setText("00:00:00");
        if (m_videoWidget && m_videoWidget->isVisible()) m_videoWidget->close();

        if (m_isPlayingRecord) 
        {
            QDateTime endTime = QDateTime::currentDateTime();
            QString dateStr = m_playStartTime.toString("yyyy-MM-dd");
            QString startStr = m_playStartTime.toString("HH:mm:ss");
            QString endStr = endTime.toString("HH:mm:ss");

            // 👑 抛出带有 "正常结束" 标签的记录
            emit playbackFinishedRecord(dateStr, m_selectedMovieName, startStr, endStr, u8"正常结束");
            m_isPlayingRecord = false;                                      // 💡 结算完成，标志位复位
        }
    }
}

// 视频列表拉取成功
void PlaybackPage::onMovieListReceived(const ServerApi::GetMovieListRsp& rsp)
{
    m_movieList->clear();
    // 确保海报和视频目录一定存在
    QDir().mkpath(MovieCoverPath);
    QDir().mkpath(MovieVideoPath);

    for (int i = 0; i < rsp.movies_size(); ++i) {
        const ServerApi::MovieInfo& movie = rsp.movies(i);

        uint64_t id = movie.movie_id();
        QString name = QString::fromStdString(movie.movie_name());
        QString coverUrl = QString::fromStdString(movie.cover_url());
        QFileInfo cover_file_info(coverUrl);
        QString cover_name = cover_file_info.fileName();
        int status = movie.play_status();
        
        // 提取纯净的下载要素
        QString fileMd5 = QString::fromStdString(movie.file_md5());
        uint64_t expectedSize = movie.file_size();

        // 获取影片密钥
        QString encryptKey = QString::fromStdString(movie.encrypt_key());

        // 物理路径推导 (纯粹用 MD5 命名)
        QString localVideoPath = MovieVideoPath + "/" + fileMd5 + ".mp4";
        QString localCoverPath = MovieCoverPath + "/" + cover_name;

        // 视频校验：看物理文件在不在，大小对不对
        bool isVideoDownloaded = false;
        QFile localVideo(localVideoPath);
        if (localVideo.exists() && localVideo.size() == expectedSize)
        {
            isVideoDownloaded = true;
        }

        // 海报路径校验
        QFile localCover(localCoverPath);
        if (!localCover.exists() || localCover.size() == 0) 
        {
            // 💡 如果本地没有海报，直接向服务器发起海报下载请求！
            ServerApi::DownloadCoverReq coverReq;
            coverReq.set_file_md5(fileMd5.toStdString());
            TCPMgr::Instance()->SendProtoMsg(ServerApi::MsgId::ID_DOWNLOAD_COVER_REQ, coverReq);

            // 注意：此时传给 AddMovieCard 的路径可以是个空字符串，
            // AddMovieCard 里如果发现为空，就显示 "加载中..." 的灰色占位图
            localCoverPath = "";
        }

        // 渲染卡片
        AddMovieCard(id, name, localCoverPath, localVideoPath, isVideoDownloaded, status, fileMd5, expectedSize, encryptKey);
    }
}

// =========================================================================================
// 📥 异步海报渲染逻辑
// =========================================================================================
void PlaybackPage::onCoverDownloaded(const QString& fileMd5, const QString& localCoverPath)
{
    // 遍历整个列表，找到这个 MD5 对应的卡片，刷新它的封面
    for (int i = 0; i < m_movieList->count(); ++i)
    {
        QListWidgetItem* item = m_movieList->item(i);
        // 回忆一下：我们在 AddMovieCard 时把 MD5 藏在了 Qt::UserRole + 4
        if (item->data(Qt::UserRole + 4).toString() == fileMd5)
        {
            // 取出这个 item 绑定的自定义 QFrame 卡片
            QWidget* cardWidget = m_movieList->itemWidget(item);
            if (cardWidget) {
                // 找到名叫 "cardCover" 的 QLabel
                QLabel* coverLabel = cardWidget->findChild<QLabel*>("cardCover");
                if (coverLabel) {
                    QPixmap pixmap(localCoverPath);
                    if (!pixmap.isNull()) {
                        coverLabel->setPixmap(pixmap.scaled(160, 220, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
                    }
                }
            }
            break; // 找到了就退出循环
        }
    }
}

// =========================================================================================
// ❌ 下载失败逻辑
// =========================================================================================
void PlaybackPage::onDownloadFailed(const QString& errMsg)
{
    QMessageBox::warning(this, u8"下载失败", errMsg);

    // 恢复 UI 状态，允许用户重试
    m_downloadProgress->hide();
    m_btnDownload->setText(u8"📥 下载到本地");
    m_btnDownload->setEnabled(true);
}

// =========================================================================================
// 🔄 下载进度条更新逻辑
// =========================================================================================
void PlaybackPage::onDownloadProgress(const QString& fileMd5, qint64 chunkSize)
{
    // 确保当前来的数据，就是我们选中的正在下载的电影
    if (fileMd5 != m_selectedMovieMd5) return;

    // 累加已下载的字节数
    m_currentDownloadBytes += chunkSize;

    // 计算百分比 (防除0保护)
    if (m_selectedMovieSize > 0) {
        int percent = (m_currentDownloadBytes * 100) / m_selectedMovieSize;
        m_downloadProgress->setValue(percent);

        // 顺便在按钮上显示下进度
        m_btnDownload->setText(QString(u8"正在下载... %1%").arg(percent));
    }
}

// =========================================================================================
// 🎉 彻底下载完成逻辑
// =========================================================================================
void PlaybackPage::onDownloadFinished(const QString& fileMd5)
{
    if (fileMd5 != m_selectedMovieMd5) return;

    m_downloadProgress->setValue(100);
    qDebug() << u8"[PlaybackPage] 界面响应: 视频下载彻底完成!";

    // 1. 恢复底层状态
    m_currentDownloadBytes = 0;
    m_downloadProgress->hide();
    m_isDownloading = false;

    // 2. 核心 UI 切换：藏起下载按钮，掏出播放按钮！
    m_selectedIsDownloaded = true;
    // 调用状态机，下载按钮瞬间消失，整套播放控制面板“嘭”地一下弹出来！
    SwitchControlPanelState(true);

    // 3. 更新内存中卡片的隐藏变量，并刷新卡片的副标题 (变绿)
    for (int i = 0; i < m_movieList->count(); ++i) {
        QListWidgetItem* item = m_movieList->item(i);
        if (item->data(Qt::UserRole + 4).toString() == fileMd5) {

            // 更新隐藏变量为已下载
            item->setData(Qt::UserRole + 3, true);

            // 找到子标题，把灰色未下载改成绿色已下载
            QWidget* cardWidget = m_movieList->itemWidget(item);
            if (cardWidget) {
                QLabel* subTitle = cardWidget->findChild<QLabel*>("cardSub");
                if (subTitle) {
                    subTitle->setText(u8"🟢 已下载");
                    subTitle->setStyleSheet("color: #4CAF50; font-weight: bold;");
                }
            }
            break;
        }
    }

    // 👑 绝杀防御：将 MessageBox 扔到下一个事件循环去执行！
        // 彻底避免网络 Socket 被 UI 弹窗阻塞导致的界面塌陷 Bug
    QTimer::singleShot(100, this, [this]() {
        QMessageBox::information(this, u8"下载完成", u8"影片已准备就绪，可以开始播放！");
        });
}