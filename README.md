# 🎬 RailCineControl (影院与游戏微控平台)

## 📖 项目简介

**RailCineControl** 是一个全栈式的影院终端控制与数字资产管理系统。项目包含一个功能丰富的 **Qt 桌面客户端** 以及一个基于 **SOA (面向服务架构)** 的高并发 **C++ 服务端**。 系统支持影片资源下发、局域网游戏串流分发、用户数字钱包管理、微信扫码支付中台对接以及精确的播放行为数据埋点。

## 🏗️ 最新系统架构体系

### 🌐 服务端架构 (RailCineControlServer)

服务端经历了深度的模块化重构，剥离了沉重的网络回调，实现了极致的“高内聚、低耦合”：

- **SOA 面向服务拆分**：所有的业务逻辑完全脱离 TCP 底层，被封装进 8 大核心 `Service` 中。
- **异步事件驱动 (MsgDispatcher)**：利用全局消息路由与自定义 `ThreadPool`，网络 IO 线程只负责数据包的组装与拆解，业务处理全部跨线程投递至 Worker 线程执行。
- **高可用数据库调度**：内置 `ConnectionPool`（数据库连接池），利用异步事务接口保证跨表更新（如：钱包扣款+流水生成+订单状态更新）的强一致性。
- **中台与双向通信**：内置轻量级 HTTP 服务 (`HttpServerMgr`) 专门监听第三方中台（如微信支付）回调；通过 `PaymentService` 将支付结果利用 `TcpServer` 的推送接口反向注入至指定用户的长连接。
- **幽灵守护任务**：引入独立的 `OrderManagementService`，不依赖任何网络请求，利用内部定时器定期巡检清理过期/异常订单。

### 🖥️ 客户端架构 (RailCineControl)

客户端基于 Qt 构建，实现了流畅的 UI 交互与多端通信能力：

- **多协议网络引擎**：`TCPMgr` 负责长连接与 Protobuf 数据交换；`UdpManager` 负责局域网节点发现与底层广播；`LocalStreamServer` 提供本地流媒体推流支持。
- **组件化 UI**：高度解耦的 Widget 系统（如 `MovieWidget`, `GameWidget`, `WalletWidget`, `UploadPage`），支持动态换肤与定制化（`CustomizeEdit`）。
- **进程级管理 (CMDTools)**：封装了底层的进程调用机制，支持外部游戏/程序的无缝拉起与状态监控。

## 🧩 核心业务模块

### 服务端 (Services)

1. **`AuthService` (认证服务)**：处理登录校验、互斥登录（顶号机制）及心跳保活。
2. **`FileTransferService` (文件服务)**：支持大文件的分片上传与断点下载。
3. **`MovieResourceService` (影片服务)**：影片元数据增删改查及海报下发。
4. **`MoviePlayRecordService` (埋点服务)**：记录并分析用户的播放时长、频次数据。
5. **`WalletService` (钱包服务)**：处理用户数字资产、套餐查询与积分流水拉取。
6. **`PaymentService` (支付中枢)**：对接生成付款二维码，消化 HTTP 支付回调并处理跨表资金事务。
7. **`OrderManagementService` (订单卫士)**：异步巡检数据库，执行未支付订单关闭及对账。
8. **`GameResourceService` (游戏中心)**：游戏安装包的分发与版本控制。

## 🛠️ 技术栈

- **编程语言**: C++ 17
- **GUI & 核心库**: Qt 5.14+
- **网络与序列化**: Google Protocol Buffers (proto3), 自定义 TCP 封包/粘包处理机制
- **数据库**: MySQL (C API), 动态连接池技术
- **第三方集成**: `httplib` (用于服务端接收 Webhook), 外部支付中台 API

## 📂 核心目录结构

Plaintext

```
/
├── RailCineControl/                  # 🖥️ 客户端代码目录 (Qt GUI)
│   ├── UI/ & Widgets/                # 各类展示面板 (Login, Movie, Wallet...)
│   ├── TCPMgr/ & UdpManager/         # 客户端网络通信模块
│   ├── LocalStreamServer/            # 本地流媒体服务
│   └── Config/                       # 客户端本地配置 (json)
│
├── RailCineControlServer_Windows/    # 🌐 服务端代码目录
│   └── RailCineControlServer/
│       ├── TcpServer.cpp/h           # 底层网络连接与 Socket 生命周期管理
│       ├── MsgDispatcher.cpp/h       # 数据包路由分发器
│       ├── ThreadPool.cpp/h          # 任务调度与数据库查询工作池
│       ├── ConnectionPool.cpp/h      # MySQL 高并发连接池
│       ├── *Service.cpp/h            # 8 大核心业务微服务模块
│       └── HttpServerMgr.cpp/h       # Webhook HTTP 监听层
│
├── common.proto & server_msg.proto   # 📝 统一通信协议文件
├── build_proto.bat                   # Protobuf 一键编译脚本
└── RailCineControl/Sql/              # 🗄️ 数据库建表与初始化脚本
```

## 🚀 编译与运行指南

### 1. 环境准备

- 安装 **Visual Studio 2017/2019/2022** (配置 C++ 桌面开发组件)。
- 安装 **Qt 5.14** 或更高版本，并在 VS 中配置好 Qt 插件 (Qt VS Tools)。
- 安装配置 **MySQL Server** (推荐 8.0+)。
- 下载并配置 **Protobuf** 编译环境（需 `protoc.exe`）。

### 2. 数据库初始化

- 登录 MySQL，创建所需数据库。
- 导入项目中的初始化 SQL 脚本： `source RailCineControlServer_Windows/RailCineControlServer/controlhub_backup.sql;`

### 3. 生成 Protobuf 协议

- 运行根目录下的 `build_proto.bat`，将 `.proto` 描述文件自动编译为 C++ 类，并替换到 Client 和 Server 工程中。

### 4. 编译与启动

- 使用 Visual Studio 打开 `RailCineControl.sln` 和 `RailCineControlServer.sln`。
- 确认服务端配置文件 `server_config.json` 中的数据库账户、密码、监听端口（如 TCP: 5486, HTTP: 8182）正确无误。
- 启动 Server 引擎，随后启动 Client 应用程序即可体验。