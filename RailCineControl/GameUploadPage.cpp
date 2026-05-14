#include "GameUploadPage.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QFileDialog>
#include <QUuid>
#include <QFileInfo>
#include <QProcess>
#include <QDebug>
#include <QPointer>
#include <QCryptographicHash>
#include <QBuffer>
#include "ThreadPool.h"
#include "TCPMgr.h"
#include "CinemaMessageBox.h"

GameUploadPage::GameUploadPage(QWidget* parent) : QWidget(parent)
{
    setAttribute(Qt::WA_StyledBackground, true);
    setObjectName("GameUploadPage");

    m_tempTarFile = new QFile(this);

    BindSignals();
    BuildUI();
}

void GameUploadPage::BindSignals()
{
    auto tcpMgr = ThreadPool::Instance()->GetTCPMgr();

    // 收到所有分片确认后 -> 提交游戏表单元数据
    connect(tcpMgr, &TCPMgr::SigAllChunksAcked, this, [this](ServerApi::FileType fileType) {
        if (fileType == ServerApi::FILE_GAME) {
            this->submitMetadataToTcp();
        }
        });

    // 单个切片上传成功后 -> 抽水泵抽下一块
    connect(tcpMgr, &TCPMgr::SigChunkUploadSuccess, this, [this](ServerApi::FileType fileType) {
        if (fileType == ServerApi::FILE_GAME) {
            this->pumpNextChunk();
        }
        });

    // 底层分片传输失败 (如断网、打包的tar文件受损等)
    connect(tcpMgr, &TCPMgr::SigChunkUploadFailed, this, [this](ServerApi::FileType fileType, QString errMsg) {
        if (fileType == ServerApi::FILE_GAME) {
            this->UnlockUI();
            CinemaMessageBox::ShowError(this, tr("游戏包传输中断"), errMsg);
        }
        });

    // 业务元数据录入失败 (如游戏名冲突、版本号重复等)
    connect(tcpMgr, &TCPMgr::SigGameUploadFailed, this, [this](QString errMsg) {
        this->UnlockUI();
        CinemaMessageBox::ShowError(this, tr("游戏登记失败"), errMsg);
        });
}

void GameUploadPage::BuildUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 20, 0, 0);
    mainLayout->setSpacing(30);

    QHBoxLayout* topLayout = new QHBoxLayout();
    topLayout->setSpacing(40);

    // 左侧：海报
    QVBoxLayout* previewLayout = new QVBoxLayout();
    QLabel* previewTitle = new QLabel(tr("海报预览"), this);
    previewTitle->setObjectName("uploadLabelNormal");

    m_coverPreview = new QLabel(this);
    m_coverPreview->setObjectName("coverPreviewArea");
    m_coverPreview->setFixedSize(240, 330);
    m_coverPreview->setAlignment(Qt::AlignCenter);
    m_coverPreview->setText(tr("点击右侧选择图片"));

    previewLayout->addWidget(previewTitle);
    previewLayout->addWidget(m_coverPreview);
    previewLayout->addStretch();

    // 右侧：表单
    QVBoxLayout* formContainerLayout = new QVBoxLayout();
    QFormLayout* formLayout = new QFormLayout();
    formLayout->setSpacing(20);
    formLayout->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

    auto createLineEdit = [&](const QString& placeholder) -> QLineEdit* {
        QLineEdit* edit = new QLineEdit(this);
        edit->setObjectName("uploadInput");
        edit->setPlaceholderText(placeholder);
        edit->setFixedHeight(35);
        return edit;
        };
    auto createBrowseBtn = [&](const QString& text) -> QPushButton* {
        QPushButton* btn = new QPushButton(text, this);
        btn->setObjectName("browseBtn");
        btn->setFixedSize(80, 35);
        return btn;
        };

    m_dirPathEdit = createLineEdit(tr("请选择打包好的 UE 游戏根目录..."));
    m_dirPathEdit->setReadOnly(true);
    QPushButton* btnBrowseDir = createBrowseBtn(tr("浏览..."));
    QHBoxLayout* dirLayout = new QHBoxLayout();
    dirLayout->addWidget(m_dirPathEdit);
    dirLayout->addWidget(btnBrowseDir);

    m_coverPathEdit = createLineEdit(tr("请选择本地海报图片..."));
    m_coverPathEdit->setReadOnly(true);
    QPushButton* btnBrowseCover = createBrowseBtn(tr("浏览..."));
    QHBoxLayout* coverLayout = new QHBoxLayout();
    coverLayout->addWidget(m_coverPathEdit);
    coverLayout->addWidget(btnBrowseCover);

    m_exePathEdit = createLineEdit(tr("请选择游戏启动程序 (.exe)..."));
    m_exePathEdit->setReadOnly(true);
    QPushButton* btnBrowseExe = createBrowseBtn(tr("浏览..."));
    QHBoxLayout* exeLayout = new QHBoxLayout();
    exeLayout->addWidget(m_exePathEdit);
    exeLayout->addWidget(btnBrowseExe);

    m_nameEdit = createLineEdit(tr("游戏名称"));
    m_versionEdit = createLineEdit(tr("版本号 (例如: v1.0.1)"));
    
    m_descEdit = new QTextEdit(this);
    m_descEdit->setObjectName("uploadDescInput");
    m_descEdit->setFixedHeight(80);

    formLayout->addRow(tr("游戏目录 (*):"), dirLayout);
    formLayout->addRow(tr("海报封面 (*):"), coverLayout);
    formLayout->addRow(tr("启动路径 (*):"), exeLayout);
    formLayout->addRow(tr("游戏名称 (*):"), m_nameEdit);
    formLayout->addRow(tr("游戏版本 (*):"), m_versionEdit);
    formLayout->addRow(tr("更新日志 :"), m_descEdit);

    formContainerLayout->addLayout(formLayout);
    formContainerLayout->addStretch();

    topLayout->addLayout(previewLayout);
    topLayout->addLayout(formContainerLayout, 1);

    // 底部进度条
    QVBoxLayout* bottomLayout = new QVBoxLayout();
    m_progressBar = new QProgressBar(this);
    m_progressBar->setObjectName("Progress");
    m_progressBar->setFixedHeight(15);
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_progressBar->setTextVisible(false);

    QHBoxLayout* btnLayout = new QHBoxLayout();
    m_btnUpload = new QPushButton(tr("🚀 压缩并上传入库"), this);
    m_btnUpload->setObjectName("uploadSubmitBtn");
    m_btnUpload->setFixedHeight(45);

    btnLayout->addStretch();
    btnLayout->addWidget(m_btnUpload, 1);
    btnLayout->addStretch();

    bottomLayout->addWidget(m_progressBar);
    bottomLayout->addSpacing(10);
    bottomLayout->addLayout(btnLayout);

    mainLayout->addLayout(topLayout, 1);
    mainLayout->addLayout(bottomLayout);

    connect(btnBrowseDir, &QPushButton::clicked, this, &GameUploadPage::onSelectFolder);
    connect(btnBrowseCover, &QPushButton::clicked, this, &GameUploadPage::onSelectCover);
    connect(btnBrowseExe, &QPushButton::clicked, this, &GameUploadPage::onSelectExe);
    connect(m_btnUpload, &QPushButton::clicked, this, &GameUploadPage::onUploadClicked);
}

void GameUploadPage::ResetUI()
{
    m_dirPathEdit->clear();
    m_coverPathEdit->clear();
    m_nameEdit->clear();
    m_versionEdit->clear();
    m_exePathEdit->clear();
    m_descEdit->clear();
    m_coverPreview->clear();
    m_progressBar->setValue(0);
    m_btnUpload->setEnabled(true);
    m_btnUpload->setText(tr("🚀 压缩并上传入库"));
    if (QFile::exists(m_currentTarPath)) QFile::remove(m_currentTarPath);
}

void GameUploadPage::UnlockUI()
{
    m_progressBar->setValue(0);
    m_btnUpload->setEnabled(true);
    m_btnUpload->setText(tr("🚀 压缩并上传入库"));
    if (m_tempTarFile->isOpen()) m_tempTarFile->close();
}

void GameUploadPage::onSelectFolder()
{
    QString dir = QFileDialog::getExistingDirectory(this, tr("选择游戏根目录"));
    if (!dir.isEmpty()) m_dirPathEdit->setText(dir);
}

void GameUploadPage::onSelectCover()
{
    QString path = QFileDialog::getOpenFileName(this, tr("选择海报"), "", tr("Images (*.png *.jpg)"));
    if (!path.isEmpty()) {
        m_coverPathEdit->setText(path);
        m_coverPreview->setPixmap(QPixmap(path).scaled(m_coverPreview->size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
    }
}

void GameUploadPage::onSelectExe()
{
    // 1. 获取当前选定的游戏根目录
    QString gameDirStr = m_dirPathEdit->text();
    if (gameDirStr.isEmpty()) {
        CinemaMessageBox::ShowWarning(this, tr("提示"), tr("请先选择上方的【游戏目录】！"));
        return;
    }

    // 2. 打开文件选择对话框 (起点直接设置为游戏根目录)
    QString exePath = QFileDialog::getOpenFileName(
        this,
        tr("选择游戏启动程序"),
        gameDirStr,                     // 默认打开路径
        tr("Executable Files (*.exe)")  // 仅限 exe 文件
    );

    if (!exePath.isEmpty())
    {
        // 3. 计算相对路径
        QDir gameDir(gameDirStr);
        QString relativePath = gameDir.relativeFilePath(exePath);

        // 4. 防呆拦截：检查所选 exe 是否真的在游戏目录内部
        if (relativePath.startsWith("..") || relativePath.contains("/../")) {
            CinemaMessageBox::ShowError(this, tr("错误"), tr("启动程序必须在选定的【游戏目录】内部！"));
            return;
        }

        // 5. 填入启动路径输入框
        m_exePathEdit->setText(relativePath);

        // ==========================================================
        // 6. 👑 智能补全游戏名称 (针对 UE 游戏专门优化)
        // ==========================================================
        // 如果当前游戏名称还是空的，我们就帮他自动填
        if (m_nameEdit->text().trimmed().isEmpty())
        {
            QFileInfo fileInfo(exePath);
            QString gameBaseName = fileInfo.baseName(); // 获取不带 .exe 的纯文件名

            // 智能过滤 UE 打包常带的 Shipping 后缀
            if (gameBaseName.endsWith("-Win64-Shipping")) {
                gameBaseName.replace("-Win64-Shipping", "");
            }
            else if (gameBaseName.endsWith("-Win32-Shipping")) {
                gameBaseName.replace("-Win32-Shipping", "");
            }

            m_nameEdit->setText(gameBaseName);
        }
    }
}

void GameUploadPage::onUploadClicked()
{
    if (m_dirPathEdit->text().isEmpty() || m_nameEdit->text().isEmpty() || m_exePathEdit->text().isEmpty() || m_versionEdit->text().isEmpty()) {
        CinemaMessageBox::ShowWarning(this, tr("提示"), tr("请填完所有带星号的必填项！"));
        return;
    }
    m_btnUpload->setEnabled(false);
    startArchiveAndUpload();
}

void GameUploadPage::startArchiveAndUpload()
{
    m_btnUpload->setText(tr("📦 [1/3] 正在静默打包整个游戏目录..."));
    m_progressBar->setMaximum(0); // 进度条跑马灯

    QString sourceDir = m_dirPathEdit->text();
    QString parentDir = QFileInfo(sourceDir).dir().absolutePath();

    // 生成文件名
    QString tarName = "game_temp_" + QUuid::createUuid().toString(QUuid::Id128) + ".tar";
    m_currentTarPath = QDir(parentDir).absoluteFilePath(tarName);

    QProcess* p = new QProcess(this);
    QStringList args;
    args << "-cf" << QDir::toNativeSeparators(m_currentTarPath) << "-C" << QDir::toNativeSeparators(sourceDir) << ".";

    connect(p, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, [this, p](int code) 
        {
            p->deleteLater();
            if (code != 0) {
                UnlockUI();
                CinemaMessageBox::ShowError(this, tr("错误"), tr("系统打包失败！"));
                return;
            }
            startTcpChunkUpload();
        });

    p->start("tar", args);
}

void GameUploadPage::startTcpChunkUpload()
{
    m_btnUpload->setText(tr("🔍 [2/3] 正在计算压缩包指纹..."));
    m_progressBar->setMaximum(100);

    QPointer<GameUploadPage> safeThis(this);
    ThreadPool::Instance()->DispatchToWorker([safeThis]() 
        {
            // 计算压缩包的md5码
            QFile file(safeThis->m_currentTarPath);
            QString md5Result;
            bool ok = false;
            if (file.open(QIODevice::ReadOnly)) {
                QCryptographicHash hash(QCryptographicHash::Md5);
                if (hash.addData(&file)) {
                    md5Result = hash.result().toHex();
                    ok = true;
                }
                file.close();
            }

            QMetaObject::invokeMethod(safeThis.data(), [safeThis, ok, md5Result]()
                {
                    if (!safeThis) return;
                    
                    // md5码计算失败
                    if (!ok) 
                    {
                        safeThis->UnlockUI();
                        return;
                    }
                    safeThis->m_currentFileMd5 = md5Result;
                    safeThis->m_tempTarFile->setFileName(safeThis->m_currentTarPath);
                    safeThis->m_tempTarFile->open(QIODevice::ReadOnly);

                    safeThis->m_totalFileSize = safeThis->m_tempTarFile->size();
                    safeThis->m_currentOffset = 0;
                    safeThis->m_chunkIndex = 0;
                    safeThis->m_btnUpload->setText(tr("🚀 [3/3] 正在高速上传分片..."));
                    safeThis->pumpNextChunk();                                                      // 开始发送分片
                }, Qt::QueuedConnection);
        });
}

void GameUploadPage::pumpNextChunk()
{
    if (!m_tempTarFile->isOpen() || m_tempTarFile->atEnd()) return;

    QByteArray chunkData = m_tempTarFile->read(1024 * 1024);
    bool isLast = m_tempTarFile->atEnd();

    ServerApi::UploadChunkReq req;
    req.set_file_md5(m_currentFileMd5.toStdString());
    req.set_chunk_index(m_chunkIndex++);
    req.set_chunk_offset(m_currentOffset);
    req.set_chunk_data(chunkData.data(), chunkData.size());
    req.set_is_last(isLast);
    req.set_file_type(ServerApi::FILE_GAME);

    ThreadPool::Instance()->GetTCPMgr()->SendProtoMsg(ServerApi::MsgId::ID_UPLOAD_CHUNK_REQ, req);

    m_currentOffset += chunkData.size();
    m_progressBar->setValue((m_currentOffset * 100) / m_totalFileSize);

    if (isLast) m_tempTarFile->close();
}

void GameUploadPage::submitMetadataToTcp()
{
    m_btnUpload->setText(tr("正在注册游戏档案与海报..."));
    m_progressBar->setValue(100);

    ServerApi::UploadGameReq req;
    req.set_game_name(m_nameEdit->text().toStdString());
    req.set_version(m_versionEdit->text().toStdString());
    req.set_exe_path(m_exePathEdit->text().toStdString());
    req.set_description(m_descEdit->toPlainText().toStdString());
    req.set_package_md5(m_currentFileMd5.toStdString());

    // 海报上传之前进行压缩
    QImage coverImage;
    if (coverImage.load(m_coverPathEdit->text())) {
        coverImage = coverImage.scaled(160, 220, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        QByteArray compressedData;
        QBuffer buffer(&compressedData);
        buffer.open(QIODevice::WriteOnly);
        coverImage.save(&buffer, "JPG", 80);
        buffer.close();
        req.set_cover_data(compressedData.data(), compressedData.size());
        req.set_cover_suffix(".jpg");
    }

    ThreadPool::Instance()->GetTCPMgr()->SendProtoMsg(ServerApi::MsgId::ID_UPLOAD_GAME_REQ, req);
}