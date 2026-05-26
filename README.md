# RailCineControl —— 轨道影院中控系统

## 项目简介

**RailCineControl** 是一个全栈式的影院终端控制与数字资产管理系统，包含一个功能丰富的 **Qt 桌面客户端** 以及一个基于 **SOA（面向服务架构）** 的高并发 **C++ 服务端**。系统支持影片资源下发、局域网游戏串流分发、用户数字钱包管理、微信扫码支付中台对接以及精确的播放行为数据埋点。

## 系统架构

```text
┌──────────────────────────────────────────────────────────────────────────┐
│                        RailCineControl Client (Qt)                       │
│ ┌───────────┐ ┌───────────┐ ┌────────────┐ ┌────────────┐ ┌───────────┐  │
│ │MovieWidget│ │GameWidget │ │WalletWidget│ │UploadPage  │ │   Login   │  │
│ └───────────┘ └───────────┘ └────────────┘ └────────────┘ └───────────┘  │
│       │             │              │              │             │        │
│       └─────────────┴──────────────┴──────────────┴─────────────┘        │
│                                 │ Qt 信号/槽                              │
│                  ┌──────────────┴──────────────┐                         │
│                  │ ControHub（客户端核心调度层）  │                         │
│                  └──────┬──────────────┬───────┘                         │
│           ┌─────────────┴──┐ ┌─────────┴─────────┐ ┌──────────────┐      │
│           │ TCPMgr (长连接) │ │UdpManager(设备发现)│ │ LocalStream  │      │
│           └────────────────┘ └───────────────────┘ └──────────────┘      │
└──────────────────────────────────┬───────────────────────────────────────┘
                                   │ Protobuf (common.proto / server_msg.proto)
┌──────────────────────────────────┴───────────────────────────────────────┐
│                       RailCineControlServer (SOA)                        │
│ ┌────────────┐   ┌─────────────────────────────────────────────────────┐ │
│ │  TcpServer │   │              MsgDispatcher (单例)                    │ │
│ │  (I/O线程)  │   │ ┌────────┬──────────┬──────────┬──────────┐         │ │
│ │            │──▶│ │AuthSvc │FileTrSvc │MovResSvc │PlayRecSvc│        │ │
│ │ HttpServer │   │ └────────┴──────────┴──────────┴──────────┘         │ │
│ │ (Webhook)  │   │ ┌────────┬──────────┬──────────┬──────────┐         │ │
│ │            │   │ │WalletSvc│  PaySvc  │OrdMgSvc  │GameResSvc│        │ │
│ └─────┬──────┘   │ └────────┴──────────┴──────────┴──────────┘         │ │
│       │          │          ▲                  │                       │ │
│ ┌─────┴───────┐  │     ThreadPool        ConnectionPool                │ │
│ │ClientSession│  └─────────────────────────────────────────────────────┘ │
│ │(连接生命周期)│   ┌─────────────────────────────────────────────────────┐ │
│ └─────────────┘  │                      MySQL                          │ │
└──────────────────┴─────────────────────────────────────────────────────┘
```



**核心设计原则：**

- **Server 只管会话，会话只管理连接生命周期** —— TcpServer 仅负责 accept 新连接并创建 `ClientSession`，不直接处理任何业务逻辑
- **事件分发器 (MsgDispatcher) 专职业务转发** —— 单例模式，根据消息 ID 将请求跨线程投递至对应的 Service，IO 线程与业务线程彻底解耦
- **前端 UI 与逻辑分离** —— Qt `.ui` 文件管理布局，`.qss` 样式表管理视觉特效，`.cpp` 中仅保留交互逻辑

## 客户端架构详解

客户端基于 **Qt 5.14+** 构建，采用多线程架构，UI 与业务逻辑严格分离：

| 模块                  | 职责                                              | 关键文件                                                     |
| :-------------------- | :------------------------------------------------ | :----------------------------------------------------------- |
| **主窗口与页面管理**  | 统筹全局页面切换、登录态管理                      | `mainWindow.cpp/h`, `ControlHubWindow.cpp/h`                 |
| **TCPMgr**            | 长连接管理与 Protobuf 数据交换，封装粘包/拆包逻辑 | `TCPMgr.cpp/h`                                               |
| **UdpManager**        | 局域网 UDP 广播，实现节点自动发现                 | `UdpManager.cpp/h`                                           |
| **LocalStreamServer** | 本地流媒体推流服务                                | `LocalStreamServer.cpp/h`                                    |
| **CMDTools**          | 进程级管理，封装外部程序（游戏）的拉起与状态监控  | `CMDTools.cpp/h`                                             |
| **MovieWidget**       | 影片列表展示、下载与播放触发                      | `MovieWidget.cpp/h`                                          |
| **GameWidget**        | 游戏列表展示、安装与启动，支持版本热更新          | `GameWidget.cpp/h`                                           |
| **WalletWidget**      | 钱包余额、套餐查询与充值入口                      | `WalletWidget.cpp/h`                                         |
| **UploadPage**        | 影片/游戏资源上传（分片）                         | `UploadPage.cpp/h`                                           |
| **PlaybackPage**      | 播放行为埋点记录                                  | `PlaybackPage.cpp/h`                                         |
| **LoginWidget**       | 用户登录与顶号检测                                | `LoginWidget.cpp/h`                                          |
| **SettingWidget**     | 中英文切换等系统设置                              | `SettingWidget.cpp/h`                                        |
| **自定义 UI 组件**    | 标题栏、消息框、表格、支付弹窗等自绘控件          | `TitleBar.cpp/h`, `CinemaMessageBox.h`, `CinemaTableWidget.h`, `CinemaPayDialog.h` |
| **样式系统**          | QSS 集中管理全部视觉特效                          | `StyleSheet/stylesheet.qss`                                  |

### QSS 特效示例

所有视觉效果（圆角、渐变、阴影、动画）均通过 QSS 实现，不依赖图片资源。例如：

```css
/* 影片海报卡片 - 圆角 + 渐变边框 */
MovieWidget {
    background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
                                stop:0 #1a1a2e, stop:1 #16213e);
    border: 2px solid transparent;
    border-radius: 12px;
    padding: 8px;
}

/* 支付按钮 - 悬停渐变动画 */
QPushButton#payBtn {
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                                stop:0 #e94560, stop:1 #c23152);
    border-radius: 8px;
    color: white;
    font-size: 16px;
    padding: 10px 24px;
}
QPushButton#payBtn:hover {
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                                stop:0 #ff6b81, stop:1 #e94560);
}
```



### 多线程架构

- **主线程（GUI 线程）** ：仅负责界面渲染与用户交互，不执行任何阻塞操作
- **TCPMgr 工作线程**：负责网络 I/O，接收服务端推送并分发至对应 Widget
- **UdpManager 线程**：独立线程执行 UDP 广播与监听
- **ThreadPool**：通用线程池，处理文件上传/下载、本地流推流等耗时任务

## 服务端架构详解

服务端同样基于 **Qt** 构建，采用 SOA 模式，从单体 `ClientSession` 重构为 **8 大独立 Service + 全局事件分发器**。

### 核心组件

| 组件                      | 职责                                                         | 关键文件                   |
| :------------------------ | :----------------------------------------------------------- | :------------------------- |
| **TcpServer**             | 管理 TCP 监听、accept 新连接、创建 ClientSession；**不含任何业务逻辑** | `TcpServer.cpp/h`          |
| **ClientSession**         | 封装单个连接的完整生命周期：socket 读写、心跳检测、断线清理  | `ClientSession.cpp/h`      |
| **MsgDispatcher（单例）** | 全局事件分发器，根据消息 ID 跨线程投递至对应 Service         | `MsgDispatcher.cpp/h`      |
| **ThreadPool**            | Worker 线程池，Service 的业务逻辑在此执行，与 IO 线程隔离    | `ThreadPool.cpp/h`         |
| **WorkerThread**          | 单个工作线程封装                                             | `WorkerThread.cpp/h`       |
| **ConnectionPool**        | MySQL 数据库连接池，复用连接、限流保护                       | `ConnectionPool.cpp/h`     |
| **HttpServerMgr**         | 轻量级 HTTP 服务，监听微信支付 Webhook 回调                  | `HttpServerMgr.cpp/h`      |
| **MidPlatformManager**    | 支付中台对接：生成二维码、主动轮询交易流水                   | `MidPlatformManager.cpp/h` |
| **SqlExec**               | SQL 语句执行封装                                             | `SqlExec.cpp/h`            |
| **LogRecord**             | 本地日志记录                                                 | `LogRecord.cpp/h`          |
| **Global**                | 全局配置管理与初始化                                         | `Global.cpp/h`             |

### 八大核心 Service

| 服务                       | 职责                                                         | 关键文件                       |
| :------------------------- | :----------------------------------------------------------- | :----------------------------- |
| **AuthService**            | 登录校验、互斥登录（顶号机制）及心跳保活                     | `AuthService.cpp/h`            |
| **FileTransferService**    | 大文件的分片上传与断点下载                                   | `FileTransferService.cpp/h`    |
| **MovieResourceService**   | 影片元数据增删改查及海报下发                                 | `MovieResourceService.cpp/h`   |
| **MoviePlayRecordService** | 播放时长、频次等行为数据埋点                                 | `MoviePlayRecordService.cpp/h` |
| **WalletService**          | 用户数字资产、套餐查询与积分流水拉取                         | `WalletService.cpp/h`          |
| **PaymentService**         | 支付中枢：生成付款二维码、消化 HTTP 回调、处理跨表资金事务   | `PaymentService.cpp/h`         |
| **OrderManagementService** | 幽灵守护任务：不依赖任何网络请求，定时巡检过期/异常订单并关闭 | `OrderManagementService.cpp/h` |
| **GameResourceService**    | 游戏安装包的分发与版本控制                                   | `GameResourceService.cpp/h`    |

### 事件分发机制（MsgDispatcher）

这是服务端架构的核心创新点。传统做法中，每个 `ClientSession` 持有自己的事件分发器，导致：

- 每个连接独占一组 Service 资源，内存开销大
- 业务逻辑与连接生命周期耦合，难以独立维护

重构后的方案：

1. **MsgDispatcher 被设计为全局单例**，所有 ClientSession 共享同一份 Service 注册表
2. 网络 IO 线程收到 Protobuf 消息后，仅做解包操作，通过 MsgDispatcher 将请求跨线程投递至 Worker 线程
3. Worker 线程中的 Service 执行业务逻辑（数据库操作、支付对接等）
4. 业务处理完成后，通过信号/槽或回调将结果返回给 ClientSession，由它负责向客户端发送响应

```text
[Client] ──TCP──▶ [TcpServer IO线程]
                        │
                  解包 Protobuf
                        │
                        ▼
               [MsgDispatcher 单例]
                  │         │
           消息ID路由    ThreadPool
                  │         │
                  ▼         ▼
              [AuthService] [PaymentService] [WalletService] ...
                  │         │
                  └────┬────┘
                       ▼
              [ClientSession 发送响应]
```



## 数据流示例：微信支付闭环

```text
用户点击充值
    │
    ▼
[WalletWidget] ──Protobuf──▶ [TcpServer] ──▶ [MsgDispatcher]
                                                  │
                                                  ▼
                                           [PaymentService]
                                         生成支付二维码URL
                                                  │
                                         ① 返回二维码给客户端
                                         ② 启动轮询定时器
                                                  │
                                    ┌─────────────┴─────────────┐
                                    ▼                           ▼
                            [HttpServerMgr]              [MidPlatformManager]
                          监听微信 Webhook              主动查询交易流水
                                    │                           │
                                    └─────────┬─────────────────┘
                                              ▼
                                    [PaymentService]
                                   钱包扣款 + 流水生成 + 订单更新
                                   （ConnectionPool 保证事务一致性）
                                              │
                                              ▼
                                    [TcpServer 推送支付结果]
                                              │
                                              ▼
                                    [WalletWidget 更新余额]
```



## 技术栈

| 层级           | 技术                             | 说明                                                         |
| :------------- | :------------------------------- | :----------------------------------------------------------- |
| **语言**       | C++ 17                           | 客户端与服务端统一语言                                       |
| **客户端 UI**  | Qt 5.14+                         | `.ui` 布局 + `.qss` 样式 + `.cpp` 逻辑，三者分离             |
| **服务端框架** | Qt (非 GUI)                      | 利用 Qt 信号/槽机制实现线程间通信                            |
| **序列化**     | Google Protocol Buffers (proto3) | `common.proto` 定义通用消息，`server_msg.proto` 定义业务协议 |
| **数据库**     | MySQL (C API)                    | ConnectionPool 动态连接池管理                                |
| **支付集成**   | 微信支付中台 API                 | `httplib` 轻量 HTTP 库接收 Webhook                           |
| **构建系统**   | Visual Studio (MSBuild)          | `.vcxproj` 工程文件管理                                      |
| **国际化**     | Qt Linguist                      | `.ts/.qm` 文件支持中英文切换                                 |
| **加密**       | VideoSecurityTool                | 影片数据上传前加密保护                                       |

## 项目结构

```text
RailCineControl/
├── RailCineControl/                    # 客户端工程
│   ├── main.cpp                        # 程序入口
│   ├── mainWindow.cpp/h/ui            # 主窗口
│   ├── ControlHubWindow.cpp/h         # 核心调度窗口
│   ├── TCPMgr.cpp/h                   # TCP 长连接管理
│   ├── UdpManager.cpp/h               # UDP 设备发现
│   ├── LocalStreamServer.cpp/h        # 本地流媒体
│   ├── CMDTools.cpp/h                 # 进程管理
│   ├── LoginWidget.cpp/h/ui           # 登录界面
│   ├── MovieWidget.cpp/h              # 影片模块
│   ├── GameWidget.cpp/h               # 游戏模块
│   ├── GameLauncherPage.cpp/h         # 游戏启动器
│   ├── GameUploadPage.cpp/h           # 游戏上传
│   ├── WalletWidget.cpp/h             # 钱包模块
│   ├── UploadPage.cpp/h               # 影片上传
│   ├── PlaybackPage.cpp/h             # 播放页
│   ├── RecordPage.cpp/h               # 播放记录
│   ├── SettingWidget.cpp/h            # 设置页
│   ├── AccountWidget.cpp/h            # 账户页
│   ├── CinemaDialogBase.cpp/h         # 对话框基类
│   ├── CinemaMessageBox.h             # 消息框组件
│   ├── CinemaTableWidget.h            # 表格组件
│   ├── CinemaPayDialog.h              # 支付弹窗
│   ├── TitleBar.cpp/h                 # 标题栏
│   ├── StateWidget.cpp/h              # 状态控件
│   ├── TipWidget.cpp/h                # 提示控件
│   ├── ClickedLabel.cpp/h             # 可点击标签
│   ├── ImageBgButton.cpp/h            # 图片背景按钮
│   ├── CustomizeEdit.cpp/h            # 自定义输入框
│   ├── CustomizeTextEdit.cpp/h        # 自定义文本编辑
│   ├── UserMgr.cpp/h                  # 用户状态管理
│   ├── Global.cpp/h                   # 全局配置
│   ├── ThreadPool.cpp/h               # 线程池
│   ├── WorkerThread.cpp/h             # 工作线程
│   ├── JsonTool.cpp/h                 # JSON 工具
│   ├── LogRecord.cpp/h                # 日志记录
│   ├── VideoSecurityTool.h            # 视频加密
│   ├── Enum.h                         # 枚举定义
│   ├── Macro.h                        # 宏定义
│   ├── ProtocolDef.h                  # 协议常量
│   ├── singletion.h                   # 单例模板
│   ├── common.proto / common.pb.h/cc  # 通用协议
│   ├── server_msg.proto / .pb.h/cc    # 业务协议
│   ├── StyleSheet/                    # QSS 样式表
│   │   └── stylesheet.qss
│   ├── Images/                        # 图片资源
│   ├── Config/                        # 配置文件
│   └── Sql/                           # 建表 SQL
│
└── RailCineControlServer_Windows/
    └── RailCineControlServer/         # 服务端工程
        ├── TcpServer.cpp/h            # TCP 服务器（仅管理连接）
        ├── ClientSession.cpp/h        # 会话生命周期管理
        ├── MsgDispatcher.cpp/h        # 全局事件分发器（单例）
        ├── ThreadPool.cpp/h           # 线程池
        ├── WorkerThread.cpp/h         # 工作线程
        ├── ConnectionPool.cpp/h       # MySQL 连接池
        ├── HttpServerMgr.cpp/h        # HTTP 服务（Webhook）
        ├── MidPlatformManager.cpp/h   # 支付中台管理
        ├── Global.cpp/h               # 全局配置
        ├── SqlExec.cpp/h              # SQL 执行工具
        ├── JsonTool.cpp/h             # JSON 工具
        ├── LogRecord.cpp/h            # 日志记录
        ├── Enum.h                     # 枚举定义
        ├── AuthService.cpp/h          # 认证服务
        ├── FileTransferService.cpp/h  # 文件传输服务
        ├── MovieResourceService.cpp/h # 影片资源服务
        ├── MoviePlayRecordService.cpp/h # 播放记录服务
        ├── WalletService.cpp/h        # 钱包服务
        ├── PaymentService.cpp/h       # 支付服务
        ├── OrderManagementService.cpp/h # 订单管理服务
        ├── GameResourceService.cpp/h  # 游戏资源服务
        ├── common.proto / common.pb.h/cc  # 通用协议
        ├── Config/                    # 数据库等配置
        └── MainWindow.cpp/h/ui        # 服务端控制面板
```



## 快速开始

### 环境要求

- **操作系统**：Windows 10+
- **编译器**：MSVC 2019+
- **Qt 版本**：Qt 5.14+
- **数据库**：MySQL 5.7+

### 构建步骤

1. 使用 Visual Studio 打开 `RailCineControl.sln` 解决方案文件（客户端）或 `RailCineControlServer.sln`（服务端）
2. 确保 Qt 环境变量已正确配置
3. 选择 Release/Debug 配置，生成解决方案
4. 运行前确保 MySQL 数据库已启动，并在 `Config/` 中配置正确的连接参数
5. 执行 `Sql/` 目录下的建表脚本初始化数据库

### 运行

bash

```
# 先启动服务端
RailCineControlServer.exe

# 再启动客户端
RailCineControl.exe
```



## 设计理念

RailCineControl 的核心设计围绕三个基本原则展开：

### 1. 关注点分离 —— 三层解耦

**UI 与逻辑分离**：Qt `.ui` 文件管理界面布局，`.qss` 样式表管理所有视觉效果，`.cpp` 代码仅包含交互逻辑与信号/槽连接。这意味着修改 UI 风格无需触碰业务代码，更换整套皮肤只需替换 QSS 文件。

**IO 与业务分离**：TcpServer 的网络线程只负责数据包的接收与发送，不执行任何数据库操作或业务计算。所有业务逻辑通过 MsgDispatcher 跨线程投递至 Worker 线程执行。

**连接管理与业务逻辑分离**：TcpServer 管理 ClientSession 的生命周期（创建、心跳、断开清理），Service 处理具体的业务请求。两者通过 MsgDispatcher 单向依赖，Service 不需要知道连接的存在。

### 2. 会话-连接-分发三层架构

```text
Server  ──管理──▶  ClientSession  ──管理──▶  Socket 连接
  │                                              │
  │                                         数据到达
  │                                              │
  └──通过──▶  MsgDispatcher  ──分发──▶  Service (Worker 线程)
```



- **Server**：只负责 `listen()` + `accept()` + 创建/销毁 `ClientSession`
- **ClientSession**：封装单个连接的全部状态（socket fd、用户信息、心跳计时器），对外暴露 `send()` 和 `close()` 接口
- **MsgDispatcher**：根据 Protobuf 消息 ID 路由到对应 Service，完全屏蔽网络细节

### 3. 支付闭环设计

支付是影院系统的核心交易链路。RailCineControl 采用 **主动轮询 + 被动回调** 双通道策略确保支付结果的可靠送达：

- `HttpServerMgr` 监听微信支付 Webhook 作为快速通道
- `MidPlatformManager` 启动独立定时器主动查询交易流水作为兜底
- `PaymentService` 统一消化两路结果，通过 `ConnectionPool` 保证钱包扣款、流水生成、订单更新的跨表事务一致性
- 支付结果通过 `TcpServer` 反向推送至指定用户的 `ClientSession`，最终到达客户端 `WalletWidget`