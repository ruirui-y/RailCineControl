#include "UploadPage.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QFileDialog>
#include <QMessageBox>
#include <QTimer>
#include <QDebug>

UploadPage::UploadPage(QWidget* parent) : QWidget(parent)
{
    m_currentProgress = 0;
    m_mockTimer = new QTimer(this);
    connect(m_mockTimer, &QTimer::timeout, this, &UploadPage::onSimulateProgress);

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

void UploadPage::onUploadClicked()
{
    // 1. 表单预校验
    if (m_videoPathEdit->text().isEmpty() || m_coverPathEdit->text().isEmpty() || m_nameEdit->text().isEmpty()) {
        QMessageBox::warning(this, u8"提示", u8"请填写完整的影片信息（带 * 为必填项）！");
        return;
    }

    // 2. 锁定 UI
    m_btnUpload->setEnabled(false);
    m_btnUpload->setText(u8"上传中，请稍候...");
    m_currentProgress = 0;
    m_progressBar->setValue(0);

    // 💡 核心业务逻辑占位：
    // 在这里，你应该开启一个 QThread 或者调用你封装的 HTTP 工具类：
    // 1. 计算 m_videoPathEdit 文件的 MD5。
    // 2. 检查秒传。如果无法秒传，则将物理文件 POST 上传至云盘 OSS。
    // 3. 将表单数据 (m_nameEdit, desc, md5, OSS路径等) 组合成 JSON，发送给后端的 INSERT 接口。

    // 这里启动一个定时器模拟网络上传过程...
    m_mockTimer->start(30);
}

void UploadPage::onSimulateProgress()
{
    m_currentProgress += 2;
    m_progressBar->setValue(m_currentProgress);

    if (m_currentProgress >= 100) {
        m_mockTimer->stop();
        m_btnUpload->setEnabled(true);
        m_btnUpload->setText(u8"🚀 开始上传并入库");

        QMessageBox::information(this, u8"成功", u8"影片已成功上传并同步至云端数据库！");

        // 👑 极其重要：发射信号，让外部大管家去通知 PlaybackPage 刷新列表
        emit uploadFinished();

        // 可选：清空表单准备下一次录入
        m_videoPathEdit->clear();
        m_coverPathEdit->clear();
        m_nameEdit->clear();
        m_descEdit->clear();
        m_coverPreview->clear();
        m_coverPreview->setText(u8"点击右侧选择图片");
        m_progressBar->setValue(0);
    }
}