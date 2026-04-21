#include "UploadPage.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QFileDialog>
#include <QMessageBox>
#include <QUuid>
#include <QFileInfo>
#include <QMediaPlayer>
#include <QEventLoop>
#include <QUrl>
#include <QDebug>
#include <QCoreApplication>
#include <QPointer>
#include <QCryptographicHash>
#include "ThreadPool.h"
#include "TCPMgr.h"
#include "VideoSecurityTool.h"

UploadPage::UploadPage(QWidget* parent) : QWidget(parent)
{
    // 初始化抽水泵和文件指针
    m_videoFile = new QFile(this);
    m_chunkPumpTimer = new QTimer(this);

    // 绑定抽水泵动作：每次定时器触发，就切一块发给 TCP
    connect(m_chunkPumpTimer, &QTimer::timeout, this, &UploadPage::pumpNextChunk);

    // 收到服务器的安全回执后，才真正提交海报与文字表单！
    connect(TCPMgr::Instance().get(), &TCPMgr::SigAllChunksAcked, this, &UploadPage::submitMetadataToTcp);

    BuildUI();
}

void UploadPage::BuildUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 20, 0, 0);
    mainLayout->setSpacing(30);

    // ================= 1. 上半部分：表单与预览区域 =================
    QHBoxLayout* topLayout = new QHBoxLayout();
    topLayout->setSpacing(40);

    // 左侧：海报预览区
    QVBoxLayout* previewLayout = new QVBoxLayout();
    QLabel* previewTitle = new QLabel(u8"海报预览", this);
    previewTitle->setObjectName("uploadLabelNormal");                           // 👑 绑定 QSS

    m_coverPreview = new QLabel(this);
    m_coverPreview->setObjectName("coverPreviewArea");                          // 👑 绑定 QSS
    m_coverPreview->setFixedSize(240, 330);
    m_coverPreview->setAlignment(Qt::AlignCenter);
    m_coverPreview->setText(u8"点击右侧选择图片");

    previewLayout->addWidget(previewTitle);
    previewLayout->addWidget(m_coverPreview);
    previewLayout->addStretch();

    // 右侧：表单输入区
    QVBoxLayout* formContainerLayout = new QVBoxLayout();
    QFormLayout* formLayout = new QFormLayout();
    formLayout->setSpacing(20);
    formLayout->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

    // 💡 工厂闭包 (去除硬编码样式)
    auto createLineEdit = [&](const QString& placeholder) -> QLineEdit* {
        QLineEdit* edit = new QLineEdit(this);
        edit->setObjectName("uploadInput");                                     // 👑 绑定 QSS
        edit->setPlaceholderText(placeholder);
        edit->setFixedHeight(35);
        return edit;
        };
    auto createBrowseBtn = [&](const QString& text) -> QPushButton* {
        QPushButton* btn = new QPushButton(text, this);
        btn->setObjectName("browseBtn");                                        // 👑 绑定 QSS
        btn->setFixedSize(80, 35);
        return btn;
        };

    // 表单项
    m_videoPathEdit = createLineEdit(u8"请选择本地 .mp4 / .mkv 视频文件...");
    m_videoPathEdit->setReadOnly(true);
    QPushButton* btnBrowseVideo = createBrowseBtn(u8"浏览...");
    QHBoxLayout* videoLayout = new QHBoxLayout();
    videoLayout->addWidget(m_videoPathEdit);
    videoLayout->addWidget(btnBrowseVideo);

    m_coverPathEdit = createLineEdit(u8"请选择本地 .jpg / .png 海报图片...");
    m_coverPathEdit->setReadOnly(true);
    QPushButton* btnBrowseCover = createBrowseBtn(u8"浏览...");
    QHBoxLayout* coverLayout = new QHBoxLayout();
    coverLayout->addWidget(m_coverPathEdit);
    coverLayout->addWidget(btnBrowseCover);

    m_nameEdit = createLineEdit(u8"请输入影片名称 (例如: 深海浩劫 4D)");

    m_descEdit = new QTextEdit(this);
    m_descEdit->setObjectName("uploadDescInput");                               // 👑 绑定 QSS
    m_descEdit->setPlaceholderText(u8"请输入影片简述或玩法说明...");
    m_descEdit->setFixedHeight(100);

    // 组装表单
    QLabel* lblVideo = new QLabel(u8"视频资源 (*):", this);
    QLabel* lblCover = new QLabel(u8"海报封面 (*):", this);
    QLabel* lblName = new QLabel(u8"影片名称 (*):", this);
    QLabel* lblDesc = new QLabel(u8"影片简介:", this);
    lblVideo->setObjectName("uploadLabelHighlight");                            // 👑 绑定 QSS
    lblCover->setObjectName("uploadLabelHighlight");
    lblName->setObjectName("uploadLabelHighlight");
    lblDesc->setObjectName("uploadLabelNormal");

    formLayout->addRow(lblVideo, videoLayout);
    formLayout->addRow(lblCover, coverLayout);
    formLayout->addRow(lblName, m_nameEdit);
    formLayout->addRow(lblDesc, m_descEdit);

    formContainerLayout->addLayout(formLayout);
    formContainerLayout->addStretch();

    topLayout->addLayout(previewLayout);
    topLayout->addLayout(formContainerLayout, 1);

    // ================= 2. 下半部分：进度条与操作按钮 =================
    QVBoxLayout* bottomLayout = new QVBoxLayout();

    m_progressBar = new QProgressBar(this);
    m_progressBar->setObjectName("uploadProgress");                             // 👑 绑定 QSS
    m_progressBar->setFixedHeight(15);
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_progressBar->setTextVisible(false);

    QHBoxLayout* btnLayout = new QHBoxLayout();
    m_btnUpload = new QPushButton(u8"🚀 开始上传并入库", this);
    m_btnUpload->setObjectName("uploadSubmitBtn");                              // 👑 绑定 QSS
    m_btnUpload->setFixedHeight(45);

    QPushButton* btn = new QPushButton(u8"QMessageBox", this);
    btn->setObjectName("uploadSubmitBtn");
    btn->setFixedHeight(45);
    connect(btn, &QPushButton::clicked, this, [this]()
        {
            QMessageBox::information(this, u8"成功", u8"影片已成功录入云端！");
        });

    btnLayout->addStretch();
    btnLayout->addWidget(m_btnUpload, 1);
    btnLayout->addWidget(btn);
    btnLayout->addStretch();

    bottomLayout->addWidget(m_progressBar);

    bottomLayout->addSpacing(10);
    bottomLayout->addLayout(btnLayout);
    mainLayout->addLayout(topLayout, 1);
    mainLayout->addLayout(bottomLayout);

    // ================= 3. 信号绑定 =================
    connect(btnBrowseVideo, &QPushButton::clicked, this, &UploadPage::onSelectVideo);
    connect(btnBrowseCover, &QPushButton::clicked, this, &UploadPage::onSelectCover);
    connect(m_btnUpload, &QPushButton::clicked, this, &UploadPage::onUploadClicked);
}

uint32_t UploadPage::GetVideoDurationSec(const QString& filePath)
{
    // 1. 创建一个临时的、不显示的“哑巴”播放器
    QMediaPlayer dummyPlayer;
    dummyPlayer.setMedia(QUrl::fromLocalFile(QFileInfo(filePath).absoluteFilePath()));

    // 2. 创建局部事件循环
    QEventLoop loop;

    // 3. 核心机制：当播放器成功解析出时长时，触发事件循环退出
    QObject::connect(&dummyPlayer, &QMediaPlayer::durationChanged, &loop, &QEventLoop::quit);

    // 🛡️ 防御性编程：万一用户选了一个损坏的 mp4 文件，播放器永远解析不出时长，
    // 为了防止程序在这里永远死锁，我们加一个 3 秒的强制超时炸弹！
    QTimer timeoutTimer;
    timeoutTimer.setSingleShot(true);
    QObject::connect(&timeoutTimer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timeoutTimer.start(3000);

    // 4. 开启循环：代码会“挂起”在这里等待，但不会卡死整个程序的 UI 刷新
    loop.exec();

    // 5. 拿到结果 (duration 返回的是毫秒)
    qint64 durationMs = dummyPlayer.duration();

    // 转换为秒并返回
    return static_cast<uint32_t>(durationMs / 1000);
}

// -------------------------------------------------------------------------
// 复位整个页面（上传成功后调用）
// -------------------------------------------------------------------------
void UploadPage::ResetUI()
{
    // 清空表单数据
    m_videoPathEdit->clear();
    m_coverPathEdit->clear();
    m_nameEdit->clear();
    m_descEdit->clear();

    // 恢复海报预览区
    m_coverPreview->clear();
    m_coverPreview->setText(u8"点击右侧选择图片");

    // 进度条归零
    m_progressBar->setValue(0);

    // 恢复按钮状态
    m_btnUpload->setEnabled(true);
    m_btnUpload->setText(u8"🚀 开始上传并入库");
}

// -------------------------------------------------------------------------
// 仅解除锁定，保留用户填写的数据（上传失败后调用）
// -------------------------------------------------------------------------
void UploadPage::UnlockUI()
{
    m_progressBar->setValue(0);                                                             // 进度条清零
    m_btnUpload->setEnabled(true);                                                          // 按钮重新可用
    m_btnUpload->setText(u8"🚀 开始上传并入库");                                              // 恢复按钮文字
    
    // 如果失败了，记得关掉抽水泵和文件
    m_chunkPumpTimer->stop();
    if (m_videoFile->isOpen()) m_videoFile->close();
}

void UploadPage::onSelectVideo()
{
    QString path = QFileDialog::getOpenFileName(this, u8"选择影片资源", "", u8"视频文件 (*.mp4 *.mkv *.avi)");
    if (!path.isEmpty()) {
        m_videoPathEdit->setText(path);
        // 如果名字为空，智能提取文件名作为默认影片名
        if (m_nameEdit->text().isEmpty()) {
            QFileInfo info(path);
            m_nameEdit->setText(info.baseName());
        }
    }
}

void UploadPage::onSelectCover()
{
    QString path = QFileDialog::getOpenFileName(this, u8"选择海报封面", "", u8"图片文件 (*.jpg *.png *.jpeg)");
    if (!path.isEmpty()) {
        m_coverPathEdit->setText(path);
        // 实时渲染缩略图
        QPixmap pix(path);
        m_coverPreview->setPixmap(pix.scaled(m_coverPreview->size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
    }
}

// =========================================================================
// 🚀 点击上传：校验并启动切片管线
// =========================================================================
void UploadPage::onUploadClicked()
{
    if (m_videoPathEdit->text().isEmpty() || m_coverPathEdit->text().isEmpty() || m_nameEdit->text().isEmpty()) {
        QMessageBox::warning(this, u8"提示", u8"请填写完整的影片信息！");
        return;
    }

    m_btnUpload->setEnabled(false);
    m_btnUpload->setText(u8"正在进行视频切片与上传 [1/2]...");
    m_progressBar->setValue(0);

    startTcpChunkUpload();
}

// =========================================================================
// 📦 管线 1：启动切片引擎
// =========================================================================
void UploadPage::startTcpChunkUpload()
{
    QString videoPath = m_videoPathEdit->text();
    if (videoPath.isEmpty()) return;

    m_btnUpload->setText(u8"正在计算文件指纹，请稍候...");

    // 🛡️ 防弹装甲：使用 QPointer 弱引用保护 UI 对象
    // 如果算 MD5 的中途用户关掉了界面 (UploadPage 被析构)，QPointer 会自动变 nullptr，防止野指针崩溃！
    QPointer<UploadPage> safeThis(this);

    // 🚀 核心跳跃：把 MD5 计算丢进你的泛型分发引擎 (丢入子线程)
    ThreadPool::Instance()->DispatchToWorker([safeThis, videoPath]() 
        {
            // =================================================================
            // [此时在子线程]：自己建个临时的 QFile 去读，绝不碰主线程的 m_videoFile
            // =================================================================
            QFile tempFile(videoPath);
            QString md5Result;
            bool calcSuccess = false;

            if (tempFile.open(QIODevice::ReadOnly)) {
                QCryptographicHash hash(QCryptographicHash::Md5);
                if (hash.addData(&tempFile)) {
                    md5Result = hash.result().toHex();
                    calcSuccess = true;
                }
                tempFile.close();
            }

            // =================================================================
            // [切回主线程]：算完之后，带着结果跳回主线程操作 UI 和启动定时器
            // =================================================================
            QMetaObject::invokeMethod(safeThis.data(), [safeThis, calcSuccess, md5Result, videoPath]() {
                // 第一时间检查 UI 是否还活着
                if (!safeThis) {
                    qDebug() << u8"[UploadPage] 界面已销毁，放弃 MD5 后续动作。";
                    return;
                }

                // A. 计算失败的情况
                if (!calcSuccess) {
                    safeThis->UnlockUI();
                    QMessageBox::critical(safeThis, u8"错误", u8"文件指纹计算失败！文件可能被占用。");
                    return;
                }

                // B. 计算成功，准备正式起飞！
                safeThis->m_currentFileMd5 = md5Result;
                qDebug() << u8"[UploadPage] 极速 MD5 计算完成:" << md5Result;

                // 回到主线程了，安全地打开主线程专属的 m_videoFile
                safeThis->m_videoFile->setFileName(videoPath);
                if (!safeThis->m_videoFile->open(QIODevice::ReadOnly)) {
                    safeThis->UnlockUI();
                    QMessageBox::critical(safeThis, u8"错误", u8"无法读取视频文件！");
                    return;
                }

                // ==========================================================
                // 1. 初始化切片状态、生成加密 Key、获取时长
                // ==========================================================
                safeThis->m_totalFileSize = safeThis->m_videoFile->size();
                safeThis->m_currentOffset = 0;
                safeThis->m_chunkIndex = 0;

                // 🔑 生成一个没有横杠的 32 位随机 UUID 作为当前视频的绝对密钥
                safeThis->m_currentEncryptKey = QUuid::createUuid().toString(QUuid::Id128);

                // ⏱️ 获取时长
                safeThis->m_currentDurationSec = safeThis->GetVideoDurationSec(videoPath);

                safeThis->m_btnUpload->setText(u8"正在进行视频切片与上传 [1/2]...");

                // 2. 轰鸣吧，抽水泵！
                safeThis->m_chunkPumpTimer->start(10);

                }, Qt::QueuedConnection);
        });
}

// =========================================================================
// ⚙️ 核心：抽水泵动作 (每次触发，切一块发送)
// =========================================================================
void UploadPage::pumpNextChunk()
{
    if (!m_videoFile->isOpen() || m_videoFile->atEnd()) {
        m_chunkPumpTimer->stop();
        return;
    }

    // 1. 每次切取 1MB 数据 (1024 * 1024)
    const int CHUNK_SIZE = 1034 * 1024;
    QByteArray chunkData = m_videoFile->read(CHUNK_SIZE);

    bool isLast = m_videoFile->atEnd();

    // ==========================================================
    // 👑 核心加密：只对第 0 块数据（包含 MP4 视频头）下毒！
    // ==========================================================
    if (m_chunkIndex == 0 && !m_currentEncryptKey.isEmpty()) 
    {
        VideoSecurityTool::XorProcessByteArray(chunkData, m_currentEncryptKey);
        qDebug() << u8"[UploadPage] 🛡️ 头部 1MB 切片已被 XOR 混淆加密！";
    }
     
    // 2. 组装 Protobuf 切片请求
    ServerApi::UploadChunkReq req;
    req.set_file_md5(m_currentFileMd5.toStdString());
    req.set_chunk_index(m_chunkIndex++);
    req.set_chunk_offset(m_currentOffset);
    req.set_chunk_data(chunkData.data(), chunkData.size());
    req.set_is_last(isLast);

    // 3. 扔给底层发送
    TCPMgr::Instance()->SendProtoMsg(ServerApi::MsgId::ID_UPLOAD_CHUNK_REQ, req);

    // 4. 更新进度与指针
    m_currentOffset += chunkData.size();
    int progress = (m_currentOffset * 100) / m_totalFileSize;
    m_progressBar->setValue(progress);

    // 5. 如果是最后一块，关闭文件，进入管线 2：发送海报和元数据
    if (isLast) {
        m_chunkPumpTimer->stop();
        m_videoFile->close();
        m_btnUpload->setText(u8"视频数据已发送，等待服务器校验...");
    }
}

// =========================================================================
// 📦 管线 2：视频传完后，一次性提交海报图片和表单信息
// =========================================================================
void UploadPage::submitMetadataToTcp()
{
    m_btnUpload->setText(u8"正在注册影片资源及海报 [2/2]...");
    m_progressBar->setValue(100);

    ServerApi::UploadMovieReq req;
    req.set_movie_name(m_nameEdit->text().toStdString());
    req.set_description(m_descEdit->toPlainText().toStdString());

    // 把刚才上传视频的专属 ID 传过去，让服务器去关联硬盘上的文件
    req.set_video_md5(m_currentFileMd5.toStdString());

    // 影片加密的密钥和影片总时长
    req.set_encrypt_key(m_currentEncryptKey.toStdString());
    req.set_duration_sec(m_currentDurationSec);

    // 👑 绝杀：直接读取海报图片的二进制数据！
    QFileInfo coverInfo(m_coverPathEdit->text());
    QFile coverFile(coverInfo.absoluteFilePath());
    if (coverFile.open(QIODevice::ReadOnly)) {
        QByteArray coverData = coverFile.readAll();                                             // 图片通常才几百KB，一口气读完没问题
        req.set_cover_data(coverData.data(), coverData.size());
        req.set_cover_suffix(coverInfo.suffix().prepend(".").toStdString());                    // 比如 ".jpg"
        coverFile.close();
    }

    // 最终一击：把配置表单扔给服务器
    TCPMgr::Instance()->SendProtoMsg(ServerApi::MsgId::ID_UPLOAD_MOVIE_REQ, req);
}