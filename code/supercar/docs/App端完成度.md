# App 端完成度 — 蓝牙上位机

> 更新时间: 2026-07-25 21:50 (APK 已生成)
> 框架: Flutter 3.38.7 / Dart 3.10.7 | BLE: flutter_blue_plus 1.36.8
> MCU 完成度: ~95% | App 文件: 22 .dart | APK: 47.2MB

## 总览

```
骨架        ■■■■■■■■■■ 100%   Flutter 项目 + BLE 扫描连接 + 协议 + PING 握手
遥控面板    ■■■■■■■■■■ 100%   摇杆 + 方向按钮 + 油门滑块 + 刹车 + 急停
参数编辑器  ■■■■■■■■■■ 100%   19 参数读写 + PARAM_READ/WRITE/LIST + Flash 保存/加载
实时图表    ■■■■■■■■■■ 100%   TELEM v2 19B + 轮速/Yaw 曲线 + CSV 导出
循迹调试    ■■■■■■■■■■ 100%   灰度 8 路可视化 + 偏差指示 + LINE PID + LINE_FOLLOW 开关
示教编程    ■■■■■■■■■■ 100%   路径录制/回放 + 航点管理 + 序列保存/加载/删除
指令示教    ■■■■■■■■■■ 100%   29 opcode + 选中播放/编辑/删除 + 执行锁
系统/调试   ■■■■■■■■■■ 100%   DEBUG_MSG 面板 + RSSI 信号 + 执行锁
─────────────────────────
总体        ■■■■■■■■■■ 100%
```

## 5 个 Tab

| Tab | 页面 | 文件 | 功能 |
|-----|------|------|------|
| 1 | 遥控 | `control_page.dart` | 摇杆 SET_ARCADE + 方向 MOVE_RAW + 油门 THROTTLE_SET + 刹车 BRAKE + 急停 EMERGENCY + 状态面板 |
| 2 | 电机参数 | `param_page.dart` | 19 参数读写 + PARAM_READ/WRITE/LIST/SAVE/LOAD + 电机实时状态 |
| 3 | 实时图表 | `telem_page.dart` | 轮速/Yaw 曲线 5~50Hz + CSV 文件导出 |
| 4 | 循迹调试 | `line_page.dart` | 灰度 8 路柱状图 + 偏差指示器 + LINE PID + LINE_FOLLOW 开关 |
| 5 | 示教编程 | `teach_page.dart` | 路径示教 / 指令示教 双模式 + 执行锁 |

## 项目结构 (22 .dart)

```
lib/
  main.dart                          # 入口 + 横屏锁定
  constants/
    app_constants.dart               # 统一颜色/尺寸/超时常量
  models/
    ble_packet.dart                  # 命令码常量 + 数据包模型
    param_entry.dart                 # 参数条目 + float32 LE 编解码
    telem_frame.dart                 # TELEM v2 19B 帧解析
  services/
    ble_protocol.dart                # 帧组包/解帧/XOR8 校验
    ble_connection.dart              # BLE 扫描/连接/GATT + RSSI
    ble_handshake.dart               # PING 握手 + 2s 心跳 + 连丢检测
    app_state.dart                   # 全局状态 Provider + 执行锁
    param_service.dart               # 参数读写服务
    telem_service.dart               # 遥测 START/STOP + 数据收集
    page_data.dart                   # GET_PAGE_DATA + GET_HW_STATUS 解析
    teach_service.dart               # 路径示教 13 条 TEACH 命令
    ins_service.dart                 # 指令示教 13 条 INS 命令 + 单步播放
  pages/
    scan_page.dart                   # 扫描页 + 主页面骨架 (5 Tab + 状态栏)
    control_page.dart                # Tab1 遥控面板
    param_page.dart                  # Tab2 参数编辑器
    telem_page.dart                  # Tab3 实时图表
    line_page.dart                   # Tab4 循迹调试
    teach_page.dart                  # Tab5 示教/指令双模式
  widgets/
    joystick.dart                    # 虚拟摇杆控件
    debug_panel.dart                 # 全局调试面板 (底部抽屉)
```

## 与 MCU 协议对齐状态

| 协议层 | 状态 |
|--------|------|
| 帧格式 SYNC/LEN/CMD/PAYLOAD/XOR8 | ✅ 与 ble_protocol.c 完全一致 |
| 命令码 50+ | ✅ 与 ble_protocol.h 逐码对齐 |
| UUID (FFF0/FFF1/FFF2) | ✅ Write→FFF2, Notify→FFF1 |
| PING 握手 | ✅ 发 PING(0x00), 收 PING 回复 payload[0]==0x01 |
| 心跳 (2s, 3 次超时断开) | ✅ 对齐文档 §4.5 |
| 设备名 | ✅ CH9141BLE2U |
| XOR8 验证 | ✅ 与 ble_protocol.c 第 153 行完全一致 |

## 编译配置

| 配置 | 值 |
|------|-----|
| Gradle 仓库 | aliun + google + mavenCentral (build.gradle.kts allprojects) |
| Gradle 版本 | 8.14 (腾讯镜像) |
| AGP | 8.11.1 + android.newDsl=false |
| JDK | 17 (C:\Users\Administrator\jdk17\jdk17) |
| 编译项目路径 | E:\Supercar\app (纯英文) |

## 已知事项

- MCU 指令示教部分 opcode 待实车验证 (MCU ~95%)
- MCU PARAM_SAVE Flash 持久化待完善 (MCU P3)
- 首次编译需 2~3 分钟下载 NDK 等依赖
- APK 仅包含 arm64-v8a，如需 32 位设备可加 armeabi-v7a
