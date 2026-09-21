import 'dart:async';
import 'dart:typed_data';
import 'package:flutter/foundation.dart';
import '../models/bench_result.dart';
import '../models/bench_session.dart';
import '../models/ble_packet.dart';
import '../models/pid_schedule_node.dart';
import 'app_state.dart';
import 'log_file_service.dart';

/// App 侧慢速监督调参服务。
/// MCU 始终在 10ms 本地闭环中执行；App 只管理独立测试会话、汇总样本和原子提交最终 PID。
class PidScheduleService extends ChangeNotifier {
  PidScheduleService({required this.app});

  final AppState app;
  final List<PidScheduleNode> _nodes = [];
  // 从 MCU 读取的 active 快照仅供只读测评使用，绝不覆盖调参候选 `_nodes`。
  final List<PidScheduleNode> _assessmentNodes = [];
  // 调参会话与只读测评会话必须物理隔离：测评不产生候选 PID，也不会写 MCU。
  final Map<String, BenchSession> _sessions = {};
  final Map<String, BenchSession> _assessmentSessions = {};
  bool _busy = false;
  // 仅用于防止测评页的“停止”误中止调参或速度整定流程。
  bool _assessmentRunActive = false;
  String _status = '未加载速度自适应 PID 曲线';
  BenchResult? _lastBenchResult;
  bool _lastResultRecorded = false;
  BenchResult? _lastAssessmentResult;
  bool _lastAssessmentResultRecorded = false;
  Completer<BenchResult?>? _activeBenchCompleter;

  List<PidScheduleNode> get nodes => List.unmodifiable(_nodes);
  /// 本次从 MCU active 表读回的只读快照；测评页面只能使用它。
  List<PidScheduleNode> get assessmentNodes => List.unmodifiable(_assessmentNodes);
  bool get busy => _busy;
  bool get assessmentBusy => _busy && _assessmentRunActive;
  String get status => _status;
  BenchResult? get lastBenchResult => _lastBenchResult;
  bool get _linkOk => app.bleConn.isConnected && app.handshake.linkAlive;

  BenchSession? sessionFor(BenchMode mode, int index) => _sessions[_sessionKey(mode, index)];
  int sessionSamples(BenchMode mode, int index) => sessionFor(mode, index)?.samples.length ?? 0;
  bool sessionReady(BenchMode mode, int index) => sessionFor(mode, index)?.isReady ?? false;

  /// 独立只读测评会话：仅用于评分当前 MCU PID，不会进入调参会话或参数提交路径。
  BenchSession? assessmentSessionFor(BenchMode mode, int index) => _assessmentSessions[_sessionKey(mode, index)];
  int assessmentSamples(BenchMode mode, int index) => assessmentSessionFor(mode, index)?.samples.length ?? 0;
  BenchResult? get lastAssessmentResult => _lastAssessmentResult;

  /// 仅当前展示的结果可被保存一次；用于让页面明确显示“已保存”而非重复计数。
  bool isBenchResultRecorded(BenchResult result) => _lastResultRecorded && identical(_lastBenchResult, result);
  bool isAssessmentResultRecorded(BenchResult result) => _lastAssessmentResultRecorded && identical(_lastAssessmentResult, result);

  /// App 侧门禁只确认本次连接已收到校准 ACK；MCU 仍保留硬件/静止状态的最终拒绝权。
  String? calibrationBlockReason(BenchMode mode) {
    if ((mode == BenchMode.lineStraight || mode == BenchMode.lineCurve) && !app.grayCalibrationValid) {
      return '请先在循迹页完成白底灰度标定';
    }
    if ((mode == BenchMode.yaw90 || mode == BenchMode.distance1m) && !app.imuCalibrationValid) {
      return '请先在陀螺仪页完成静止校准';
    }
    return null;
  }

  bool canStartBench(BenchMode mode) => calibrationBlockReason(mode) == null;

  Future<bool> load() async {
    if (!_linkOk || _busy) return false;
    _busy = true;
    _status = '正在读取 MCU 参数曲线…';
    notifyListeners();
    try {
      final loaded = <PidScheduleNode>[];
      for (var i = 0; i < 3; i++) {
        final node = await _readNode(i);
        if (node == null) {
          _status = '读取节点 $i 失败';
          return false;
        }
        loaded.add(node);
      }
      _nodes
        ..clear()
        ..addAll(loaded);
      _status = '已加载 ${loaded.length} 个速度节点；每个闭环会话至少采集 3 次后才会改 PID';
      return true;
    } finally {
      _busy = false;
      notifyListeners();
    }
  }

  /// 读取 MCU 当前 active PID 至独立测评快照，不会覆盖调参候选值或中断调参会话。
  Future<bool> loadAssessment() async {
    if (!_linkOk || _busy) return false;
    _busy = true;
    _status = '正在读取 MCU 当前已固化 PID…';
    notifyListeners();
    try {
      final loaded = <PidScheduleNode>[];
      for (var i = 0; i < 3; i++) {
        final node = await _readNode(i);
        if (node == null) {
          _status = '读取 MCU 测评节点 $i 失败';
          return false;
        }
        loaded.add(node);
      }
      _assessmentNodes
        ..clear()
        ..addAll(loaded);
      _assessmentSessions.clear();
      _lastAssessmentResult = null;
      _lastAssessmentResultRecorded = false;
      _status = '已读取 ${loaded.length} 个 MCU 当前节点；可开始只读测评，调参候选保持不变';
      return true;
    } finally {
      _busy = false;
      notifyListeners();
    }
  }

  Future<PidScheduleNode?> _readNode(int index) async {
    final completer = Completer<PidScheduleNode?>();
    late StreamSubscription<BlePacket> sub;
    sub = app.handshake.packetStream.listen((pkt) {
      if (pkt.cmd == CmdCode.pidScheduleRead) {
        try {
          final node = PidScheduleNode.fromPayload(Uint8List.fromList(pkt.payload));
          if (node.index == index && !completer.isCompleted) completer.complete(node);
        } catch (_) {
          if (!completer.isCompleted) completer.complete(null);
        }
      } else if (pkt.cmd == CmdCode.nack && !completer.isCompleted) {
        completer.complete(null);
      }
    });
    final timer = Timer(const Duration(seconds: 2), () {
      if (!completer.isCompleted) completer.complete(null);
    });
    final sent = await app.sendPacket(CmdCode.pidScheduleRead, [index]);
    if (!sent && !completer.isCompleted) completer.complete(null);
    final result = await completer.future;
    timer.cancel();
    await sub.cancel();
    return result;
  }

  void replaceNode(PidScheduleNode updated) {
    final index = updated.index;
    if (index < 0 || index >= _nodes.length) return;
    _nodes[index] = updated;
    _status = '${updated.speedCmS.toStringAsFixed(0)} cm/s 节点已修改，尚未提交 MCU';
    notifyListeners();
  }

  Future<bool> saveAll() async {
    if (!_linkOk || _busy || _nodes.length != 3) return false;
    _busy = true;
    _status = '正在暂存 PID 曲线…';
    notifyListeners();
    try {
      final ok = await _writeAndCommit();
      _status = ok ? '参数曲线已提交；MCU 将在本地按速度插值' : '提交失败：请确认小车已停车且参数范围有效';
      return ok;
    } finally {
      _busy = false;
      notifyListeners();
    }
  }

  /// 架空测试：速度环沿用原有一次测试逻辑，不属于四类落地闭环会话。
  Future<bool> autoTuneSpeedNode(int index) async {
    if (!_linkOk || _busy || index < 0 || index >= _nodes.length) return false;
    _busy = true;
    final speed = _nodes[index].speedCmS;
    _status = '准备 ${speed.toStringAsFixed(0)} cm/s 架空阶跃测试…';
    notifyListeners();
    try {
      final speedPayload = ByteData(4)..setFloat32(0, speed, Endian.little);
      if (!await _sendAck(CmdCode.pidScheduleAutotune, speedPayload.buffer.asUint8List())) {
        _status = 'MCU 拒绝启动：小车未静止或已运行其他闭环';
        return false;
      }
      _status = '正在执行 ${speed.toStringAsFixed(0)} cm/s 阶跃（约 4.5 秒）…';
      notifyListeners();
      final result = await _waitSpeedTuneResult();
      if (result == null) {
        _status = '阶跃超时：MCU 未在 7 秒内返回结果';
        return false;
      }
      _applySpeedHeuristic(_nodes[index], result);
      final committed = await _writeAndCommit();
      _status = committed
          ? '${speed.toStringAsFixed(0)} cm/s 速度整定完成：均速 ${result.avgSpeed.toStringAsFixed(1)}，波动 ${result.ripple.toStringAsFixed(1)}'
          : '速度结果已收到，但候选参数未能提交';
      return committed;
    } finally {
      _busy = false;
      notifyListeners();
    }
  }

  /// 运行一次落地闭环。结果只暂存为会话候选，绝不会在单次测试后立即改 PID。
  Future<BenchResult?> runBench(BenchMode mode, int index) async {
    if (!_linkOk || _busy || index < 0 || index >= _nodes.length) return null;
    final calibrationBlock = calibrationBlockReason(mode);
    if (calibrationBlock != null) {
      _status = '测评前置项未满足：$calibrationBlock';
      notifyListeners();
      return null;
    }
    _busy = true;
    final speed = _nodes[index].speedCmS;
    _status = '正在启动 ${speed.toStringAsFixed(0)} cm/s ${mode.label}…';
    notifyListeners();
    final completer = Completer<BenchResult?>();
    _activeBenchCompleter = completer;
    final startCmd = _startCommand(mode);
    late StreamSubscription<BlePacket> sub;
    sub = app.handshake.packetStream.listen((pkt) {
      if (pkt.cmd == CmdCode.benchResult && !completer.isCompleted) {
        try {
          final result = BenchResult.fromPayload(Uint8List.fromList(pkt.payload));
          if (result.mode == mode && result.nodeIndex == index) completer.complete(result);
        } catch (_) {
          completer.complete(null);
        }
      } else if (pkt.cmd == CmdCode.nack && pkt.payload.isNotEmpty && pkt.payload.first == startCmd && !completer.isCompleted) {
        completer.complete(null);
      }
    });
    try {
      final started = await _sendAck(startCmd, _startPayload(mode, index));
      if (!started) {
        _status = 'MCU 拒绝测试：请确认停车、未遥控且 IMU/灰度已校准';
        return null;
      }
      _status = '${mode.label}运行中；本轮结束后将计入独立 3 次会话，STOP/刹车/急停可中止…';
      notifyListeners();
      final timeout = Timer(Duration(seconds: _timeoutSeconds(mode)), () {
        if (!completer.isCompleted) completer.complete(null);
      });
      final result = await completer.future;
      timeout.cancel();
      if (result == null) {
        _status = '本轮未形成结果或已中止；现有会话样本保持不变';
        return null;
      }
      _lastBenchResult = result;
      _lastResultRecorded = false;
      _status = '${mode.label}结束：${result.reasonLabel}；请录入本轮必要的人工反馈后计入会话';
      app.addLog('BENCH ${mode.name} @${speed.toStringAsFixed(0)}: ${result.reasonLabel}, dist=${result.distanceCm.toStringAsFixed(1)}cm yaw=${result.finalYawDeg.toStringAsFixed(1)}');
      return result;
    } finally {
      _activeBenchCompleter = null;
      await sub.cancel();
      _busy = false;
      notifyListeners();
    }
  }

  /// 使用当前 MCU 已固化的节点运行一次只读测评。
  /// 此流程只产生 `_lastAssessmentResult`，不会修改 `_nodes`、调参会话或 MCU 参数。
  Future<BenchResult?> runAssessment(BenchMode mode, int index) async {
    if (!_linkOk || _busy || index < 0 || index >= _assessmentNodes.length) return null;
    final calibrationBlock = calibrationBlockReason(mode);
    if (calibrationBlock != null) {
      _status = '测评前置项未满足：$calibrationBlock';
      notifyListeners();
      return null;
    }
    _busy = true;
    _assessmentRunActive = true;
    final speed = _assessmentNodes[index].speedCmS;
    _status = '正在以 MCU 已固化 PID 运行 ${speed.toStringAsFixed(0)} cm/s ${mode.label}测评…';
    notifyListeners();
    final completer = Completer<BenchResult?>();
    _activeBenchCompleter = completer;
    final startCmd = _startCommand(mode);
    late StreamSubscription<BlePacket> sub;
    sub = app.handshake.packetStream.listen((pkt) {
      if (pkt.cmd == CmdCode.benchResult && !completer.isCompleted) {
        try {
          final result = BenchResult.fromPayload(Uint8List.fromList(pkt.payload));
          if (result.mode == mode && result.nodeIndex == index) completer.complete(result);
        } catch (_) {
          completer.complete(null);
        }
      } else if (pkt.cmd == CmdCode.nack && pkt.payload.isNotEmpty && pkt.payload.first == startCmd && !completer.isCompleted) {
        completer.complete(null);
      }
    });
    try {
      final started = await _sendAck(startCmd, _startPayload(mode, index));
      if (!started) {
        _status = 'MCU 拒绝测评：请确认停车、未遥控且已完成对应校准';
        return null;
      }
      _status = '${mode.label}只读测评运行中；本轮仅用于评分，不会改 PID…';
      notifyListeners();
      final timeout = Timer(Duration(seconds: _timeoutSeconds(mode)), () {
        if (!completer.isCompleted) completer.complete(null);
      });
      final result = await completer.future;
      timeout.cancel();
      if (result == null) {
        _status = '本轮测评未形成结果或已中止；既有测评记录保持不变';
        return null;
      }
      _lastAssessmentResult = result;
      _lastAssessmentResultRecorded = false;
      _status = '${mode.label}测评结束：${result.reasonLabel}；可保存到只读测评记录';
      app.addLog('ASSESS ${mode.name} @${speed.toStringAsFixed(0)}: ${result.reasonLabel}, dist=${result.distanceCm.toStringAsFixed(1)}cm');
      return result;
    } finally {
      _activeBenchCompleter = null;
      await sub.cancel();
      _assessmentRunActive = false;
      _busy = false;
      notifyListeners();
    }
  }

  /// 将一次只读测评结果写入独立三次记录；绝不触发 PID 候选计算或 MCU 写入。
  Future<bool> recordAssessmentSample({
    required BenchResult result,
    double? manualTurnErrorDeg,
    double? actualDistanceCm,
    double? manualHeadingErrorDeg,
  }) async {
    final index = result.nodeIndex;
    if (_lastAssessmentResultRecorded) {
      _status = '当前测评结果已保存，不能重复计数';
      notifyListeners();
      return false;
    }
    if (!identical(_lastAssessmentResult, result)) {
      _status = '当前测评结果已过期；请重新完成一次测评';
      notifyListeners();
      return false;
    }
    if (index < 0 || index >= _assessmentNodes.length) {
      _status = '测评速度节点无效，不能保存记录';
      notifyListeners();
      return false;
    }
    final lineEndpoint = (result.mode == BenchMode.lineStraight || result.mode == BenchMode.lineCurve) && result.reason == 1;
    final normalFinish = result.reason == 0 || lineEndpoint;
    if (!normalFinish) {
      _status = result.reason == 2 ? '本轮安全超时并自动停车，不能计入三次测评' : '本轮未正常完成，不能计入三次测评';
      notifyListeners();
      return false;
    }
    // 航向误差以 MCU 本轮回传的相对最终 yaw 为准：finalYaw - 90°。
    // 手工填值仅保留向后兼容，不再要求用户观察并输入，避免正常结果被表单阻塞。
    final turnErrorDeg = result.mode == BenchMode.yaw90
        ? result.finalYawDeg - 90.0
        : manualTurnErrorDeg;
    if (result.mode == BenchMode.yaw90 && (turnErrorDeg == null || turnErrorDeg.abs() > 90.0)) {
      _status = 'MCU 回传的相对航向异常，不能保存本轮测评';
      notifyListeners();
      return false;
    }
    if (result.mode == BenchMode.distance1m && (actualDistanceCm == null || actualDistanceCm < 50.0 || actualDistanceCm > 200.0)) {
      _status = '请填写 50–200 cm 范围内的卷尺实测距离';
      notifyListeners();
      return false;
    }
    if (manualHeadingErrorDeg != null && manualHeadingErrorDeg.abs() > 90.0) {
      _status = '方向偏差必须在 -90° 到 +90° 之间';
      notifyListeners();
      return false;
    }
    final key = _sessionKey(result.mode, index);
    final session = _assessmentSessions.putIfAbsent(
      key,
      () => BenchSession(
        mode: result.mode,
        nodeIndex: index,
        speedCmS: _assessmentNodes[index].speedCmS,
        pidBefore: _assessmentNodes[index].copy(),
        startedAt: DateTime.now(),
      ),
    );
    if (session.isReady) {
      _status = '该节点已经完成 3 次只读测评；如需重测请先清空此测评记录';
      notifyListeners();
      return false;
    }
    session.samples.add(BenchSample(
      result: result,
      receivedAt: DateTime.now(),
      manualTurnErrorDeg: turnErrorDeg,
      actualDistanceCm: actualDistanceCm,
      manualHeadingErrorDeg: manualHeadingErrorDeg,
    ));
    _lastAssessmentResultRecorded = true;
    final finishNote = lineEndpoint ? '已在终点出线并自动停车；' : '';
    _status = '$finishNote${result.mode.label}只读测评 ${session.samples.length}/${BenchSession.requiredSamples} 次已保存；不会调整或提交 PID';
    notifyListeners();
    return true;
  }

  /// 只清空当前“模式 + 速度”的测评记录，不影响调参会话、MCU 已固化 PID 或其他测评。
  void clearAssessmentSession(BenchMode mode, int index) {
    if (_assessmentSessions.remove(_sessionKey(mode, index)) == null) return;
    _status = '${mode.label} ${index + 1} 号速度节点的只读测评记录已清空；PID 未改动';
    notifyListeners();
  }

  /// 添加一条完整样本。航向的 manualTurnErrorDeg：正=超转，负=少转；距离的实际距离必填，方向偏差选填。
  /// 直线/曲线循迹的线段终点由“出线并自动停车”(reason=1) 标记，属于正常完成并计入三次样本。
  /// 安全超时及其它模式的非正常结束不能混入三次调参样本。
  Future<bool> recordBenchSample({
    required BenchResult result,
    double? manualTurnErrorDeg,
    double? actualDistanceCm,
    double? manualHeadingErrorDeg,
  }) async {
    final index = result.nodeIndex;
    if (_lastResultRecorded) {
      _status = '当前结果已经保存为本轮样本，不能重复计数';
      notifyListeners();
      return false;
    }
    if (!identical(_lastBenchResult, result)) {
      _status = '当前结果已过期；请重新完成一次测试后再保存样本';
      notifyListeners();
      return false;
    }
    if (index < 0 || index >= _nodes.length) {
      _status = '测试速度节点无效，不能保存样本';
      notifyListeners();
      return false;
    }
    final lineEndpoint = (result.mode == BenchMode.lineStraight || result.mode == BenchMode.lineCurve) && result.reason == 1;
    final normalFinish = result.reason == 0 || lineEndpoint;
    if (!normalFinish) {
      _status = result.reason == 2
          ? '本轮安全超时并自动停车，不能计入 PID 提交样本'
          : '本轮未正常完成，不能计入 PID 提交样本';
      notifyListeners();
      return false;
    }
    // 调参样本同样直接使用 MCU 回传的相对最终 yaw，避免手工抄录误差。
    final turnErrorDeg = result.mode == BenchMode.yaw90
        ? result.finalYawDeg - 90.0
        : manualTurnErrorDeg;
    if (result.mode == BenchMode.yaw90 && (turnErrorDeg == null || turnErrorDeg.abs() > 90.0)) {
      _status = 'MCU 回传的相对航向异常，不能保存航向样本';
      notifyListeners();
      return false;
    }
    if (result.mode == BenchMode.distance1m && (actualDistanceCm == null || actualDistanceCm < 50.0 || actualDistanceCm > 200.0)) {
      _status = '请填写 50–200 cm 范围内的卷尺实测距离，才能保存距离样本';
      notifyListeners();
      return false;
    }
    if (manualHeadingErrorDeg != null && manualHeadingErrorDeg.abs() > 90.0) {
      _status = '方向偏差必须在 -90° 到 +90° 之间';
      notifyListeners();
      return false;
    }

    final key = _sessionKey(result.mode, index);
    final session = _sessions.putIfAbsent(
      key,
      () => BenchSession(
        mode: result.mode,
        nodeIndex: index,
        speedCmS: _nodes[index].speedCmS,
        pidBefore: _nodes[index].copy(),
        startedAt: DateTime.now(),
      ),
    );
    if (session.isFinalized) {
      _status = '此会话已经完成并保存；请先新建会话后再采样';
      notifyListeners();
      return false;
    }
    session.samples.add(BenchSample(
      result: result,
      receivedAt: DateTime.now(),
      manualTurnErrorDeg: turnErrorDeg,
      actualDistanceCm: actualDistanceCm,
      manualHeadingErrorDeg: manualHeadingErrorDeg,
    ));
    _lastResultRecorded = true;
    // 未完成会话只保留在内存中：切换闭环/速度节点不会丢样本，但不产生进度 CSV。
    // 凑齐 3 次后立即自动汇总最终 PID 并覆盖保存会话 CSV（与直线/曲线/航向/距离共用）。
    final finishNote = lineEndpoint ? '已在终点出线并自动停车；' : '';
    if (!session.isReady) {
      _status = '$finishNote${result.mode.label} ${session.samples.length}/${BenchSession.requiredSamples} 次样本已暂存；还需 ${session.remainingSamples} 次';
      notifyListeners();
      return true;
    }

    _status = '$finishNote${result.mode.label} 已集齐 ${session.samples.length} 次样本，正在自动汇总最终 PID 并保存 CSV…';
    notifyListeners();
    final finalized = await finalizeSession(result.mode, index);
    if (!finalized && !session.isFinalized) {
      // 样本已入栈，仅提交失败；保留手动汇总入口供静止后重试。
      _status = '${result.mode.label} 已集齐 3 次样本，但自动提交失败（请确认小车静止后点击琥珀色汇总按钮）。CSV 尚未生成。';
      notifyListeners();
    }
    return true;
  }

  /// 至少三次样本后，基于中位数生成最终 PID，原子提交 MCU，并自动生成本会话专属 CSV。
  Future<bool> finalizeSession(BenchMode mode, int index) async {
    final session = sessionFor(mode, index);
    if (session == null || !session.isReady || session.isFinalized || _busy || !_linkOk) return false;
    _busy = true;
    _status = '正在汇总 ${session.samples.length} 次 ${mode.label} 样本并计算最终 PID…';
    notifyListeners();
    try {
      final candidate = session.pidBefore.copy();
      _applySessionTune(candidate, session);
      _nodes[index] = candidate;
      final committed = await _writeAndCommit();
      if (!committed) {
        _nodes[index] = session.pidBefore.copy();
        _status = '最终 PID 未提交：小车必须静止，且候选参数必须通过 MCU 范围校验';
        return false;
      }
      session.pidAfter = candidate.copy();
      session.finalizedAt = DateTime.now();
      session.csvUri = await _saveSessionCsv(session);
      _status = '${mode.label}最终 PID 已按 ${session.samples.length} 次样本提交；已覆盖保存该速度节点的唯一 CSV';
      app.addLog('BENCH FINAL ${mode.name} @${session.speedCmS.toStringAsFixed(0)}: ${session.samples.length} samples, CSV=${session.csvUri}');
      return true;
    } catch (_) {
      _status = '最终 PID 已计算，但 CSV 保存失败或提交过程异常；请检查下载存储权限与连接';
      return false;
    } finally {
      _busy = false;
      notifyListeners();
    }
  }

  /// 取消仅清空指定类型/速度节点的未完成会话；其它闭环会话互不影响。
  void discardSession(BenchMode mode, int index) {
    final session = sessionFor(mode, index);
    if (session == null || session.isFinalized) return;
    _sessions.remove(_sessionKey(mode, index));
    _status = '${mode.label} ${index + 1} 号速度节点的未完成会话已丢弃；其它会话已保留';
    notifyListeners();
  }

  /// 仅中止由只读测评页发起的测试，绝不会影响调参/速度整定会话。
  Future<bool> abortAssessment() async {
    if (!_linkOk || !_assessmentRunActive) return false;
    final ok = await _sendAck(CmdCode.benchAbort, const []);
    if (ok) {
      if (_activeBenchCompleter != null && !_activeBenchCompleter!.isCompleted) _activeBenchCompleter!.complete(null);
      _status = '已中止当前只读测评；已有测评记录保持不变，PID 未改动';
      notifyListeners();
    }
    return ok;
  }

  Future<bool> abortBench() async {
    if (!_linkOk) return false;
    final ok = await _sendAck(CmdCode.benchAbort, const []);
    if (ok) {
      if (_activeBenchCompleter != null && !_activeBenchCompleter!.isCompleted) _activeBenchCompleter!.complete(null);
      _status = '已中止当前测试；未完成会话样本保留，可稍后继续或单独丢弃';
      notifyListeners();
    }
    return ok;
  }

  Future<bool> _writeAndCommit() async {
    for (final node in _nodes) {
      if (!await _sendAck(CmdCode.pidScheduleWrite, node.toPayload())) return false;
    }
    return _sendAck(CmdCode.pidScheduleCommit, const []);
  }

  int _startCommand(BenchMode mode) => switch (mode) {
    BenchMode.lineStraight => CmdCode.benchLineStraightStart,
    BenchMode.lineCurve => CmdCode.benchLineCurveStart,
    _ => CmdCode.benchStart,
  };

  List<int> _startPayload(BenchMode mode, int index) => switch (mode) {
    BenchMode.lineStraight => [index],
    BenchMode.lineCurve => [index],
    _ => [mode.code, index],
  };

  int _timeoutSeconds(BenchMode mode) => switch (mode) {
    BenchMode.lineStraight => 35,
    BenchMode.lineCurve => 35,
    _ => 18,
  };

  String _sessionKey(BenchMode mode, int index) => '${mode.code}:$index';

  void _applySessionTune(PidScheduleNode node, BenchSession session) {
    final samples = session.samples;
    final error = _median(samples.map((s) => s.result.meanAbsError));
    final steer = _median(samples.map((s) => s.result.meanAbsSteer));
    if (session.mode == BenchMode.lineStraight) {
      final errorRatio = error / node.curveErrorTrigger.clamp(1.0, double.infinity).toDouble();
      final steerUse = steer / node.lineMaxSteer.clamp(1.0, double.infinity).toDouble();
      if (steerUse >= 0.90) {
        node.lineKd *= 1.0 + errorRatio;
        node.lineMaxSteer *= 1.0 + errorRatio * 0.25;
      } else {
        node.lineKp *= 1.0 + errorRatio;
      }
    } else if (session.mode == BenchMode.lineCurve) {
      // 曲线时黑线可长期位于侧面探头，质心绝对位置不是偏离目标曲线的误差。
      // 327.67 是旧结果包的饱和值，不能据此继续自动加大 Kp/Kd；等待后续
      // MCU 回传相对轨迹误差，或只由用户审核后手动改曲线节点。
      if (error >= 327.66) return;
      final errorRatio = (error / node.curveErrorTrigger.clamp(1.0, double.infinity).toDouble()).clamp(0.0, 0.25);
      final steerUse = steer / node.curveMaxSteer.clamp(1.0, double.infinity).toDouble();
      if (steerUse >= 0.90) {
        node.curveKd *= 1.0 + errorRatio;
        node.curveMaxSteer *= 1.0 + errorRatio * 0.25;
      } else {
        node.curveKp *= 1.0 + errorRatio;
      }
    } else if (session.mode == BenchMode.yaw90) {
      final turnError = _median(samples.map((s) => s.manualTurnErrorDeg ?? (s.result.finalYawDeg - 90.0)));
      final overshoot = _median(samples.map((s) => s.result.peakYawDeg - 90.0));
      final magnitude = turnError.abs() / 90.0;
      if (turnError > 0.0 || overshoot > 0.0) {
        final damping = 1.0 + (turnError > 0.0 ? magnitude : overshoot.abs() / 90.0);
        node.yawKp /= damping;
        node.yawKd *= damping;
      } else if (turnError < 0.0) {
        node.yawKp *= 1.0 + magnitude;
      }
    } else if (session.mode == BenchMode.distance1m) {
      final measured = _median(samples.map((s) => s.actualDistanceCm!));
      final encoder = _median(samples.map((s) => s.result.distanceCm));
      if (encoder > 0.1) node.distanceScale *= measured / encoder;
      // 位置 PID 已接入：卷尺实际距离与目标距离的中位误差用于温和修正 pos_kp。
      // 每次最多±20%，避免编码器比例尚未校正时把位置环调得过激。
      final distanceError = _median(samples.map((s) => s.actualDistanceCm! - 100.0));
      final positionRatio = (distanceError.abs() / 100.0).clamp(0.0, 0.20);
      if (distanceError < -0.5) {
        node.posKp *= 1.0 + positionRatio;
      } else if (distanceError > 0.5) {
        node.posKp *= 1.0 - positionRatio;
      }
      final headingValues = samples.where((s) => s.manualHeadingErrorDeg != null).map((s) => s.manualHeadingErrorDeg!);
      if (headingValues.isNotEmpty) {
        final heading = _median(headingValues);
        final ratio = heading.abs() / 90.0;
        if (ratio > 0.0) node.yawKp *= 1.0 + ratio;
      }
    }
  }

  Future<String> _saveSessionCsv(BenchSession session) {
    // 一个“闭环类型 + 速度节点”只保留一份最终汇总；再次完成同一节点时覆盖旧文件。
    final fileName = 'bench_${session.mode.name}_${session.speedCmS.round()}cms.csv';
    return LogFileService.saveLatest(fileName: fileName, content: _sessionCsv(session));
  }

  String _sessionCsv(BenchSession session) {
    final before = session.pidBefore;
    final after = session.pidAfter;
    final lines = <String>[
      'record_type,session_id,mode,node_index,speed_cm_s,sample_no,received_at,reason,elapsed_ms,encoder_distance_cm,relative_final_yaw_deg,peak_abs_yaw_deg,mean_abs_error,mean_abs_steer,control_samples,manual_turn_error_deg,actual_distance_cm,manual_heading_error_deg,pid_before,pid_after,conclusion',
    ];
    final sessionId = '${session.mode.name}_${session.nodeIndex}_${session.startedAt.toIso8601String()}';
    for (var i = 0; i < session.samples.length; i++) {
      final sample = session.samples[i];
      final r = sample.result;
      lines.add([
        'sample', sessionId, session.mode.name, session.nodeIndex, _n(session.speedCmS), i + 1,
        sample.receivedAt.toIso8601String(), r.reasonLabel, r.elapsed.inMilliseconds, _n(r.distanceCm), _n(r.finalYawDeg),
        _n(r.peakYawDeg), _n(r.meanAbsError), _n(r.meanAbsSteer), r.samples, _nOrBlank(sample.manualTurnErrorDeg),
        _nOrBlank(sample.actualDistanceCm), _nOrBlank(sample.manualHeadingErrorDeg), _pidSnapshot(before), '', '',
      ].map(_csv).join(','));
    }
    lines.add([
      'summary', sessionId, session.mode.name, session.nodeIndex, _n(session.speedCmS), '',
      session.finalizedAt?.toIso8601String() ?? '', '', '', '', '', '', '', '', '', '', '', '',
      _pidSnapshot(before), _pidSnapshot(after), _conclusion(session),
    ].map(_csv).join(','));
    return '${lines.join('\n')}\n';
  }

  String _conclusion(BenchSession session) => switch (session.mode) {
    BenchMode.lineStraight => '基于${session.samples.length}次直线循迹样本自动调整直线 PID',
    BenchMode.lineCurve => '基于${session.samples.length}次曲线循迹样本自动调整曲线 PID',
    BenchMode.yaw90 => '基于${session.samples.length}次相对90度转向与人工超转/少转反馈自动调整航向 PID',
    BenchMode.distance1m => '基于${session.samples.length}次实际量尺距离自动调整距离比例；仅在填写方向偏差时调整航向 PID',
  };

  String _pidSnapshot(PidScheduleNode? n) {
    if (n == null) return '';
    return 'speed(kp=${_n(n.speedKp)},ki=${_n(n.speedKi)},kd=${_n(n.speedKd)},kf=${_n(n.speedKf)});line(kp=${_n(n.lineKp)},kd=${_n(n.lineKd)},max=${_n(n.lineMaxSteer)});curve(kp=${_n(n.curveKp)},kd=${_n(n.curveKd)},max=${_n(n.curveMaxSteer)});yaw(kp=${_n(n.yawKp)},kd=${_n(n.yawKd)},max=${_n(n.yawMaxSteer)});distance(scale=${_n(n.distanceScale)},decel=${_n(n.decelPulses)},end=${_n(n.endSpeed)})';
  }

  String _csv(Object? value) {
    final text = value?.toString() ?? '';
    return '"${text.replaceAll('"', '""')}"';
  }

  String _n(double value) => value.toStringAsFixed(5);
  String _nOrBlank(double? value) => value == null ? '' : _n(value);
  double _median(Iterable<double> input) {
    final values = input.toList()..sort();
    if (values.isEmpty) return 0.0;
    final mid = values.length ~/ 2;
    return values.length.isOdd ? values[mid] : (values[mid - 1] + values[mid]) / 2.0;
  }

  void _applySpeedHeuristic(PidScheduleNode node, _SpeedTuneResult result) {
    final target = node.speedCmS;
    final avg = result.avgSpeed;
    if (avg <= 0.1) return;
    final steadyRatio = (target - avg) / target;
    final overshootRatio = (result.peak - target) / target;
    final rippleRatio = result.ripple / target;
    node.speedKf *= target / avg;
    if (overshootRatio > 0) {
      node.speedKp *= 1.0 / (1.0 + overshootRatio);
      node.speedKd *= 1.0 + overshootRatio + rippleRatio;
    } else {
      node.speedKp *= 1.0 + steadyRatio.abs();
      node.speedKd *= 1.0 + rippleRatio;
    }
    if (steadyRatio.abs() > 0.02 && rippleRatio < 0.25) {
      node.speedKi += steadyRatio * target * 0.02;
      if (node.speedKi < 0) node.speedKi = 0;
    }
  }

  Future<_SpeedTuneResult?> _waitSpeedTuneResult() async {
    final completer = Completer<_SpeedTuneResult?>();
    late StreamSubscription<BlePacket> sub;
    sub = app.handshake.packetStream.listen((pkt) {
      if (pkt.cmd == CmdCode.speedTune && pkt.payload.length >= 18 && !completer.isCompleted) {
        final data = Uint8List.fromList(pkt.payload);
        completer.complete(_SpeedTuneResult(
          avgLeft: _i16(data, 0) / 10.0,
          avgRight: _i16(data, 2) / 10.0,
          peak: _i16(data, 4) / 10.0,
          valley: _i16(data, 6) / 10.0,
          ripple: _i16(data, 8) / 10.0,
        ));
      } else if (pkt.cmd == CmdCode.nack && !completer.isCompleted) {
        completer.complete(null);
      }
    });
    /* 阶跃总时长 4.5s + settle 1.5s + BLE/采样/排队余量，拉长到 7s；
     * MCU 已由 0x38 真正启动阶跃，不再硬依赖二次 AT+。 */
    final timer = Timer(const Duration(seconds: 7), () {
      if (!completer.isCompleted) completer.complete(null);
    });
    final result = await completer.future;
    timer.cancel();
    await sub.cancel();
    return result;
  }

  Future<bool> _sendAck(int cmd, List<int> payload) async {
    final completer = Completer<bool>();
    late StreamSubscription<BlePacket> sub;
    sub = app.handshake.packetStream.listen((pkt) {
      if (pkt.cmd == CmdCode.ack && pkt.payload.isNotEmpty && pkt.payload.first == cmd && !completer.isCompleted) {
        completer.complete(true);
      } else if (pkt.cmd == CmdCode.nack && pkt.payload.isNotEmpty && pkt.payload.first == cmd && !completer.isCompleted) {
        completer.complete(false);
      }
    });
    final timer = Timer(const Duration(seconds: 2), () {
      if (!completer.isCompleted) completer.complete(false);
    });
    final sent = await app.sendPacket(cmd, payload);
    if (!sent && !completer.isCompleted) completer.complete(false);
    final ok = await completer.future;
    timer.cancel();
    await sub.cancel();
    return ok;
  }

  String exportCConfig() {
    if (_nodes.length != 3) return '/* 请先从 MCU 读取完整 PID 曲线。 */';
    final lines = <String>[
      '/* ===== 速度自适应 PID 与底层闭环配置快照 ===== */',
      '/* 写入 MCU ble_param.c 的 g_pid_schedule_active/pending 初始化器。 */',
      '#define PID_SCHEDULE_NODE_COUNT 3U',
      '',
      'static const pid_schedule_node_t PID_SCHEDULE[PID_SCHEDULE_NODE_COUNT] = {',
    ];
    for (final n in _nodes) {
      lines.add('    {${n.speedCmS.toStringAsFixed(1)}f, ${n.speedKp.toStringAsFixed(4)}f, ${n.speedKi.toStringAsFixed(4)}f, ${n.speedKd.toStringAsFixed(4)}f, ${n.speedKf.toStringAsFixed(4)}f, ${n.lineKp.toStringAsExponential(7)}f, ${n.lineKd.toStringAsExponential(7)}f, ${n.lineMaxSteer.toStringAsFixed(2)}f, ${n.curveKp.toStringAsExponential(7)}f, ${n.curveKd.toStringAsExponential(7)}f, ${n.curveMaxSteer.toStringAsFixed(2)}f, ${n.curveErrorTrigger.toStringAsFixed(1)}f, ${n.yawKp.toStringAsFixed(4)}f, ${n.yawKd.toStringAsFixed(4)}f, ${n.yawMaxSteer.toStringAsFixed(2)}f, ${n.posKp.toStringAsFixed(5)}f, ${n.decelPulses.toStringAsFixed(1)}f, ${n.endSpeed.toStringAsFixed(2)}f, ${n.distanceScale.toStringAsFixed(4)}f},');
    }
    lines.add('};');
    return lines.join('\n');
  }

  static int _i16(Uint8List d, int off) => (d[off] | (d[off + 1] << 8)).toSigned(16);
}

class _SpeedTuneResult {
  final double avgLeft;
  final double avgRight;
  final double peak;
  final double valley;
  final double ripple;
  const _SpeedTuneResult({required this.avgLeft, required this.avgRight, required this.peak, required this.valley, required this.ripple});
  double get avgSpeed => (avgLeft + avgRight) / 2.0;
}
