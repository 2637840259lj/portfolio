import 'dart:async';
import 'dart:typed_data';
import 'package:flutter/material.dart';
import 'package:provider/provider.dart';
import '../models/ble_packet.dart';
import '../models/telem_frame.dart';
import '../services/app_state.dart';
import '../services/log_file_service.dart';
import '../theme.dart';

/// H题单圈循迹实时调试页。
/// 此页只开启MCU遥测与I2C调试策略；正式起跑仍由实体KEY2触发，App绝不接管运动控制。
class H1LineDebugPage extends StatefulWidget {
  const H1LineDebugPage({super.key});

  @override
  State<H1LineDebugPage> createState() => _H1LineDebugPageState();
}

class _H1LineDebugPageState extends State<H1LineDebugPage> {
  static const int _maxSamples = 160;
  StreamSubscription<BlePacket>? _sub;
  bool _ready = false;
  bool _active = false;
  bool _busy = false;
  String _status = '未开启：先完成白底标定，再开启实时调试';
  TelemFrame? _latest;
  final List<_LineSample> _samples = [];
  final List<_RecordedTelem> _recorded = [];
  int _frameCount = 0;
  int _lostCount = 0;
  bool _recording = false;
  bool _lapStarted = false;
  bool _exporting = false;
  String _exportStatus = '尚未保存本轮数据';
  DateTime? _lastFrameAt;
  bool _calibrating = false;
  String _calibrationStatus = '可静止采集陀螺仪零偏或白底灰度；采样结果会立即应用到本次运行。';
  _CalibrationResult? _calibrationResult;

  @override
  void didChangeDependencies() {
    super.didChangeDependencies();
    if (_ready) return;
    _ready = true;
    _sub = context.read<AppState>().handshake.packetStream.listen(_onPacket);
  }

  @override
  void dispose() {
    _sub?.cancel();
    final app = context.read<AppState>();
    if (!app.isPageSwitching && app.bleConn.isConnected) {
      unawaited(_stopDebug(silent: true));
    }
    super.dispose();
  }

  void _onPacket(BlePacket packet) {
    if (packet.cmd == CmdCode.calResult) {
      _handleCalibrationResult(packet.payload);
      return;
    }
    if (packet.cmd != CmdCode.telemFrame) return;
    try {
      final frame = TelemFrame.parse(Uint8List.fromList(packet.payload));
      if (!mounted) return;
      setState(() {
        _latest = frame;
        _lastFrameAt = DateTime.now();
        _frameCount++;
        _samples.add(_LineSample.fromFrame(frame));
        if (_samples.length > _maxSamples) _samples.removeAt(0);
        if (_recording && frame.lineDebugValid) {
          if (!_lapStarted) {
            _lapStarted = true;
            _recorded.clear();
            _lostCount = 0;
            _exportStatus = '已检测到KEY2启动，正在记录单圈原始遥测…';
          }
          _recorded.add(_RecordedTelem.fromFrame(frame));
          if (frame.lineLost) _lostCount++;
        }
        if (_active && !frame.h1DebugMode) {
          _status = 'MCU未确认调试模式，请重新开启';
        }
      });
    } catch (_) {
      if (mounted) setState(() => _status = '收到无法解析的遥测帧，请更新MCU固件');
    }
  }

  bool get _linkOk {
    final app = context.read<AppState>();
    return app.bleConn.isConnected && app.handshake.linkAlive;
  }

  bool get _streamOnline {
    final last = _lastFrameAt;
    return _active && last != null && DateTime.now().difference(last) < const Duration(seconds: 2);
  }

  Future<bool> _waitAck(int expected) async {
    final app = context.read<AppState>();
    final completer = Completer<bool>();
    late StreamSubscription<BlePacket> sub;
    sub = app.handshake.packetStream.listen((packet) {
      if (packet.cmd == CmdCode.ack && packet.payload.isNotEmpty && packet.payload.first == expected && !completer.isCompleted) {
        completer.complete(true);
      }
      if (packet.cmd == CmdCode.nack && packet.payload.isNotEmpty && packet.payload.first == expected && !completer.isCompleted) {
        completer.complete(false);
      }
    });
    final timer = Timer(const Duration(seconds: 2), () {
      if (!completer.isCompleted) completer.complete(false);
    });
    final result = await completer.future;
    timer.cancel();
    await sub.cancel();
    return result;
  }

  Future<bool> _sendWithAck(int cmd, List<int> payload) async {
    final waiter = _waitAck(cmd);
    final sent = await context.read<AppState>().sendPacket(cmd, payload);
    if (!sent) return false;
    return waiter;
  }

  Future<void> _startDebug() async {
    if (!_linkOk || _busy || _active) return;
    final app = context.read<AppState>();
    if (!app.grayCalibrationValid) {
      setState(() => _status = '请先到“循迹”页完成白底标定，再开启单圈调试');
      return;
    }
    setState(() {
      _busy = true;
      _status = '正在切换I2C调试策略：暂停OLED、保持IMU更新…';
      _samples.clear();
      _recorded.clear();
      _latest = null;
      _frameCount = 0;
      _lostCount = 0;
      _recording = true;
      _lapStarted = false;
      _exportStatus = '正在等待KEY2启动单圈；静止数据不会写入CSV';
      _lastFrameAt = null;
    });
    try {
      final modeOk = await _sendWithAck(CmdCode.h1DebugMode, const [1]);
      if (!modeOk) {
        setState(() => _status = 'MCU未确认调试模式；请确认已烧录最新固件');
        return;
      }
      final telemOk = await _sendWithAck(CmdCode.telemStart, const [20, 0]);
      if (!telemOk) {
        await _sendWithAck(CmdCode.h1DebugMode, const [0]);
        setState(() => _status = '遥测未启动，请检查蓝牙链路');
        return;
      }
      if (mounted) {
        setState(() {
          _active = true;
          _status = '实时调试已开启：OLED暂停刷新；现在按车上的KEY2开始单圈';
        });
      }
    } finally {
      if (mounted) setState(() => _busy = false);
    }
  }

  Future<void> _stopDebug({bool silent = false}) async {
    if (_busy) return;
    if (mounted) setState(() => _busy = true);
    try {
      if (_linkOk) {
        await _sendWithAck(CmdCode.telemStop, const []);
        await _sendWithAck(CmdCode.h1DebugMode, const [0]);
      }
      if (mounted) {
        setState(() {
          _active = false;
          _recording = false;
          _lapStarted = false;
          if (!silent) _status = '已退出实时调试：OLED与正式比赛I2C策略已恢复';
        });
      }
    } finally {
      if (mounted) setState(() => _busy = false);
    }
  }

  Future<void> _safetyStop() async {
    if (!_linkOk) return;
    await context.read<AppState>().requestSafetyStop(reason: 'H题循迹调试页停止单圈');
    if (mounted) setState(() => _status = '已发送安全停止；请确认小车已静止');
  }

  Future<void> _startCalibration(int kind, int method) async {
    if (!_linkOk || _busy || _calibrating || _active) return;
    final label = kind == 1 ? '陀螺仪静止60秒采样' : '白底灰度20次采样';
    setState(() {
      _calibrating = true;
      _calibrationResult = null;
      _calibrationStatus = '$label 已开始：请保持小车完全静止，采样中不要切页或发送运动指令。';
    });
    final ok = await _sendWithAck(CmdCode.calCapture, [kind, method]);
    if (!mounted) return;
    if (!ok) {
      setState(() {
        _calibrating = false;
        _calibrationStatus = 'MCU拒绝启动采样：请确保未在运行、未开启实时调试且小车已静止。';
      });
    }
  }

  void _handleCalibrationResult(List<int> payload) {
    final result = _CalibrationResult.parse(payload);
    if (!mounted) return;
    setState(() {
      _calibrating = false;
      _calibrationResult = result;
      _calibrationStatus = result.ok
          ? '${result.label}完成：结果已应用到本次运行；请保存CSV，并将常量回填固件后重新烧录以永久固定。'
          : '${result.label}失败：采样有效性不足或I2C通信中断，请保持静止后重试。';
    });
  }

  Future<void> _exportCalibrationCsv() async {
    final result = _calibrationResult;
    if (result == null || _exporting) return;
    setState(() => _exporting = true);
    try {
      final stamp = DateTime.now().toIso8601String().replaceAll(':', '-').replaceAll('.', '-');
      final fileName = 'h1_calibration_$stamp.csv';
      await LogFileService.saveH1LineCsv(fileName: fileName, content: result.toCsv());
      if (mounted) setState(() => _calibrationStatus = '${result.label} CSV已保存：手机 Download/$fileName');
    } catch (e) {
      if (mounted) setState(() => _calibrationStatus = '校准CSV保存失败：$e');
    } finally {
      if (mounted) setState(() => _exporting = false);
    }
  }

  Future<void> _exportCsv() async {
    if (_exporting || _recorded.isEmpty) return;
    setState(() {
      _exporting = true;
      _recording = false;
      _exportStatus = '正在生成 CSV…';
    });
    try {
      final stamp = DateTime.now().toIso8601String().replaceAll(':', '-').replaceAll('.', '-');
      final fileName = 'h1_line_$stamp.csv';
      final content = StringBuffer(_RecordedTelem.csvHeader);
      for (final sample in _recorded) {
        content..write('\n')..write(sample.toCsv());
      }
      await LogFileService.saveH1LineCsv(
        fileName: fileName,
        content: content.toString(),
      );
      if (!mounted) return;
      setState(() => _exportStatus = '已保存 ${_recorded.length} 帧：手机 Download/$fileName');
    } catch (e) {
      if (mounted) setState(() => _exportStatus = '导出失败：$e');
    } finally {
      if (mounted) setState(() => _exporting = false);
    }
  }

  @override
  Widget build(BuildContext context) {
    final app = context.watch<AppState>();
    final linkOk = app.bleConn.isConnected && app.handshake.linkAlive;
    final latest = _latest;
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
                  _operationCard(linkOk),
                  const SizedBox(height: 10),
                  _calibrationCard(linkOk),
                  const SizedBox(height: 10),
                  _lapResultCard(latest),
                  const SizedBox(height: 10),
                  _grayCard(latest),
                  const SizedBox(height: 10),
                  _metricsCard(latest),
                  const SizedBox(height: 10),
                  _chartCard('循迹外环：误差 / 转向输出', _LineChartType.line, CyberpunkTheme.primary),
                  const SizedBox(height: 10),
                  _chartCard('速度内环：目标 / 实际速度', _LineChartType.speed, CyberpunkTheme.cyan),
                  const SizedBox(height: 10),
                  _diagnosisCard(latest),
                  const SizedBox(height: 10),
                  _dataExportCard(),
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

  Widget _header(bool linkOk) {
    final color = _active ? (_streamOnline ? CyberpunkTheme.green : CyberpunkTheme.amber) : (linkOk ? CyberpunkTheme.dim : CyberpunkTheme.red);
    final label = _active ? (_streamOnline ? '20 Hz 数据流' : '等待遥测') : (linkOk ? '未开启' : '未连接');
    return Container(
      margin: const EdgeInsets.fromLTRB(8, 8, 8, 4),
      padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 11),
      decoration: BoxDecoration(
        color: CyberpunkTheme.raisedSurface,
        borderRadius: BorderRadius.circular(CyberpunkTheme.radiusCard),
        border: Border.all(color: color.withAlpha(170)),
        boxShadow: CyberpunkTheme.raisedShadows,
      ),
      child: Row(children: [
        const Icon(Icons.route_rounded, color: CyberpunkTheme.primary, size: 19),
        const SizedBox(width: 8),
        const Expanded(child: Text('H题单圈循迹调试', style: TextStyle(color: CyberpunkTheme.text, fontWeight: FontWeight.w700, fontSize: 13))),
        Container(
          padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 3),
          decoration: BoxDecoration(color: color.withAlpha(28), borderRadius: BorderRadius.circular(20)),
          child: Text(label, style: TextStyle(color: color, fontWeight: FontWeight.w700, fontSize: 10)),
        ),
      ]),
    );
  }

  Widget _operationCard(bool linkOk) => _card(
    child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
      const Text('运行方式', style: TextStyle(color: CyberpunkTheme.text, fontWeight: FontWeight.w700, fontSize: 13)),
      const SizedBox(height: 5),
      const Text('本页只打开实时遥测与I2C调试策略，不会发出运动指令。开启后OLED停止I2C刷新，MPU6050持续更新；实体KEY2才会启动/停止单圈任务。', style: TextStyle(color: CyberpunkTheme.dim, fontSize: 10, height: 1.4)),
      const SizedBox(height: 11),
      Row(children: [
        Expanded(child: OutlinedButton.icon(
          onPressed: linkOk && !_busy && !_active ? _startDebug : null,
          icon: const Icon(Icons.sensors, size: 17),
          label: Text(_busy && !_active ? '正在开启…' : '开启实时调试'),
          style: OutlinedButton.styleFrom(foregroundColor: CyberpunkTheme.cyan, side: BorderSide(color: CyberpunkTheme.cyan.withAlpha(170)), padding: const EdgeInsets.symmetric(vertical: 12)),
        )),
        const SizedBox(width: 9),
        Expanded(child: ElevatedButton.icon(
          onPressed: _active && !_busy ? _stopDebug : null,
          icon: const Icon(Icons.stop_circle_outlined, size: 17),
          label: Text(_busy && _active ? '正在退出…' : '退出调试'),
          style: ElevatedButton.styleFrom(backgroundColor: CyberpunkTheme.darkGray, foregroundColor: CyberpunkTheme.text, padding: const EdgeInsets.symmetric(vertical: 12)),
        )),
      ]),
      const SizedBox(height: 8),
      SizedBox(width: double.infinity, child: OutlinedButton.icon(
        onPressed: _active && !_busy ? _safetyStop : null,
        icon: const Icon(Icons.warning_amber_rounded, size: 17),
        label: const Text('立即停止小车'),
        style: OutlinedButton.styleFrom(foregroundColor: CyberpunkTheme.red, side: BorderSide(color: CyberpunkTheme.red.withAlpha(170)), padding: const EdgeInsets.symmetric(vertical: 10)),
      )),
      const SizedBox(height: 8),
      Row(children: [
        const Icon(Icons.info_outline, color: CyberpunkTheme.amber, size: 14),
        const SizedBox(width: 5),
        Expanded(child: Text(_status, style: const TextStyle(color: CyberpunkTheme.dim, fontSize: 10))),
      ]),
    ]),
  );

  Widget _calibrationCard(bool linkOk) {
    final result = _calibrationResult;
    return _card(child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
      Row(children: [
        const Icon(Icons.tune_rounded, color: CyberpunkTheme.amber, size: 17),
        const SizedBox(width: 6),
        const Text('静止校准采样', style: TextStyle(color: CyberpunkTheme.text, fontWeight: FontWeight.w700, fontSize: 12)),
        const Spacer(),
        Text(_calibrating ? '采样中' : '静止可用', style: TextStyle(color: _calibrating ? CyberpunkTheme.amber : CyberpunkTheme.dim, fontWeight: FontWeight.w700, fontSize: 10)),
      ]),
      const SizedBox(height: 7),
      const Text('这两项不驱动车轮。采样完成后立即用于本次运行；保存CSV后将显示的常量回填到固件并重新烧录，才能免除后续重复标定。', style: TextStyle(color: CyberpunkTheme.dim, fontSize: 10, height: 1.4)),
      const SizedBox(height: 10),
      SizedBox(width: double.infinity, child: OutlinedButton.icon(
        onPressed: linkOk && !_busy && !_active && !_calibrating ? () => _startCalibration(1, 0) : null,
        icon: const Icon(Icons.sensors_rounded, size: 17),
        label: Text(_calibrating ? '正在采样…' : '静止 60 秒：陀螺仪零偏'),
        style: OutlinedButton.styleFrom(foregroundColor: CyberpunkTheme.cyan, side: BorderSide(color: CyberpunkTheme.cyan.withAlpha(160)), padding: const EdgeInsets.symmetric(vertical: 10)),
      )),
      const SizedBox(height: 8),
      Row(children: [
        Expanded(child: OutlinedButton(
          onPressed: linkOk && !_busy && !_active && !_calibrating ? () => _startCalibration(2, 0) : null,
          child: const Text('白底 20 次取平均'),
        )),
        const SizedBox(width: 8),
        Expanded(child: OutlinedButton(
          onPressed: linkOk && !_busy && !_active && !_calibrating ? () => _startCalibration(2, 1) : null,
          child: const Text('白底 20 次取最低'),
        )),
      ]),
      const SizedBox(height: 8),
      Text(_calibrationStatus, style: const TextStyle(color: CyberpunkTheme.dim, fontSize: 9, height: 1.35)),
      if (result != null) ...[
        const SizedBox(height: 9),
        Container(
          width: double.infinity,
          padding: const EdgeInsets.all(9),
          decoration: BoxDecoration(color: CyberpunkTheme.insetDeep, borderRadius: BorderRadius.circular(8), border: Border.all(color: (result.ok ? CyberpunkTheme.green : CyberpunkTheme.red).withAlpha(120))),
          child: SelectableText(result.summary, style: TextStyle(color: result.ok ? CyberpunkTheme.green : CyberpunkTheme.red, fontFamily: 'monospace', fontSize: 10, height: 1.35)),
        ),
        const SizedBox(height: 8),
        SizedBox(width: double.infinity, child: ElevatedButton.icon(
          onPressed: result.ok && !_exporting ? _exportCalibrationCsv : null,
          icon: Icon(_exporting ? Icons.hourglass_top_rounded : Icons.save_alt_rounded, size: 17),
          label: Text(_exporting ? '正在保存…' : '保存校准 CSV 到 Download'),
          style: ElevatedButton.styleFrom(backgroundColor: CyberpunkTheme.green, foregroundColor: CyberpunkTheme.background, padding: const EdgeInsets.symmetric(vertical: 10)),
        )),
      ],
    ]));
  }

  static const double _pulsesPerCm = 71.3;

  String _runStateLabel(int state) {
    switch (state) {
      case 1: return '正在运行';
      case 2: return 'A点确认完成';
      case 3: return '超时/失败';
      case 4: return '手动停止';
      case 5: return '任务失败';
      default: return '等待 KEY2';
    }
  }

  Color _runStateColor(int state) {
    switch (state) {
      case 1: return CyberpunkTheme.cyan;
      case 2: return CyberpunkTheme.green;
      case 3:
      case 5: return CyberpunkTheme.red;
      case 4: return CyberpunkTheme.amber;
      default: return CyberpunkTheme.dim;
    }
  }

  Widget _lapResultCard(TelemFrame? f) {
    final supported = f?.hasH1Result ?? false;
    final state = f?.h1RunState ?? 0;
    final color = supported ? _runStateColor(state) : CyberpunkTheme.amber;
    final leftCm = (f?.h1LapLeftPulses ?? 0) / _pulsesPerCm;
    final rightCm = (f?.h1LapRightPulses ?? 0) / _pulsesPerCm;
    final avgCm = (leftCm + rightCm) / 2.0;
    final timeSec = (f?.h1ElapsedMs ?? 0) / 1000.0;
    final stateText = supported ? _runStateLabel(state) : '固件未提供本圈结果';
    final hint = !supported
        ? '当前收到的是旧版遥测帧（少于67字节），本卡片不能显示有效结果；请烧录本次MCU固件后再运行。'
        : state == 2
        ? '已命中A点横线；测量车体基准点相对停车线的偏差。'
        : state == 3
        ? '未在限时内完成；检查最后弧线出弧姿态和A点横线覆盖。'
        : state == 4
        ? '本圈由人工中止，不作为比赛成绩。'
        : state == 1
        ? '实时更新中：实体KEY2再次短按可安全停止。'
        : '开启实时调试后，按车上KEY2开始单圈。';
    return _card(child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
      Row(children: [
        const Icon(Icons.flag_rounded, color: CyberpunkTheme.primary, size: 17),
        const SizedBox(width: 6),
        const Text('本圈结果', style: TextStyle(color: CyberpunkTheme.text, fontWeight: FontWeight.w700, fontSize: 12)),
        const Spacer(),
        Container(
          padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 3),
          decoration: BoxDecoration(color: color.withAlpha(28), borderRadius: BorderRadius.circular(20)),
          child: Text(stateText, style: TextStyle(color: color, fontWeight: FontWeight.w700, fontSize: 10)),
        ),
      ]),
      const SizedBox(height: 10),
      Wrap(spacing: 8, runSpacing: 8, children: [
        _metric('用时', timeSec, 's', color),
        _metric('左轮行程', leftCm, 'cm', CyberpunkTheme.lightBlue),
        _metric('右轮行程', rightCm, 'cm', CyberpunkTheme.lightBlue),
        _metric('平均行程', avgCm, 'cm', CyberpunkTheme.green),
        _metric('A点最大黑路', f?.h1MaxBlackBits ?? 0, '/ 8', CyberpunkTheme.amber),
      ]),
      const SizedBox(height: 8),
      Text(hint, style: const TextStyle(color: CyberpunkTheme.dim, fontSize: 10, height: 1.35)),
    ]));
  }

  Widget _grayCard(TelemFrame? frame) => _card(
    child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
      Row(children: [
        const Text('8路灰度与横线状态', style: TextStyle(color: CyberpunkTheme.text, fontWeight: FontWeight.w700, fontSize: 12)),
        const Spacer(),
        Text('0x${(frame?.gray ?? 0).toRadixString(16).toUpperCase().padLeft(2, '0')}', style: const TextStyle(color: CyberpunkTheme.cyan, fontWeight: FontWeight.w700, fontFamily: 'monospace')),
      ]),
      const SizedBox(height: 10),
      Row(children: List.generate(8, (displayIndex) {
        final sensor = 7 - displayIndex;
        final onBlack = (((frame?.gray ?? 0) >> sensor) & 1) != 0;
        final color = onBlack ? (displayIndex == 3 || displayIndex == 4 ? CyberpunkTheme.green : CyberpunkTheme.amber) : CyberpunkTheme.darkGray;
        return Expanded(child: Container(
          height: 48,
          margin: EdgeInsets.only(right: displayIndex == 7 ? 0 : 5),
          decoration: BoxDecoration(color: onBlack ? color.withAlpha(140) : CyberpunkTheme.insetDeep, borderRadius: BorderRadius.circular(8), border: Border.all(color: onBlack ? color : CyberpunkTheme.darkBorder)),
          child: Column(mainAxisAlignment: MainAxisAlignment.center, children: [
            Text('S${sensor + 1}', style: const TextStyle(color: CyberpunkTheme.dim, fontSize: 9)),
            const SizedBox(height: 3),
            Text(onBlack ? '黑' : '白', style: TextStyle(color: onBlack ? color : CyberpunkTheme.dim, fontSize: 10, fontWeight: FontWeight.w700)),
          ]),
        ));
      })),
      const SizedBox(height: 8),
      Text(frame?.lineLost == true ? '丢线：当前8路均未检到黑线' : (frame?.lineDebugValid == true ? 'H1外环有效：正在执行比赛循迹原语' : '尚未按KEY2启动单圈，或任务已结束'), style: TextStyle(color: frame?.lineLost == true ? CyberpunkTheme.red : CyberpunkTheme.dim, fontSize: 10)),
    ]),
  );

  Widget _metricsCard(TelemFrame? f) => _card(
    child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
      const Text('实时控制量', style: TextStyle(color: CyberpunkTheme.text, fontWeight: FontWeight.w700, fontSize: 12)),
      const SizedBox(height: 10),
      Wrap(spacing: 8, runSpacing: 8, children: [
        _metric('循迹误差', f?.lineError ?? 0, '', CyberpunkTheme.primary),
        _metric('转向输出', f?.lineSteer ?? 0, '', CyberpunkTheme.amber),
        _metric('段速度', f?.lineBaseSpeed ?? 0, 'cm/s', CyberpunkTheme.green),
        _metric('Yaw', f?.yawDeg ?? 0, '°', CyberpunkTheme.cyan),
        _metric('左轮 实/目', '${(f?.speedL ?? 0).toStringAsFixed(1)}/${(f?.targetLeft ?? 0).toStringAsFixed(1)}', 'cm/s', CyberpunkTheme.lightBlue),
        _metric('右轮 实/目', '${(f?.speedR ?? 0).toStringAsFixed(1)}/${(f?.targetRight ?? 0).toStringAsFixed(1)}', 'cm/s', CyberpunkTheme.lightBlue),
      ]),
    ]),
  );

  Widget _metric(String label, dynamic value, String unit, Color color) => SizedBox(
    width: 146,
    child: Container(
      padding: const EdgeInsets.all(9),
      decoration: BoxDecoration(color: CyberpunkTheme.insetDeep, borderRadius: BorderRadius.circular(10), border: Border.all(color: color.withAlpha(90))),
      child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
        Text(label, style: const TextStyle(color: CyberpunkTheme.dim, fontSize: 9)),
        const SizedBox(height: 3),
        Text(value is double ? value.toStringAsFixed(1) : value.toString(), style: TextStyle(color: color, fontWeight: FontWeight.w700, fontFamily: 'monospace', fontSize: 15)),
        if (unit.isNotEmpty) Text(unit, style: const TextStyle(color: CyberpunkTheme.dim, fontSize: 8)),
      ]),
    ),
  );

  Widget _chartCard(String title, _LineChartType type, Color color) => _card(
    child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
      Text(title, style: const TextStyle(color: CyberpunkTheme.text, fontWeight: FontWeight.w700, fontSize: 12)),
      const SizedBox(height: 4),
      Text(type == _LineChartType.line ? '紫：循迹误差  黄：转向输出' : '青/蓝：左/右实际速度  虚线：左右目标速度', style: const TextStyle(color: CyberpunkTheme.dim, fontSize: 9)),
      const SizedBox(height: 8),
      SizedBox(height: 142, width: double.infinity, child: CustomPaint(painter: _LineChartPainter(samples: _samples, type: type, accent: color))),
    ]),
  );

  Widget _diagnosisCard(TelemFrame? f) {
    final text = _diagnosis(f);
    final color = f?.lineLost == true ? CyberpunkTheme.red : (f?.lineDebugValid == true ? CyberpunkTheme.amber : CyberpunkTheme.dim);
    return _card(child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
      Row(children: [const Icon(Icons.analytics_outlined, color: CyberpunkTheme.primary, size: 17), const SizedBox(width: 6), const Text('本轮诊断提示', style: TextStyle(color: CyberpunkTheme.text, fontWeight: FontWeight.w700, fontSize: 12))]),
      const SizedBox(height: 7),
      Text(text, style: TextStyle(color: color, fontSize: 11, height: 1.45)),
      const SizedBox(height: 7),
      Text('采样 $_frameCount 帧；丢线帧 $_lostCount。PID必须在小车静止时通过“PID测评”页修改并提交，正在运行时本页只读，避免改变控制对象。', style: const TextStyle(color: CyberpunkTheme.dim, fontSize: 9, height: 1.35)),
    ]));
  }

  String _diagnosis(TelemFrame? f) {
    if (!_active) return '开启后先观察静止时8路灰度与Yaw；确认正常后，再按实体KEY2跑单圈。';
    if (!_streamOnline) return '未收到连续遥测。请检查蓝牙连接、固件版本，或重新点击“开启实时调试”。';
    if (f == null || !f.lineDebugValid) return '遥测已正常，但当前未执行H题循迹外环；请按实体KEY2启动单圈，或确认任务尚未结束。';
    if (f.lineLost) return '出现丢线。先检查传感器高度、白底标定和赛道反光；不要仅靠提高P强行拉回。';
    if (f.lineError.abs() > 150 && f.lineSteer.abs() > 0.8 * 30) return '误差大且转向输出接近饱和：先降低段速度或提高最大转向限幅，再检查P是否不足。';
    if (_samples.length >= 20 && _oscillationScore() > 0.55) return '误差正在频繁左右反转：优先降低循迹P或提高循迹D，保持一次只改一小步。';
    if ((f.speedErrorLeft.abs() + f.speedErrorRight.abs()) > 8) return '左右速度内环跟踪误差较大：先在PID测评页整定速度环，再调循迹P/D。';
    return '当前误差和转向输出平稳。继续跑完整圈，重点记录两段半圆弧中误差峰值、速度掉速和Yaw漂移。';
  }

  double _oscillationScore() {
    if (_samples.length < 8) return 0;
    var turns = 0;
    var previous = 0.0;
    for (final sample in _samples.skip(_samples.length > 50 ? _samples.length - 50 : 0)) {
      if (sample.lineError.abs() < 15) continue;
      if (previous != 0 && sample.lineError.sign != previous.sign) turns++;
      previous = sample.lineError;
    }
    return turns / 12.0;
  }

  Widget _dataExportCard() => _card(
    child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
      Row(children: [
        const Icon(Icons.save_alt_rounded, color: CyberpunkTheme.green, size: 17),
        const SizedBox(width: 6),
        const Text('本轮数据保存', style: TextStyle(color: CyberpunkTheme.text, fontWeight: FontWeight.w700, fontSize: 12)),
        const Spacer(),
        Container(
          padding: const EdgeInsets.symmetric(horizontal: 7, vertical: 3),
          decoration: BoxDecoration(color: (_recording ? CyberpunkTheme.green : CyberpunkTheme.dim).withAlpha(25), borderRadius: BorderRadius.circular(20)),
          child: Text(_recording ? '正在记录' : '未记录', style: TextStyle(color: _recording ? CyberpunkTheme.green : CyberpunkTheme.dim, fontSize: 9, fontWeight: FontWeight.w700)),
        ),
      ]),
      const SizedBox(height: 7),
      Text('已缓存 ${_recorded.length} 帧原始遥测；包含时间、灰度、循迹误差/转向、左右轮目标/实际、速度环、Yaw/角速度、编码器及状态标志。保存后直接在手机 Download 根目录取文件。', style: const TextStyle(color: CyberpunkTheme.dim, fontSize: 10, height: 1.42)),
      const SizedBox(height: 10),
      SizedBox(width: double.infinity, child: ElevatedButton.icon(
        onPressed: _recorded.isNotEmpty && !_exporting ? _exportCsv : null,
        icon: Icon(_exporting ? Icons.hourglass_top_rounded : Icons.ios_share_rounded, size: 17),
        label: Text(_exporting ? '正在保存…' : '保存本轮 CSV 到 Download'),
        style: ElevatedButton.styleFrom(backgroundColor: CyberpunkTheme.green, foregroundColor: CyberpunkTheme.background, padding: const EdgeInsets.symmetric(vertical: 11)),
      )),
      const SizedBox(height: 7),
      Text(_exportStatus, style: const TextStyle(color: CyberpunkTheme.dim, fontSize: 9, height: 1.35)),
    ]),
  );

  Widget _guideCard() => _card(child: const Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
    Text('正确调参顺序', style: TextStyle(color: CyberpunkTheme.text, fontWeight: FontWeight.w700, fontSize: 12)),
    SizedBox(height: 6),
    Text('1. 循迹页白底标定；2. 本页开启实时调试；3. 按车上KEY2跑单圈；4. 先用速度曲线处理左右轮跟踪误差；5. 再用误差/转向曲线调整循迹P、D和最大转向；6. 停车后到“PID测评”页提交参数，再复跑验证。', style: TextStyle(color: CyberpunkTheme.dim, fontSize: 10, height: 1.5)),
  ]));

  Widget _card({required Widget child}) => Container(
    width: double.infinity,
    padding: const EdgeInsets.all(13),
    decoration: BoxDecoration(color: CyberpunkTheme.raisedSurface, borderRadius: BorderRadius.circular(CyberpunkTheme.radiusCard), border: Border.all(color: CyberpunkTheme.edgeHighlight), boxShadow: CyberpunkTheme.raisedShadows),
    child: child,
  );
}

enum _LineChartType { line, speed }

class _LineSample {
  final double lineError;
  final double lineSteer;
  final double speedL;
  final double speedR;
  final double targetL;
  final double targetR;
  _LineSample({required this.lineError, required this.lineSteer, required this.speedL, required this.speedR, required this.targetL, required this.targetR});
  factory _LineSample.fromFrame(TelemFrame f) => _LineSample(lineError: f.lineError, lineSteer: f.lineSteer, speedL: f.speedL, speedR: f.speedR, targetL: f.targetLeft, targetR: f.targetRight);
}

class _RecordedTelem {
  final int tMs;
  final int encL;
  final int encR;
  final int gray;
  final double lineError;
  final double lineSteer;
  final double lineBaseSpeed;
  final double targetLeft;
  final double targetRight;
  final double speedL;
  final double speedR;
  final double speedErrorLeft;
  final double speedErrorRight;
  final int pidOutputLeft;
  final int pidOutputRight;
  final double yawDeg;
  final double yawRateDps;
  final double gxDps;
  final double gyDps;
  final double rollDeg;
  final bool lineLost;
  final bool lineDebugValid;
  final bool h1DebugMode;
  final bool hasH1Result;
  final int h1RunState;
  final int h1ElapsedMs;
  final int h1LapLeftPulses;
  final int h1LapRightPulses;
  final int h1MaxBlackBits;

  const _RecordedTelem({
    required this.tMs, required this.encL, required this.encR, required this.gray,
    required this.lineError, required this.lineSteer, required this.lineBaseSpeed,
    required this.targetLeft, required this.targetRight, required this.speedL, required this.speedR,
    required this.speedErrorLeft, required this.speedErrorRight, required this.pidOutputLeft,
    required this.pidOutputRight, required this.yawDeg, required this.yawRateDps,
    required this.gxDps, required this.gyDps, required this.rollDeg, required this.lineLost,
    required this.lineDebugValid, required this.h1DebugMode, required this.hasH1Result,
    required this.h1RunState, required this.h1ElapsedMs, required this.h1LapLeftPulses,
    required this.h1LapRightPulses, required this.h1MaxBlackBits,
  });

  factory _RecordedTelem.fromFrame(TelemFrame f) => _RecordedTelem(
    tMs: f.tMs, encL: f.encL, encR: f.encR, gray: f.gray,
    lineError: f.lineError, lineSteer: f.lineSteer, lineBaseSpeed: f.lineBaseSpeed,
    targetLeft: f.targetLeft, targetRight: f.targetRight, speedL: f.speedL, speedR: f.speedR,
    speedErrorLeft: f.speedErrorLeft, speedErrorRight: f.speedErrorRight,
    pidOutputLeft: f.pidOutputLeft, pidOutputRight: f.pidOutputRight, yawDeg: f.yawDeg,
    yawRateDps: f.yawRateDps, gxDps: f.gxDps, gyDps: f.gyDps, rollDeg: f.rollDeg,
    lineLost: f.lineLost, lineDebugValid: f.lineDebugValid, h1DebugMode: f.h1DebugMode,
    hasH1Result: f.hasH1Result, h1RunState: f.h1RunState, h1ElapsedMs: f.h1ElapsedMs,
    h1LapLeftPulses: f.h1LapLeftPulses, h1LapRightPulses: f.h1LapRightPulses,
    h1MaxBlackBits: f.h1MaxBlackBits,
  );

  static const csvHeader = 't_ms,enc_left,enc_right,gray_hex,line_error,line_steer,line_base_speed_cm_s,target_left_cm_s,target_right_cm_s,speed_left_cm_s,speed_right_cm_s,speed_error_left_cm_s,speed_error_right_cm_s,pid_output_left,pid_output_right,yaw_deg,yaw_rate_dps,gx_dps,gy_dps,roll_deg,line_lost,line_debug_valid,h1_debug_mode,h1_result_supported,h1_run_state,h1_elapsed_ms,h1_lap_left_pulses,h1_lap_right_pulses,h1_max_black_bits';

  String toCsv() => [
    tMs, encL, encR, '0x${gray.toRadixString(16).toUpperCase().padLeft(2, '0')}',
    _f(lineError), _f(lineSteer), _f(lineBaseSpeed), _f(targetLeft), _f(targetRight),
    _f(speedL), _f(speedR), _f(speedErrorLeft), _f(speedErrorRight), pidOutputLeft, pidOutputRight,
    _f(yawDeg), _f(yawRateDps), _f(gxDps), _f(gyDps), _f(rollDeg),
    lineLost ? 1 : 0, lineDebugValid ? 1 : 0, h1DebugMode ? 1 : 0,
    hasH1Result ? 1 : 0, h1RunState, h1ElapsedMs, h1LapLeftPulses, h1LapRightPulses,
    h1MaxBlackBits,
  ].join(',');

  static String _f(double value) => value.toStringAsFixed(3);
}

class _CalibrationResult {
  final int kind;
  final bool ok;
  final int method;
  final int samples;
  final List<int> values;

  const _CalibrationResult({required this.kind, required this.ok, required this.method, required this.samples, required this.values});

  String get label => kind == 1 ? '陀螺仪60秒静止校准' : '白底灰度20次采样';

  String get summary {
    if (!ok) return '$label\n结果：失败，请检查静止状态和I2C通信。';
    if (kind == 1) {
      return '$label\n有效采样：$samples\n建议写入 MPU6050 s_gyroBias[3]：{${values.join(', ')}}';
    }
    final source = method == 0 ? '平均值' : '最低值';
    return '$label（$source）\n有效采样：$samples\n建议写入 empty.c white[8]：{${values.join(', ')}}';
  }

  String toCsv() {
    final out = StringBuffer('kind,status,method,samples,axis_or_sensor,value\n');
    if (kind == 1) {
      const axes = ['gyro_bias_x_lsb', 'gyro_bias_y_lsb', 'gyro_bias_z_lsb'];
      for (var i = 0; i < values.length && i < axes.length; i++) {
        out.writeln('imu60s,${ok ? 'ok' : 'failed'},raw_mean,$samples,${axes[i]},${values[i]}');
      }
    } else {
      for (var i = 0; i < values.length; i++) {
        out.writeln('gray20,${ok ? 'ok' : 'failed'},${method == 0 ? 'mean' : 'minimum'},$samples,S${i + 1},${values[i]}');
      }
    }
    return out.toString();
  }

  static _CalibrationResult parse(List<int> data) {
    if (data.length < 4) return const _CalibrationResult(kind: 0, ok: false, method: 0, samples: 0, values: []);
    final kind = data[0];
    final ok = data[1] == 1;
    final method = data[2];
    if (kind == 1 && data.length >= 17) {
      final samples = _i16(data, 15);
      return _CalibrationResult(kind: kind, ok: ok, method: method, samples: samples, values: [_i32(data, 3), _i32(data, 7), _i32(data, 11)]);
    }
    if (kind == 2 && data.length >= 20) {
      return _CalibrationResult(kind: kind, ok: ok, method: method, samples: data[3], values: List.generate(8, (i) => _i16(data, 4 + i * 2)));
    }
    return _CalibrationResult(kind: kind, ok: false, method: method, samples: 0, values: const []);
  }

  static int _i16(List<int> d, int o) => (d[o] | (d[o + 1] << 8)).toSigned(16);
  static int _i32(List<int> d, int o) => (d[o] | (d[o + 1] << 8) | (d[o + 2] << 16) | (d[o + 3] << 24)).toSigned(32);
}

class _LineChartPainter extends CustomPainter {
  final List<_LineSample> samples;
  final _LineChartType type;
  final Color accent;
  _LineChartPainter({required this.samples, required this.type, required this.accent});

  @override
  void paint(Canvas canvas, Size size) {
    final rect = Rect.fromLTWH(0, 0, size.width, size.height);
    final bg = Paint()..color = CyberpunkTheme.insetDeep;
    canvas.drawRRect(RRect.fromRectAndRadius(rect, const Radius.circular(10)), bg);
    final grid = Paint()..color = CyberpunkTheme.darkBorder.withAlpha(130)..strokeWidth = 1;
    for (var i = 1; i < 4; i++) canvas.drawLine(Offset(0, size.height * i / 4), Offset(size.width, size.height * i / 4), grid);
    canvas.drawLine(Offset(0, size.height / 2), Offset(size.width, size.height / 2), grid);
    if (samples.length < 2) {
      final p = TextPainter(text: const TextSpan(text: '等待单圈数据…', style: TextStyle(color: CyberpunkTheme.dim, fontSize: 11)), textDirection: TextDirection.ltr)..layout();
      p.paint(canvas, Offset((size.width - p.width) / 2, (size.height - p.height) / 2));
      return;
    }
    final series = type == _LineChartType.line
        ? <_Series>[ _Series((s) => s.lineError, CyberpunkTheme.primary, false), _Series((s) => s.lineSteer, CyberpunkTheme.amber, false) ]
        : <_Series>[ _Series((s) => s.speedL, CyberpunkTheme.cyan, false), _Series((s) => s.speedR, CyberpunkTheme.lightBlue, false), _Series((s) => s.targetL, CyberpunkTheme.cyan, true), _Series((s) => s.targetR, CyberpunkTheme.lightBlue, true) ];
    var maxAbs = 1.0;
    for (final item in samples) {
      for (final one in series) {
        final v = one.read(item).abs();
        if (v > maxAbs) maxAbs = v;
      }
    }
    maxAbs *= 1.12;
    for (final one in series) {
      final path = Path();
      for (var i = 0; i < samples.length; i++) {
        final x = i * size.width / (samples.length - 1);
        final y = size.height / 2 - one.read(samples[i]) / maxAbs * (size.height * 0.43);
        if (i == 0) path.moveTo(x, y); else path.lineTo(x, y);
      }
      final paint = Paint()..color = one.color..style = PaintingStyle.stroke..strokeWidth = one.dashed ? 1.2 : 2.0;
      if (!one.dashed) {
        canvas.drawPath(path, paint);
      } else {
        _drawDashed(canvas, path, paint);
      }
    }
  }

  void _drawDashed(Canvas canvas, Path path, Paint paint) {
    for (final metric in path.computeMetrics()) {
      var distance = 0.0;
      while (distance < metric.length) {
        final next = (distance + 5).clamp(0.0, metric.length).toDouble();
        canvas.drawPath(metric.extractPath(distance, next), paint);
        distance += 9;
      }
    }
  }

  @override
  bool shouldRepaint(covariant _LineChartPainter oldDelegate) => true;
}

class _Series {
  final double Function(_LineSample) read;
  final Color color;
  final bool dashed;
  _Series(this.read, this.color, this.dashed);
}
