# Godot 4 C# 多人联机框架 Demo

基于 Godot 4（C# / .NET）的多人联机框架演示，覆盖从主机/客户端联机到专用服务器、再到 mod 扩展的完整链路。可作为多人游戏项目的网络层参考实现。

## 功能特性

- **双联机模式**：局域网主机直连 + 专用服务器（`Bootstrap.cs` 支持命令行参数启动无头专用服）
- **服务端权威**：状态同步与校验在服务端完成，客户端仅表现（`NetworkManager.cs`，约 800 行）
- **房间系统**：创建/加入、房间密码（哈希校验，不存明文）、人数上限、昵称身份管理
- **断线重连**：记忆上次连接参数（地址/端口/凭据），一键重连
- **聊天系统**：基于 RPC 的全局聊天（`Chat.cs`）
- **Mod 加载器**：`Modding/` 定义 `IMod` 接口与 `ModLoader`，附带示例 Mod，客户端可注入扩展逻辑

## 目录结构

```
├── NetworkManager.cs   网络核心：建服/加入/同步/重连/密码校验
├── Bootstrap.cs        启动入口：区分客户端与专用服务器（命令行端口/密码/人数）
├── ServerConfig.cs     专用服配置（端口、人数、密码）
├── Game.cs / Player.cs 游戏逻辑与玩家同步表现
├── Identity.cs         玩家身份信息
├── Chat.cs             聊天 RPC
└── Modding/            IMod 接口 + ModLoader + SampleMod 示例
```

## 运行

```bash
# Godot 4.x（.NET 版）打开项目目录，直接 F5 运行

# 启动专用服务器（命令行）
./MultiplayerDemo --server --port 7777 --max-players 8 --password xxx
```

## 设计说明

- 网络层与游戏逻辑分离：`NetworkManager` 只负责连接管理与状态同步，游戏规则在 `Game.cs` 中处理，便于移植到实际项目
- Mod 系统采用接口注入：Mod 实现 `IMod`，由 `ModLoader` 在启动时发现并加载，客户端与服务端扩展点分离

## 说明

本项目为个人学习与实践用的联机框架 demo，代码可自由参考。
