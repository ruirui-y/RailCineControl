#pragma once
#include <QTableWidget>
#include <QHeaderView>
#include <QStringList>

class CinemaTableWidget : public QTableWidget
{
    Q_OBJECT
public:
    explicit CinemaTableWidget(QWidget* parent = nullptr) : QTableWidget(parent)
    {
        // ==========================================
        // 核心骨架配置 (一次编写，全局生效)
        // ==========================================
        this->setAlternatingRowColors(true);                                // 开启交替行斑马纹
        this->setEditTriggers(QAbstractItemView::NoEditTriggers);           // 单元格只读，禁止双击编辑
        this->setSelectionBehavior(QAbstractItemView::SelectRows);          // 点击时默认选中整行
        this->verticalHeader()->setVisible(false);                          // 隐藏左侧丑陋的默认自带行号
        this->setFocusPolicy(Qt::NoFocus);                                  // 消除点击时的焦点虚线框

        // 优化：点击表头时，不要让表头文字变粗高亮，保持冷峻的工业感
        this->horizontalHeader()->setHighlightSections(false);
    }

    ~CinemaTableWidget() override = default;

    // 👑 暴露给外界：一键设置表头与列数
    void setHeaders(const QStringList& headers)
    {
        // 1. 根据传入的表头数组大小，自动设置列数
        this->setColumnCount(headers.size());

        // 2. 设置表头文字
        this->setHorizontalHeaderLabels(headers);

        // 3. 默认让所有列均匀拉伸填满整个控件宽度
        this->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    }
};