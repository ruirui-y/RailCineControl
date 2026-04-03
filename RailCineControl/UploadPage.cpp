#include "UploadPage.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QFileDialog>
#include <QMessageBox>
#include <QUuid>
#include <QFileInfo>
#include <QDebug>
#include "TCPMgr.h"

UploadPage::UploadPage(QWidget* parent) : QWidget(parent)
{
    // 初始化抽水泵和文件指针
    m_videoFile = new QFile(this);
    m_chunkPumpTimer = new QTimer(this);

    // 绑定抽水泵动作：每次定时器触发，就切一块发给 TCP
    connect(m_chunkPumpTimer, &QTimer::timeout, this, &UploadPage::pumpNextChunk);

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

    btnLayout->addStretch();
    btnLayout->addWidget(m_btnUpload, 1);
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
    m_videoFile->setFileName(m_videoPathEdit->text());
    if (!m_videoFile->open(QIODevice::ReadOnly))
    {
        UnlockUI();
        QMessageBox::critical(this, u8"错误", u8"无法读取视频文件！");
        return;
    }

    // 1. 初始化切片状态
    m_totalFileSize = m_videoFile->size();
    m_currentOffset = 0;
    m_chunkIndex = 0;

    // 💡 实战技巧：为了不卡死 UI，用 UUID 代替全量计算 MD5 来作为文件的唯一标识
    m_currentFileMd5 = QUuid::createUuid().toString().remove("{").remove("}").remove("-");

    // 2. 开启抽水泵！(间隔 10 毫秒抽一次，配合下面的 1MB 块，理论最高速度约 100MB/s)
    m_chunkPumpTimer->start(10);
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

        submitMetadataToTcp();
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