#include "SettingWidget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QComboBox>
#include <QFrame>
#include <QJsonDocument>
#include <QJsonObject>
#include "Global.h"         
#include "JsonTool.h"       
#include "CinemaMessageBox.h"

SettingWidget::SettingWidget(QWidget* parent)
	: QWidget(parent)
{
	setAttribute(Qt::WA_StyledBackground, true);
	setObjectName("SettingWidget");
	BuildUI();
	BindSignals();
}

SettingWidget::~SettingWidget()
{
}

// ---------------------------------------------------------
// 模块 1：构建纯 UI 结构 (平铺式布局)
// ---------------------------------------------------------
void SettingWidget::BuildUI()
{
	// 根布局采用垂直布局
	QVBoxLayout* root = new QVBoxLayout(this);
	root->setContentsMargins(40, 30, 40, 30); // 给四周留出舒适的呼吸空间
	root->setSpacing(20);

	// 1. 大标题
	QLabel* mainTitle = new QLabel(tr("系统设置"), this);
	mainTitle->setObjectName("settingMainTitle");
	// 建议在QSS中设置较大的字号和加粗，例如: font-size: 24px; font-weight: bold;
	root->addWidget(mainTitle);

	// ==========================================================
	// 分组 1：常规设置 (General Settings)
	// ==========================================================
	QFrame* generalGroup = new QFrame(this);
	generalGroup->setObjectName("settingGroupFrame"); // 建议在QSS加个背景色和圆角，如 background: #2D323E; border-radius: 8px;
	QVBoxLayout* generalLayout = new QVBoxLayout(generalGroup);
	generalLayout->setContentsMargins(20, 20, 20, 20);
	generalLayout->setSpacing(15);

	// 小标题
	QLabel* generalTitle = new QLabel(tr("常规设置"), generalGroup);
	generalTitle->setObjectName("settingGroupTitle");
	// 建议QSS: font-size: 16px; color: #888;
	generalLayout->addWidget(generalTitle);

	// --- 设置项：界面语言 ---
	QHBoxLayout* langLayout = new QHBoxLayout();
	QLabel* langLbl = new QLabel(tr("界面语言:"), generalGroup);

	m_langBox = new QComboBox(generalGroup);
	m_langBox->setObjectName("settingComboBox");
	m_langBox->setFixedSize(160, 35);
	m_langBox->setCursor(Qt::PointingHandCursor);
	// 添加选项 (文字是显示给用户看的，Data 是存在 JSON 里的代码)
	m_langBox->addItem("简体中文", "zh");
	m_langBox->addItem("English", "en");

	// 读取当前系统的语言配置，设置下拉框的默认选中项
	QJsonDocument doc;
	JsonTool::Instance()->readJsonFile(AppConfigPath, doc);
	QString currentLang = doc.object()["Language"].toString("zh");
	int index = m_langBox->findData(currentLang);
	if (index != -1) {
		m_langBox->setCurrentIndex(index);
	}

	langLayout->addWidget(langLbl);
	langLayout->addWidget(m_langBox);
	langLayout->addStretch(); // 把控件推到左边，右边留白

	generalLayout->addLayout(langLayout);
	// ==========================================================

	// 将“常规设置”分组加入根布局
	root->addWidget(generalGroup);

	// ==========================================================
	// 占位符：以后在这里添加新的分组 (如 "高级设置", "网络配置" 等)
	// ==========================================================
	// QFrame* advancedGroup = new QFrame(this); ...
	// root->addWidget(advancedGroup);

	// 最底下加一个弹簧，把所有的设置块往上顶
	root->addStretch(1);
}

// ---------------------------------------------------------
// 模块 2：信号与槽绑定
// ---------------------------------------------------------
void SettingWidget::BindSignals()
{
	// 绑定语言切换事件
	connect(m_langBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) 
		{
			// 1. 获取选中的语言代码 ("zh" 或 "en")
			QString langCode = m_langBox->itemData(index).toString();

			// 2. 读取现有的系统配置文件
			QJsonDocument doc;
			JsonTool::Instance()->readJsonFile(AppConfigPath, doc);
			QJsonObject obj = doc.object();

			// 3. 修改 Language 字段并写回
			obj["Language"] = langCode;
			doc.setObject(obj);
			JsonTool::Instance()->writeJsonFile(AppConfigPath, doc);

			// 4. 弹出重启生效提示 (使用你自定义的提示框)
			CinemaMessageBox::ShowInfo(this, tr("提示"), tr("语言设置已保存，将在下次启动软件时生效。"));
		});
}