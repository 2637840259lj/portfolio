# Supercar App — UI 动画优化方案

> 参考来源：[emilkowalski/skills](https://github.com/emilkowalski/skills)（已下载到 `~/.workbuddy/skills/emil-*`）
> 适用范围：严格遵循 `UI调试对话要求须知.md`，**仅改 UI 层**，不动蓝牙/协议/MCU/控制逻辑。

---

## 一、核心动画哲学（emilkowalski 精华）

| # | 原则 | 一句话 |
|---|---|---|
| 1 | Justified motion | 每个动画都要回答"为什么动"，不是"看起来酷" |
| 2 | Frequency-appropriate | 高频操作不动画；中频极简；低频标准；罕见可加 delight |
| 3 | Responsive easing | 进入/退出用 `ease-out`，**绝不用 `ease-in`**；用强 cubic-bezier |
| 4 | Sub-300ms UI | UI 动画 < 300ms；按钮 100–160ms；下拉 150–250ms；弹层 200–500ms |
| 5 | Origin-aware | 浮层从触发点 scale，不从 center；**绝不用 `scale(0)`**，从 0.9–0.97 + opacity 起 |
| 6 | Interruptibility | 高频触发用 transition 不用 keyframe；手势用 spring |
| 7 | GPU-only | 只动 `transform` 和 `opacity`，别动 width/height/top/left |
| 8 | Accessibility | `prefers-reduced-motion` 要尊重；hover 用 media query gate |
| 9 | Asymmetric | 按住慢（决定中），松开快（响应） |
| 10 | Cohesion | 动画匹配组件性格；赛博朋克 = 利落 + 一点科技感脉冲 |

---

## 二、Web → Flutter 翻译对照

emilkowalski 的 skill 是 Web 向（CSS/React/Framer Motion），本项目是 Flutter。原则通用，实现载体不同：

| Web (emilkowalski) | Flutter 等价 | 说明 |
|---|---|---|
| `cubic-bezier(0.23, 1, 0.32, 1)` | `Cubic(0.23, 1, 0.32, 1)` | 强 ease-out，UI 默认 |
| `cubic-bezier(0.77, 0, 0.175, 1)` | `Cubic(0.77, 0, 0.175, 1)` | 强 ease-in-out，屏内移动 |
| `cubic-bezier(0.32, 0.72, 0, 1)` | `Cubic(0.32, 0.72, 0, 1)` | iOS 抽屉曲线 |
| `:active { scale(0.97) }` + `transition 160ms` | `AnimatedScale` + `GestureDetector` 或自写 `PressScale` widget | 按钮按下反馈 |
| CSS `transition` | `AnimatedContainer` / `AnimatedDefaultTextStyle` / `TweenAnimationBuilder` | 隐式动画，可中断 |
| Framer Motion `spring` | `SpringSimulation` + `AnimationController.animateWith` | 物理回弹 |
| `@starting-style` | `Tween` + `forward` animation + `AnimatedSwitcher` | 入场动画 |
| `transform-origin` | `Alignment` / `FractionalOffset` | 锚点 |
| `@media (prefers-reduced-motion)` | `MediaQuery.disableAnimations` / 自检 | 减弱动画 |
| `clip-path: inset()` | `ClipPath` + `CustomClipper` | 揭示动画 |

---

## 三、审查发现（Findings Table）

### 🔴 HIGH — 感觉破坏性（必须修）

| # | 位置 | 现状 | 问题（违反标准） | 修复方案 |
|---|---|---|---|---|
| H1 | `scan_page.dart:560-575` `_buildTabContent` | Tab 切换直接 `switch` 返回新页面，无过渡 | 违反 #1 防止 jarring change；5 个 tab 是中频操作，硬切像"坏掉" | `AnimatedSwitcher` + `FadeThroughTransition`，200ms ease-out；或 `IndexedStack` 保状态 + 内容区 `AnimatedOpacity` |
| H2 | `control_page.dart:583-624` `_holdBtn` / `_tapBtn` | 用 `GestureDetector` 包 `Container`，按下无 scale 反馈 | 违反 #5 "Buttons must feel responsive"；遥控按钮是高频操作，必须有反馈 | 抽 `PressScale` widget：`AnimatedScale(scale: _pressed ? 0.96 : 1.0, duration: 120ms, curve: _kEaseOut)`；`onTapDown` 置 true，`onTapUp/Cancel` 置 false |
| H3 | `control_page.dart:394` `_rightPanel` dirColor / `:415` 蓝牙色 / `:399` 速度色；`line_page.dart:330-334` 误差色；`telem_page.dart:176-182` 状态色 | 颜色随状态硬跳变（`_direction` 变 → 颜色瞬间切） | 违反 #1 防止 jarring change；状态指示类必须过渡 | 把 `Container(decoration: BoxDecoration(color: ...))` 换成 `AnimatedContainer(duration: 200ms, curve: _kEaseOut, decoration: ...)`；文字颜色用 `AnimatedDefaultTextStyle` |
| H4 | `line_page.dart:264-309` `_sensorPanel` 8 路灰度 | 黑/白切换时 `color` + `border` 瞬间跳变 | 违反 #1；循迹是核心调试场景，灰度跳变最刺眼 | 每格 `AnimatedContainer(duration: 150ms, curve: _kEaseOut, decoration: ...)`；激活时可加 0.7→1.0 的 scale 微脉冲 |
| H5 | `line_page.dart:376-418` `_errorPanel` marker | `Positioned(left: markerX)` 直接跳变，无过渡 | 违反 #1 + #7；marker 是"读数据"的，跳变妨碍判读 | `AnimatedPositioned(left: markerX, duration: 200ms, curve: _kEaseOut)`；或用 `SpringSimulation` 让它有惯性地滑到目标 |

### 🟡 MEDIUM — 明显不足（建议修）

| # | 位置 | 现状 | 问题 | 修复方案 |
|---|---|---|---|---|
| M1 | `joystick.dart:46-51` `_onDragEnd` | 松开后 `setState(() => _thumbX = 0)` 直接归零 | 违反 #6；手势驱动应用 spring 带惯性地回中 | `AnimationController` + `SpringSimulation(SpringDescription(mass:1, stiffness:300, damping:24), _thumbX, 0, Velocity(_lastVelocity))` |
| M2 | `emergency_fab.dart:59-66` 急停拖动 | 拖动停止后无 momentum，硬停 | 违反 #6；可拖动元素应有物理感 | 拖动结束时记录速度，用 `SpringSimulation` 衰减到边界；边界用阻尼而非硬墙 |
| M3 | `telem_page.dart:247/255/263/271` `_valueCard`；`param_page.dart` 数值显示 | 数值直接 `setState` 刷新，数字跳变 | 违反 #1；遥测数据高频刷新时数字抖动难读 | `AnimatedSwitcher` + `FadeThroughTransition` 80ms；或自写 `NumberTicker` 用 `Tween<int>` 平滑过渡。注意：高频(50ms级)刷新的数值**不要**加 ticker，会糊；只给状态切换类的数值加 |
| M4 | `scan_page.dart:242-294` 设备列表 | `ListView.builder` 的 `Card` 直接出现 | 违反 #1；首次扫描到设备是低频"首次见"时刻，可加 delight | 包 `AnimatedSwitcher`/入场 `Tween`：`opacity 0→1 + translateY 8→0`，stagger 40ms，200ms ease-out。**注意**：只对"新出现"的设备加，已存在的不要每次重建都动画 |
| M5 | `scan_page.dart:489-513` 侧边栏 tab indicator | `Border(left: BorderSide(color: active ? primary : transparent))` 硬切 | 违反 #1；tab 切换是中频，indicator 应跟随 | 用 `AnimatedContainer` 200ms ease-out 过渡 border color；或参考苹果设计 skill，用 `LayoutBuilder` + `AnimatedPositioned` 做 sliding indicator |
| M6 | `scan_page.dart:544-558` `_buildLinkDot` 连接点 | 已有 `boxShadow` 但无脉冲（仅 control_page 的 `_connDot` 有脉冲） | 主页顶栏的连接点是核心状态指示，应统一脉冲 | 复用 control_page 的 `_pulseCtrl` 模式，或抽 `PulseDot` widget 统一 |

### 🟢 LOW — 打磨（可选）

| # | 位置 | 现状 | 建议 |
|---|---|---|---|
| L1 | `scan_page.dart:228` / `telem_page.dart:222` 空状态图标 | 静态 `Icon` | 加 subtle float：`Transform.translate` + `Sin` 2s 循环，±3px。**注意**：这是罕见时刻，可动 |
| L2 | `control_page.dart` 连接成功瞬间 | 从扫描页 `pushReplacement` 直接进主页 | 可加一次性的 success flash：主页背景 `AnimatedContainer` 闪一下 primary 色再回到 background，400ms |
| L3 | `emergency_fab.dart` 急停按下 | `onTap` 直接触发，无视觉反馈 | 加 `AnimatedScale(0.93, 100ms)` 按下反馈；急停必须即时，但视觉反馈不影响响应 |
| L4 | `line_page.dart:264` 灰度激活 | 仅颜色变化 | 可加 clip-path 波纹扩散（激活瞬间从中心 reveal），但属于装饰，优先级最低 |

---

## 四、遗漏的动画机会（Missed Opportunities）

按 `find-animation-opportunities` 的 Gate 筛选，只列通过频率+目的+速度+功能四问的：

| # | 位置 | 机会 | 频率 | 目的 | 建议 |
|---|---|---|---|---|---|
| O1 | `control_page.dart` 速度滑块 thumb | 滑动时 thumb 无 active 脉冲 | 中频（每次遥控） | State indication | `linkOk` 时 thumb 加 `AnimatedScale` 微脉冲（1.0→1.06，1.2s alternate），断连时静止 |
| O2 | `telem_page.dart` Yaw 归零按钮按下 | 按下后 IMU 数据归零，无视觉确认 | 偶尔 | Feedback | 按下时数值区做一次 `AnimatedOpacity` 闪现（0.4→1.0，300ms ease-out）确认归零 |
| O3 | `scan_page.dart` 连接成功 | 进入主页无空间过渡 | 罕见（首次连接） | Spatial consistency + Delight | `pushReplacement` 配合 `PageRouteBuilder` 做 shared-element 风格过渡：连接按钮 → 主页AppBar 的连接点 |
| O4 | `line_page.dart` 循迹启动 | `LF+` 发送后无启动反馈 | 偶尔 | Feedback | 启动按钮 → 状态条之间做一次 `clip-path` 从左向右 reveal 的"启动波" |

**被拒绝的候选**（透明化，说明为什么不加）：
- ❌ 控制页 50ms 周期的 `_emit` 发送反馈 — 高频，加了反而干扰操控
- ❌ 遥测数值实时滚动 ticker — 50ms 级刷新，ticker 会糊成一团，妨碍判读
- ❌ 灰度每格 hover 放大 — 触屏设备，无 hover 场景

---

## 五、实施优先级建议

按"影响 ÷ 工作量"排序，分三批：

### 第一批：核心反馈（HIGH 全部 + M1/M2）
- H2 按钮 press scale（最明显，工作量小）
- H3 状态颜色 `AnimatedContainer`（覆盖面广，工作量小）
- H1 Tab 切换过渡（影响整体感）
- H4 灰度阵列过渡（循迹调试核心）
- H5 误差 marker 平滑（循迹调试核心）
- M1 摇杆 spring 回中（操控手感）
- M2 急停拖动 momentum（细节）

### 第二批：状态指示统一（MEDIUM 剩余）
- M3 数值过渡（仅状态切换类，非实时刷新类）
- M4 设备列表入场
- M5 侧栏 indicator
- M6 连接点脉冲统一

### 第三批：打磨（LOW + Opportunities）
- L1-L4 + O1-O4，按需取舍

---

## 六、关键代码片段（可直接复用）

### 0. 在 `theme.dart` 新增动画 token

```dart
class CyberpunkTheme {
  // ... 现有颜色 ...

  // ── 动画曲线 token（对应 emilkowalski 的强 cubic-bezier）──
  static const Curve easeOut = Cubic(0.23, 1, 0.32, 1);        // UI 默认
  static const Curve easeInOut = Cubic(0.77, 0, 0.175, 1);     // 屏内移动
  static const Curve easeDrawer = Cubic(0.32, 0.72, 0, 1);     // iOS 抽屉

  // ── 时长 token ──
  static const Duration durPress = Duration(milliseconds: 120);   // 按钮反馈
  static const Duration durFast = Duration(milliseconds: 150);    // 小元素
  static const Duration durStd = Duration(milliseconds: 200);     // 标准
  static const Duration durSlow = Duration(milliseconds: 300);    // 弹层
}
```

### 1. PressScale 通用 widget（解决 H2）

新建 `app/lib/widgets/press_scale.dart`：

```dart
import 'package:flutter/material.dart';
import '../theme.dart';

/// 按下时 scale 缩小反馈，对应 emilkowalski 的 :active scale(0.97)。
/// 用于任何可点击元素，让 UI "听得到"用户。
class PressScale extends StatefulWidget {
  final Widget child;
  final VoidCallback? onTap;
  final double scaleDown; // 0.95–0.98
  const PressScale({
    super.key,
    required this.child,
    this.onTap,
    this.scaleDown = 0.96,
  });

  @override
  State<PressScale> createState() => _PressScaleState();
}

class _PressScaleState extends State<PressScale> {
  bool _pressed = false;

  @override
  Widget build(BuildContext context) {
    return GestureDetector(
      onTapDown: (_) => setState(() => _pressed = true),
      onTapUp: (_) {
        setState(() => _pressed = false);
        widget.onTap?.call();
      },
      onTapCancel: () => setState(() => _pressed = false),
      child: AnimatedScale(
        scale: _pressed ? widget.scaleDown : 1.0,
        duration: CyberpunkTheme.durPress,
        curve: CyberpunkTheme.easeOut,
        child: widget.child,
      ),
    );
  }
}
```

### 2. 状态颜色过渡（解决 H3）

把 `_rightPanel` 里的 `Container` 换成 `AnimatedContainer`：

```dart
// Before:
Container(
  decoration: BoxDecoration(
    color: CyberpunkTheme.surface.withAlpha(100),
    border: Border.all(color: linkOk ? primary : danger),
  ),
  ...
)

// After:
AnimatedContainer(
  duration: CyberpunkTheme.durStd,
  curve: CyberpunkTheme.easeOut,
  decoration: BoxDecoration(
    color: CyberpunkTheme.surface.withAlpha(100),
    borderRadius: BorderRadius.circular(8),
    border: Border.all(
      color: linkOk
          ? CyberpunkTheme.primary.withAlpha(30)
          : CyberpunkTheme.danger.withAlpha(60),
    ),
  ),
  ...
)
```

### 3. 摇杆 spring 回中（解决 M1）

`joystick.dart` 改造：

```dart
class _JoystickState extends State<Joystick>
    with SingleTickerProviderStateMixin {
  late AnimationController _ctrl;
  double _thumbX = 0;
  double _lastVelocity = 0;
  // ...

  @override
  void initState() {
    super.initState();
    _ctrl = AnimationController(vsync: this)
      ..addListener(() {
        setState(() => _thumbX = _ctrl.value);
      });
  }

  void _onDragEnd(DragEndDetails d) {
    _isDragging = false;
    _sendTimer?.cancel();
    _lastVelocity = d.primaryVelocity?.clamp(-5.0, 5.0) ?? 0;
    // spring 回中，带松手速度
    _ctrl.animateWith(
      SpringSimulation(
        const SpringDescription(mass: 1, stiffness: 300, damping: 24),
        _thumbX,         // 起点
        0.0,             // 终点（归零）
        Velocity(_lastVelocity / 100),  // 入射速度
      ),
    );
    widget.onRelease?.call();
  }
  // ...
}
```

### 4. Tab 切换过渡（解决 H1）

`scan_page.dart` 的 `_buildTabContent` + `body`：

```dart
// 用 IndexedStack 保状态 + AnimatedSwitcher 做内容过渡
IndexedStack(
  index: _tabIndex,
  children: [
    _wrapWithFade(const ControlPage()),
    _wrapWithFade(const ParamPage()),
    _wrapWithFade(const TelemPage()),
    _wrapWithFade(const LinePage()),
    _wrapWithFade(const TeachPage()),
  ],
)

Widget _wrapWithFade(Widget child) {
  return AnimatedSwitcher(
    duration: CyberpunkTheme.durStd,
    switchInCurve: CyberpunkTheme.easeOut,
    switchOutCurve: CyberpunkTheme.easeOut,
    transitionBuilder: (c, anim) => FadeTransition(opacity: anim, child: c),
    child: KeyedSubtree(key: ValueKey(_tabIndex), child: child),
  );
}
```

> 注意：`IndexedStack` 会同时 build 所有页面。若担心性能，可改用 `AnimatedSwitcher` 单页面切换，但会丢失页面状态——需结合 `_switchTab` 已有的 STP 清理逻辑权衡。

### 5. 灰度阵列过渡（解决 H4）

`line_page.dart` `_sensorPanel` 每格：

```dart
// Before: Container(decoration: BoxDecoration(color: color.withAlpha(...), ...))
// After:
AnimatedContainer(
  duration: CyberpunkTheme.durFast,
  curve: CyberpunkTheme.easeOut,
  decoration: BoxDecoration(
    color: color.withAlpha(active ? 105 : 80),
    borderRadius: BorderRadius.circular(8),
    border: Border.all(color: active ? color : CyberpunkTheme.darkBorder),
  ),
  // ...
)
```

### 6. 误差 marker 平滑（解决 H5）

`line_page.dart` `_errorPanel`：

```dart
// Before: Positioned(left: markerX, ...)
// After:
AnimatedPositioned(
  left: markerX,
  top: 2,
  duration: CyberpunkTheme.durStd,
  curve: CyberpunkTheme.easeOut,
  child: Container(width: 22, height: 16, ...),
)
```

---

## 七、符合 UI 调试要求须知的声明

以上所有方案**严格满足** `UI调试对话要求须知.md`：

- ✅ 只改 `app/lib/pages/`、`app/lib/widgets/`、`app/lib/theme.dart`
- ✅ 不动 `services/`、`models/`、`ble_*`、`telem_service`、`page_data`、`app_state`
- ✅ 不动 MCU 目录、协议、命令发送、超时、重试、心跳
- ✅ 不改 `pubspec.yaml` 依赖（只用 Flutter 内置 `dart:ui` 的 `Cubic`、`SpringSimulation`）
- ✅ 不为动画发送任何 BLE 命令或改变命令频率
- ✅ 动画只读取已有公开状态（`linkOk`、`_direction`、`_gray` 等），不改变状态更新时机

实施时会在每次修改后明确列出变更文件并复述："未修改 MCU、通信协议、蓝牙服务、命令发送、超时和车辆控制逻辑。"

---

## 八、验证方法

参照 emilkowalski 的调试建议：

1. **慢速测试**：把 `durStd` 临时调到 3x（600ms），观察颜色/位置过渡是否平滑、有无双状态重叠
2. **真机手势测试**：摇杆 spring 回中、急停拖动 momentum 必须在真机上验，模拟器手感不可信
3. **隔天复审**：动画细节当天看不准，隔天再看能发现 timing 不协调
4. **性能检查**：Flutter DevTools 的 Performance 面板，确认动画期间无 60fps 掉帧；只动 transform/opacity（AnimatedContainer 在 Flutter 里会触发 repaint，但 60fps 下无感）
