import 'dart:async';
import 'package:flutter/material.dart';
import 'package:provider/provider.dart';
import '../services/app_state.dart';
import '../services/page_data.dart';
import '../models/ble_packet.dart';
import '../widgets/joystick.dart';
import '../widgets/debug_panel.dart';
import '../widgets/press_scale.dart';
import '../theme.dart';

/// Tab1: 遥控面板 — 速度滑块 + 按住式方向 + 左右参数面板
class ControlPage extends StatefulWidget {
  const ControlPage({super.key});
  @override
  State<ControlPage> createState() => _ControlPageState();
}

class _ControlPageState extends State<ControlPage>
    with SingleTickerProviderStateMixin {
  Timer? _refreshTimer;
  Timer? _driveTimer;
  PageData? _pageData;
  bool _linkWasAlive = false;
  double _speedValue = 0; // 速度滑块 0~100
  int _direction = 0; // 0=停 1=前进 -1=倒车
  int _steer = 0; // 转向 -127~127
  double _steerDeg = 0; // 转向角度(显示)
  int _lastStopSeq = 0; // 上次处理的停车序号
  int _lastSentLeft = 0;
  int _lastSentRight = 0;
  DateTime? _lastDriveSentAt;
  late AnimationController _pulseCtrl;

  static const Duration _driveKeepAliveInterval = Duration(milliseconds: 220);

  // static const _bg = Color(0xFF0F0F23);
  // static const _surface = Color(0xFF1A1A35);
  // static const _primary = Color(0xFF7C3AED);
  // static const _success = Color(0xFF10B981);
  // static const _danger = Color(0xFFEF4444);
  // static const _warn = Color(0xFFF59E0B);
  // static const _text = Color(0xFFE2E8F0);
  // static const _dim = Color(0xFF94A3B8);

  @override
  void initState() {
    super.initState();
    _pulseCtrl = AnimationController(
      vsync: this,
      duration: const Duration(seconds: 2),
    )..repeat(reverse: true);
    _pageData = PageData();
    _pageData!.addListener(() {
      if (mounted) setState(() {});
    });
    // 直接注册，不能延后到 post-frame；否则极快切页时可能出现 dispose 后仍 addListener 的残留回调。
    context.read<AppState>().addListener(_onAppStateChanged);
    _onAppStateChanged();
    // 50ms 周期发送驱动命令 (保持按住时持续发)
    _driveTimer = Timer.periodic(
      const Duration(milliseconds: 50),
      (_) => _emit(),
    );
  }

  void _onAppStateChanged() {
    if (!mounted) return;
    final app = context.read<AppState>();
    final alive = app.bleConn.isConnected && app.handshake.linkAlive;
    if (alive && !_linkWasAlive) {
      _linkWasAlive = true;
      _pageData!.queryHwStatus(app);
      _refreshTimer = Timer.periodic(const Duration(seconds: 2), (_) {
        if (mounted) {
          final a = context.read<AppState>();
          if (a.bleConn.isConnected && a.handshake.linkAlive)
            _pageData!.queryHwStatus(a);
        }
      });
    } else if (!alive && _linkWasAlive) {
      _linkWasAlive = false;
      _refreshTimer?.cancel();
      _lastSentLeft = 0;
      _lastSentRight = 0;
      _lastDriveSentAt = null;
      setState(() {});
    }
    // 急停/刹车请求 → 归零
    if (app.stopSeq != _lastStopSeq) {
      _lastStopSeq = app.stopSeq;
      setState(() {
        _speedValue = 0;
        _direction = 0;
        _steer = 0;
        _steerDeg = 0;
      });
      _lastSentLeft = 0;
      _lastSentRight = 0;
      _lastDriveSentAt = null;
    }
  }

  @override
  void dispose() {
    _pulseCtrl.dispose();
    _driveTimer?.cancel();
    try {
      context.read<AppState>().removeListener(_onAppStateChanged);
    } catch (_) {}
    _refreshTimer?.cancel();
    _pageData?.dispose();
    super.dispose();
  }

  /// 差速遥控: App 端计算左右轮速度, 直接发 L/R 命令。
  /// throttle -100~100, steer -127~127 (>0=左转)。
  void _sendDrive(int throttle, int steer, {bool force = false}) {
    final a = context.read<AppState>();
    if (a.bleConn.isConnected && a.handshake.linkAlive) {
      final t = throttle.clamp(-100, 100) / 100.0;
      final s = steer.clamp(-127, 127) / 127.0;
      // 以当前油门幅值为总输出上限进行差速分配：行驶中转向只会
      // 降低内侧轮，不会把外侧轮突然放大到高于当前速度档位。
      // 油门为零时仍保留原地转向能力。
      final steering = s.abs();
      double leftMix;
      double rightMix;
      if (t == 0) {
        leftMix = -s;
        rightMix = s;
      } else {
        final scale = 1.0 / (1.0 + steering);
        leftMix = t * (1.0 - s) * scale;
        rightMix = t * (1.0 + s) * scale;
      }
      final left = (leftMix * 100).round().clamp(-100, 100);
      final right = (rightMix * 100).round().clamp(-100, 100);
      final lh = (left + 128).toRadixString(16).toUpperCase().padLeft(2, '0');
      final rh = (right + 128).toRadixString(16).toUpperCase().padLeft(2, '0');
      final now = DateTime.now();
      final commandChanged = left != _lastSentLeft || right != _lastSentRight;
      // 仅非零目标需要周期保活：MCU 在500ms无L/R时自动停车。
      // 静止时只在指令变化/显式强制时发送一次L80/R80，避免占满校准等高优先级命令的入口。
      final motionActive = left != 0 || right != 0;
      final keepAliveExpired =
          motionActive &&
          (_lastDriveSentAt == null ||
              now.difference(_lastDriveSentAt!) >= _driveKeepAliveInterval);
      if (!force && !commandChanged && !keepAliveExpired) return;
      a.bleConn.sendDrivePair('L$lh\n', 'R$rh\n');
      _lastSentLeft = left;
      _lastSentRight = right;
      _lastDriveSentAt = motionActive ? now : null;
    }
  }

  /// 50ms 周期: 按当前 方向×速度 + 转向 发驱动命令
  void _emit() {
    final v = _direction * _speedValue.round();
    _sendDrive(v, _steer);
  }

  /// 方向按钮: 按下设方向, 松开归零(保留转向)
  void _setDirection(int dir) {
    setState(() => _direction = dir);
    if (dir == 0) {
      _sendDrive(0, _steer, force: true); // 松开方向 → 速度归零, 保留转向(原地转或停)
    } else {
      _emit();
    }
  }

  void _sendBrake() {
    setState(() {
      _speedValue = 0;
      _direction = 0;
      _steer = 0;
      _steerDeg = 0;
    });
    _lastSentLeft = 0;
    _lastSentRight = 0;
    _lastDriveSentAt = null;
    // BRK 是 S0，直接清空并抢占旧实时命令；不要先发普通 L80/R80。
    final app = context.read<AppState>();
    app.requestStop();
    app.bleConn.sendAscii('BRK\n');
  }

  @override
  Widget build(BuildContext context) {
    final app = context.read<AppState>();
    final pd = _pageData;
    final linkOk = app.bleConn.isConnected && app.handshake.linkAlive;
    final hwFresh = pd?.hwStatusFresh ?? false;
    final screenH = MediaQuery.of(context).size.height;
    final joySize = (screenH * 0.5).clamp(150.0, 220.0);

    return Container(
      color: CyberpunkTheme.background,
      child: Stack(
        children: [
          Positioned.fill(child: CustomPaint(painter: _ScanlinePainter())),

          // ── 左侧: MCU 参数面板 ──
          Positioned(top: 20, left: 8, child: _leftPanel(pd, linkOk, hwFresh)),

          // ── 右侧: App 状态面板 ──
          Positioned(top: 20, right: 8, child: _rightPanel(app, linkOk)),

          // ── 左侧中下: 摇杆 (转向) ──
          Positioned(
            left: 16,
            top: screenH * 0.42 + 48,
            child: Container(
              width: joySize * 1.8 + 24,
              padding: const EdgeInsets.fromLTRB(12, 10, 12, 12),
              decoration: BoxDecoration(
                color: CyberpunkTheme.raisedSurface,
                borderRadius: BorderRadius.circular(CyberpunkTheme.radiusSheet),
                border: Border.all(
                  color: (linkOk ? CyberpunkTheme.primary : CyberpunkTheme.dim).withAlpha(155),
                  width: 1.2,
                ),
                boxShadow: CyberpunkTheme.raisedShadowsStrong,
              ),
              child: Column(
                mainAxisSize: MainAxisSize.min,
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Row(
                    children: [
                      Icon(Icons.swap_horiz_rounded, size: 15, color: linkOk ? CyberpunkTheme.primary : CyberpunkTheme.dim),
                      const SizedBox(width: 5),
                      Text(
                        '方向控制',
                        style: TextStyle(
                          fontSize: 11,
                          color: linkOk ? CyberpunkTheme.text : CyberpunkTheme.dim,
                          fontWeight: FontWeight.w700,
                          letterSpacing: 0.4,
                        ),
                      ),
                    ],
                  ),
                  const SizedBox(height: 8),
                  Joystick(
              size: joySize,
              baseThrottle: 0,
              activeColor: linkOk ? CyberpunkTheme.primary : CyberpunkTheme.dim,
              onChange: (t, s) {
                setState(() {
                  _steer = s;
                  _steerDeg = s / 127.0 * 90;
                });
              },
              onRelease: () {
                setState(() {
                  _steer = 0;
                  _steerDeg = 0;
                });
                if (_direction == 0) {
                  _sendDrive(0, 0, force: true); // 没按方向 → 立即停
                } else {
                  _emit(); // 有方向 → 继续直行
                }
              },
                  ),
                ],
              ),
            ),
          ),

          // ── 右侧中间偏上: 速度滑块 + 方向按钮 + 刹车 ──
          Positioned(
            right: 16,
            top: screenH * 0.33 + 30,
            child: _speedControls(linkOk),
          ),
        ],
      ),
    );
  }

  // ── 连接点 ──
  Widget _connDot(bool ok, int rssi) {
    return AnimatedBuilder(
      animation: _pulseCtrl,
      builder: (_, __) => Row(
        mainAxisSize: MainAxisSize.min,
        children: [
          Container(
            width: 6,
            height: 6,
            decoration: BoxDecoration(
              shape: BoxShape.circle,
              color: ok ? CyberpunkTheme.success : CyberpunkTheme.danger,
              boxShadow: ok
                  ? [
                      BoxShadow(
                        color: CyberpunkTheme.success.withAlpha(
                          40 + (30 * _pulseCtrl.value).round(),
                        ),
                        blurRadius: 5,
                      ),
                    ]
                  : null,
            ),
          ),
          const SizedBox(width: 5),
          Text(
            ok ? '$rssi' : 'OFF',
            style: TextStyle(
              fontSize: 10,
              color: ok ? CyberpunkTheme.success : CyberpunkTheme.danger,
              fontFamily: 'monospace',
            ),
          ),
        ],
      ),
    );
  }

  // ── 左侧: MCU 参数面板 ──
  Widget _leftPanel(PageData? pd, bool linkOk, bool hwFresh) {
    final dataOk = linkOk && hwFresh;
    final spdL = dataOk ? (pd?.speedL ?? 0.0) : 0.0;
    final spdR = dataOk ? (pd?.speedR ?? 0.0) : 0.0;
    final tgtL = dataOk ? (pd?.targetL ?? 0.0) : 0.0;
    final tgtR = dataOk ? (pd?.targetR ?? 0.0) : 0.0;
    final encL = dataOk ? (pd?.encL ?? 0) : 0;
    final encR = dataOk ? (pd?.encR ?? 0) : 0;
    final yaw = dataOk ? (pd?.yaw ?? 0.0) : 0.0;
    String v(String on, String off) => dataOk ? on : off;

    return AnimatedContainer(
      duration: CyberpunkTheme.durStandard,
      curve: CyberpunkTheme.easeOut,
      width: 200,
      padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 9),
      decoration: BoxDecoration(
        color: CyberpunkTheme.raisedSurface,
        borderRadius: BorderRadius.circular(CyberpunkTheme.radiusCard),
        border: Border.all(
          color: dataOk
              ? CyberpunkTheme.cyan.withAlpha(150)
              : CyberpunkTheme.danger.withAlpha(185),
          width: 1.2,
        ),
        boxShadow: dataOk
            ? CyberpunkTheme.raisedShadowsStrong
            : [
                BoxShadow(
                  color: CyberpunkTheme.danger.withAlpha(70),
                  blurRadius: 14,
                  spreadRadius: 1,
                ),
                ...CyberpunkTheme.raisedShadows,
              ],
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        mainAxisSize: MainAxisSize.min,
        children: [
          Row(
            children: [
              _hwDot(dataOk && (pd?.imuOk ?? false), 'IMU'),
              _hwDot(dataOk && (pd?.encOk ?? false), 'ENC'),
              _hwDot(dataOk && (pd?.grayOk ?? false), 'GRY'),
              _hwDot(dataOk && (pd?.motorOk ?? false), 'MOT'),
            ],
          ),
          const SizedBox(height: 4),
          Row(
            children: [
              _cell('ENC_L', v('$encL', '--')),
              const SizedBox(width: 12),
              _cell('ENC_R', v('$encR', '--')),
            ],
          ),
          Row(
            children: [
              _cell('YAW', v('${yaw.toStringAsFixed(1)}°', '--')),
              const SizedBox(width: 12),
              _cell(
                'TGT',
                v(
                  '${tgtL.toStringAsFixed(0)}/${tgtR.toStringAsFixed(0)}',
                  '--',
                ),
              ),
            ],
          ),
          const SizedBox(height: 4),
          Row(
            children: [
              _cell(
                'V_L',
                v('${spdL.toStringAsFixed(1)}', '--'),
                color: linkOk && spdL.abs() > 0.5
                    ? CyberpunkTheme.success
                    : CyberpunkTheme.dim,
              ),
              const SizedBox(width: 12),
              _cell(
                'V_R',
                v('${spdR.toStringAsFixed(1)}', '--'),
                color: linkOk && spdR.abs() > 0.5
                    ? CyberpunkTheme.success
                    : CyberpunkTheme.dim,
              ),
            ],
          ),
        ],
      ),
    );
  }

  // ── 右侧: App 状态面板 ──
  Widget _rightPanel(AppState app, bool linkOk) {
    final dirStr = _direction > 0 ? '前进' : (_direction < 0 ? '倒车' : '停止');
    final dirColor = _direction > 0
        ? CyberpunkTheme.success
        : (_direction < 0 ? CyberpunkTheme.warn : CyberpunkTheme.dim);
    return AnimatedContainer(
      duration: CyberpunkTheme.durStandard,
      curve: CyberpunkTheme.easeOut,
      width: 200,
      padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 9),
      decoration: BoxDecoration(
        color: CyberpunkTheme.raisedSurface,
        borderRadius: BorderRadius.circular(CyberpunkTheme.radiusCard),
        border: Border.all(
          color: linkOk
              ? CyberpunkTheme.primary.withAlpha(165)
              : CyberpunkTheme.danger.withAlpha(185),
          width: 1.2,
        ),
        boxShadow: linkOk
            ? CyberpunkTheme.raisedShadowsStrong
            : [
                BoxShadow(
                  color: CyberpunkTheme.danger.withAlpha(70),
                  blurRadius: 14,
                  spreadRadius: 1,
                ),
                ...CyberpunkTheme.raisedShadows,
              ],
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        mainAxisSize: MainAxisSize.min,
        children: [
          Row(
            children: [
              _cell('方向', dirStr, color: dirColor),
              const SizedBox(width: 12),
              _cell(
                '速度',
                linkOk ? '${_speedValue.round()}%' : '--',
                color: linkOk ? CyberpunkTheme.primary : CyberpunkTheme.danger,
              ),
            ],
          ),
          Row(
            children: [
              _cell(
                '转向',
                linkOk ? '${_steerDeg.toStringAsFixed(0)}°' : '--',
                color: linkOk && _steerDeg.abs() > 1
                    ? CyberpunkTheme.warn
                    : CyberpunkTheme.dim,
              ),
              const SizedBox(width: 12),
              _cell(
                '蓝牙',
                linkOk ? '在线' : '断线',
                color: linkOk ? CyberpunkTheme.success : CyberpunkTheme.danger,
              ),
            ],
          ),
          const SizedBox(height: 4),
          Row(
            children: [
              _cell(
                'RSSI',
                linkOk ? '${app.bleConn.rssi}dBm' : '--',
                color: linkOk ? CyberpunkTheme.success : CyberpunkTheme.danger,
              ),
            ],
          ),
        ],
      ),
    );
  }

  Widget _hwDot(bool ok, String label) => Row(
    mainAxisSize: MainAxisSize.min,
    children: [
      Container(
        width: 5,
        height: 5,
        decoration: BoxDecoration(
          shape: BoxShape.circle,
          color: ok ? CyberpunkTheme.success : CyberpunkTheme.danger,
          boxShadow: ok
              ? [
                  BoxShadow(
                    color: CyberpunkTheme.success.withAlpha(80),
                    blurRadius: 3,
                  ),
                ]
              : null,
        ),
      ),
      const SizedBox(width: 3),
      Text(
        label,
        style: TextStyle(
          fontSize: 9,
          color: ok ? CyberpunkTheme.success : CyberpunkTheme.danger,
          fontFamily: 'monospace',
          letterSpacing: 0.5,
        ),
      ),
      const SizedBox(width: 6),
    ],
  );

  Widget _cell(String label, String v, {Color? color}) => Container(
    margin: const EdgeInsets.symmetric(vertical: 2),
    padding: const EdgeInsets.symmetric(horizontal: 5, vertical: 3),
    decoration: BoxDecoration(
      color: CyberpunkTheme.insetDeep,
      borderRadius: BorderRadius.circular(7),
      border: Border.all(color: (color ?? CyberpunkTheme.darkBorder).withAlpha(55)),
    ),
    child: Row(
      mainAxisSize: MainAxisSize.min,
      children: [
        Text(
          '$label ',
          style: const TextStyle(
            fontSize: 10,
            color: CyberpunkTheme.dim,
            fontFamily: 'monospace',
            letterSpacing: 0.5,
          ),
        ),
        AnimatedDefaultTextStyle(
          duration: CyberpunkTheme.durFast,
          curve: CyberpunkTheme.easeOut,
          style: TextStyle(
            fontSize: 11,
            color: color ?? CyberpunkTheme.text,
            fontFamily: 'monospace',
            fontWeight: FontWeight.w600,
          ),
          child: Text(v),
        ),
      ],
    ),
  );

  // ── 速度滑块 + 方向按钮 + 刹车 ──
  Widget _speedControls(bool linkOk) {
    return Container(
      width: 232,
      padding: const EdgeInsets.all(CyberpunkTheme.space3),
      decoration: BoxDecoration(
        color: CyberpunkTheme.raisedSurface,
        borderRadius: BorderRadius.circular(CyberpunkTheme.radiusSheet),
        border: Border.all(
          color: CyberpunkTheme.primary.withAlpha(145),
          width: 1.2,
        ),
        boxShadow: CyberpunkTheme.raisedShadowsStrong,
      ),
      child: Column(
        mainAxisSize: MainAxisSize.min,
        children: [
          Container(
            padding: const EdgeInsets.symmetric(horizontal: 4),
            decoration: BoxDecoration(
              color: CyberpunkTheme.insetDeep,
              borderRadius: BorderRadius.circular(CyberpunkTheme.radiusControl),
              border: Border.all(color: CyberpunkTheme.edgeHighlight),
              boxShadow: CyberpunkTheme.insetShadowsStrong,
            ),
            child: SliderTheme(
              data: const SliderThemeData(
                trackHeight: 6,
                thumbShape: RoundSliderThumbShape(enabledThumbRadius: 10),
                activeTrackColor: CyberpunkTheme.success,
                inactiveTrackColor: Color(0xFF27273B),
                thumbColor: CyberpunkTheme.success,
                overlayColor: Color(0x2210B981),
              ),
              child: Slider(
              value: _speedValue,
              min: 0,
              max: 100,
                onChanged: linkOk ? (v) => setState(() => _speedValue = v) : null,
              ),
            ),
          ),
          const SizedBox(height: 4),
          Text(
            '速度 ${_speedValue.round()}%',
            style: TextStyle(
              fontSize: 12,
              color: linkOk ? CyberpunkTheme.text : CyberpunkTheme.dim,
              fontFamily: 'monospace',
              fontWeight: FontWeight.w700,
              letterSpacing: 0.3,
            ),
          ),
          const SizedBox(height: 10),
          Row(
            mainAxisAlignment: MainAxisAlignment.spaceEvenly,
            children: [
              _holdBtn(
                '前进',
                Icons.arrow_upward,
                CyberpunkTheme.success,
                linkOk,
                () => _setDirection(1),
                () => _setDirection(0),
              ),
              _holdBtn(
                '倒车',
                Icons.arrow_downward,
                CyberpunkTheme.warn,
                linkOk,
                () => _setDirection(-1),
                () => _setDirection(0),
              ),
              _tapBtn(
                '刹车',
                Icons.stop,
                CyberpunkTheme.danger,
                linkOk,
                _sendBrake,
              ),
            ],
          ),
        ],
      ),
    );
  }

  /// 按住式按钮: 按下 onDown, 松开 onUp
  Widget _holdBtn(
    String label,
    IconData icon,
    Color color,
    bool linkOk,
    VoidCallback onDown,
    VoidCallback onUp,
  ) {
    final active =
        linkOk &&
        ((label == '前进' && _direction > 0) ||
            (label == '倒车' && _direction < 0));
    return PressScale(
      enabled: linkOk,
      onTapDown: (_) => onDown(),
      onTapUp: (_) => onUp(),
      onTapCancel: onUp,
      child: AnimatedContainer(
        duration: CyberpunkTheme.durFast,
        curve: CyberpunkTheme.easeOut,
        width: 64,
        height: 58,
        decoration: BoxDecoration(
          gradient: active
              ? LinearGradient(
                  begin: Alignment.topLeft,
                  end: Alignment.bottomRight,
                  colors: [color.withAlpha(245), color.withAlpha(175)],
                )
              : LinearGradient(
                  begin: Alignment.topLeft,
                  end: Alignment.bottomRight,
                  colors: linkOk
                      ? [CyberpunkTheme.raisedSurface, CyberpunkTheme.surface]
                      : [CyberpunkTheme.dim.withAlpha(55), CyberpunkTheme.dim.withAlpha(35)],
                ),
          borderRadius: BorderRadius.circular(CyberpunkTheme.radiusControl),
          border: Border.all(
            color: active
                ? Colors.white.withAlpha(120)
                : (linkOk ? color.withAlpha(210) : CyberpunkTheme.dim),
            width: active ? 1.5 : 1.3,
          ),
          boxShadow: active
              ? [
                  BoxShadow(color: color.withAlpha(105), blurRadius: 14, spreadRadius: 1),
                  const BoxShadow(color: CyberpunkTheme.shadowDark, offset: Offset(4, 5), blurRadius: 8),
                ]
              : CyberpunkTheme.raisedShadowsStrong,
        ),
        child: Column(
          mainAxisAlignment: MainAxisAlignment.center,
          children: [
            Icon(
              icon,
              color: active
                  ? Colors.white
                  : (linkOk ? color : CyberpunkTheme.dim),
              size: 20,
            ),
            const SizedBox(height: 1),
            Text(
              label,
              style: TextStyle(
                fontSize: 9,
                color: active
                    ? Colors.white
                    : (linkOk ? color : CyberpunkTheme.dim),
                fontWeight: FontWeight.w700,
              ),
            ),
          ],
        ),
      ),
    );
  }

  /// 点击式按钮
  Widget _tapBtn(
    String label,
    IconData icon,
    Color color,
    bool linkOk,
    VoidCallback onTap,
  ) {
    return PressScale(
      enabled: linkOk,
      onTap: onTap,
      child: AnimatedContainer(
        duration: CyberpunkTheme.durFast,
        curve: CyberpunkTheme.easeOut,
        width: 64,
        height: 58,
        decoration: BoxDecoration(
          gradient: linkOk
              ? LinearGradient(
                  begin: Alignment.topLeft,
                  end: Alignment.bottomRight,
                  colors: [color.withAlpha(250), color.withAlpha(175)],
                )
              : LinearGradient(
                  colors: [CyberpunkTheme.dim.withAlpha(80), CyberpunkTheme.dim.withAlpha(50)],
                ),
          borderRadius: BorderRadius.circular(CyberpunkTheme.radiusControl),
          border: Border.all(
            color: linkOk ? Colors.white.withAlpha(125) : CyberpunkTheme.dim,
            width: 1.5,
          ),
          boxShadow: linkOk
              ? [
                  BoxShadow(color: color.withAlpha(125), blurRadius: 15, spreadRadius: 1),
                  const BoxShadow(color: CyberpunkTheme.shadowDark, offset: Offset(5, 6), blurRadius: 10),
                ]
              : CyberpunkTheme.raisedShadows,
        ),
        child: Column(
          mainAxisAlignment: MainAxisAlignment.center,
          children: [
            Icon(icon, color: Colors.white, size: 20),
            const SizedBox(height: 1),
            Text(
              label,
              style: const TextStyle(
                fontSize: 9,
                color: Colors.white,
                fontWeight: FontWeight.w700,
              ),
            ),
          ],
        ),
      ),
    );
  }
}

class _ScanlinePainter extends CustomPainter {
  @override
  void paint(Canvas c, Size s) {
    final p = Paint()
      ..color = CyberpunkTheme.primary.withAlpha(3)
      ..strokeWidth = 0.5;
    for (double y = 0; y < s.height; y += 3)
      c.drawLine(Offset(0, y), Offset(s.width, y), p);
  }

  @override
  bool shouldRepaint(covariant _ScanlinePainter o) => false;
}
