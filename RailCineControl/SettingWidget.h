#ifndef SETTINGWIDGET_H
#define SETTINGWIDGET_H

#include <QWidget>

class QComboBox;

class SettingWidget : public QWidget
{
	Q_OBJECT

public:
	explicit SettingWidget(QWidget* parent = nullptr);
	~SettingWidget();

private:
	// --- 核心功能模块化拆分 ---
	void BuildUI();
	void BindSignals();

private:
	QComboBox* m_langBox = nullptr;    // 语言选择下拉框
};

#endif // SETTINGWIDGET_H