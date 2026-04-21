#ifndef CINEMAMESSAGEBOX_H
#define CINEMAMESSAGEBOX_H

#include "CinemaDialogBase.h"
#include <QLabel>
#include <QPushButton>
#include <QHBoxLayout>

class CinemaMessageBox : public CinemaDialogBase
{
    Q_OBJECT
public:
    enum Type { Info, Warning, Error };

    static void ShowInfo(QWidget* parent, const QString& title, const QString& text) {
        CinemaMessageBox box(parent, title, text, Info);
        box.exec();
    }
    static void ShowWarning(QWidget* parent, const QString& title, const QString& text) {
        CinemaMessageBox box(parent, title, text, Warning);
        box.exec();
    }
    static void ShowError(QWidget* parent, const QString& title, const QString& text) {
        CinemaMessageBox box(parent, title, text, Error);
        box.exec();
    }

private:
    CinemaMessageBox(QWidget* parent, const QString& title, const QString& text, Type type)
        : CinemaDialogBase(parent)
    {
        this->resize(400, 220);                                         // 稍微加宽一点，显得更大气

        // 1. 设置带 Emoji 的标题
        QString fullTitle;
        if (type == Info) fullTitle = u8"💡 " + title;
        else if (type == Warning) fullTitle = u8"⚠️ " + title;
        else fullTitle = u8"❌ " + title;
        this->SetDialogTitle(fullTitle);

        QVBoxLayout* content = this->GetContentLayout();

        // 2. 核心提示文字 (彻底交给 QSS)
        QLabel* lbl_msg = new QLabel(text, this);
        lbl_msg->setObjectName("CinemaMsgLabel");                       // 👑 绑定专属 ID
        lbl_msg->setWordWrap(true);
        lbl_msg->setAlignment(Qt::AlignCenter);

        // 3. 底部确认按钮
        QHBoxLayout* btn_layout = new QHBoxLayout();
        QPushButton* btn_ok = new QPushButton(u8"我知道了", this);
        btn_ok->setMinimumWidth(120);
        btn_ok->setMinimumHeight(38);                                   // 撑起按钮高度

        // 4. 根据类型分配专属样式的 ID
        if (type == Info) {
            btn_ok->setObjectName("btnCinemaInfo");                     // 科技蓝
        }
        else if (type == Warning) {
            btn_ok->setObjectName("btnCinemaWarning");                  // 影院金
        }
        else {
            btn_ok->setObjectName("btnCinemaError");                    // 警示红
        }

        btn_layout->addStretch();
        btn_layout->addWidget(btn_ok);
        btn_layout->addStretch();

        content->addStretch();
        content->addWidget(lbl_msg);
        content->addStretch();
        content->addLayout(btn_layout);

        connect(btn_ok, &QPushButton::clicked, this, &CinemaMessageBox::accept);
    }
};

#endif // CINEMAMESSAGEBOX_H