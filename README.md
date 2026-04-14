# 轨道影院 (RailCineControl) 商业级中控系统

## 📖 项目简介

**RailCineControl** 是一套专为轨道影院（移动式沉浸剧场）设计的全链路中控解决方案。本项目基于 **CommonHub 商业级中控基础架构** 开发，采用高性能 C/S 异步通信模型，集成了影片分发、回放控制、设备监控及视频安全防护等核心功能。

系统通过 **Protobuf** 协议确保跨平台、低延迟的指令下发，能够稳定支撑轨道平台与视觉内容的高精度同步联动。

------

## 🏗️ 系统架构

本项目由 **客户端 (Frontend)** 和 **服务端 (Backend)** 两大部分组成，底层共用一套高并发通讯与线程管理组件。

### 1. 业务功能模块

- **回放管理 (PlaybackPage)**：支持影片的实时预览、进度控制与轨道同步触发。
- **素材上传 (UploadPage)**：集成了大文件切片上传逻辑，支持视频素材的远程分发。
- **录制系统 (RecordPage)**：监控影院运行状态，并提供运行日志与画面同步录制功能。
- **安全防护 (VideoSecurityTool)**：针对商业影片资产，内建了视频加密与鉴权解密工具。
- **本地流服务 (LocalStreamServer)**：通过本地流协议，实现内网低延迟视频串流。

### 2. 核心底层组件 (Inherited from CommonHub)

- **TCP 异步引擎**：基于 `O(1)` 复杂度的路由表，实现毫秒级指令响应。
- **线程池管理**：利用 `WorkerThread` 锚定事件循环，确保 SQL 操作与网络 IO 互不阻塞。
- **智能单例与 RAII**：全链路采用 `QSharedPointer` 与定制化删除器，彻底杜绝多线程环境下的内存泄漏。

------

## 🛠️ 技术栈

| **维度**    | **技术实现**                                               |
| ----------- | ---------------------------------------------------------- |
| **语言**    | C++17 (大量使用 `if constexpr`, `std::function`, `atomic`) |
| **框架**    | Qt 5.15+ (QObject 深度解耦)                                |
| **通讯**    | Google Protocol Buffers (v3), QTcpSocket, QUdpSocket       |
| **数据库**  | MySQL (Connector/C++ 驱动), 异步连接池                     |
| **UI 设计** | QSS 分模块绘制 + 堆栈式窗口管理 + 无边框自定义标题栏       |

------

## 🚀 快速开始

### 1. 环境准备

- 安装 **Visual Studio 2019+** 或 **Qt Creator**。
- 配置 **Protobuf v3** 编译器环境。
- 部署 **MySQL 8.0** 数据库，执行 `sql/sys_account.sql` 初始化用户权限表。

### 2. 协议生成

在项目根目录运行编译脚本，将 `.proto` 文件转换为 C++ 源码：

Bash

```
cd RailCineControl
./build_proto.bat
```

### 3. 项目编译

1. 打开 `RailCineControl.sln`。
2. 配置 `Props` 路径（检查 `RailCineControl_Debug.props` 中的库路径是否正确）。
3. 编译运行服务端 `RailCineControlServer`，随后启动客户端。

------

## 📦 核心协议定义 (Protocol Spec)

系统采用“火车式”封包格式：`[TotalLen(4B)] + [HeaderLen(2B)] + [HeaderData] + [BodyData]`。

Protocol Buffers

```
// 轨道控制核心协议示例
message PlaybackCmd {
    string movie_id = 1;      // 影片唯一ID
    uint64 timestamp = 2;     // 同步起始时间戳
    float play_speed = 3;     // 播放倍速
    bool enable_track = 4;    // 是否启动轨道联动
}
```



------

## 🛡️ 安全性改进建议

基于你目前的商业架构，在未来的版本迭代中建议增加：

1. **传输加密**：将 `QTcpSocket` 升级为 `QSslSocket`，实现全链路 TLS 加密。
2. **攻击防御**：在 `onReadyRead` 拆包环节增加 `MAX_PACKET_SIZE` 校验，防止内存爆破攻击。
3. **连接池优化**：针对轨道同步的极高性能要求，可引入时间轮 (Timing Wheel) 算法优化心跳超时的清理效率。

------

## ⚖️ 开源协议

该项目遵循 **MIT License**。

------

**Yu Jingjing (于京京)** *GDUT Master of Engineering*

*C++ Developer*