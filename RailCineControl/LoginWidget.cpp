#include "LoginWidget.h"
#include <QJsonObject>
#include <QJsonDocument>
#include <QPixmap>
#include <QPainter>
#include <QPainterPath>
#include <QGraphicsDropShadowEffect>
#include <QLabel>
#include <QCheckBox>
#include <QFontDatabase>
#include <QFont>
#include <QApplication>
#include <QMessageBox>
#include "TCPMgr.h"
#include "LogRecord.h"
#include "Global.h"
#include "UserMgr.h"
#include "JsonTool.h"

LoginWidget::LoginWidget(QWidget*parent)
	: QWidget(parent), _Parent(parent)
{
	setFixedSize(1100, 700);

	setAttribute(Qt::WA_StyledBackground, true);
	m_bgRaw = QPixmap(":/MiNi/Images/MiNiWorld/Login/Background.png");

	BuildUI();
	ApplyStyle();
	LoadFont();
	qApp->setFont(m_font);

	hide();

	BindSlots();

	QTimer::singleShot(1000, this, [this]()
		{
			emit sig_connect_tcp();															// 与ChatServer建立稳定的TCP连接
		}
	);
}

LoginWidget::~LoginWidget()
{
	if (m_remember->checkState())
	{
		WriteLoginConfig();
	}
}

void LoginWidget::BuildUI()
{
	// 背景层
	m_bg = new QLabel(this);
	m_bg->setScaledContents(true);															
	m_bg->lower();
	
	// 设置背景
	const QSize s = size();
	QPixmap fit = m_bgRaw.scaled(s, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
	m_bg->setPixmap(fit);
	m_bg->setGeometry(rect());

	// 中央面板
	m_panel = new QWidget(this);
	m_panel->setObjectName("panel");
	m_panel->setMinimumWidth(560);

	// 阴影
	auto* shadow = new QGraphicsDropShadowEffect(this);
	shadow->setOffset(0, 8);
	shadow->setBlurRadius(32);
	shadow->setColor(QColor(0, 0, 0, 150));
	m_panel->setGraphicsEffect(shadow);

	// 内容
	m_logo = new QLabel(m_panel);
	m_logo->setObjectName("logo");
	m_logo->setPixmap(QPixmap(":/MiNi/Images/MiNiWorld/Login/Logo.png"));
	m_logo->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
	m_logo->setMinimumHeight(96);
	m_logo->setScaledContents(true);

	m_userLbl = new QLabel(QStringLiteral("账号"), m_panel);
	m_userEdit = new QLineEdit(m_panel);
	m_userEdit->setPlaceholderText(QStringLiteral("请输入账号"));

	m_passLbl = new QLabel(QStringLiteral("密码"), m_panel);
	m_passEdit = new QLineEdit(m_panel);
	m_passEdit->setPlaceholderText(QStringLiteral("请输入密码"));
	// m_passEdit->setEchoMode(QLineEdit::Password);

	m_remember = new QCheckBox(QStringLiteral("记住密码"), m_panel);
	m_remember->setChecked(true);
	m_loginBtn = new QPushButton(QStringLiteral("登 录"), m_panel);
	m_loginBtn->setCursor(Qt::PointingHandCursor);
	m_loginBtn->setFixedHeight(54);

	// 布局（面板）
	auto* form = new QVBoxLayout(m_panel);
	form->setContentsMargins(32, 32, 32, 32);
	form->setSpacing(16);
	form->addWidget(m_logo, 0, Qt::AlignHCenter);

	auto addField = [&](QLabel* lbl, QLineEdit* edit) {
		lbl->setObjectName("fieldLabel");
		auto* v = new QVBoxLayout();
		v->setSpacing(8);
		v->addWidget(lbl);
		v->addWidget(edit);
		form->addLayout(v);
		};
	addField(m_userLbl, m_userEdit);
	addField(m_passLbl, m_passEdit);

	form->addWidget(m_remember);
	form->addSpacing(4);
	form->addWidget(m_loginBtn);

	// 外层布局：让面板居中
	auto* outer = new QHBoxLayout(this);
	outer->setContentsMargins(48, 48, 48, 48);
	outer->addStretch();
	outer->addWidget(m_panel, 0, Qt::AlignVCenter);
	outer->addStretch();
	setLayout(outer);
}

void LoginWidget::ApplyStyle()
{
	const QString qss = QString(R"(
        QWidget { color: #EAF2FF; }
        #panel { background: rgba(0,0,0,180); border-radius: 16px; }
        #logo  { margin-top: 8px; margin-bottom: 8px; }
        #fieldLabel { color: #CBE1FF; font-size: 14px; }

        QLineEdit {
            height: 44px; padding: 0 14px;
            background: rgba(255,255,255,0.6);
            border: 1px solid rgba(255,255,255,0.15);
            border-radius: 8px; color: #FFFFFF;
            selection-background-color: #3aa7ff;
            selection-color: #061018;
        }
        QLineEdit:hover { border-color: #4DC3FF; }
        QLineEdit:focus { border: 1px solid #4DC3FF; }

        QCheckBox { spacing: 8px; color: #A9A9A9; }
        QCheckBox::indicator {
            width: 18px; height: 18px; border-radius: 9px;
            border: 1px solid rgba(255,255,255,0.35);
            background: rgba(255,255,255,0.08);
        }

		/* 选中：带绿色勾的框 */
		QCheckBox::indicator:checked 
		{
		    image: url(:/MiNi/Images/MiNiWorld/Login/BoxCheck.png);
		}

        QPushButton 
		{
			color: #905900; font-weight: 700; font-size: 20px;
            border: none; border-radius: 10px;
            background-image: url(:/MiNi/Images/MiNiWorld/Login/NormalBtn.png);
            background-position: center; background-repeat: no-repeat;
        }
		QPushButton:hover 
		{
			background-image: url(:/MiNi/Images/MiNiWorld/Login/HoverBtn.png);
		}
		QPushButton:pressed 
		{
		    background-image: url(:/MiNi/Images/MiNiWorld/Login/PressBtn.png);
		    margin-top: 2px;                /* 整个按钮向下“压”2px */
		    margin-bottom: 0px;
		}

    )");

	setStyleSheet(qss);
}

void LoginWidget::LoadFont()
{
	const int id = QFontDatabase::addApplicationFont(":/MiNi/Images/MiNiWorld/Login/FZTingBian.ttf");
	if (id >= 0) 
	{
		const QString fam = QFontDatabase::applicationFontFamilies(id).value(0);
		if (!fam.isEmpty()) 
		{
			m_font = QFont(fam);
			m_font.setPointSize(12);
			setFont(m_font);
		}
	}
}

void LoginWidget::BindSlots()
{
	connect(m_loginBtn, &QPushButton::clicked, this, &LoginWidget::OnLoginButtonClicked);										// 登录按钮点击事件

	connect(this, &LoginWidget::sig_connect_tcp, TCPMgr::Instance().get(), &TCPMgr::SlotTcpConnect);
	connect(TCPMgr::Instance().get(), &TCPMgr::SigConnectSuccess, this, &LoginWidget::slot_tcp_con_finished);
	connect(TCPMgr::Instance().get(), &TCPMgr::SigLoginFailed, this, &LoginWidget::slot_login_failed);
}

bool LoginWidget::EnableBtn(bool enable)
{
	m_loginBtn->setEnabled(enable);
	return enable;
}

void LoginWidget::AutoLogin()
{
	if (!bIsAutoLogin)
		return;

	bIsAutoLogin = false;

	QJsonDocument doc;
	JsonTool::Instance()->readJsonFile(LoginConfigPath, doc);
	QJsonObject obj = doc.object();
	if (obj.isEmpty())
	{
		qDebug() << "AutoLogin: config file is empty";
		show();
		_Parent->show();
		return;
	}
	
	QString user = obj["Account"].toString();
	QString pass = obj["Password"].toString();

	if (user.isEmpty() || pass.isEmpty())
	{
		qDebug() << "AutoLogin: user or pass is empty";
		show();
		_Parent->show();
		return;
	}

	qDebug() << "AutoLogin: user:" << user << " pass:" << pass;

	m_userEdit->setText(user);
	m_passEdit->setText(pass);
	OnLoginButtonClicked();
}

void LoginWidget::WriteLoginConfig()
{
	UserInfo user_info = UserMgr::Instance()->getUserInfo();

	QJsonObject login_json;
	login_json["Account"] = user_info.UserName;
	login_json["Password"] = user_info.Password;
	login_json["icon"] = "";

	// 写入配置文件中
	QJsonDocument login_doc(login_json);
	JsonTool::Instance()->writeJsonFile(LoginConfigPath, login_doc);
}

/* 检查用户名是否合法 */
bool LoginWidget::CheckUserValid()
{
	if (m_userEdit->text().isEmpty())
	{
		qDebug() << QString::fromLocal8Bit("用户名不能为空");
		return false;
	}

	return true;
}

/* 检验密码是否合法 */
bool LoginWidget::CheckPasswordValid()
{
	QString passText = m_passEdit->text();
	if (passText.length() < 6 || passText.length() > 15)
	{
		qDebug() << QString::fromLocal8Bit("密码长度必须在6-15位之间");
		return false;
	}

	/* 密码长度至少6位 可以是字母、数字、特定的特殊字符 */
	QRegularExpression regExp("^[a-zA-Z0-9!@#$%^&*]{6,15}$");
	bool match = regExp.match(passText).hasMatch();
	if (!match)
	{
		qDebug() << QString::fromLocal8Bit("密码只能包含字母、数字、!@#$%^&*");
		return false;
	}

	return true;
}

/* 登录按钮点击时 */
void LoginWidget::OnLoginButtonClicked()
{
	if (!CheckUserValid())
	{
		EnableBtn(true);
		return;
	}
		
	if (!CheckPasswordValid())
	{
		EnableBtn(true);
		return;
	}

	QString user = m_userEdit->text();
	QString pass = m_passEdit->text();

	// 设置用户名和密码
	UserInfo user_info;
	user_info.UserName = user;
    user_info.Password = pass;
	UserMgr::Instance()->setUserInfo(user_info);

	// 禁止重复点击
	EnableBtn(false);

	// 写入配置文件中
	WriteLoginConfig();

	// 开始记录日志
	QString FileName = QString("Log_%1.txt").arg(user);
	LogRecord::startRecord(FileName);

	TCPMgr::Instance()->Login(user, pass);												// 发送登录请求

	QTimer::singleShot(2000, this, [this]()
		{
			EnableBtn(true);
		}
	);
}

/* 如果连接聊天服务器成功，则发送登录请求 */
void LoginWidget::slot_tcp_con_finished(bool success)
{
	if (success)
	{
		qDebug() << QString::fromLocal8Bit("网络正常,可以登录");
		// 自动登录
		AutoLogin();
	}
	else
	{
		qDebug() << QString::fromLocal8Bit("网络异常,连接服务器失败，无法登录");
		EnableBtn(true);
		_Parent->show();
	}
}

void LoginWidget::slot_login_failed(int errCode)
{
	QString result;
	if (errCode == ErrorCodes::LOGIN_USER_EXIT_ERR)
	{
		result = QString::fromLocal8Bit("用户已登录，请勿重复登录");

	}
	else if (errCode == ErrorCodes::LOGIN_PWD_ERR)
	{
		result = QString::fromLocal8Bit("密码错误");
	}
	else if (errCode == ErrorCodes::LOGIN_USER_NOT_EXIST_ERR)
	{
		result = QString::fromLocal8Bit("用户不存在");
	}

	QMessageBox msgBox(QMessageBox::Warning,
		QString::fromLocal8Bit("登录失败"),
		result,
		QMessageBox::Ok,
		this);

	// 强制设置文本标签的颜色为黑色
	msgBox.setStyleSheet("QLabel{ color: black; font-weight: bold; }");

	_Parent->show();

	msgBox.exec();

	qDebug() << QString::fromLocal8Bit("登录失败,错误码:") << errCode << "  " << result;
	EnableBtn(true);
}