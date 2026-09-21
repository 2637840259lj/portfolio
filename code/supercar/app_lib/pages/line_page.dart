import 'dart:async';
import 'dart:typed_data';
import 'package:flutter/material.dart';
import 'package:provider/provider.dart';
import '../models/ble_packet.dart';
import '../services/app_state.dart';
import '../theme.dart';

/// Tab4: 8 路灰度 / 循迹 PD 台架调试。
/// 上行全部固定 4 字节 ASCII：LST、GCL、LF+/LF-、PPx、PDx。
class LinePage extends StatefulWidget {
  const LinePage({super.key});

  @override
  State<LinePage> createState() => _LinePageState();
}

class _LinePageState extends State<LinePage> {
  StreamSubscription<BlePacket>? _sub;
  Timer? _pollTimer;
  bool _ready = false;
  bool _running = false;
  bool _calibrating = false;
  int _gray = 0;
  DateTime? _lastStatusAt;
  double _error = 0, _kp = 0, _kd = 0;
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
      // 3.3Hz 已足够用于灰度观察，也为控制/心跳预留 CH9141 写入带宽。
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
    // 正常切页由全局传输栅栏统一停车；避免 STP 与 LF- 在同一 S0 队列中互相覆盖。
    final app = context.read<AppState>();
    if (!app.isPageSwitching && app.bleConn.isConnected) {
      if (_running) app.bleConn.sendAscii('LF-\n');
      app.requestSafetyStop(reason: '离开循迹页');
    }
    super.dispose();
  }

  void _onPacket(BlePacket pkt) {
    if (pkt.cmd == CmdCode.lineStatus && pkt.payload.length >= 8) {
      final d = Uint8List.fromList(pkt.payload);
      if (!mounted) return;
      setState(() {
        _gray = d[0];
        _lastStatusAt = DateTime.now();
        _running = d[1] != 0;
        _error = _i16(d, 2) / 10.0;
        _kp = _i16(d, 4) / 100000.0;
        _kd = _i16(d, 6) / 100000.0;
        _status = _running
            ? '低速循迹运行中'
            : (_gray == 0 ? '白底：当前未检测到黑线' : '传感器实时监测');
      });
    } else if (pkt.cmd == CmdCode.ack && pkt.payload.isNotEmpty && mounted) {
      final command = pkt.payload.first;
      if (command == CmdCode.grayCal) {
        context.read<AppState>().markGrayCalibrationValid();
        setState(() {
          _calibrating = false;
          _status = '白底标定完成';
        });
      }
      if (command == CmdCode.lineStart) setState(() => _status = '低速循迹已启动');
      if (command == CmdCode.lineStop) setState(() => _status = '循迹已停止');
    }
  }

  int _i16(Uint8List d, int o) => (d[o] | (d[o + 1] << 8)).toSigned(16);

  bool get _linkOk {
    final app = context.read<AppState>();
    return app.bleConn.isConnected && app.handshake.linkAlive;
  }

  bool get _sensorOnline {
    final last = _lastStatusAt;
    return _linkOk &&
        last != null &&
        DateTime.now().difference(last) < const Duration(seconds: 1);
  }

  void _requestStatus() {
    // 白底标定占用 MCU 约 1.7 秒；期间不再堆叠 LST 查询，确保 GCL 的 ACK 能及时返回。
    if (_linkOk && !_calibrating) {
      // 状态帧超过 1 秒未返回才判为“等待传感器”；灰度 0x00 只表示全白。
      if (_lastStatusAt != null && !_sensorOnline && mounted) {
        setState(() {
          _lastStatusAt = null;
          if (!_running) _status = '未收到传感器状态帧，请检查 MCU 固件与蓝牙链路';
        });
      }
      context.read<AppState>().bleConn.sendAscii('LST\n');
    }
  }

  Future<void> _calibrateWhite() async {
    if (!_linkOk || _calibrating) return;
    setState(() {
      _calibrating = true;
      _status = '正在采样白底，请保持传感器在白色区域';
    });
    await context.read<AppState>().bleConn.sendAscii('GCL\n');
    // MCU 采集 100 次（约 1 秒）并伴随蜂鸣提示；预留队列传输余量。
    Future<void>.delayed(const Duration(seconds: 5), () {
      if (mounted && _calibrating)
        setState(() {
          _calibrating = false;
          _status = '标定未确认，请检查连接';
        });
    });
  }

  void _toggleLine() {
    if (!_linkOk) return;
    context.read<AppState>().bleConn.sendAscii(_running ? 'LF-\n' : 'LF+\n');
    setState(() => _status = _running ? '正在停止循迹...' : '正在启动低速循迹...');
  }

  void _adjust(String kind, int delta) {
    if (!_linkOk || delta == 0) return;
    final nibble = (delta.clamp(-8, 7) + 8).toRadixString(16).toUpperCase();
    context.read<AppState>().bleConn.sendAscii('$kind$nibble\n');
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
                  _sensorPanel(),
                  const SizedBox(height: 10),
                  _errorPanel(),
                  const SizedBox(height: 10),
                  _pidPanel(linkOk),
                  const SizedBox(height: 10),
                  _controlPanel(linkOk),
                  const SizedBox(height: 10),
                  _guidePanel(),
                ],
              ),
            ),
          ),
        ],
      ),
    );
  }

  Widget _header(bool linkOk) => Container(
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
        Icon(
          Icons.route_rounded,
          color: linkOk ? CyberpunkTheme.primary : CyberpunkTheme.dim,
          size: 19,
        ),
        const SizedBox(width: 7),
        const Text(
          '循迹调试台',
          style: TextStyle(
            color: CyberpunkTheme.text,
            fontSize: 13,
            fontWeight: FontWeight.w700,
          ),
        ),
        const Spacer(),
        Container(
          padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 3),
          decoration: BoxDecoration(
            color:
                (_running
                        ? CyberpunkTheme.green
                        : _sensorOnline
                        ? CyberpunkTheme.amber
                        : CyberpunkTheme.red)
                    .withAlpha(25),
            borderRadius: BorderRadius.circular(20),
          ),
          child: Text(
            _running
                ? '循迹中'
                : _sensorOnline
                ? '监测中'
                : linkOk
                ? '等待传感器'
                : '未连接',
            style: TextStyle(
              color: _running
                  ? CyberpunkTheme.green
                  : _sensorOnline
                  ? CyberpunkTheme.amber
                  : CyberpunkTheme.red,
              fontSize: 10,
              fontWeight: FontWeight.w700,
            ),
          ),
        ),
      ],
    ),
  );

  Widget _sensorPanel() => _card(
    child: Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Row(
          children: [
            const Text(
              '8 路灰度阵列',
              style: TextStyle(
                color: CyberpunkTheme.text,
                fontWeight: FontWeight.w700,
                fontSize: 12,
              ),
            ),
            const Spacer(),
            Text(
              '0x${_gray.toRadixString(16).toUpperCase().padLeft(2, '0')}',
              style: const TextStyle(
                color: CyberpunkTheme.cyan,
                fontFamily: 'monospace',
                fontSize: 13,
                fontWeight: FontWeight.w700,
              ),
            ),
          ],
        ),
        const SizedBox(height: 11),
        Row(
          children: List.generate(8, (displayIndex) {
            // 硬件物理安装方向：车头朝前时，屏幕左→右对应传感器 S8→S1。
            final sensorIndex = 7 - displayIndex;
            final active = ((_gray >> sensorIndex) & 1) != 0;
            final centerDistance = (displayIndex - 3.5).abs();
            final color = active
                ? (centerDistance < 1.1
                      ? CyberpunkTheme.green
                      : CyberpunkTheme.amber)
                : CyberpunkTheme.darkGray;
            return Expanded(
              child: AnimatedContainer(
                duration: CyberpunkTheme.durFast,
                curve: CyberpunkTheme.easeOut,
                height: 58,
                margin: EdgeInsets.only(right: displayIndex == 7 ? 0 : 5),
                decoration: BoxDecoration(
                  gradient: active
                      ? LinearGradient(
                          begin: Alignment.topLeft,
                          end: Alignment.bottomRight,
                          colors: [color.withAlpha(180), color.withAlpha(110)],
                        )
                      : null,
                  color: active ? null : CyberpunkTheme.insetDeep,
                  borderRadius: BorderRadius.circular(9),
                  border: Border.all(
                    color: active ? Colors.white.withAlpha(100) : CyberpunkTheme.darkBorder,
                    width: active ? 1.3 : 1,
                  ),
                  boxShadow: active
                      ? [
                          BoxShadow(color: color.withAlpha(120), blurRadius: 10, spreadRadius: 1),
                          const BoxShadow(color: CyberpunkTheme.shadowDark, offset: Offset(2, 3), blurRadius: 5),
                        ]
                      : CyberpunkTheme.insetShadows,
                ),
                child: Column(
                  mainAxisAlignment: MainAxisAlignment.center,
                  children: [
                    Text(
                      'S${sensorIndex + 1}',
                      style: const TextStyle(
                        color: CyberpunkTheme.dim,
                        fontSize: 9,
                      ),
                    ),
                    const SizedBox(height: 4),
                    Text(
                      active ? '黑' : '白',
                      style: TextStyle(
                        color: active ? color : CyberpunkTheme.dim,
                        fontSize: 11,
                        fontWeight: FontWeight.w700,
                      ),
                    ),
                  ],
                ),
              ),
            );
          }),
        ),
        const SizedBox(height: 8),
        const Row(
          children: [
            Icon(Icons.info_outline, size: 13, color: CyberpunkTheme.dim),
            SizedBox(width: 5),
            Expanded(
              child: Text(
                '车头朝前时，屏幕从左至右为 S8 → S1。全白仅表示未压到黑线，不表示离线；在线状态以状态帧是否持续返回为准。',
                style: TextStyle(color: CyberpunkTheme.dim, fontSize: 10),
              ),
            ),
          ],
        ),
      ],
    ),
  );

  Widget _errorPanel() {
    final normalized = (_error / 350.0).clamp(-1.0, 1.0);
    final color = _error.abs() < 30
        ? CyberpunkTheme.green
        : _error.abs() < 120
        ? CyberpunkTheme.amber
        : CyberpunkTheme.red;
    return _card(
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          const Text(
            '循迹误差',
            style: TextStyle(
              color: CyberpunkTheme.text,
              fontWeight: FontWeight.w700,
              fontSize: 12,
            ),
          ),
          const SizedBox(height: 9),
          Row(
            children: [
              Expanded(
                child: Text(
                  _error.toStringAsFixed(1),
                  style: TextStyle(
                    color: color,
                    fontSize: 28,
                    fontWeight: FontWeight.w800,
                    fontFamily: 'monospace',
                  ),
                ),
              ),
              Text(
                _error.abs() < 30
                    ? '居中稳定'
                    : _error > 0
                    ? '偏右'
                    : '偏左',
                style: TextStyle(
                  color: color,
                  fontSize: 12,
                  fontWeight: FontWeight.w700,
                ),
              ),
            ],
          ),
          const SizedBox(height: 8),
          LayoutBuilder(
            builder: (_, c) {
              final mid = c.maxWidth / 2;
              final markerX = (mid + normalized * (mid - 11)).clamp(
                0.0,
                c.maxWidth - 22.0,
              );
              return SizedBox(
                height: 20,
                child: Stack(
                  children: [
                    Positioned(
                      left: mid - 0.5,
                      top: 0,
                      bottom: 0,
                      child: Container(
                        width: 1,
                        color: CyberpunkTheme.dim.withAlpha(120),
                      ),
                    ),
                    AnimatedPositioned(
                      duration: CyberpunkTheme.durStandard,
                      curve: CyberpunkTheme.easeOut,
                      left: markerX,
                      top: 2,
                      child: AnimatedContainer(
                        duration: CyberpunkTheme.durFast,
                        curve: CyberpunkTheme.easeOut,
                        width: 22,
                        height: 16,
                        decoration: BoxDecoration(
                          color: color,
                          borderRadius: BorderRadius.circular(9),
                          boxShadow: [
                            BoxShadow(
                              color: color.withAlpha(90),
                              blurRadius: 6,
                            ),
                          ],
                        ),
                      ),
                    ),
                  ],
                ),
              );
            },
          ),
          const SizedBox(height: 5),
          const Row(
            mainAxisAlignment: MainAxisAlignment.spaceBetween,
            children: [
              Text(
                '左偏',
                style: TextStyle(color: CyberpunkTheme.dim, fontSize: 9),
              ),
              Text(
                '中心',
                style: TextStyle(color: CyberpunkTheme.dim, fontSize: 9),
              ),
              Text(
                '右偏',
                style: TextStyle(color: CyberpunkTheme.dim, fontSize: 9),
              ),
            ],
          ),
        ],
      ),
    );
  }

  Widget _pidPanel(bool linkOk) => _card(
    child: Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        const Text(
          '循迹 PD 快调',
          style: TextStyle(
            color: CyberpunkTheme.text,
            fontWeight: FontWeight.w700,
            fontSize: 12,
          ),
        ),
        const SizedBox(height: 4),
        const Text(
          '每次点击仅下发一个受限小步，便于在赛道上渐进整定。',
          style: TextStyle(color: CyberpunkTheme.dim, fontSize: 10),
        ),
        const SizedBox(height: 10),
        Row(
          children: [
            Expanded(
              child: _pidControl(
                '比例 P',
                _kp,
                'PP',
                1,
                0.0001,
                CyberpunkTheme.primary,
                linkOk,
              ),
            ),
            const SizedBox(width: 9),
            Expanded(
              child: _pidControl(
                '微分 D',
                _kd,
                'PD',
                1,
                0.00001,
                CyberpunkTheme.cyan,
                linkOk,
              ),
            ),
          ],
        ),
      ],
    ),
  );

  Widget _pidControl(
    String label,
    double value,
    String command,
    int step,
    double unit,
    Color color,
    bool enabled,
  ) => Container(
    padding: const EdgeInsets.all(10),
    decoration: BoxDecoration(
      color: CyberpunkTheme.insetDeep,
      border: Border.all(color: color.withAlpha(130), width: 1.1),
      borderRadius: BorderRadius.circular(CyberpunkTheme.radiusControl),
      boxShadow: CyberpunkTheme.insetShadows,
    ),
    child: Column(
      children: [
        Text(
          label,
          style: TextStyle(
            color: color,
            fontSize: 10,
            fontWeight: FontWeight.w700,
          ),
        ),
        const SizedBox(height: 5),
        Text(
          value.toStringAsFixed(5),
          style: const TextStyle(
            color: CyberpunkTheme.text,
            fontFamily: 'monospace',
            fontSize: 14,
            fontWeight: FontWeight.w700,
          ),
        ),
        const SizedBox(height: 8),
        Row(
          children: [
            Expanded(
              child: _miniButton(
                '−',
                enabled,
                color,
                () => _adjust(command, -step),
              ),
            ),
            const SizedBox(width: 6),
            Expanded(
              child: _miniButton(
                '+',
                enabled,
                color,
                () => _adjust(command, step),
              ),
            ),
          ],
        ),
      ],
    ),
  );

  Widget _miniButton(
    String text,
    bool enabled,
    Color color,
    VoidCallback onTap,
  ) => SizedBox(
    height: 29,
    child: OutlinedButton(
      onPressed: enabled ? onTap : null,
      style: OutlinedButton.styleFrom(
        padding: EdgeInsets.zero,
        foregroundColor: color,
        side: BorderSide(color: color.withAlpha(150)),
      ),
      child: Text(
        text,
        style: const TextStyle(fontSize: 16, fontWeight: FontWeight.w700),
      ),
    ),
  );

  Widget _controlPanel(bool linkOk) => _card(
    child: Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        const Text(
          '低速循迹台架模式',
          style: TextStyle(
            color: CyberpunkTheme.text,
            fontWeight: FontWeight.w700,
            fontSize: 12,
          ),
        ),
        const SizedBox(height: 4),
        const Text(
          '固定低速 18 cm/s，用于验证灰度方向和 PD 参数。连续丢线 250ms 自动停车；不等同于正式比赛任务。',
          style: TextStyle(
            color: CyberpunkTheme.dim,
            fontSize: 10,
            height: 1.35,
          ),
        ),
        const SizedBox(height: 10),
        Row(
          children: [
            Expanded(
              child: OutlinedButton.icon(
                onPressed: linkOk && !_calibrating ? _calibrateWhite : null,
                icon: Icon(
                  Icons.brightness_high_rounded,
                  size: 16,
                  color: _calibrating
                      ? CyberpunkTheme.dim
                      : CyberpunkTheme.amber,
                ),
                label: Text(_calibrating ? '标定中...' : '白底标定'),
                style: OutlinedButton.styleFrom(
                  foregroundColor: CyberpunkTheme.amber,
                  side: BorderSide(color: CyberpunkTheme.amber.withAlpha(160)),
                  padding: const EdgeInsets.symmetric(vertical: 12),
                ),
              ),
            ),
            const SizedBox(width: 9),
            Expanded(
              child: ElevatedButton.icon(
                onPressed: linkOk && !_calibrating ? _toggleLine : null,
                icon: Icon(
                  _running ? Icons.stop_rounded : Icons.play_arrow_rounded,
                  size: 18,
                ),
                label: Text(_running ? '停止循迹' : '开始循迹'),
                style: ElevatedButton.styleFrom(
                  backgroundColor: _running
                      ? CyberpunkTheme.red
                      : CyberpunkTheme.green,
                  foregroundColor: Colors.white,
                  padding: const EdgeInsets.symmetric(vertical: 12),
                ),
              ),
            ),
          ],
        ),
        const SizedBox(height: 8),
        Row(
          children: [
            Icon(Icons.sensors, size: 13, color: CyberpunkTheme.cyan),
            const SizedBox(width: 5),
            Expanded(
              child: Text(
                _status,
                style: const TextStyle(color: CyberpunkTheme.dim, fontSize: 10),
              ),
            ),
          ],
        ),
      ],
    ),
  );

  Widget _guidePanel() => Container(
    width: double.infinity,
    margin: const EdgeInsets.symmetric(vertical: 2),
    padding: const EdgeInsets.all(12),
    decoration: BoxDecoration(
      color: CyberpunkTheme.raisedSurface,
      borderRadius: BorderRadius.circular(CyberpunkTheme.radiusCard),
      border: Border.all(color: CyberpunkTheme.primary.withAlpha(120), width: 1.1),
      boxShadow: CyberpunkTheme.raisedShadows,
    ),
    child: const Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Text(
          '电赛循迹调试顺序',
          style: TextStyle(
            color: CyberpunkTheme.text,
            fontWeight: FontWeight.w700,
            fontSize: 11,
          ),
        ),
        SizedBox(height: 6),
        Text(
          '1. 车置于白底，执行白底标定；2. 手动横移车身，确认 S1~S8 的黑线响应连续、误差方向正确；3. 低速启动循迹，先调 P 到能回正，再增 D 抑制摆动；4. 速度环和循迹环分开调，高速与弧线参数在正式赛道任务中单独验证。',
          style: TextStyle(
            color: CyberpunkTheme.dim,
            fontSize: 10,
            height: 1.48,
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
