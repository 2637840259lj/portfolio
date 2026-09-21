import 'package:flutter/material.dart';
import 'package:provider/provider.dart';
import '../services/app_state.dart';
import '../services/telem_service.dart';
import '../theme.dart';
import '../widgets/press_scale.dart';

/// Tab3: IMU 联调台 — 实时姿态、漂移诊断、校准与航向归零。
class TelemPage extends StatefulWidget {
  const TelemPage({super.key});

  @override
  State<TelemPage> createState() => _TelemPageState();
}

class _TelemPageState extends State<TelemPage> {
  TelemService? _svc;
  bool _init = false;
  bool _calibrating = false;

  // static const _bg = Color(0xFF0F0F23);
  // static const _surface = Color(0xFF1A1A35);
  // static const _primary = Color(0xFF7C3AED);
  // static const _success = Color(0xFF10B981);
  // static const _warn = Color(0xFFF59E0B);
  // static const _danger = Color(0xFFEF4444);
  // static const _text = Color(0xFFE2E8F0);
  // static const _dim = Color(0xFF94A3B8);

  @override
  void didChangeDependencies() {
    super.didChangeDependencies();
    if (!_init) {
      _svc = TelemService(app: context.read<AppState>());
      _svc!.addListener(_refresh);
      _init = true;
    }
  }

  void _refresh() {
    if (mounted) setState(() {});
  }

  @override
  void dispose() {
    _svc?.removeListener(_refresh);
    // 页面销毁前必须停止 MCU 遥测，避免切换 Tab 后仍持续回传并堵塞链路。
    _svc?.stopAndCancel();
    _svc?.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final svc = _svc;
    final app = context.watch<AppState>();
    if (svc == null) return const SizedBox();
    final linkOk = app.bleConn.isConnected && app.handshake.linkAlive;
    final driftRate = svc.latestYawRate.abs();
    final stable = svc.active && driftRate < 0.8;

    return Container(
      color: CyberpunkTheme.background,
      child: Column(
        children: [
          _toolbar(svc, app, linkOk),
          _statusStrip(svc, linkOk, stable),
          Expanded(
            child: svc.timeSec.isEmpty
                ? _emptyState(linkOk)
                : SingleChildScrollView(
                    padding: const EdgeInsets.fromLTRB(10, 6, 10, 16),
                    child: Column(
                      children: [
                        _attitudeRow(svc, stable),
                        const SizedBox(height: 10),
                        _calibrationCard(svc),
                        const SizedBox(height: 10),
                        _chartCard(
                          'Yaw 漂移',
                          '角度 (°)',
                          svc.timeSec,
                          svc.yaw,
                          CyberpunkTheme.primary,
                        ),
                        const SizedBox(height: 10),
                        _chartCard(
                          'Yaw 角速度',
                          '角速度 (°/s)',
                          svc.timeSec,
                          svc.yawRate,
                          stable ? CyberpunkTheme.success : CyberpunkTheme.warn,
                        ),
                        const SizedBox(height: 10),
                        _guideCard(),
                      ],
                    ),
                  ),
          ),
        ],
      ),
    );
  }

  Widget _toolbar(TelemService svc, AppState app, bool linkOk) {
    return Container(
      margin: const EdgeInsets.fromLTRB(8, 8, 8, 4),
      padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 9),
      decoration: BoxDecoration(
        color: CyberpunkTheme.raisedSurface,
        borderRadius: BorderRadius.circular(CyberpunkTheme.radiusCard),
        border: Border.all(color: CyberpunkTheme.edgeHighlight),
        boxShadow: CyberpunkTheme.raisedShadows,
      ),
      child: Row(
        children: [
          Icon(
            Icons.sensors,
            size: 17,
            color: linkOk ? CyberpunkTheme.primary : CyberpunkTheme.dim,
          ),
          const SizedBox(width: 7),
          const Text(
            'IMU 联调',
            style: TextStyle(
              color: CyberpunkTheme.text,
              fontSize: 13,
              fontWeight: FontWeight.w700,
            ),
          ),
          const Spacer(),
          _smallAction(
            label: svc.active ? '停止监测' : '开始监测',
            icon: svc.active
                ? Icons.stop_circle_outlined
                : Icons.play_circle_outline,
            color: svc.active ? CyberpunkTheme.danger : CyberpunkTheme.primary,
            enabled: linkOk && !_calibrating,
            onTap: svc.active
                ? () async {
                    await svc.stop();
                  }
                : () async {
                    final started = await svc.start();
                    if (!started && mounted) {
                      ScaffoldMessenger.of(context).showSnackBar(
                        const SnackBar(
                          content: Text('初始校准或遥测启动未获 MCU 确认：请检查固件与蓝牙连接'),
                        ),
                      );
                    }
                  },
          ),
          const SizedBox(width: 6),
          _smallAction(
            label: '归零',
            icon: Icons.exposure_zero,
            color: CyberpunkTheme.success,
            enabled: linkOk && !_calibrating,
            onTap: () => _resetYaw(app),
          ),
          const SizedBox(width: 6),
          _smallAction(
            label: _calibrating ? '校准中' : '校准',
            icon: Icons.tune,
            color: CyberpunkTheme.warn,
            enabled: linkOk && !_calibrating,
            onTap: () => _calibrate(svc),
          ),
        ],
      ),
    );
  }

  Widget _statusStrip(TelemService svc, bool linkOk, bool stable) {
    final status = !linkOk
        ? '蓝牙未连接'
        : !svc.active
        ? '未开始监测'
        : stable
        ? '静止稳定'
        : '检测到角速度';
    final color = !linkOk
        ? CyberpunkTheme.danger
        : !svc.active
        ? CyberpunkTheme.dim
        : stable
        ? CyberpunkTheme.success
        : CyberpunkTheme.warn;
    return AnimatedContainer(
      duration: CyberpunkTheme.durFast,
      curve: CyberpunkTheme.easeOut,
      width: double.infinity,
      padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 6),
      decoration: BoxDecoration(
        border: Border(bottom: BorderSide(color: color.withAlpha(80))),
      ),
      child: Row(
        children: [
          AnimatedContainer(
            duration: CyberpunkTheme.durFast,
            curve: CyberpunkTheme.easeOut,
            width: 7,
            height: 7,
            decoration: BoxDecoration(color: color, shape: BoxShape.circle),
          ),
          const SizedBox(width: 7),
          AnimatedDefaultTextStyle(
            duration: CyberpunkTheme.durFast,
            curve: CyberpunkTheme.easeOut,
            style: TextStyle(
              color: color,
              fontSize: 11,
              fontWeight: FontWeight.w600,
            ),
            child: Text(status),
          ),
          const Spacer(),
          Text(
            '帧数 ${svc.frameCount}',
            style: const TextStyle(
              color: CyberpunkTheme.dim,
              fontSize: 10,
              fontFamily: 'monospace',
            ),
          ),
        ],
      ),
    );
  }

  Widget _emptyState(bool linkOk) => Center(
    child: TweenAnimationBuilder<double>(
      duration: CyberpunkTheme.durStandard,
      curve: CyberpunkTheme.easeOut,
      tween: Tween(begin: 0.0, end: 1.0),
      builder: (_, value, child) => Opacity(
        opacity: value,
        child: Transform.translate(
          offset: Offset(0, (1 - value) * 10),
          child: child,
        ),
      ),
      child: Column(
        mainAxisSize: MainAxisSize.min,
        children: [
          TweenAnimationBuilder<Color?>(
            duration: CyberpunkTheme.durFast,
            curve: CyberpunkTheme.easeOut,
            tween: ColorTween(
              end: linkOk ? CyberpunkTheme.primary : CyberpunkTheme.dim,
            ),
            builder: (_, color, __) => Icon(
              Icons.explore_rounded,
              size: 42,
              color: color,
            ),
          ),
          const SizedBox(height: 12),
          Text(
            linkOk ? '点击“开始监测”读取实时 IMU 数据' : '请先连接小车',
            style: const TextStyle(color: CyberpunkTheme.dim, fontSize: 12),
          ),
          const SizedBox(height: 6),
          const Text(
            '校准时请保持小车完全静止约 3 秒，期间会暂停心跳命令',
            style: TextStyle(color: CyberpunkTheme.lightGray, fontSize: 10),
          ),
        ],
      ),
    ),
  );

  Widget _attitudeRow(TelemService svc, bool stable) => Row(
    children: [
      Expanded(
        child: _valueCard(
          'Yaw',
          '${svc.latestYaw.toStringAsFixed(1)}°',
          CyberpunkTheme.primary,
        ),
      ),
      const SizedBox(width: 8),
      Expanded(
        child: _valueCard(
          '漂移速率',
          '${svc.latestYawRate.toStringAsFixed(2)}°/s',
          stable ? CyberpunkTheme.success : CyberpunkTheme.warn,
        ),
      ),
      const SizedBox(width: 8),
      Expanded(
        child: _valueCard(
          '横滚',
          '${svc.latestRoll.toStringAsFixed(1)}°',
          CyberpunkTheme.text,
        ),
      ),
      const SizedBox(width: 8),
      Expanded(
        child: _valueCard(
          '区间漂移',
          '${svc.driftDeg.toStringAsFixed(2)}°',
          svc.driftDeg.abs() < 1 ? CyberpunkTheme.success : CyberpunkTheme.warn,
        ),
      ),
    ],
  );

  Widget _valueCard(String label, String value, Color color) => AnimatedContainer(
    duration: CyberpunkTheme.durFast,
    curve: CyberpunkTheme.easeOut,
    padding: const EdgeInsets.symmetric(horizontal: 9, vertical: 10),
    decoration: BoxDecoration(
      color: CyberpunkTheme.insetDeep,
      borderRadius: BorderRadius.circular(CyberpunkTheme.radiusControl),
      border: Border.all(color: color.withAlpha(130), width: 1.1),
      boxShadow: CyberpunkTheme.insetShadows,
    ),
    child: Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Text(
          label,
          style: const TextStyle(color: CyberpunkTheme.dim, fontSize: 9),
        ),
        const SizedBox(height: 4),
        Text(
          value,
          overflow: TextOverflow.ellipsis,
          style: TextStyle(
            color: color,
            fontFamily: 'monospace',
            fontSize: 12,
            fontWeight: FontWeight.w700,
          ),
        ),
      ],
    ),
  );

  Widget _calibrationCard(TelemService svc) {
    final available = svc.hasGyroBiasZ;
    final statusColor =
        svc.calibrationStatus.contains('完成') ||
            svc.calibrationStatus.contains('收敛') ||
            svc.calibrationStatus.contains('已修正')
        ? CyberpunkTheme.success
        : svc.calibrationStatus.contains('超时') ||
              svc.calibrationStatus.contains('运动') ||
              svc.calibrationStatus.contains('波动')
        ? CyberpunkTheme.warn
        : CyberpunkTheme.dim;
    return Container(
      width: double.infinity,
      padding: const EdgeInsets.all(12),
      decoration: BoxDecoration(
        color: CyberpunkTheme.raisedSurface,
        borderRadius: BorderRadius.circular(CyberpunkTheme.radiusCard),
        border: Border.all(
          color: (available ? CyberpunkTheme.success : CyberpunkTheme.dim)
              .withAlpha(140),
          width: 1.1,
        ),
        boxShadow: CyberpunkTheme.raisedShadows,
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            children: [
              Icon(
                Icons.tune,
                size: 14,
                color: available ? CyberpunkTheme.success : CyberpunkTheme.dim,
              ),
              const SizedBox(width: 6),
              const Text(
                'IMU 角度校准',
                style: TextStyle(
                  color: CyberpunkTheme.text,
                  fontSize: 11,
                  fontWeight: FontWeight.w700,
                ),
              ),
              const Spacer(),
              Text(
                !available
                    ? '等待新固件数据'
                    : svc.initialCalibrationReady
                    ? '角度观测已就绪'
                    : '请先完成初始校准',
                style: TextStyle(
                  color: !available
                      ? CyberpunkTheme.warn
                      : svc.initialCalibrationReady
                      ? CyberpunkTheme.success
                      : CyberpunkTheme.warn,
                  fontSize: 9,
                  fontWeight: FontWeight.w600,
                ),
              ),
            ],
          ),
          const SizedBox(height: 8),
          Row(
            children: [
              Expanded(
                child: _calValue(
                  'MCU 零偏参考',
                  available ? '${svc.gyroBiasZ} LSB' : '--',
                  CyberpunkTheme.primary,
                ),
              ),
              const SizedBox(width: 8),
              Expanded(
                child: _calValue(
                  '等效零偏',
                  available
                      ? '${svc.gyroBiasZDps.toStringAsFixed(3)}°/s'
                      : '--',
                  CyberpunkTheme.primary,
                ),
              ),
              const SizedBox(width: 8),
              Expanded(
                child: _calValue(
                  '最近修正',
                  svc.lastBiasDelta == 0
                      ? '--'
                      : '${svc.lastBiasDelta >= 0 ? '+' : ''}${svc.lastBiasDelta} LSB',
                  CyberpunkTheme.success,
                ),
              ),
            ],
          ),
          const SizedBox(height: 8),
          Text(
            svc.calibrationStatus,
            style: TextStyle(
              color: statusColor,
              fontSize: 10,
              fontWeight: FontWeight.w600,
            ),
          ),
          const SizedBox(height: 4),
          const Text(
            '初始角度校准约需 3 秒：App 会暂停心跳，避免命令串扰；完成后仅显示 Yaw/角速度变化，不再自动下发 Z 轴修正命令。',
            style: TextStyle(
              color: CyberpunkTheme.dim,
              fontSize: 9,
              height: 1.35,
            ),
          ),
        ],
      ),
    );
  }

  Widget _calValue(String label, String value, Color color) => Column(
    crossAxisAlignment: CrossAxisAlignment.start,
    children: [
      Text(
        label,
        style: const TextStyle(color: CyberpunkTheme.dim, fontSize: 9),
      ),
      const SizedBox(height: 3),
      Text(
        value,
        overflow: TextOverflow.ellipsis,
        style: TextStyle(
          color: color,
          fontFamily: 'monospace',
          fontSize: 11,
          fontWeight: FontWeight.w700,
        ),
      ),
    ],
  );

  Widget _chartCard(
    String title,
    String unit,
    List<double> t,
    List<double> values,
    Color color,
  ) => Container(
    height: 150,
    padding: const EdgeInsets.fromLTRB(11, 9, 11, 11),
    decoration: BoxDecoration(
      color: CyberpunkTheme.raisedSurface,
      borderRadius: BorderRadius.circular(CyberpunkTheme.radiusCard),
      border: Border.all(color: CyberpunkTheme.edgeHighlight),
      boxShadow: CyberpunkTheme.raisedShadows,
    ),
    child: Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Row(
          children: [
            Text(
              title,
              style: const TextStyle(
                color: CyberpunkTheme.text,
                fontSize: 11,
                fontWeight: FontWeight.w600,
              ),
            ),
            const SizedBox(width: 6),
            Text(
              unit,
              style: const TextStyle(color: CyberpunkTheme.dim, fontSize: 9),
            ),
          ],
        ),
        const SizedBox(height: 6),
        Expanded(
          child: CustomPaint(
            size: Size.infinite,
            painter: _ImuChartPainter(t, values, color),
          ),
        ),
      ],
    ),
  );

  Widget _guideCard() => Container(
    width: double.infinity,
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
          '联调判定',
          style: TextStyle(
            color: CyberpunkTheme.text,
            fontSize: 11,
            fontWeight: FontWeight.w700,
          ),
        ),
        SizedBox(height: 5),
        Text(
          '1. 点击“开始监测”会先执行约 3 秒的初始静止角度校准。\n2. 观察 30 秒：角速度应接近 0°/s，Yaw 角度变化应平稳。\n3. 若角度漂移明显，重新点击“校准”，并确认小车未受震动、未在充电或电机运转。\n4. 当前联调只处理角度观测与归零，不自动调整 Z 轴零偏。',
          style: TextStyle(
            color: CyberpunkTheme.dim,
            fontSize: 10,
            height: 1.5,
          ),
        ),
      ],
    ),
  );

  Widget _smallAction({
    required String label,
    required IconData icon,
    required Color color,
    required bool enabled,
    required Future<void> Function() onTap,
  }) => PressScale(
    enabled: enabled,
    onTapAsync: onTap,
    child: AnimatedContainer(
      duration: CyberpunkTheme.durFast,
      curve: CyberpunkTheme.easeOut,
      padding: const EdgeInsets.symmetric(horizontal: 9, vertical: 7),
      decoration: BoxDecoration(
        gradient: enabled
            ? LinearGradient(
                begin: Alignment.topLeft,
                end: Alignment.bottomRight,
                colors: [color.withAlpha(75), color.withAlpha(35)],
              )
            : null,
        color: enabled ? null : CyberpunkTheme.dim.withAlpha(25),
        borderRadius: BorderRadius.circular(CyberpunkTheme.radiusControl),
        border: Border.all(
          color: enabled
              ? color.withAlpha(180)
              : CyberpunkTheme.dim.withAlpha(50),
          width: 1.1,
        ),
        boxShadow: enabled ? CyberpunkTheme.raisedShadows : null,
      ),
      child: Row(
        mainAxisSize: MainAxisSize.min,
        children: [
          Icon(icon, size: 13, color: enabled ? color : CyberpunkTheme.dim),
          const SizedBox(width: 4),
          Text(
            label,
            style: TextStyle(
              color: enabled ? color : CyberpunkTheme.dim,
              fontSize: 10,
              fontWeight: FontWeight.w600,
            ),
          ),
        ],
      ),
    ),
  );

  Future<void> _resetYaw(AppState app) async {
    await app.bleConn.sendAscii('YZR\n');
    app.addLog('IMU YAW ZERO');
  }

  Future<void> _calibrate(TelemService svc) async {
    setState(() => _calibrating = true);
    if (mounted) {
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(
          content: Text('正在校准：请保持小车静止约 2 秒'),
          duration: Duration(seconds: 3),
        ),
      );
    }
    final ok = await svc.calibrateInitial(force: true);
    if (mounted) {
      setState(() => _calibrating = false);
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(
          content: Text(ok ? '初始零偏校准完成，可开始监测' : '校准未获 MCU 确认，请检查烧录和蓝牙连接'),
        ),
      );
    }
  }
}

class _ImuChartPainter extends CustomPainter {
  final List<double> time;
  final List<double> values;
  final Color color;
  _ImuChartPainter(this.time, this.values, this.color);

  @override
  void paint(Canvas canvas, Size size) {
    final grid = Paint()
      ..color = CyberpunkTheme.lightGray.withAlpha(45)
      ..strokeWidth = 0.5;
    for (var i = 1; i < 4; i++) {
      final y = size.height * i / 4;
      canvas.drawLine(Offset(0, y), Offset(size.width, y), grid);
    }
    if (time.length < 2 || values.length < 2) return;
    final minT = time.first;
    final spanT = (time.last - minT).abs() < 0.001 ? 1.0 : time.last - minT;
    var peak = 0.5;
    for (final v in values) {
      if (v.abs() > peak) peak = v.abs();
    }
    final p = Paint()
      ..color = color
      ..strokeWidth = 1.8
      ..style = PaintingStyle.stroke;
    final path = Path();
    for (var i = 0; i < values.length; i++) {
      final x = (time[i] - minT) / spanT * size.width;
      final y = size.height / 2 - values[i] / peak * (size.height * 0.44);
      if (i == 0) {
        path.moveTo(x, y);
      } else {
        path.lineTo(x, y);
      }
    }
    canvas.drawPath(path, p);
    final zero = Paint()
      ..color = color.withAlpha(90)
      ..strokeWidth = 0.7;
    canvas.drawLine(
      Offset(0, size.height / 2),
      Offset(size.width, size.height / 2),
      zero,
    );
  }

  @override
  bool shouldRepaint(covariant _ImuChartPainter old) =>
      old.time.length != time.length || old.values.length != values.length;
}
