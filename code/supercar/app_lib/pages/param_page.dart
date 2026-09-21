import 'dart:async';
import 'dart:typed_data';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:provider/provider.dart';
import '../models/ble_packet.dart';
import '../services/app_state.dart';
import '../theme.dart';
import '../widgets/press_scale.dart';

/// Tab2: 电机 / 编码器 / 速度闭环调试台。
/// 所有上行控制均为 CH9141 兼容的固定 4 字节 ASCII：MST、Lxx、Rxx、BRK、AT+/AT-。
class ParamPage extends StatefulWidget {
  const ParamPage({super.key});

  @override
  State<ParamPage> createState() => _ParamPageState();
}

class _ParamPageState extends State<ParamPage> {
  StreamSubscription<BlePacket>? _sub;
  Timer? _pollTimer;
  Timer? _driveKeepAliveTimer;
  bool _ready = false;
  bool _jogging = false;
  bool _tuning = false;
  String? _activeDrive;
  int _runLeft = 0, _runRight = 0;
  double _jogPercent = 45.0;
  int _encL = 0, _encR = 0;
  double _speedL = 0, _speedR = 0, _targetL = 0, _targetR = 0;
  double _kp = 0, _ki = 0, _kd = 0, _kf = 0;
  double? _tuneAvgL, _tuneAvgR;
  String _status = '等待连接';

  // static const _bg = Color(0xFF0F0F23);
  // static const _surface = Color(0xFF1A1A35);
  // static const _primary = Color(0xFF7C3AED);
  // static const _cyan = Color(0xFF22D3EE);
  // static const _green = Color(0xFF4ADE80);
  // static const _amber = Color(0xFFF59E0B);
  // static const _red = Color(0xFFEF4444);
  // static const _text = Color(0xFFE2E8F0);
  // static const _dim = Color(0xFF94A3B8);

  @override
  void didChangeDependencies() {
    super.didChangeDependencies();
    if (!_ready) {
      _ready = true;
      final app = context.read<AppState>();
      _sub = app.handshake.packetStream.listen(_onPacket);
      _pollTimer = Timer.periodic(
        const Duration(milliseconds: 300),
        (_) => _requestStatus(),
      );
      _requestStatus();
    }
  }

  @override
  void dispose() {
    _sub?.cancel();
    _pollTimer?.cancel();
    _driveKeepAliveTimer?.cancel();
    _driveKeepAliveTimer = null;
    _jogging = false;
    _activeDrive = null;
    _runLeft = 0;
    _runRight = 0;
    // 页面切换时全局传输栅栏会统一发送最终 STP；这里不再补发互相覆盖的 S0 命令。
    // 若此页被单独移除（非切页），仍显式中止自动整定并停车。
    final app = context.read<AppState>();
    if (!app.isPageSwitching && app.bleConn.isConnected) {
      if (_tuning) app.bleConn.sendAscii('AT-\n');
      app.requestSafetyStop(reason: '离开电机调试页');
    }
    super.dispose();
  }

  void _onPacket(BlePacket pkt) {
    if (!mounted) return;
    if (pkt.cmd == CmdCode.motorStatus && pkt.payload.length >= 24) {
      final d = Uint8List.fromList(pkt.payload);
      setState(() {
        _encL = _i32(d, 0);
        _encR = _i32(d, 4);
        _speedL = _i16(d, 8) / 10.0;
        _speedR = _i16(d, 10) / 10.0;
        _targetL = _i16(d, 12) / 10.0;
        _targetR = _i16(d, 14) / 10.0;
        _kp = _i16(d, 16) / 100.0;
        _ki = _i16(d, 18) / 1000.0;
        _kd = _i16(d, 20) / 100.0;
        _kf = _i16(d, 22) / 100.0;
        if (!_tuning)
          _status = _jogging
              ? '$_activeDrive 运行中：${_jogPercent.round()}% 输出；再次点击同一按钮停止'
              : '实时数据 @ ${DateTime.now().toString().substring(11, 19)}';
      });
    } else if (pkt.cmd == CmdCode.speedTune && pkt.payload.length >= 18) {
      final d = Uint8List.fromList(pkt.payload);
      setState(() {
        _tuning = false;
        _tuneAvgL = _i16(d, 0) / 10.0;
        _tuneAvgR = _i16(d, 2) / 10.0;
        _kp = _i16(d, 10) / 10.0;
        _ki = _i16(d, 12) / 1000.0;
        _kd = _i16(d, 14) / 10.0;
        _kf = _i16(d, 16) / 10.0;
        _status = '阶跃数据已返回：请使用下方速度自适应 PID 面板执行评分与节点更新';
      });
    } else if (pkt.cmd == CmdCode.ack &&
        pkt.payload.isNotEmpty &&
        pkt.payload.first == CmdCode.speedTune) {
      setState(() => _status = _tuning ? '速度阶跃测试中（约 4.5 秒）…' : '自动整定已中止');
    }
  }

  void _requestStatus() {
    // 点动/整定期间只保留控制命令，状态帧暂停，避免与控制命令争用 CH9141 的单通道。
    if (_tuning) return;
    final app = context.read<AppState>();
    if (!app.bleConn.isConnected || !app.handshake.linkAlive) return;
    app.bleConn.sendAscii('MST\n');
  }

  int _i16(Uint8List d, int o) => (d[o] | (d[o + 1] << 8)).toSigned(16);
  int _i32(Uint8List d, int o) =>
      (d[o] | (d[o + 1] << 8) | (d[o + 2] << 16) | (d[o + 3] << 24)).toSigned(
        32,
      );

  String _driveHex(int value) => (value.clamp(-100, 100) + 128)
      .toRadixString(16)
      .toUpperCase()
      .padLeft(2, '0');

  void _sendDrive(int left, int right) {
    final app = context.read<AppState>();
    if (!app.bleConn.isConnected || !app.handshake.linkAlive) return;
    app.bleConn.sendDrivePair(
      'L${_driveHex(left)}\n',
      'R${_driveHex(right)}\n',
    );
  }

  /// 点一次开始、再点同一按钮停止。运行中每 250ms 续发，满足 MCU 500ms 失效保护。
  /// 长按不会提高发送频率；UI 只用一次 onTap 作为开关动作。
  void _toggleDrive(String mode, int leftSign, int rightSign) {
    if (_tuning) return;
    if (_jogging && _activeDrive == mode) {
      _stop();
      return;
    }
    final output = _jogPercent.round().clamp(1, 100);
    _driveKeepAliveTimer?.cancel();
    _activeDrive = mode;
    _runLeft = leftSign * output;
    _runRight = rightSign * output;
    _jogging = true;
    _sendDrive(_runLeft, _runRight);
    _driveKeepAliveTimer = Timer.periodic(const Duration(milliseconds: 250), (
      _,
    ) {
      if (_jogging) _sendDrive(_runLeft, _runRight);
    });
    setState(() => _status = '$mode 运行中：${_jogPercent.round()}% 输出；再次点击同一按钮停止');
  }

  void _setJogSpeed(double value) {
    setState(() => _jogPercent = value);
    if (_jogging) {
      final output = value.round().clamp(1, 100);
      _runLeft = _runLeft == 0 ? 0 : (_runLeft < 0 ? -output : output);
      _runRight = _runRight == 0 ? 0 : (_runRight < 0 ? -output : output);
      _sendDrive(_runLeft, _runRight);
    }
  }

  void _stop() {
    _driveKeepAliveTimer?.cancel();
    _driveKeepAliveTimer = null;
    _jogging = false;
    _activeDrive = null;
    _runLeft = 0;
    _runRight = 0;
    // BRK 是 S0，负责清零 MCU 目标并抢占旧控制；不要先发普通 L80/R80，
    // 否则它们会重新激活实时控制状态。
    final app = context.read<AppState>();
    app.requestStop();
    app.bleConn.sendAscii('BRK\n');
    if (mounted) setState(() => _status = '已停止并刹车');
  }

  Future<void> _toggleAutoTune() async {
    if (_tuning) {
      context.read<AppState>().bleConn.sendAscii('AT-\n');
      setState(() {
        _tuning = false;
        _status = '正在中止自动整定…';
      });
      return;
    }
    final confirmed = await showDialog<bool>(
      context: context,
      builder: (dialogContext) => AlertDialog(
        backgroundColor: CyberpunkTheme.surface,
        title: const Text(
          '开始速度 PID 自动整定？',
          style: TextStyle(color: CyberpunkTheme.text, fontSize: 16),
        ),
        content: const Text(
          '测试将以 35cm/s 驱动双轮约 4.5 秒。请务必架空小车，确认编码器方向正确，周围没有人或障碍物。',
          style: TextStyle(
            color: CyberpunkTheme.dim,
            fontSize: 13,
            height: 1.45,
          ),
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(dialogContext, false),
            child: const Text('取消'),
          ),
          ElevatedButton(
            onPressed: () => Navigator.pop(dialogContext, true),
            child: const Text('已架空，开始'),
          ),
        ],
      ),
    );
    if (!mounted || confirmed != true) return;
    // 自动整定为固定 35cm/s、4.5秒阶跃；启动前 MCU 会清零旧控制权。
    context.read<AppState>().bleConn.sendAscii('AT+\n');
    setState(() {
      _tuning = true;
      _tuneAvgL = _tuneAvgR = null;
      _status = '正在启动速度闭环自动整定…';
    });
  }

  @override
  Widget build(BuildContext context) {
    final app = context.watch<AppState>();
    final linkOk = app.bleConn.isConnected && app.handshake.linkAlive;
    return Container(
      color: CyberpunkTheme.background,
      child: Column(
        children: [
          _header(linkOk),
          Expanded(
            child: SingleChildScrollView(
              padding: const EdgeInsets.fromLTRB(12, 10, 12, 18),
              child: Column(
                children: [
                  _speedPanel(linkOk),
                  const SizedBox(height: 10),
                  _encoderPanel(),
                  const SizedBox(height: 10),
                  _testPanel(linkOk),
                  const SizedBox(height: 10),
                  _pidPanel(),
                  const SizedBox(height: 10),
                  _safetyNote(),
                ],
              ),
            ),
          ),
        ],
      ),
    );
  }

  Widget _header(bool linkOk) => AnimatedContainer(
    duration: CyberpunkTheme.durFast,
    curve: CyberpunkTheme.easeOut,
    margin: const EdgeInsets.fromLTRB(8, 8, 8, 4),
    padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 11),
    decoration: BoxDecoration(
      color: CyberpunkTheme.raisedSurface,
      borderRadius: BorderRadius.circular(CyberpunkTheme.radiusCard),
      border: Border.all(
        color: linkOk
            ? CyberpunkTheme.green.withAlpha(150)
            : CyberpunkTheme.red.withAlpha(170),
        width: 1.1,
      ),
      boxShadow: CyberpunkTheme.raisedShadows,
    ),
    child: Row(
      children: [
        TweenAnimationBuilder<Color?>(
          duration: CyberpunkTheme.durFast,
          curve: CyberpunkTheme.easeOut,
          tween: ColorTween(
            end: linkOk ? CyberpunkTheme.cyan : CyberpunkTheme.dim,
          ),
          builder: (_, color, __) => Icon(
            Icons.speed_rounded,
            color: color,
            size: 19,
          ),
        ),
        const SizedBox(width: 7),
        const Text(
          '电机闭环调试',
          style: TextStyle(
            color: CyberpunkTheme.text,
            fontSize: 13,
            fontWeight: FontWeight.w700,
          ),
        ),
        const Spacer(),
        AnimatedContainer(
          duration: CyberpunkTheme.durFast,
          curve: CyberpunkTheme.easeOut,
          padding: const EdgeInsets.symmetric(horizontal: 7, vertical: 3),
          decoration: BoxDecoration(
            color: (linkOk ? CyberpunkTheme.green : CyberpunkTheme.red)
                .withAlpha(25),
            borderRadius: BorderRadius.circular(20),
          ),
          child: AnimatedDefaultTextStyle(
            duration: CyberpunkTheme.durFast,
            curve: CyberpunkTheme.easeOut,
            style: TextStyle(
              color: linkOk ? CyberpunkTheme.green : CyberpunkTheme.red,
              fontSize: 10,
              fontWeight: FontWeight.w700,
            ),
            child: Text(linkOk ? '实时在线' : '未连接'),
          ),
        ),
      ],
    ),
  );

  Widget _speedPanel(bool linkOk) => _card(
    child: Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        const Text(
          '双轮速度闭环',
          style: TextStyle(
            color: CyberpunkTheme.text,
            fontWeight: FontWeight.w700,
            fontSize: 12,
          ),
        ),
        const SizedBox(height: 10),
        Row(
          children: [
            Expanded(
              child: _wheelGauge('左轮', _speedL, _targetL, CyberpunkTheme.cyan),
            ),
            const SizedBox(width: 10),
            Expanded(
              child: _wheelGauge(
                '右轮',
                _speedR,
                _targetR,
                CyberpunkTheme.primary,
              ),
            ),
          ],
        ),
        const SizedBox(height: 9),
        Row(
          children: [
            Icon(Icons.sync_rounded, size: 13, color: CyberpunkTheme.dim),
            const SizedBox(width: 5),
            Expanded(
              child: Text(
                _status,
                style: const TextStyle(color: CyberpunkTheme.dim, fontSize: 10),
              ),
            ),
            Text(
              '单位 cm/s',
              style: TextStyle(
                color: linkOk ? CyberpunkTheme.green : CyberpunkTheme.dim,
                fontSize: 10,
              ),
            ),
          ],
        ),
      ],
    ),
  );

  Widget _wheelGauge(String label, double actual, double target, Color color) {
    final ratio = (actual.abs() / 60.0).clamp(0.0, 1.0);
    return Container(
      padding: const EdgeInsets.all(11),
      decoration: BoxDecoration(
        color: CyberpunkTheme.insetDeep,
        border: Border.all(color: color.withAlpha(120), width: 1.1),
        borderRadius: BorderRadius.circular(CyberpunkTheme.radiusControl),
        boxShadow: CyberpunkTheme.insetShadows,
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text(
            label,
            style: TextStyle(
              color: color,
              fontSize: 10,
              fontWeight: FontWeight.w700,
            ),
          ),
          const SizedBox(height: 6),
          Text(
            '${actual.toStringAsFixed(1)}',
            style: TextStyle(
              color: CyberpunkTheme.text,
              fontFamily: 'monospace',
              fontSize: 24,
              fontWeight: FontWeight.w800,
            ),
          ),
          Text(
            '目标 ${target.toStringAsFixed(1)}',
            style: const TextStyle(color: CyberpunkTheme.dim, fontSize: 10),
          ),
          const SizedBox(height: 7),
          ClipRRect(
            borderRadius: BorderRadius.circular(4),
            child: TweenAnimationBuilder<double>(
              duration: CyberpunkTheme.durFast,
              curve: CyberpunkTheme.easeOut,
              tween: Tween(end: ratio),
              builder: (_, value, __) => LinearProgressIndicator(
                value: value,
                minHeight: 5,
                color: color,
                backgroundColor: CyberpunkTheme.activeBorder,
              ),
            ),
          ),
        ],
      ),
    );
  }

  Widget _encoderPanel() => _card(
    child: Row(
      children: [
        Icon(
          Icons.settings_input_component_rounded,
          color: CyberpunkTheme.amber,
          size: 20,
        ),
        const SizedBox(width: 9),
        Expanded(child: _metric('左编码器', '$_encL', CyberpunkTheme.cyan)),
        Container(width: 1, height: 36, color: CyberpunkTheme.activeBorder),
        Expanded(child: _metric('右编码器', '$_encR', CyberpunkTheme.primary)),
      ],
    ),
  );

  Widget _metric(String title, String value, Color color) => Padding(
    padding: const EdgeInsets.symmetric(horizontal: 10),
    child: Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Text(
          title,
          style: const TextStyle(color: CyberpunkTheme.dim, fontSize: 9),
        ),
        const SizedBox(height: 3),
        Text(
          value,
          style: TextStyle(
            color: color,
            fontSize: 16,
            fontWeight: FontWeight.w700,
            fontFamily: 'monospace',
          ),
        ),
      ],
    ),
  );

  Widget _testPanel(bool linkOk) => _card(
    child: Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        const Text(
          '开关式电机测试',
          style: TextStyle(
            color: CyberpunkTheme.text,
            fontWeight: FontWeight.w700,
            fontSize: 12,
          ),
        ),
        const SizedBox(height: 4),
        const Text(
          '点一次开始，再点同一按钮停止；滑条可在运行中实时调整输出。建议架空小车测试。',
          style: TextStyle(color: CyberpunkTheme.dim, fontSize: 10),
        ),
        const SizedBox(height: 9),
        Row(
          children: [
            Icon(Icons.speed_rounded, color: CyberpunkTheme.amber, size: 17),
            const SizedBox(width: 6),
            const Text(
              '输出速度',
              style: TextStyle(color: CyberpunkTheme.dim, fontSize: 10),
            ),
            const Spacer(),
            Text(
              '${_jogPercent.round()}%',
              style: const TextStyle(
                color: CyberpunkTheme.amber,
                fontFamily: 'monospace',
                fontSize: 16,
                fontWeight: FontWeight.w800,
              ),
            ),
          ],
        ),
        Slider(
          value: _jogPercent,
          min: 10,
          max: 100,
          divisions: 18,
          activeColor: CyberpunkTheme.amber,
          inactiveColor: CyberpunkTheme.activeBorder,
          label: '${_jogPercent.round()}%',
          onChanged: linkOk && !_tuning ? _setJogSpeed : null,
        ),
        Row(
          children: [
            Expanded(
              child: _tapButton(
                '左轮',
                Icons.rotate_left_rounded,
                CyberpunkTheme.cyan,
                linkOk,
                () => _toggleDrive('左轮', 1, 0),
                _activeDrive == '左轮',
              ),
            ),
            const SizedBox(width: 8),
            Expanded(
              child: _tapButton(
                '双轮',
                Icons.arrow_upward_rounded,
                CyberpunkTheme.green,
                linkOk,
                () => _toggleDrive('双轮', 1, 1),
                _activeDrive == '双轮',
              ),
            ),
            const SizedBox(width: 8),
            Expanded(
              child: _tapButton(
                '右轮',
                Icons.rotate_right_rounded,
                CyberpunkTheme.primary,
                linkOk,
                () => _toggleDrive('右轮', 0, 1),
                _activeDrive == '右轮',
              ),
            ),
          ],
        ),
        const SizedBox(height: 8),
        SizedBox(
          width: double.infinity,
          child: OutlinedButton.icon(
            onPressed: linkOk ? _stop : null,
            icon: const Icon(Icons.stop_circle_outlined, size: 16),
            label: const Text('停止并刹车'),
            style: OutlinedButton.styleFrom(
              foregroundColor: CyberpunkTheme.red,
              side: BorderSide(color: CyberpunkTheme.red.withAlpha(150)),
            ),
          ),
        ),
      ],
    ),
  );

  Widget _tapButton(
    String label,
    IconData icon,
    Color color,
    bool enabled,
    VoidCallback onTap,
    bool active,
  ) => PressScale(
    enabled: enabled && !_tuning,
    onTap: onTap,
    child: AnimatedContainer(
      duration: CyberpunkTheme.durFast,
      curve: CyberpunkTheme.easeOut,
      height: 68,
      decoration: BoxDecoration(
        gradient: !enabled
            ? null
            : active
            ? LinearGradient(
                begin: Alignment.topLeft,
                end: Alignment.bottomRight,
                colors: [color.withAlpha(150), color.withAlpha(70)],
              )
            : null,
        color: !enabled
            ? CyberpunkTheme.dim.withAlpha(40)
            : active
            ? null
            : CyberpunkTheme.raisedSurface,
        borderRadius: BorderRadius.circular(CyberpunkTheme.radiusControl),
        border: Border.all(
          color: active
              ? Colors.white.withAlpha(110)
              : enabled
              ? color.withAlpha(190)
              : CyberpunkTheme.dim,
          width: active ? 1.6 : 1.2,
        ),
        boxShadow: active
            ? [
                BoxShadow(color: color.withAlpha(90), blurRadius: 12, spreadRadius: 1),
                const BoxShadow(color: CyberpunkTheme.shadowDark, offset: Offset(3, 4), blurRadius: 7),
              ]
            : enabled
            ? CyberpunkTheme.raisedShadows
            : null,
      ),
      child: Column(
        mainAxisAlignment: MainAxisAlignment.center,
        children: [
          Icon(
            active ? Icons.pause_circle_outline : icon,
            color: enabled ? color : CyberpunkTheme.dim,
            size: 19,
          ),
          const SizedBox(height: 4),
          Text(
            active ? '点击停止' : label,
            style: TextStyle(
              color: enabled ? CyberpunkTheme.text : CyberpunkTheme.dim,
              fontSize: 10,
              fontWeight: FontWeight.w700,
            ),
          ),
        ],
      ),
    ),
  );

  Widget _pidPanel() => _card(
    child: Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        const Row(
          children: [
            Icon(
              Icons.auto_graph_rounded,
              color: CyberpunkTheme.primary,
              size: 20,
            ),
            SizedBox(width: 8),
            Text(
              '速度闭环 PID 自动整定',
              style: TextStyle(
                color: CyberpunkTheme.text,
                fontWeight: FontWeight.w700,
                fontSize: 12,
              ),
            ),
          ],
        ),
        const SizedBox(height: 8),
        Row(
          children: [
            Expanded(
              child: _metric(
                '当前 Kp',
                _kp.toStringAsFixed(2),
                CyberpunkTheme.primary,
              ),
            ),
            Expanded(
              child: _metric(
                '当前 Ki',
                _ki.toStringAsFixed(3),
                CyberpunkTheme.green,
              ),
            ),
            Expanded(
              child: _metric(
                '当前 Kd',
                _kd.toStringAsFixed(2),
                CyberpunkTheme.cyan,
              ),
            ),
            Expanded(
              child: _metric(
                '当前 Kf',
                _kf.toStringAsFixed(2),
                CyberpunkTheme.amber,
              ),
            ),
            if (_tuneAvgL != null && _tuneAvgR != null)
              Expanded(
                child: _metric(
                  '整定均速',
                  '${((_tuneAvgL! + _tuneAvgR!) / 2).toStringAsFixed(1)}',
                  CyberpunkTheme.green,
                ),
              ),
          ],
        ),
        const SizedBox(height: 8),
        Text(
          _tuning
              ? '正在以 35cm/s 做双轮阶跃试验；约4.5秒后自动停车并更新 Kp/Ki/Kd/Kf。'
              : '分速度节点整定会根据稳态误差、超调与波动按实测比例更新 Kp/Ki/Kd/Kf；仅用于已架空的编码器速度环。',
          style: const TextStyle(
            color: CyberpunkTheme.dim,
            fontSize: 10,
            height: 1.35,
          ),
        ),
        const SizedBox(height: 9),
        SizedBox(
          width: double.infinity,
          child: ElevatedButton.icon(
            onPressed: _linkOkForTune ? _toggleAutoTune : null,
            icon: Icon(
              _tuning ? Icons.stop_circle_outlined : Icons.play_circle_outline,
              size: 17,
            ),
            label: Text(_tuning ? '中止自动整定' : '开始速度 PID 自动整定'),
            style: ElevatedButton.styleFrom(
              backgroundColor: _tuning
                  ? CyberpunkTheme.red
                  : CyberpunkTheme.primary,
              foregroundColor: Colors.white,
            ),
          ),
        ),
      ],
    ),
  );

  bool get _linkOkForTune {
    final app = context.read<AppState>();
    return app.bleConn.isConnected && app.handshake.linkAlive && !_jogging;
  }

  Widget _safetyNote() => Container(
    width: double.infinity,
    margin: const EdgeInsets.symmetric(vertical: 2),
    padding: const EdgeInsets.all(11),
    decoration: BoxDecoration(
      color: CyberpunkTheme.raisedSurface,
      border: Border.all(color: CyberpunkTheme.amber.withAlpha(140), width: 1.1),
      borderRadius: BorderRadius.circular(CyberpunkTheme.radiusCard),
      boxShadow: CyberpunkTheme.raisedShadows,
    ),
    child: const Row(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Icon(Icons.info_outline, size: 16, color: CyberpunkTheme.warn),
        SizedBox(width: 7),
        Expanded(
          child: Text(
            '调参顺序建议：先确认左右编码器方向与速度一致，再调速度环 P、D，最后再进入循迹 PD 调整。电机点动只用于台架检查，不建议在赛道上长按。',
            style: TextStyle(
              color: Color(0xFFCBD5E1),
              fontSize: 10,
              height: 1.4,
            ),
          ),
        ),
      ],
    ),
  );

  Widget _card({required Widget child}) => Container(
    width: double.infinity,
    margin: const EdgeInsets.symmetric(vertical: 2),
    padding: const EdgeInsets.all(13),
    decoration: BoxDecoration(
      color: CyberpunkTheme.raisedSurface,
      borderRadius: BorderRadius.circular(CyberpunkTheme.radiusCard),
      border: Border.all(color: CyberpunkTheme.edgeHighlight, width: 1),
      boxShadow: CyberpunkTheme.raisedShadows,
    ),
    child: child,
  );
}
