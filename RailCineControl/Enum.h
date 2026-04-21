#ifndef ENUM_H
#define ENUM_H

enum Modules
{
	REGISTERMOD		                        = 0,				// 注册模块
	LOGINMOD		                        = 1,				// 登录模块
};

/* 标签错误类型 */
enum TipErr {
    TIP_SUCCESS                             = 0,
    TIP_EMAIL_ERR                           = 1,
    TIP_PWD_ERR                             = 2,
    TIP_CONFIRM_ERR                         = 3,
    TIP_PWD_CONFIRM                         = 4,
    TIP_VERIFY_ERR                          = 5,
    TIP_USER_ERR                            = 6
};

enum ClickLabelState
{
    Normal = 0,                                                 // 普通状态
    Selected = 1,                                               // 选中状态
};


// 自定义QListWidgetItem的几种类型
enum ListItemType {
    CHAT_USER_ITEM,                                             // 聊天用户
    CONTACT_USER_ITEM,                                          // 联系人用户
    SEARCH_USER_ITEM,                                           // 搜索到的用户
    ADD_USER_TIP_ITEM,                                          // 提示添加用户
    INVALID_ITEM,                                               // 不可点击条目
    GROUP_TIP_ITEM,                                             // 分组提示条目
    LINE_ITEM,                                                  // 分割线
    APPLY_FRIEND_ITEM,                                          // 好友申请
};

// 聊天界面模式
enum DemandUIMode {
    SearchMode,                                                 // 搜索模式
    InstallMode,                                                // 安装模式
    MainMode,                                                   // 联系模式
    SettingsMode,                                               // 设置模式
};

// 角色类型
enum ChatRole
{
    Self,                                                       // 自己
    Other,                                                      // 其他人
};

// 界面状态
enum UIStatus
{
    LOGIN_STATUS,                                               // 登录状态
    REGISTER_STATUS,                                            // 注册状态
    RESET_STATUS,                                               // 重置密码状态
    CHAT_STATUS,                                                // 聊天状态
};

/* 游戏卡片信息 */
enum GameCardInfo
{
    IdRole = Qt::UserRole + 1,                                  // 游戏唯一ID（QString）。双击/启动等逻辑用它定位条目
    TitleRole,                                                  // 标题（QString），卡片下方大写文字
    CoverRole,                                                  // 封面（通常 QString：资源/文件路径；也可用 QPixmap，但务必统一）
    MinPlayersRole,                                             // 最小玩家数（int）
    MaxPlayersRole,                                             // 最大玩家数（int）
    InstalledRole,                                              // 是否已安装（bool）
    ComingSoonRole                                              // 是否即将上线（bool）
};

#endif // ENUM_H