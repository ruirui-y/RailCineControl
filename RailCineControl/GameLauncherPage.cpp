#include "GameLauncherPage.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>
#include <QDir>
#include <QFileInfo>
#include <QDebug>
#include <QPointer>
#include <QTimer>
#include "TCPMgr.h"
#include "Global.h"
#include "ThreadPool.h"
#include "CinemaMessageBox.h"

GameLauncherPage::GameLauncherPage(QWidget* parent) : QWidget(parent)
{
    setAttribute(Qt::WA_StyledBackground, true);
    setObjectName("GameLauncherPage");

    m_gameProcess = new QProcess(this);
    connect(m_gameProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
        this, &GameLauncherPage::onGameProcessFinished);

    BuildUI();

    // 绑定 TCP 信号
    connect(ThreadPool::Instance()->GetTCPMgr(), &TCPMgr::SigGameListReceived, this, &GameLauncherPage::onGameListReceived);

    // 下载游戏
    auto tcpMgr = ThreadPool::Instance()->GetTCPMgr();
    connect(tcpMgr, &TCPMgr::SigCoverDownloaded, this, [this](ServerApi::FileType type, const QString& md5, const QString& path) 
        {
            if (type == ServerApi::FILE_GAME) this->onCoverDownloaded(md5, path);
        });
    connect(tcpMgr, &TCPMgr::SigDownloadFailed, this, [this](ServerApi::FileType type, const QString& msg)
        {
            if (type == ServerApi::FILE_GAME) this->onDownloadFailed(msg);
        });
    connect(tcpMgr, &TCPMgr::SigDownloadProgress, this, [this](ServerApi::FileType type, const QString& md5, qint64 size) 
        {
            if (type == ServerApi::FILE_GAME) this->onDownloadProgress(md5, size);
        });
    connect(tcpMgr, &TCPMgr::SigDownloadFinished, this, [this](ServerApi::FileType type, const QString& md5) 
        {
            if (type == ServerApi::FILE_GAME) this->onDownloadFinished(md5);
        });

    RefreshGames();
}

GameLauncherPage::~GameLauncherPage()
{
    if (m_gameProcess->state() == QProcess::Running) {
        m_gameProcess->terminate();
        m_gameProcess->waitForFinished(2000);
    }
}

void GameLauncherPage::BuildUI()
{
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_gameList = new QListWidget(this);
    m_gameList->setViewMode(QListView::IconMode);
    m_gameList->setResizeMode(QListView::Adjust);
    m_gameList->setSpacing(15);
    connect(m_gameList, &QListWidget::currentItemChanged, this, &GameLauncherPage::onGameSelected);

    // 底部控制面板
    QFrame* controlPanel = new QFrame(this);
    controlPanel->setObjectName("controlPanel");
    controlPanel->setFixedHeight(80);

    QHBoxLayout* mainCtrlLayout = new QHBoxLayout(controlPanel);
    mainCtrlLayout->setContentsMargins(30, 0, 30, 0);

    auto createTextBtn = [&](const QString& text) -> QPushButton* {
        QPushButton* btn = new QPushButton(text, controlPanel);
        btn->setObjectName("controlBtn");
        btn->setFixedHeight(40);
        btn->setMinimumWidth(80);
        btn->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
        btn->setCursor(Qt::PointingHandCursor);
        return btn;
        };

    m_btnLaunch = createTextBtn(tr("▶ 启动游戏"));
    m_btnDownload = createTextBtn(tr("📥 下载 / 更新"));
    m_btnStop = createTextBtn(tr("⏹ 强制结束"));
    m_btnFetch = createTextBtn(tr("🔄 刷新游戏库"));
    m_btnFetch->setStyleSheet("background-color: transparent; border: 1px solid rgba(255,255,255,0.2);");

    m_btnLaunch->hide();
    m_btnDownload->hide();
    m_btnStop->hide();

    QHBoxLayout* centerLayout = new QHBoxLayout();
    centerLayout->setSpacing(15);
    centerLayout->addWidget(m_btnLaunch);
    centerLayout->addWidget(m_btnDownload);

    QHBoxLayout* rightLayout = new QHBoxLayout();
    rightLayout->setSpacing(15);
    rightLayout->addWidget(m_btnStop);
    rightLayout->addSpacing(15);
    rightLayout->addWidget(m_btnFetch);

    mainCtrlLayout->addStretch(1);
    mainCtrlLayout->addLayout(centerLayout);
    mainCtrlLayout->addStretch(1);
    mainCtrlLayout->addLayout(rightLayout);

    connect(m_btnLaunch, &QPushButton::clicked, this, &GameLauncherPage::onLaunchClicked);
    connect(m_btnDownload, &QPushButton::clicked, this, &GameLauncherPage::onDownloadClicked);
    connect(m_btnStop, &QPushButton::clicked, this, &GameLauncherPage::onStopClicked);
    connect(m_btnFetch, &QPushButton::clicked, this, &GameLauncherPage::RefreshGames);

    m_downloadProgress = new QProgressBar(this);
    m_downloadProgress->setObjectName("Progress");
    m_downloadProgress->setFixedHeight(15);
    m_downloadProgress->setRange(0, 100);
    m_downloadProgress->setValue(0);
    m_downloadProgress->setTextVisible(false);
    m_downloadProgress->hide();

    layout->addWidget(m_gameList, 1);
    layout->addWidget(controlPanel);
    layout->addWidget(m_downloadProgress);
}

void GameLauncherPage::RefreshGames()
{
    m_gameList->clear();
    ServerApi::GetGameListReq req;
    req.set_page_index(1);
    req.set_page_size(100);
    ThreadPool::Instance()->GetTCPMgr()->SendProtoMsg(ServerApi::MsgId::ID_GET_GAME_LIST_REQ, req);
}

void GameLauncherPage::onGameListReceived(const ServerApi::GetGameListRsp& rsp)
{
    int total = rsp.games_size();

    // =========================================================================
    // 1. 提取云端下发的“合法凭证库” (提取合法的 MD5 和 海报文件名)
    // =========================================================================
    QSet<QString> validMd5s;
    QSet<QString> validCovers;
    for (int i = 0; i < total; ++i) {
        ServerApi::GameInfo game = rsp.games(i);
        validMd5s.insert(QString::fromStdString(game.package_md5()));
        validCovers.insert(QFileInfo(QString::fromStdString(game.cover_url())).fileName());
    }

    QPointer<GameLauncherPage> safeThis(this);

    // =========================================================================
    // 🌊 第一阶段：将耗时的“本地磁盘清理工作”丢入后台线程，防止 UI 卡死
    // =========================================================================
    ThreadPool::Instance()->DispatchToWorker([safeThis, rsp, validMd5s, validCovers, total]() {

        // --- 1.1 清理本地已废弃的海报 ---
        QDir coverDir(GameCoverPath);
        for (const QFileInfo& info : coverDir.entryInfoList(QDir::Files)) {
            if (!validCovers.contains(info.fileName())) {
                QFile::remove(info.absoluteFilePath());
            }
        }

        // --- 1.2 清理本地已废弃的压缩包 (.tar) ---
        QDir tarDir(GameTarPath);
        for (const QFileInfo& info : tarDir.entryInfoList(QDir::Files)) {
            // 如果后缀是 tar，且它的文件名 (MD5) 不在合法列表里，删！
            if (info.suffix() == "tar" && !validMd5s.contains(info.baseName())) {
                QFile::remove(info.absoluteFilePath());
            }
        }

        // --- 1.3 👑 清理本地已废弃的游戏解压目录 (核心排雷) ---
        QDir installDir(GameInstallPath);
        for (const QFileInfo& info : installDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            // 游戏解压目录名就是 MD5
            if (!validMd5s.contains(info.fileName())) {
                QDir dirToRemove(info.absoluteFilePath());
                dirToRemove.removeRecursively(); // 递归强删几十GB的文件夹
            }
        }

        // 如果云端返回 0 个游戏 (说明服务器被清空了)，清理完本地后直接结束，无需走 UI 渲染
        if (total == 0) return;

        // =========================================================================
        // 🌊 第二阶段：清理完毕后，安全回到主线程发起“并发数据组装”
        // =========================================================================
        QMetaObject::invokeMethod(safeThis.data(), [safeThis, rsp, total]() {
            if (!safeThis) return;

            auto payloadList = std::make_shared<std::vector<GameUIPayload>>(total);
            auto completedCount = std::make_shared<std::atomic<int>>(0);

            for (int i = 0; i < total; ++i) {
                ServerApi::GameInfo game = rsp.games(i);

                // 并发执行每个游戏卡片的数据加载与图片读取
                ThreadPool::Instance()->DispatchToWorker([safeThis, game, i, total, payloadList, completedCount]() {

                    // 将rsp转换成游戏卡片结构体信息
                    GameUIPayload payload;
                    payload.id = game.game_id();
                    payload.name = QString::fromStdString(game.game_name());
                    payload.version = QString::fromStdString(game.version());
                    payload.fileMd5 = QString::fromStdString(game.package_md5());
                    payload.expectedSize = game.package_size();
                    payload.exeRelativePath = QString::fromStdString(game.exe_path());

                    QString coverName = QFileInfo(QString::fromStdString(game.cover_url())).fileName();
                    payload.localCoverPath = GameCoverPath + "/" + coverName;
                    payload.localTarPath = GameTarPath + "/" + payload.fileMd5 + ".tar";
                    payload.localInstallDir = GameInstallPath + "/" + payload.fileMd5;

                    // 检查是否已经解压就绪 (存在 EXE 就算就绪)
                    QString absoluteExePath = payload.localInstallDir + "/" + payload.exeRelativePath;
                    payload.isDownloaded = QFile::exists(absoluteExePath);

                    // 检查海报是否存在
                    QFile localCover(payload.localCoverPath);
                    if (!localCover.exists() || localCover.size() == 0) {
                        payload.needFetchCover = true;
                        payload.localCoverPath = "";
                    }
                    else {
                        payload.needFetchCover = false;
                        payload.coverImage.load(payload.localCoverPath);
                    }

                    (*payloadList)[i] = std::move(payload);

                    // =========================================================================
                    // 🌊 第三阶段：所有线程组装完毕，最后统一回主线程渲染 UI
                    // =========================================================================
                    if (++(*completedCount) == total)
                    {
                        QMetaObject::invokeMethod(safeThis.data(), [safeThis, payloadList]() {
                            if (!safeThis) return;
                            safeThis->m_gameList->setUpdatesEnabled(false);

                            for (const auto& data : *payloadList) {
                                if (data.needFetchCover) {
                                    ServerApi::DownloadCoverReq req;
                                    req.set_file_md5(data.fileMd5.toStdString());
                                    req.set_file_type(ServerApi::FILE_GAME); // 👑 注意这里带上了你的游戏类型标签
                                    ThreadPool::Instance()->GetTCPMgr()->SendProtoMsg(ServerApi::MsgId::ID_DOWNLOAD_COVER_REQ, req);
                                }
                                safeThis->AddGameCard(data);
                            }

                            safeThis->m_gameList->setUpdatesEnabled(true);
                            }, Qt::QueuedConnection);
                    }
                    });
            }
            }, Qt::QueuedConnection);
        });
}
void GameLauncherPage::AddGameCard(const GameUIPayload& data)
{
    QFrame* card = new QFrame();
    card->setObjectName("gameCard");
    card->setProperty("selected", false);

    QVBoxLayout* layout = new QVBoxLayout(card);
    layout->setContentsMargins(8, 10, 5, 10);
    layout->setSpacing(8);

    QLabel* cover = new QLabel(card);
    cover->setObjectName("cardCover");
    cover->setFixedSize(160, 220);
    cover->setAlignment(Qt::AlignCenter);
    QPixmap pixmap = QPixmap::fromImage(data.coverImage);
    if (pixmap.isNull()) cover->setText(tr("海报加载中..."));
    else cover->setPixmap(pixmap.scaled(160, 220, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));

    QLabel* title = new QLabel(data.name, card);
    title->setObjectName("cardTitle");

    QString statusText = data.isDownloaded ? tr("🟢 已就绪 | ") + data.version : tr("☁️ 未安装 | ") + data.version;
    QLabel* subTitle = new QLabel(statusText, card);
    subTitle->setObjectName("cardSub");
    subTitle->setProperty("downloaded", data.isDownloaded);

    layout->addWidget(cover);
    layout->addWidget(title);
    layout->addWidget(subTitle);

    QListWidgetItem* item = new QListWidgetItem(m_gameList);
    item->setSizeHint(QSize(180, 300));

    item->setData(Qt::UserRole, data.name);
    item->setData(Qt::UserRole + 1, data.localTarPath);
    item->setData(Qt::UserRole + 2, data.localInstallDir);                                          // 游戏路径
    item->setData(Qt::UserRole + 3, data.exeRelativePath);
    item->setData(Qt::UserRole + 4, data.isDownloaded);
    item->setData(Qt::UserRole + 5, data.fileMd5);
    item->setData(Qt::UserRole + 6, data.expectedSize);

    m_gameList->addItem(item);
    m_gameList->setItemWidget(item, card);
}

void GameLauncherPage::SwitchControlPanelState(bool isDownloaded)
{
    if (isDownloaded) {
        m_btnDownload->hide();
        m_btnLaunch->show();
        m_btnLaunch->setEnabled(true);
        m_btnStop->show();
        m_btnStop->setEnabled(false);
    }
    else {
        m_btnLaunch->hide();
        m_btnStop->hide();
        m_btnDownload->show();
        m_btnDownload->setEnabled(true);
        m_btnDownload->setText(tr("📥 下载游戏"));
    }
}

void GameLauncherPage::onGameSelected(QListWidgetItem* current, QListWidgetItem* prev)
{
    if (prev) {
        QWidget* preW = m_gameList->itemWidget(prev);
        preW->setProperty("selected", false);
        preW->style()->unpolish(preW);
        preW->style()->polish(preW);
    }
    if (current) {
        QWidget* curW = m_gameList->itemWidget(current);
        curW->setProperty("selected", true);
        curW->style()->unpolish(curW);
        curW->style()->polish(curW);

        if (m_isDownloading) return;

        m_selectedGameTarPath = current->data(Qt::UserRole + 1).toString();
        m_selectedGameInstallDir = current->data(Qt::UserRole + 2).toString();
        m_selectedGameExePath = current->data(Qt::UserRole + 3).toString();
        m_selectedIsDownloaded = current->data(Qt::UserRole + 4).toBool();
        m_selectedGameMd5 = current->data(Qt::UserRole + 5).toString();
        m_selectedGameSize = current->data(Qt::UserRole + 6).toULongLong();

        SwitchControlPanelState(m_selectedIsDownloaded);
    }
}

void GameLauncherPage::onDownloadClicked()
{
    if (m_selectedGameMd5.isEmpty()) return;

    m_currentDownloadBytes = 0;
    m_isDownloading = true;
    m_btnDownload->setEnabled(false);
    m_btnDownload->setText(tr("正在建立传输通道..."));
    m_downloadProgress->setValue(0);
    m_downloadProgress->show();

    // 这一步使用与 Movie 完全相同的下载协议和目录结构
    // (注意：这里我们将下载下来的归置到 GameTarPath)
    QDir().mkpath(GameTarPath);
    QFile localFile(m_selectedGameTarPath);
    if (!localFile.open(QIODevice::WriteOnly)) {
        m_btnDownload->setText(tr("本地磁盘错误"));
        return;
    }
    localFile.close();

    ServerApi::DownloadChunkReq req;
    req.set_file_md5(m_selectedGameMd5.toStdString());
    req.set_chunk_index(0);
    req.set_file_type(ServerApi::FILE_GAME);
    ThreadPool::Instance()->GetTCPMgr()->SendProtoMsg(ServerApi::MsgId::ID_DOWNLOAD_CHUNK_REQ, req);
}

void GameLauncherPage::onLaunchClicked()
{
    m_btnLaunch->setEnabled(false);
    m_gameList->setEnabled(false);

    QString fullExePath = m_selectedGameInstallDir + "/" + m_selectedGameExePath;
    m_gameProcess->setProgram(fullExePath);
    m_gameProcess->setWorkingDirectory(m_selectedGameInstallDir); // 必须设置工作目录为游戏根目录
    m_gameProcess->start();

    if (m_gameProcess->waitForStarted(2000)) {
        m_btnStop->setEnabled(true);
    }
    else {
        CinemaMessageBox::ShowError(this, tr("错误"), tr("游戏启动失败！请检查路径:") + fullExePath);
        m_btnLaunch->setEnabled(true);
        m_gameList->setEnabled(true);
    }
}

void GameLauncherPage::onStopClicked()
{
    m_btnStop->setEnabled(false);
    if (m_gameProcess->state() == QProcess::Running) {
        m_gameProcess->terminate();
        if (!m_gameProcess->waitForFinished(1000)) {
            QStringList args;
            args << "/F" << "/T" << "/PID" << QString::number(m_gameProcess->processId());
            QProcess::execute("taskkill", args);
        }
    }
}

void GameLauncherPage::onGameProcessFinished()
{
    m_btnStop->setEnabled(false);
    m_btnLaunch->setEnabled(true);
    m_gameList->setEnabled(true);
}

void GameLauncherPage::onDownloadFinished(const QString& fileMd5)
{
    if (fileMd5 != m_selectedGameMd5) return;

    // 下载完成，开始极速解压
    m_btnDownload->setText(tr("📦 下载完成，正在解压..."));
    ExtractGame(m_selectedGameTarPath, m_selectedGameInstallDir);
}

void GameLauncherPage::ExtractGame(const QString& tarPath, const QString& destDir)
{
    QDir().mkpath(destDir);
    QProcess* p = new QProcess(this);
    QStringList args;
    args << "-xf" << QDir::toNativeSeparators(tarPath) << "-C" << QDir::toNativeSeparators(destDir);

    connect(p, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, [this, p](int code) {
        p->deleteLater();
        m_isDownloading = false;
        m_downloadProgress->hide();

        // 删除临时 Tar 包释放空间
        QFile::remove(m_selectedGameTarPath);

        CinemaMessageBox::ShowInfo(this, tr("完成"), tr("游戏安装就绪，随时可以启动！"));

        m_btnDownload->setEnabled(true);
        m_btnDownload->setText(tr("📥 下载游戏"));

        RefreshGames(); // 刷新整个列表状态
        });

    p->start("tar", args);
}

// 其余下载进度和失败的回调与 MoviePlayback 类似
void GameLauncherPage::onCoverDownloaded(const QString& fileMd5, const QString& localCoverPath) {
    RefreshGames(); // 粗暴点，海报下完直接刷新列表
}

void GameLauncherPage::onDownloadFailed(const QString& errMsg) {
    m_isDownloading = false;
    m_downloadProgress->hide();
    m_btnDownload->setText(tr("📥 下载游戏"));
    m_btnDownload->setEnabled(true);
    CinemaMessageBox::ShowError(this, tr("下载失败"), errMsg);
}

void GameLauncherPage::onDownloadProgress(const QString& fileMd5, qint64 chunkSize) {
    if (fileMd5 != m_selectedGameMd5) return;
    m_currentDownloadBytes += chunkSize;
    if (m_selectedGameSize > 0) {
        int percent = (m_currentDownloadBytes * 100) / m_selectedGameSize;
        m_downloadProgress->setValue(percent);
        m_btnDownload->setText(tr("📥 正在下载... %1%").arg(percent));
    }
}